#include "ros2_modbus_gateway/gateway_buffer.hpp"
#include "ros2_modbus_gateway/monotonic_clock.hpp"

namespace ros2_modbus_gateway {

GatewayBuffer::GatewayBuffer(const StationConfig& config)
    : station_id_(config.station_id),
      poll_period_ms_(config.poll_period_ms),
      command_timeout_ms_(config.command_timeout_ms),
      recovery_progress_count_(config.recovery_progress_count),
      current_generation_(0),
      link_state_(LinkState::DISCONNECTED),
      alarm_active_(true),
      data_valid_(false),
      has_sample_(false),
      safety_monitor_(config) {}

GatewayBuffer::GatewayBuffer(StationId station_id,
                             uint16_t poll_period_ms,
                             uint8_t consecutive_failures_limit,
                             uint16_t heartbeat_timeout_ms,
                             uint16_t command_timeout_ms,
                             uint8_t recovery_progress_count)
    : station_id_(station_id),
      poll_period_ms_(poll_period_ms),
      command_timeout_ms_(command_timeout_ms),
      recovery_progress_count_(recovery_progress_count),
      current_generation_(0),
      link_state_(LinkState::DISCONNECTED),
      alarm_active_(true),
      data_valid_(false),
      has_sample_(false),
      safety_monitor_(station_id, consecutive_failures_limit, heartbeat_timeout_ms) {}

Generation GatewayBuffer::begin_connection(MonotonicNs now) {
    std::optional<CommandResult> cancelled_res;
    CommandCallback cmd_cb;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_generation_++;
        link_state_ = LinkState::CONNECTING;
        consecutive_success_count_ = 0;
        data_valid_ = false;

        safety_monitor_.set_generation(current_generation_);
        safety_monitor_.reset_heartbeat();

        // Cancel any stale pending command from previous session
        if (current_command_.has_value()) {
            CommandResult res;
            res.id = current_command_->id;
            res.station_id = station_id_;
            res.generation = current_command_->generation;
            res.outcome = CommandOutcome::NOT_SENT;
            res.error = GatewayError(ErrorCode::GENERATION_EXPIRED, "Session closed before command executed");
            res.completed_ns = now;
            last_command_result_ = res;
            cancelled_res = res;
            current_command_.reset();
            cmd_cb = command_callback_;
        }
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (cancelled_res.has_value() && cmd_cb) {
        cmd_cb(*cancelled_res);
    }

    return current_generation_;
}

std::optional<AlarmEvent> GatewayBuffer::accept_sample(const PlcSample& sample) {
    MonotonicNs now = sample.sampled_ns > 0 ? sample.sampled_ns : now_ns();
    return accept_sample(sample, now);
}

std::optional<AlarmEvent> GatewayBuffer::accept_sample(const PlcSample& sample, MonotonicNs now) {
    std::optional<AlarmEvent> alarm_event;
    AlarmCallback cb_to_invoke;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Generation Isolation: discard samples belonging to previous sessions
        if (sample.generation != current_generation_) {
            return std::nullopt;
        }

        poll_attempt_count_++;
        last_sample_ = sample;
        has_sample_ = true;

        if (link_state_ == LinkState::CONNECTING || link_state_ == LinkState::COMM_FAULT) {
            consecutive_success_count_++;
            if (consecutive_success_count_ >= recovery_progress_count_) {
                link_state_ = LinkState::OPERATIONAL;
            }
        }

        alarm_event = safety_monitor_.observe(sample, now);
        alarm_active_ = safety_monitor_.is_alarm_active();

        if (alarm_event.has_value()) {
            link_state_ = LinkState::COMM_FAULT;
            data_valid_ = false;
            cb_to_invoke = alarm_callback_;
        } else {
            // Determine data freshness
            bool fresh = true;
            if (sample.sampled_ns > 0 && now >= sample.sampled_ns) {
                DurationNs age = now - sample.sampled_ns;
                if (age > static_cast<DurationNs>(poll_period_ms_) * 3'000'000ULL) {
                    fresh = false;
                }
            }
            // data_valid recovers immediately upon valid sample reception even if alarm_active is true
            data_valid_ = (link_state_ == LinkState::OPERATIONAL) && sample.valid && fresh;
        }
    } // CRITICAL: Mutex released BEFORE invoking external callback (Alien Method Call defense)

    if (alarm_event.has_value() && cb_to_invoke) {
        cb_to_invoke(*alarm_event);
    }

    return alarm_event;
}

