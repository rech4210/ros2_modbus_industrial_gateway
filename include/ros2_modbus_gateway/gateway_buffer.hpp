#pragma once

#include "ros2_modbus_gateway/types.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/safety_monitor.hpp"
#include <mutex>
#include <optional>

namespace ros2_modbus_gateway {

class GatewayBuffer {
public:
    explicit GatewayBuffer(const StationConfig& config);
    explicit GatewayBuffer(StationId station_id = 1,
                           uint16_t poll_period_ms = 20,
                           uint8_t consecutive_failures_limit = 3,
                           uint16_t heartbeat_timeout_ms = 80,
                           uint16_t command_timeout_ms = 300,
                           uint8_t recovery_progress_count = 2);

    // Specification §2.5 Core Methods
    Generation begin_connection(MonotonicNs now);
    std::optional<AlarmEvent> accept_sample(const PlcSample& sample);
    std::optional<AlarmEvent> accept_sample(const PlcSample& sample, MonotonicNs now);
    std::optional<AlarmEvent> report_failure(Generation gen, const GatewayError& err, MonotonicNs now);
    std::optional<AlarmEvent> evaluate_stale(MonotonicNs now);
    Result<CommandId> submit_command(const CommandRequest& req, MonotonicNs now);
    std::optional<CommandTask> take_command(Generation gen, MonotonicNs now);
    void complete_command(const CommandResult& res);
    Result<AlarmEvent> clear_fault(bool force, MonotonicNs now);
    GatewayStatus get_status() const;
    void begin_shutdown(MonotonicNs now);

    // Alien Method Call Deadlock Prevention: callbacks invoked outside mutex
    void set_alarm_callback(AlarmCallback cb);
    void set_command_callback(CommandCallback cb);

    // Inspectability & Testing Helpers
    StationId station_id() const noexcept;
    Generation current_generation() const noexcept;
    LinkState link_state() const noexcept;
    bool has_sample() const noexcept;
    bool is_alarm_active() const noexcept;
    bool is_data_valid() const noexcept;
    std::optional<PlcSample> last_sample() const;
    std::optional<CommandTask> current_command() const;
    std::optional<CommandResult> last_command_result() const;
    const SafetyMonitor& safety_monitor() const noexcept;
    SafetyMonitor& safety_monitor() noexcept;

    void set_link_state(LinkState state);

private:
    mutable std::mutex mutex_;

    StationId station_id_{1};
    uint16_t poll_period_ms_{20};
    uint16_t command_timeout_ms_{300};
    uint8_t recovery_progress_count_{2};

    Generation current_generation_{0};
    LinkState link_state_{LinkState::DISCONNECTED};
    bool alarm_active_{true};
    bool data_valid_{false};
    bool has_sample_{false};

    std::optional<PlcSample> last_sample_{};
    GatewayError last_error_{};
    uint64_t poll_attempt_count_{0};
    uint64_t io_error_count_{0};
    uint8_t consecutive_success_count_{0};

    CommandId next_command_id_{0};
    std::optional<CommandTask> current_command_{};
    std::optional<CommandResult> last_command_result_{};

    SafetyMonitor safety_monitor_;

    AlarmCallback alarm_callback_{};
    CommandCallback command_callback_{};
};

} // namespace ros2_modbus_gateway
