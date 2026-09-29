#include "ros2_modbus_gateway/station_context.hpp"
#include "ros2_modbus_gateway/register_map.hpp"

#include <iostream>

namespace ros2_modbus_gateway {

StationContext::StationContext(const StationConfig& config, boost::asio::io_context& ioc)
    : config_(config),
      ioc_(ioc),
      buffer_(config),
      client_(ioc),
      poll_timer_(ioc),
      reconnect_timer_(ioc),
      watchdog_timer_(ioc) {}

StationContext::~StationContext() {
    stop();
}

void StationContext::start() {
    stopped_ = false;
    schedule_watchdog();
    schedule_reconnect(0);
}

void StationContext::stop() noexcept {
    stopped_ = true;
    boost::system::error_code ec;
    poll_timer_.cancel(ec);
    reconnect_timer_.cancel(ec);
    watchdog_timer_.cancel(ec);
    client_.close();
    buffer_.begin_shutdown(Clock::now_ns());
}

GatewayStatus StationContext::get_status() const {
    return buffer_.get_status();
}

Result<CommandId> StationContext::submit_command(const CommandRequest& req, MonotonicNs now) {
    return buffer_.submit_command(req, now);
}

Result<AlarmEvent> StationContext::clear_fault(bool force, MonotonicNs now) {
    return buffer_.clear_fault(force, now);
}

void StationContext::set_alarm_callback(AlarmCallback cb) {
    buffer_.set_alarm_callback(std::move(cb));
}

void StationContext::set_command_callback(CommandCallback cb) {
    buffer_.set_command_callback(std::move(cb));
}

void StationContext::schedule_reconnect(uint32_t delay_ms) {
    if (stopped_) return;
    boost::system::error_code ec;
    reconnect_timer_.cancel(ec);
    reconnect_timer_.expires_after(std::chrono::milliseconds(delay_ms));
    reconnect_timer_.async_wait([this](const boost::system::error_code& timer_ec) {
        if (timer_ec || stopped_) return;
        do_connect();
    });
}

void StationContext::do_connect() {
    if (stopped_) return;

    MonotonicNs now = Clock::now_ns();
    current_generation_ = buffer_.begin_connection(now);

    client_.async_connect(config_, [this](const Result<void>& res) {
        if (stopped_) return;

        if (res.is_ok()) {
            reconnect_backoff_ms_ = 100;
            schedule_poll(0); // Immediately start polling on connection success
        } else {
            MonotonicNs fail_ns = Clock::now_ns();
            buffer_.report_failure(current_generation_, res.error(), fail_ns);
            reconnect_backoff_ms_ = std::min<uint32_t>(reconnect_backoff_ms_ * 2, 1000);
            schedule_reconnect(reconnect_backoff_ms_);
        }
    });
}

void StationContext::schedule_poll(uint32_t delay_ms) {
    if (stopped_ || !client_.is_connected()) return;
    boost::system::error_code ec;
    poll_timer_.cancel(ec);
    if (delay_ms == 0) {
        next_poll_time_ = std::chrono::steady_clock::now();
        poll_timer_.expires_at(next_poll_time_);
    } else {
        next_poll_time_ += std::chrono::milliseconds(config_.poll_period_ms);
        auto now = std::chrono::steady_clock::now();
        if (next_poll_time_ < now) {
            next_poll_time_ = now + std::chrono::milliseconds(config_.poll_period_ms);
        }
        poll_timer_.expires_at(next_poll_time_);
    }
    poll_timer_.async_wait([this](const boost::system::error_code& timer_ec) {
        if (timer_ec || stopped_) return;
        do_poll();
    });
}


void StationContext::do_poll() {
    if (stopped_ || !client_.is_connected()) return;

    // in_flight transaction protection: skip slot if operation already executing
    if (in_flight_) {
        schedule_poll(config_.poll_period_ms);
        return;
    }

    in_flight_ = true;
    MonotonicNs poll_start = Clock::now_ns();
    int64_t jitter = 0;
    if (last_poll_start_ns_ > 0) {
        jitter = static_cast<int64_t>(poll_start - last_poll_start_ns_) -
                 static_cast<int64_t>(config_.poll_period_ms * 1'000'000ULL);
    }
    last_poll_start_ns_ = poll_start;

    // Check if there is a pending command to take
    auto task_opt = buffer_.take_command(current_generation_, poll_start);
    if (task_opt.has_value()) {
        std::cout << "[StationContext] Executing queued command id=" << task_opt->id
                  << " cmd=" << static_cast<int>(task_opt->request.command) << std::endl;
        execute_command_and_poll(*task_opt, poll_start, jitter);
    } else {
        read_holding_bulk_and_observe(poll_start, jitter);
    }
}

void StationContext::execute_command_and_poll(CommandTask task, MonotonicNs poll_start, int64_t jitter) {
    // Dispatch write packet based on command type
    switch (task.request.command) {
        case Command::START: {
            uint16_t coil_addr = config_.coil_base + COIL_RUN_REQUESTED;
            client_.async_write_coil(coil_addr, true, [this, task, poll_start, jitter](const Result<void>& wres) {
                if (stopped_) return;
                if (wres.is_ok()) {
                    read_holding_bulk_and_observe(poll_start, jitter, task);
                } else {
                    in_flight_ = false;
                    CommandResult cres;
                    cres.id = task.id;
                    cres.station_id = config_.station_id;
                    cres.generation = current_generation_;
                    cres.outcome = CommandOutcome::UNKNOWN;
                    cres.error = wres.error();
                    cres.completed_ns = Clock::now_ns();
                    buffer_.complete_command(cres);

                    MonotonicNs fail_ns = Clock::now_ns();
                    buffer_.report_failure(current_generation_, wres.error(), fail_ns);
                    client_.close();
                    schedule_reconnect(reconnect_backoff_ms_);
                }
            });
            break;
        }
        case Command::STOP: {
            uint16_t coil_addr = config_.coil_base + COIL_RUN_REQUESTED;
            client_.async_write_coil(coil_addr, false, [this, task, poll_start, jitter](const Result<void>& wres) {
                if (stopped_) return;
                if (wres.is_ok()) {
                    read_holding_bulk_and_observe(poll_start, jitter, task);
                } else {
                    in_flight_ = false;
                    CommandResult cres;
                    cres.id = task.id;
                    cres.station_id = config_.station_id;
                    cres.generation = current_generation_;
                    cres.outcome = CommandOutcome::UNKNOWN;
                    cres.error = wres.error();
                    cres.completed_ns = Clock::now_ns();
                    buffer_.complete_command(cres);

                    MonotonicNs fail_ns = Clock::now_ns();
                    buffer_.report_failure(current_generation_, wres.error(), fail_ns);
                    client_.close();
                    schedule_reconnect(reconnect_backoff_ms_);
                }
            });
            break;
        }
        case Command::RESET: {
            uint16_t coil_addr = config_.coil_base + COIL_RESET_REQUESTED;
            client_.async_write_coil(coil_addr, true, [this, task, poll_start, jitter](const Result<void>& wres) {
                if (stopped_) return;
                if (wres.is_ok()) {
                    read_holding_bulk_and_observe(poll_start, jitter, task);
                } else {
                    in_flight_ = false;
                    CommandResult cres;
                    cres.id = task.id;
                    cres.station_id = config_.station_id;
                    cres.generation = current_generation_;
                    cres.outcome = CommandOutcome::UNKNOWN;
                    cres.error = wres.error();
                    cres.completed_ns = Clock::now_ns();
                    buffer_.complete_command(cres);

                    MonotonicNs fail_ns = Clock::now_ns();
                    buffer_.report_failure(current_generation_, wres.error(), fail_ns);
                    client_.close();
                    schedule_reconnect(reconnect_backoff_ms_);
                }
            });
            break;
        }
        case Command::SET_SETPOINT: {
            uint16_t reg_addr = config_.holding_base + HR_SETPOINT_RAW;
            client_.async_write_register(reg_addr, task.request.value, [this, task, poll_start, jitter](const Result<void>& wres) {
                if (stopped_) return;
                if (wres.is_ok()) {
                    read_holding_bulk_and_observe(poll_start, jitter, task);
                } else {
                    in_flight_ = false;
                    CommandResult cres;
                    cres.id = task.id;
                    cres.station_id = config_.station_id;
                    cres.generation = current_generation_;
                    cres.outcome = CommandOutcome::UNKNOWN;
                    cres.error = wres.error();
                    cres.completed_ns = Clock::now_ns();
                    buffer_.complete_command(cres);

                    MonotonicNs fail_ns = Clock::now_ns();
                    buffer_.report_failure(current_generation_, wres.error(), fail_ns);
                    client_.close();
                    schedule_reconnect(reconnect_backoff_ms_);
                }
            });
            break;
        }
    }
}

void StationContext::read_holding_bulk_and_observe(MonotonicNs poll_start,
                                                  int64_t jitter,
                                                  std::optional<CommandTask> /*active_task*/) {
    client_.async_read_holding_bulk(
        config_.holding_base,
        HOLDING_REGISTER_COUNT,
        [this, poll_start, jitter](const Result<std::array<uint16_t, 6>>& res) {
            if (stopped_) return;
            in_flight_ = false;
            MonotonicNs sampled_ns = Clock::now_ns();
            DurationNs rtt = sampled_ns >= poll_start ? (sampled_ns - poll_start) : 0;

            if (res.is_ok()) {
                const auto& regs = res.value();
                PlcSample sample;
                sample.generation = current_generation_;
                sample.sequence = ++last_sample_seq_;
                sample.image.registers = regs;
                sample.poll_started_ns = poll_start;
                sample.sampled_ns = sampled_ns;
                sample.read_rtt_ns = rtt;
                sample.poll_jitter_ns = jitter;

                // Validate sample
                bool sensor_ok = (sample.image.sensor_raw() <= SENSOR_RAW_MAX);
                bool setpoint_ok = (sample.image.setpoint_raw() <= SETPOINT_RAW_MAX);
                bool fault_ok = (sample.image.fault_code() <= FAULT_CODE_ESTOP);
                bool flags_ok = ((sample.image.status_flags() & STATUS_BIT_RESERVED_MASK) == 0);
                sample.valid = (sensor_ok && setpoint_ok && fault_ok && flags_ok);

                buffer_.accept_sample(sample, sampled_ns);

                // Check command confirmation
                check_command_confirmation(sample, sampled_ns);

                // Re-arm regular 20ms periodic poll
                schedule_poll(config_.poll_period_ms);
            } else {
                GatewayError err = res.error();
                buffer_.report_failure(current_generation_, err, sampled_ns);

                if (err.code == static_cast<uint16_t>(ErrorCode::IO_TIMEOUT)) {
                    // Immediate Retry Logic:
                    auto st = buffer_.get_status();
                    if (st.consecutive_failures < config_.consecutive_failures_limit) {
                        // Consecutive failure limit not yet reached: immediately trigger retry!
                        do_poll();
                    } else {
                        // Limit reached (3 consecutive timeouts): transition to COMM_FAULT
                        client_.close();
                        reconnect_backoff_ms_ = 100;
                        schedule_reconnect(reconnect_backoff_ms_);
                    }
                } else {
                    // Non-timeout error (e.g. CONNECTION_LOST, PROTOCOL_ERROR): close and reconnect
                    client_.close();
                    reconnect_backoff_ms_ = std::min<uint32_t>(reconnect_backoff_ms_ * 2, 1000);
                    schedule_reconnect(reconnect_backoff_ms_);
                }
            }
        });
}

void StationContext::check_command_confirmation(const PlcSample& sample, MonotonicNs now) {
    auto current_cmd_opt = buffer_.current_command();
    if (!current_cmd_opt.has_value()) {
        return;
    }

    const auto& cmd = *current_cmd_opt;
    if (cmd.phase != CommandPhase::EXECUTING && cmd.phase != CommandPhase::WAITING_CONFIRMATION) {
        return;
    }

    // Check timeout deadline
    if (now > cmd.deadline_ns) {
        CommandResult cres;
        cres.id = cmd.id;
        cres.station_id = config_.station_id;
        cres.generation = current_generation_;
        cres.outcome = CommandOutcome::UNKNOWN;
        cres.error = GatewayError(ErrorCode::COMMAND_TIMEOUT, "Command confirmation timed out (300ms)");
        cres.completed_ns = now;
        buffer_.complete_command(cres);
        return;
    }

    bool confirmed = false;
    uint16_t counter = sample.image.applied_command_counter();
    if (!cmd.expected_counter.has_value()) {
        return;
    }
    uint16_t exp = *cmd.expected_counter;
    if (counter != exp) {
        return;
    }

    switch (cmd.request.command) {
        case Command::START: {
            if (sample.image.run_requested() && sample.image.running()) {
                confirmed = true;
            }
            break;
        }
        case Command::STOP: {
            if (!sample.image.run_requested() && !sample.image.running()) {
                confirmed = true;
            }
            break;
        }
        case Command::RESET: {
            if (sample.image.fault_code() == 0 && !sample.image.reset_requested()) {
                confirmed = true;
            }
            break;
        }
        case Command::SET_SETPOINT: {
            if (sample.image.setpoint_raw() == cmd.request.value) {
                confirmed = true;
            }
            break;
        }
    }

    if (confirmed) {
        std::cout << "[StationContext] Command confirmed! id=" << cmd.id << " counter=" << counter << std::endl;
        CommandResult cres;
        cres.id = cmd.id;
        cres.station_id = config_.station_id;
        cres.generation = current_generation_;
        cres.outcome = CommandOutcome::CONFIRMED;
        cres.completed_ns = now;
        cres.confirmed_sample_sequence = sample.sequence;
        buffer_.complete_command(cres);
    } else {
        std::cout << "[StationContext] Command not yet confirmed: id=" << cmd.id << " counter=" << counter << " exp=" << exp << std::endl;
    }
}

void StationContext::schedule_watchdog() {
    if (stopped_) return;
    boost::system::error_code ec;
    watchdog_timer_.cancel(ec);
    watchdog_timer_.expires_after(std::chrono::milliseconds(10));
    watchdog_timer_.async_wait([this](const boost::system::error_code& timer_ec) {
        if (timer_ec || stopped_) return;
        MonotonicNs now = Clock::now_ns();
        buffer_.evaluate_stale(now);

        // Check if active command exceeded 300ms deadline (freed even if communication is lost)
        auto cur_cmd = buffer_.current_command();
        if (cur_cmd.has_value() && now > cur_cmd->deadline_ns) {
            CommandResult cres;
            cres.id = cur_cmd->id;
            cres.station_id = config_.station_id;
            cres.generation = cur_cmd->generation;
            cres.outcome = (cur_cmd->phase == CommandPhase::QUEUED) ? CommandOutcome::NOT_SENT : CommandOutcome::UNKNOWN;
            cres.error = GatewayError(ErrorCode::COMMAND_TIMEOUT, "Command confirmation timed out (300ms watchdog)");
            cres.completed_ns = now;
            buffer_.complete_command(cres);
        }

        schedule_watchdog();
    });
}

} // namespace ros2_modbus_gateway