std::optional<AlarmEvent> GatewayBuffer::report_failure(Generation gen,
                                                        const GatewayError& err,
                                                        MonotonicNs now) {
    std::optional<AlarmEvent> alarm_event;
    AlarmCallback cb_to_invoke;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Generation Isolation: ignore failures from previous sessions
        if (gen != current_generation_) {
            return std::nullopt;
        }

        poll_attempt_count_++;
        io_error_count_++;
        last_error_ = err;
        consecutive_success_count_ = 0;
        data_valid_ = false;

        alarm_event = safety_monitor_.on_io_failure(err, now);
        alarm_active_ = safety_monitor_.is_alarm_active();

        if (alarm_event.has_value()) {
            link_state_ = LinkState::COMM_FAULT;
            cb_to_invoke = alarm_callback_;
        }
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (alarm_event.has_value() && cb_to_invoke) {
        cb_to_invoke(*alarm_event);
    }

    return alarm_event;
}

std::optional<AlarmEvent> GatewayBuffer::evaluate_stale(MonotonicNs now) {
    std::optional<AlarmEvent> alarm_event;
    AlarmCallback cb_to_invoke;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        alarm_event = safety_monitor_.evaluate_stale(now);
        alarm_active_ = safety_monitor_.is_alarm_active();

        if (alarm_event.has_value()) {
            link_state_ = LinkState::COMM_FAULT;
            data_valid_ = false;
            cb_to_invoke = alarm_callback_;
        }
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (alarm_event.has_value() && cb_to_invoke) {
        cb_to_invoke(*alarm_event);
    }

    return alarm_event;
}

Result<CommandId> GatewayBuffer::submit_command(const CommandRequest& req, MonotonicNs now) {
    CommandCallback cb_to_invoke;
    std::optional<CommandResult> timeout_res;
    Result<CommandId> result = Result<CommandId>::err(
        GatewayError(ErrorCode::NOT_READY, "Uninitialized submit_command"));

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (link_state_ == LinkState::STOPPING) {
            return Result<CommandId>::err(GatewayError(ErrorCode::SHUTTING_DOWN, "Gateway is shutting down"));
        }

        // Single Command Slot: enforce at most 1 active command per station
        if (current_command_.has_value()) {
            if (now > current_command_->deadline_ns) {
                // Existing command timed out in slot: retire it
                CommandResult res;
                res.id = current_command_->id;
                res.station_id = station_id_;
                res.generation = current_command_->generation;
                res.outcome = CommandOutcome::UNKNOWN;
                res.error = GatewayError(ErrorCode::COMMAND_TIMEOUT, "Previous command timed out in slot");
                res.completed_ns = now;
                last_command_result_ = res;
                timeout_res = res;
                current_command_.reset();
                cb_to_invoke = command_callback_;
            } else {
                return Result<CommandId>::err(GatewayError(ErrorCode::BUSY, "Command slot occupied"));
            }
        }

        // Check link operational status
        if (link_state_ != LinkState::OPERATIONAL) {
            return Result<CommandId>::err(GatewayError(ErrorCode::NOT_READY, "Station link is not operational"));
        }

        if (!has_sample_ || !last_sample_.has_value()) {
            return Result<CommandId>::err(GatewayError(ErrorCode::NOT_READY, "No valid sample available for station"));
        }

        if (!last_sample_->valid) {
            return Result<CommandId>::err(GatewayError(ErrorCode::NOT_READY, "Sample data is not valid"));
        }

        // Freshness check: sample must be within 3 polling periods (e.g. 60ms for 20ms period)
        if (last_sample_->sampled_ns > 0 && now >= last_sample_->sampled_ns) {
            DurationNs age = now - last_sample_->sampled_ns;
            if (age > static_cast<DurationNs>(poll_period_ms_) * 3'000'000ULL) {
                return Result<CommandId>::err(GatewayError(ErrorCode::NOT_READY, "Sample is stale"));
            }
        }

        // Command-specific acceptance rules as per spec §3.5
        switch (req.command) {
            case Command::START: {
                if (alarm_active_) {
                    return Result<CommandId>::err(
                        GatewayError(ErrorCode::ALARM_ACTIVE, "Cannot START while alarm latch is active"));
                }
                if (last_sample_->image.physical_estop() || last_sample_->image.fault_code() != 0 ||
                    !last_sample_->image.ready() || last_sample_->image.alarm_tripped()) {
                    return Result<CommandId>::err(
                        GatewayError(ErrorCode::INTERLOCK_ACTIVE, "PLC interlock active: physical E-Stop, fault, alarm, or not ready"));
                }
                break;
            }
            case Command::STOP: {
                // STOP is always permitted during OPERATIONAL for emergency controlled stop
                break;
            }
            case Command::RESET: {
                if (last_sample_->image.physical_estop()) {
                    return Result<CommandId>::err(
                        GatewayError(ErrorCode::INTERLOCK_ACTIVE, "Cannot RESET while physical E-Stop is engaged"));
                }
                break;
            }
            case Command::SET_SETPOINT: {
                if (req.value > 1000) {
                    return Result<CommandId>::err(
                        GatewayError(ErrorCode::INVALID_ARGUMENT, "Setpoint value out of valid range (0~1000)"));
                }
                if (alarm_active_) {
                    return Result<CommandId>::err(
                        GatewayError(ErrorCode::ALARM_ACTIVE, "Cannot SET_SETPOINT while alarm latch is active"));
                }
                break;
            }
            default:
                return Result<CommandId>::err(GatewayError(ErrorCode::INVALID_ARGUMENT, "Unknown command type"));
        }

        next_command_id_++;
        CommandTask task;
        task.id = next_command_id_;
        task.station_id = station_id_;
        task.request = req;
        task.generation = current_generation_;
        task.accepted_ns = now;
        task.deadline_ns = now + (static_cast<DurationNs>(command_timeout_ms_) * 1'000'000ULL);
        task.phase = CommandPhase::QUEUED;
        task.expected_counter = static_cast<uint16_t>((last_sample_->image.applied_command_counter() + 1) % 65536);

        current_command_ = task;
        result = Result<CommandId>::ok(task.id);
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (timeout_res.has_value() && cb_to_invoke) {
        cb_to_invoke(*timeout_res);
    }

    return result;
}

