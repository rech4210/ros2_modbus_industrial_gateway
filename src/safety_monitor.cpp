#include "ros2_modbus_gateway/safety_monitor.hpp"

namespace ros2_modbus_gateway {

SafetyMonitor::SafetyMonitor(const StationConfig& config)
    : station_id_(config.station_id),
      consecutive_failures_limit_(config.consecutive_failures_limit),
      heartbeat_timeout_ns_(static_cast<DurationNs>(config.heartbeat_timeout_ms) * 1'000'000ULL),
      alarm_active_(true),
      current_cause_(AlarmCause::STARTUP),
      last_error_(static_cast<uint16_t>(ErrorCode::NOT_READY), "Initial startup safety latch active") {}

SafetyMonitor::SafetyMonitor(StationId station_id,
                             uint8_t consecutive_failures_limit,
                             uint16_t heartbeat_timeout_ms)
    : station_id_(station_id),
      consecutive_failures_limit_(consecutive_failures_limit),
      heartbeat_timeout_ns_(static_cast<DurationNs>(heartbeat_timeout_ms) * 1'000'000ULL),
      alarm_active_(true),
      current_cause_(AlarmCause::STARTUP),
      last_error_(static_cast<uint16_t>(ErrorCode::NOT_READY), "Initial startup safety latch active") {}

std::optional<AlarmEvent> SafetyMonitor::trip_unlocked(AlarmCause cause,
                                                       const GatewayError& err,
                                                       MonotonicNs now) {
    alarm_active_ = true;
    current_cause_ = cause;
    last_error_ = err;
    alarm_sequence_++;

    AlarmEvent ev;
    ev.station_id = station_id_;
    ev.sequence = alarm_sequence_;
    ev.active = true;
    ev.cause = cause;
    ev.error = err;
    ev.generation = generation_;
    ev.detected_ns = now;
    ev.last_progress_ns = last_progress_ns_;
    return ev;
}

std::optional<AlarmEvent> SafetyMonitor::observe(const PlcSample& sample, MonotonicNs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = sample.generation;
    consecutive_failures_ = 0;
    has_sample_ = true;
    last_physical_estop_ = sample.image.physical_estop();
    last_fault_code_ = sample.image.fault_code();
    last_alarm_tripped_ = sample.image.alarm_tripped();

    uint16_t curr_hb = sample.image.heartbeat();
    std::optional<AlarmEvent> triggered_alarm;

    if (!last_heartbeat_.has_value()) {
        // First sample received in session
        last_heartbeat_ = curr_hb;
        last_progress_ns_ = now;
    } else {
        uint16_t prev_hb = *last_heartbeat_;
        uint16_t delta = static_cast<uint16_t>((curr_hb - prev_hb) & 0xFFFF);

        if (delta >= 1 && delta <= 32767) {
            // Normal progression: update heartbeat and progress timestamp
            last_progress_ns_ = now;
            last_heartbeat_ = curr_hb;
        } else if (delta == 0) {
            // Heartbeat stalled: do NOT advance last_progress_ns_
            if (last_progress_ns_.has_value() && (now >= *last_progress_ns_) &&
                (now - *last_progress_ns_ >= heartbeat_timeout_ns_)) {
                if (!alarm_active_ || current_cause_ != AlarmCause::HEARTBEAT_STALE) {
                    triggered_alarm = trip_unlocked(
                        AlarmCause::HEARTBEAT_STALE,
                        GatewayError(ErrorCode::HEARTBEAT_STALE, "PLC heartbeat stalled for >= 80ms"),
                        now);
                }
            }
        } else {
            // Abnormal reversal (delta >= 32768): reboot or memory corruption
            last_heartbeat_ = curr_hb;
            last_progress_ns_ = now; // Re-establish baseline upon reversal
            if (!alarm_active_ || current_cause_ != AlarmCause::HEARTBEAT_STALE) {
                triggered_alarm = trip_unlocked(
                    AlarmCause::HEARTBEAT_STALE,
                    GatewayError(ErrorCode::HEARTBEAT_STALE, "Heartbeat backward jump detected"),
                    now);
            }
        }
    }

    // Check physical E-stop or PLC fault condition
    if (sample.image.physical_estop() || sample.image.fault_code() != 0 || sample.image.alarm_tripped()) {
        if (!alarm_active_ || current_cause_ != AlarmCause::PLC_INTERLOCK) {
            std::string reason;
            if (sample.image.physical_estop()) {
                reason = "Physical E-Stop active";
            } else if (sample.image.fault_code() != 0) {
                reason = "PLC fault code active: " + std::to_string(sample.image.fault_code());
            } else {
                reason = "PLC internal alarm tripped";
            }
            triggered_alarm = trip_unlocked(
                AlarmCause::PLC_INTERLOCK,
                GatewayError(ErrorCode::INTERLOCK_ACTIVE, reason),
                now);
        }
    }

    return triggered_alarm;
}

std::optional<AlarmEvent> SafetyMonitor::on_io_failure(const GatewayError& err, MonotonicNs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    consecutive_failures_++;

    bool is_immediate_fault = (err.code == static_cast<uint16_t>(ErrorCode::CONNECTION_LOST));

    if (consecutive_failures_ >= consecutive_failures_limit_ || is_immediate_fault) {
        if (!alarm_active_ || current_cause_ != AlarmCause::COMM_TIMEOUT) {
            return trip_unlocked(AlarmCause::COMM_TIMEOUT, err, now);
        }
    }
    return std::nullopt;
}

std::optional<AlarmEvent> SafetyMonitor::evaluate_stale(MonotonicNs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (consecutive_failures_ == 0 && last_progress_ns_.has_value() && (now >= *last_progress_ns_) &&
        (now - *last_progress_ns_ >= heartbeat_timeout_ns_)) {
        if (!alarm_active_ || current_cause_ != AlarmCause::HEARTBEAT_STALE) {
            return trip_unlocked(
                AlarmCause::HEARTBEAT_STALE,
                GatewayError(ErrorCode::HEARTBEAT_STALE, "Heartbeat stale timeout exceeded"),
                now);
        }
    }
    return std::nullopt;
}

std::optional<AlarmEvent> SafetyMonitor::trip(AlarmCause cause,
                                              const GatewayError& err,
                                              MonotonicNs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return trip_unlocked(cause, err, now);
}

Result<AlarmEvent> SafetyMonitor::clear_alarm(bool force, MonotonicNs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!force) {
        if (!has_sample_) {
            return Result<AlarmEvent>::err(
                GatewayError(ErrorCode::NOT_READY, "Cannot clear alarm: no sample observed yet"));
        }
        if (consecutive_failures_ >= consecutive_failures_limit_) {
            return Result<AlarmEvent>::err(
                GatewayError(ErrorCode::NOT_READY, "Cannot clear alarm: consecutive communication failures active"));
        }
        if (last_progress_ns_.has_value() && (now >= *last_progress_ns_) &&
            (now - *last_progress_ns_ >= heartbeat_timeout_ns_)) {
            return Result<AlarmEvent>::err(
                GatewayError(ErrorCode::HEARTBEAT_STALE, "Cannot clear alarm: heartbeat is currently stale"));
        }
        if (last_physical_estop_ || last_fault_code_ != 0 || last_alarm_tripped_) {
            return Result<AlarmEvent>::err(
                GatewayError(ErrorCode::INTERLOCK_ACTIVE, "Cannot clear alarm: physical E-Stop, PLC fault, or alarm active"));
        }
    }

