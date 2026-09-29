#pragma once

#include "ros2_modbus_gateway/types.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/gateway_runtime.hpp"
#include "ros2_modbus_gateway/monotonic_clock.hpp"

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/guard_condition.hpp>
#include <rclcpp/waitable.hpp>
#include "ros2_modbus_gateway/msg/plc_state.hpp"
#include "ros2_modbus_gateway/msg/safety_alarm.hpp"
#include "ros2_modbus_gateway/srv/trigger_command.hpp"
#include "ros2_modbus_gateway/srv/clear_fault.hpp"

#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <functional>

namespace ros2_modbus_gateway {

using PlcState = ros2_modbus_gateway::msg::PlcState;
using SafetyAlarm = ros2_modbus_gateway::msg::SafetyAlarm;
using TriggerCommand = ros2_modbus_gateway::srv::TriggerCommand;
using ClearFault = ros2_modbus_gateway::srv::ClearFault;

class AlarmWaitable : public rclcpp::Waitable {
public:
    AlarmWaitable(rclcpp::GuardCondition::SharedPtr gc, std::function<void()> cb)
        : gc_(std::move(gc)), callback_(std::move(cb)) {}

    void trigger() {
        if (gc_) {
            gc_->trigger();
        }
    }

    size_t get_number_of_ready_guard_conditions() override {
        return 1;
    }

    void add_to_wait_set(rcl_wait_set_t& wait_set) override {
        if (gc_) {
            rcl_ret_t ret = rcl_wait_set_add_guard_condition(&wait_set, &gc_->get_rcl_guard_condition(), &index_);
            (void)ret;
        }
    }

    bool is_ready(const rcl_wait_set_t& wait_set) override {
        if (gc_ && index_ < wait_set.size_of_guard_conditions) {
            return wait_set.guard_conditions[index_] != nullptr;
        }
        return false;
    }

    std::shared_ptr<void> take_data() override {
        return nullptr;
    }

    void execute(const std::shared_ptr<void>&) override {
        if (callback_) {
            callback_();
        }
    }

private:
    rclcpp::GuardCondition::SharedPtr gc_;
    std::function<void()> callback_;
    size_t index_{0};
};

class GatewayNode : public rclcpp::Node {
public:
    explicit GatewayNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    explicit GatewayNode(const GatewayConfig& config, const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
    ~GatewayNode() override;

    // Specification §2.5 Methods
    void on_publish_timer();
    void on_alarm_guard_condition();
    void on_alarm_event(const AlarmEvent& event);
    void on_command_completed(const CommandResult& res);
    void on_trigger_command(std::shared_ptr<rmw_request_id_t> hdr, std::shared_ptr<TriggerCommand::Request> req);
    void on_clear_fault(std::shared_ptr<rmw_request_id_t> hdr, std::shared_ptr<ClearFault::Request> req);
    void publish_state(const GatewayStatus& st, MonotonicNs pub_ns);
    void publish_alarm(const AlarmEvent& ev);
    void send_command_response(const CommandResult& res);

    GatewayRuntime& runtime() noexcept { return *runtime_; }
    const GatewayConfig& gateway_config() const noexcept { return config_; }

private:
    struct PendingCommand {
        CommandId id{0};
        std::shared_ptr<rmw_request_id_t> header;
        std::shared_ptr<rclcpp::Service<TriggerCommand>> service;
        MonotonicNs deadline_ns{0};
        StationId station_id{1};
        MonotonicNs submitted_ns{0};
    };

    struct StationEndpoints {
        StationId station_id{1};
        rclcpp::Publisher<PlcState>::SharedPtr plc_state_pub;
        rclcpp::Publisher<SafetyAlarm>::SharedPtr safety_alarm_pub;
        rclcpp::Service<TriggerCommand>::SharedPtr trigger_command_service;
        rclcpp::Service<TriggerCommand>::SharedPtr command_service_alias;
        rclcpp::Service<ClearFault>::SharedPtr clear_fault_service;
    };

    void init_node();
    void init_publishers_and_services();
    void load_parameters();
    void on_trigger_command_with_service(
        StationId sid,
        std::shared_ptr<rclcpp::Service<TriggerCommand>> srv,
        std::shared_ptr<rmw_request_id_t> hdr,
        std::shared_ptr<TriggerCommand::Request> req);
    void on_clear_fault_with_service(
        StationId sid,
        std::shared_ptr<rclcpp::Service<ClearFault>> srv,
        std::shared_ptr<rmw_request_id_t> hdr,
        std::shared_ptr<ClearFault::Request> req);

    GatewayConfig config_;
    std::unique_ptr<GatewayRuntime> runtime_;

    InstanceId instance_id_{};
    uint64_t publish_sequence_{0};

    rclcpp::TimerBase::SharedPtr publish_timer_;
    rclcpp::GuardCondition::SharedPtr alarm_guard_condition_;
    std::shared_ptr<AlarmWaitable> alarm_waitable_;

    // Endpoints (single station default or multi-station map)
    std::unordered_map<StationId, StationEndpoints> station_endpoints_;
    rclcpp::Publisher<PlcState>::SharedPtr default_plc_state_pub_;
    rclcpp::Publisher<SafetyAlarm>::SharedPtr default_safety_alarm_pub_;

    // Thread-safe queues and pending command tracking
    std::mutex alarm_queue_mutex_;
    std::vector<AlarmEvent> pending_alarms_;

    std::mutex pending_commands_mutex_;
    std::unordered_map<CommandId, PendingCommand> pending_commands_;
};


} // namespace ros2_modbus_gateway