std::optional<CommandTask> GatewayBuffer::take_command(Generation gen, MonotonicNs now) {
    CommandCallback cb_to_invoke;
    std::optional<CommandResult> timeout_res;
    std::optional<CommandTask> task_to_return;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!current_command_.has_value()) {
            return std::nullopt;
        }

        if (current_command_->generation != gen) {
            return std::nullopt;
        }

        if (current_command_->phase != CommandPhase::QUEUED) {
            return std::nullopt;
        }

        if (now > current_command_->deadline_ns) {
            // Deadline exceeded while waiting in queue
            current_command_->phase = CommandPhase::TERMINAL;
            CommandResult res;
            res.id = current_command_->id;
            res.station_id = station_id_;
            res.generation = gen;
            res.outcome = CommandOutcome::NOT_SENT;
            res.error = GatewayError(ErrorCode::COMMAND_TIMEOUT, "Command timed out before dispatch");
            res.completed_ns = now;
            last_command_result_ = res;
            timeout_res = res;
            current_command_.reset();
            cb_to_invoke = command_callback_;
        } else {
            current_command_->phase = CommandPhase::EXECUTING;
            task_to_return = current_command_;
        }
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (timeout_res.has_value() && cb_to_invoke) {
        cb_to_invoke(*timeout_res);
    }

    return task_to_return;
}

void GatewayBuffer::complete_command(const CommandResult& res) {
    CommandCallback cmd_cb_to_invoke;
    AlarmCallback alarm_cb_to_invoke;
    std::optional<AlarmEvent> alarm_ev_to_invoke;
    CommandResult result_copy = res;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (current_command_.has_value() && current_command_->id == res.id) {
            last_command_result_ = res;

            // If confirmed RESET command, clear internal safety alarm latch
            if (res.outcome == CommandOutcome::CONFIRMED &&
                current_command_->request.command == Command::RESET) {
                if (alarm_active_) {
                    auto clear_res = safety_monitor_.clear_alarm(true, res.completed_ns);
                    alarm_active_ = false;
                    if (clear_res.is_ok()) {
                        alarm_ev_to_invoke = clear_res.value();
                        alarm_cb_to_invoke = alarm_callback_;
                    }
                }
            }

            current_command_.reset(); // Free single command slot
        }

        cmd_cb_to_invoke = command_callback_;
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (cmd_cb_to_invoke) {
        cmd_cb_to_invoke(result_copy);
    }
    if (alarm_ev_to_invoke.has_value() && alarm_cb_to_invoke) {
        alarm_cb_to_invoke(*alarm_ev_to_invoke);
    }
}