    bool was_alarm_active = alarm_active_;
    alarm_active_ = false;
    current_cause_ = AlarmCause::NONE;
    last_error_ = GatewayError(ErrorCode::OK, "Alarm cleared");
    if (was_alarm_active) {
        alarm_sequence_++;
    }

    AlarmEvent ev;
    ev.station_id = station_id_;
    ev.sequence = alarm_sequence_;
    ev.active = false;
    ev.cause = AlarmCause::NONE;
    ev.error = last_error_;
    ev.generation = generation_;
    ev.detected_ns = now;
    ev.last_progress_ns = last_progress_ns_;
    return Result<AlarmEvent>::ok(ev);
}

bool SafetyMonitor::is_alarm_active() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return alarm_active_;
}

AlarmCause SafetyMonitor::current_cause() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_cause_;
}

GatewayError SafetyMonitor::last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

uint8_t SafetyMonitor::consecutive_failures() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return consecutive_failures_;
}

std::optional<MonotonicNs> SafetyMonitor::last_progress_ns() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_progress_ns_;
}

std::optional<uint16_t> SafetyMonitor::last_heartbeat() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_heartbeat_;
}

uint64_t SafetyMonitor::alarm_sequence() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return alarm_sequence_;
}

StationId SafetyMonitor::station_id() const noexcept {
    return station_id_;
}

Generation SafetyMonitor::generation() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

void SafetyMonitor::set_generation(Generation gen) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = gen;
}

bool SafetyMonitor::has_sample() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_sample_;
}

void SafetyMonitor::reset_heartbeat() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    last_heartbeat_.reset();
    last_progress_ns_.reset();
    has_sample_ = false;
    last_physical_estop_ = false;
    last_fault_code_ = 0;
    last_alarm_tripped_ = false;
}

} // namespace ros2_modbus_gateway

