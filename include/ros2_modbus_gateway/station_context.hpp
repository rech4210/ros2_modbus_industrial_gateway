#pragma once

#include "ros2_modbus_gateway/types.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/gateway_buffer.hpp"
#include "ros2_modbus_gateway/modbus_client.hpp"
#include "ros2_modbus_gateway/monotonic_clock.hpp"

#include <boost/asio.hpp>
#include <memory>
#include <atomic>

namespace ros2_modbus_gateway {

class StationContext {
public:
    StationContext(const StationConfig& config, boost::asio::io_context& ioc);
    ~StationContext();

    StationContext(const StationContext&) = delete;
    StationContext& operator=(const StationContext&) = delete;

    void start();
    void stop() noexcept;

    GatewayStatus get_status() const;
    Result<CommandId> submit_command(const CommandRequest& req, MonotonicNs now);
    Result<AlarmEvent> clear_fault(bool force, MonotonicNs now);

    void set_alarm_callback(AlarmCallback cb);
    void set_command_callback(CommandCallback cb);

    StationId station_id() const noexcept { return config_.station_id; }
    const StationConfig& config() const noexcept { return config_; }
    GatewayBuffer& buffer() noexcept { return buffer_; }
    ModbusClient& client() noexcept { return client_; }
    bool in_flight() const noexcept { return in_flight_; }


private:
    void schedule_reconnect(uint32_t delay_ms);
    void do_connect();
    void schedule_poll(uint32_t delay_ms);
    void do_poll();
    void execute_command_and_poll(CommandTask task, MonotonicNs poll_start, int64_t jitter);
    void read_holding_bulk_and_observe(MonotonicNs poll_start, int64_t jitter,
                                       std::optional<CommandTask> active_task = std::nullopt);
    void check_command_confirmation(const PlcSample& sample, MonotonicNs now);
    void schedule_watchdog();

    StationConfig config_;
    boost::asio::io_context& ioc_;
    GatewayBuffer buffer_;
    ModbusClient client_;

    boost::asio::steady_timer poll_timer_;
    boost::asio::steady_timer reconnect_timer_;
    boost::asio::steady_timer watchdog_timer_;

    std::atomic<bool> stopped_{false};
    bool in_flight_{false};
    uint32_t reconnect_backoff_ms_{100};
    MonotonicNs last_poll_start_ns_{0};
    SampleSequence last_sample_seq_{0};
    Generation current_generation_{0};
    std::chrono::steady_clock::time_point next_poll_time_{};
};


} // namespace ros2_modbus_gateway