Result<AlarmEvent> GatewayBuffer::clear_fault(bool force, MonotonicNs now) {
    AlarmCallback cb_to_invoke;
    Result<AlarmEvent> result = Result<AlarmEvent>::err(
        GatewayError(ErrorCode::INTERNAL_ERROR, "Uninitialized clear_fault"));

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!force) {
            if (link_state_ != LinkState::OPERATIONAL) {
                return Result<AlarmEvent>::err(
                    GatewayError(ErrorCode::NOT_READY, "Cannot clear fault: link is not OPERATIONAL"));
            }
            if (!has_sample_ || !last_sample_.has_value()) {
                return Result<AlarmEvent>::err(
                    GatewayError(ErrorCode::NOT_READY, "Cannot clear fault: no valid sample available"));
            }
            if (last_sample_->image.physical_estop() || last_sample_->image.fault_code() != 0 ||
                last_sample_->image.alarm_tripped()) {
                return Result<AlarmEvent>::err(
                    GatewayError(ErrorCode::INTERLOCK_ACTIVE, "Cannot clear fault: physical E-Stop, fault, or alarm active"));
            }
        }

        bool was_alarm_active = alarm_active_;
        result = safety_monitor_.clear_alarm(force, now);
        if (result.is_ok()) {
            alarm_active_ = false;
            if (link_state_ == LinkState::OPERATIONAL && has_sample_ && last_sample_.has_value()) {
                data_valid_ = last_sample_->valid;
            }
            if (was_alarm_active) {
                cb_to_invoke = alarm_callback_;
            }
        }
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (result.is_ok() && cb_to_invoke) {
        cb_to_invoke(result.value());
    }

    return result;
}

GatewayStatus GatewayBuffer::get_status() const {
    std::lock_guard<std::mutex> lock(mutex_);

    GatewayStatus st;
    st.station_id = station_id_;
    st.link_state = link_state_;
    st.generation = current_generation_;
    st.alarm_active = alarm_active_;
    st.last_sample = last_sample_;
    st.last_progress_ns = safety_monitor_.last_progress_ns();
    st.data_valid = data_valid_;
    st.last_error = last_error_;
    st.poll_attempt_count = poll_attempt_count_;
    st.io_error_count = io_error_count_;
    st.consecutive_failures = safety_monitor_.consecutive_failures();
    return st;
}

void GatewayBuffer::begin_shutdown(MonotonicNs now) {
    AlarmCallback alarm_cb_to_invoke;
    CommandCallback cmd_cb_to_invoke;
    std::optional<AlarmEvent> alarm_event;
    std::optional<CommandResult> cancelled_cmd;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        link_state_ = LinkState::STOPPING;
        data_valid_ = false;

        if (current_command_.has_value()) {
            CommandResult res;
            res.id = current_command_->id;
            res.station_id = station_id_;
            res.generation = current_command_->generation;
            res.outcome = CommandOutcome::NOT_SENT;
            res.error = GatewayError(ErrorCode::SHUTTING_DOWN, "Node is shutting down");
            res.completed_ns = now;
            last_command_result_ = res;
            cancelled_cmd = res;
            current_command_.reset();
            cmd_cb_to_invoke = command_callback_;
        }

        alarm_event = safety_monitor_.trip(
            AlarmCause::SHUTDOWN,
            GatewayError(ErrorCode::SHUTTING_DOWN, "Shutdown initiated"),
            now);
        alarm_active_ = true;
        alarm_cb_to_invoke = alarm_callback_;
    } // CRITICAL: Mutex released BEFORE invoking external callback

    if (cancelled_cmd.has_value() && cmd_cb_to_invoke) {
        cmd_cb_to_invoke(*cancelled_cmd);
    }
    if (alarm_event.has_value() && alarm_cb_to_invoke) {
        alarm_cb_to_invoke(*alarm_event);
    }
}

void GatewayBuffer::set_alarm_callback(AlarmCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    alarm_callback_ = std::move(cb);
}

void GatewayBuffer::set_command_callback(CommandCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    command_callback_ = std::move(cb);
}

StationId GatewayBuffer::station_id() const noexcept {
    return station_id_;
}

Generation GatewayBuffer::current_generation() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_generation_;
}

LinkState GatewayBuffer::link_state() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return link_state_;
}

bool GatewayBuffer::has_sample() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_sample_;
}

bool GatewayBuffer::is_alarm_active() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return alarm_active_;
}

bool GatewayBuffer::is_data_valid() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_valid_;
}

std::optional<PlcSample> GatewayBuffer::last_sample() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_sample_;
}

std::optional<CommandTask> GatewayBuffer::current_command() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_command_;
}

std::optional<CommandResult> GatewayBuffer::last_command_result() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_command_result_;
}

const SafetyMonitor& GatewayBuffer::safety_monitor() const noexcept {
    return safety_monitor_;
}

SafetyMonitor& GatewayBuffer::safety_monitor() noexcept {
    return safety_monitor_;
}

void GatewayBuffer::set_link_state(LinkState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    link_state_ = state;
}

} // namespace ros2_modbus_gateway
