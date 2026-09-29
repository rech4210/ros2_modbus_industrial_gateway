#pragma once

#include "ros2_modbus_gateway/types.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/station_context.hpp"

#include <boost/asio.hpp>
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <vector>

namespace ros2_modbus_gateway {

class GatewayRuntime {
public:
    explicit GatewayRuntime(const GatewayConfig& config);
    ~GatewayRuntime();

    GatewayRuntime(const GatewayRuntime&) = delete;
    GatewayRuntime& operator=(const GatewayRuntime&) = delete;

    // Specification §2.5 Methods
    Result<void> start();
    Result<CommandId> submit(StationId sid, const CommandRequest& req);
    Result<void> clear_fault(StationId sid, bool force);
    void set_alarm_callback(AlarmCallback cb);
    void set_command_callback(CommandCallback cb);
    void request_stop() noexcept;
    void join();

    // Inspection & State Helpers
    GatewayStatus get_status(StationId sid = 1) const;
    std::vector<GatewayStatus> get_all_statuses() const;
    bool is_running() const noexcept { return running_.load(); }
    const GatewayConfig& config() const noexcept { return config_; }

private:
    GatewayConfig config_;
    boost::asio::io_context io_context_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;
    std::thread io_thread_;

    std::unordered_map<StationId, std::unique_ptr<StationContext>> stations_;
    AlarmCallback alarm_callback_{};
    CommandCallback command_callback_{};

    std::atomic<bool> running_{false};
};

} // namespace ros2_modbus_gateway
