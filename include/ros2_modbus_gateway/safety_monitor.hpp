#pragma once

#include "ros2_modbus_gateway/types.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/register_map.hpp"
#include <mutex>
#include <optional>

namespace ros2_modbus_gateway {

class SafetyMonitor {
public:
    explicit SafetyMonitor(const StationConfig& config);
    SafetyMonitor(StationId station_id = 1,
                  uint8_t consecutive_failures_limit = 3,
                  uint16_t heartbeat_timeout_ms = 80);

    // Core Safety Assessment Methods
    std::optional<AlarmEvent> observe(const PlcSample& sample, MonotonicNs now);
    std::optional<AlarmEvent> on_io_failure(const GatewayError& err, MonotonicNs now);
    std::optional<AlarmEvent> evaluate_stale(MonotonicNs now);
    std::optional<AlarmEvent> trip(AlarmCause cause, const GatewayError& err, MonotonicNs now);
    Result<AlarmEvent> clear_alarm(bool force, MonotonicNs now);

    // State Accessors
    bool is_alarm_active() const noexcept;
    AlarmCause current_cause() const noexcept;
    GatewayError last_error() const;
    uint8_t consecutive_failures() const noexcept;
    std::optional<MonotonicNs> last_progress_ns() const noexcept;
    std::optional<uint16_t> last_heartbeat() const noexcept;
    uint64_t alarm_sequence() const noexcept;
    StationId station_id() const noexcept;
    Generation generation() const noexcept;
    bool has_sample() const noexcept;

    // Mutators
    void set_generation(Generation gen) noexcept;
    void reset_heartbeat() noexcept;

private:
    std::optional<AlarmEvent> trip_unlocked(AlarmCause cause, const GatewayError& err, MonotonicNs now);

    mutable std::mutex mutex_;
    StationId station_id_{1};
    uint8_t consecutive_failures_limit_{3};
    DurationNs heartbeat_timeout_ns_{80'000'000ULL};

    uint8_t consecutive_failures_{0};
    bool alarm_active_{true}; // Startup safety latch
    AlarmCause current_cause_{AlarmCause::STARTUP};
    GatewayError last_error_{static_cast<uint16_t>(ErrorCode::NOT_READY), "Initial startup safety latch active"};
    uint64_t alarm_sequence_{0};
    Generation generation_{0};

    std::optional<uint16_t> last_heartbeat_{};
    std::optional<MonotonicNs> last_progress_ns_{};
    bool last_physical_estop_{false};
    uint16_t last_fault_code_{0};
    bool last_alarm_tripped_{false};
    bool has_sample_{false};
};

} // namespace ros2_modbus_gateway

