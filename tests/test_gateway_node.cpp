#include <gtest/gtest.h>
#include "ros2_modbus_gateway/gateway_node.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/register_map.hpp"

#include <rclcpp/rclcpp.hpp>
#include <chrono>
#include <thread>
#include <atomic>

using namespace ros2_modbus_gateway;

class GatewayNodeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        rclcpp::init(0, nullptr);
    }

    static void TearDownTestSuite() {
        rclcpp::shutdown();
    }
};

TEST_F(GatewayNodeTest, NodeInitializesPublishersAndServices) {
    StationConfig st_cfg;
    st_cfg.station_id = 1;
    st_cfg.plc_host = "127.0.0.1";
    st_cfg.plc_port = 5020;
    st_cfg.poll_period_ms = 20;
    st_cfg.publish_period_ms = 20;

    GatewayConfig cfg;
    cfg.instance_name = "test_node";
    cfg.stations = {st_cfg};

    auto node = std::make_shared<GatewayNode>(cfg);
    ASSERT_NE(node, nullptr);

    // Verify topic graph contains our endpoints
    auto topic_names_and_types = node->get_topic_names_and_types();
    EXPECT_NE(topic_names_and_types.find("/plc/state"), topic_names_and_types.end());
    EXPECT_NE(topic_names_and_types.find("/safety/alarm"), topic_names_and_types.end());

    auto service_names_and_types = node->get_service_names_and_types();
    EXPECT_NE(service_names_and_types.find("/plc/trigger_command"), service_names_and_types.end());
    EXPECT_NE(service_names_and_types.find("/plc/command"), service_names_and_types.end());
    EXPECT_NE(service_names_and_types.find("/plc/clear_fault"), service_names_and_types.end());
}

TEST_F(GatewayNodeTest, PublishesInitialStartupSafetyAlarmViaTransientLocal) {
    StationConfig st_cfg;
    st_cfg.station_id = 1;
    st_cfg.plc_host = "127.0.0.1";
    st_cfg.plc_port = 5020;

    GatewayConfig cfg;
    cfg.instance_name = "test_node_alarm";
    cfg.stations = {st_cfg};

    auto node = std::make_shared<GatewayNode>(cfg);

    // Create a subscriber with Transient-Local QoS to verify startup alarm reception
    auto test_sub_node = std::make_shared<rclcpp::Node>("alarm_sub_node");
    rclcpp::QoS qos(1);
    qos.reliable();
    qos.transient_local();

    std::atomic<bool> alarm_received{false};
    SafetyAlarm received_alarm;

    auto sub = test_sub_node->create_subscription<SafetyAlarm>(
        "/safety/alarm", qos,
        [&](const SafetyAlarm::SharedPtr msg) {
            alarm_received = true;
            received_alarm = *msg;
        });

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(test_sub_node);

    for (int i = 0; i < 50; ++i) {
        executor.spin_some();
        if (alarm_received.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(alarm_received.load());
    EXPECT_TRUE(received_alarm.active);
    EXPECT_EQ(received_alarm.cause, static_cast<uint8_t>(AlarmCause::STARTUP));
    EXPECT_EQ(received_alarm.station_id, 1);
}

TEST_F(GatewayNodeTest, ClearFaultServiceResponds) {
    StationConfig st_cfg;
    st_cfg.station_id = 1;
    st_cfg.plc_host = "127.0.0.1";
    st_cfg.plc_port = 5020;

    GatewayConfig cfg;
    cfg.instance_name = "test_node_cf";
    cfg.stations = {st_cfg};

    auto node = std::make_shared<GatewayNode>(cfg);
    auto client_node = std::make_shared<rclcpp::Node>("cf_client_node");

    auto client = client_node->create_client<ClearFault>("/plc/clear_fault");
    ASSERT_TRUE(client->wait_for_service(std::chrono::seconds(1)));

    auto req = std::make_shared<ClearFault::Request>();
    req->force_clear = true; // Force clear startup alarm

    auto future = client->async_send_request(req);

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(client_node);

    bool received = false;
    for (int i = 0; i < 50; ++i) {
        executor.spin_some();
        if (future.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready) {
            received = true;
            break;
        }
    }

    EXPECT_TRUE(received);
    auto resp = future.get();
    EXPECT_TRUE(resp->success);
    EXPECT_EQ(resp->error_code, 0);
}

TEST_F(GatewayNodeTest, TriggerCommandRejectsInvalidArgumentImmediately) {
    StationConfig st_cfg;
    st_cfg.station_id = 1;
    st_cfg.plc_host = "127.0.0.1";
    st_cfg.plc_port = 5020;

    GatewayConfig cfg;
    cfg.instance_name = "test_node_cmd_inv";
    cfg.stations = {st_cfg};

    auto node = std::make_shared<GatewayNode>(cfg);
    auto client_node = std::make_shared<rclcpp::Node>("cmd_client_node");

    auto client = client_node->create_client<TriggerCommand>("/plc/trigger_command");
    ASSERT_TRUE(client->wait_for_service(std::chrono::seconds(1)));

    auto req = std::make_shared<TriggerCommand::Request>();
    req->command = 99; // Invalid command enum

    auto future = client->async_send_request(req);

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(client_node);

    bool received = false;
    for (int i = 0; i < 50; ++i) {
        executor.spin_some();
        if (future.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready) {
            received = true;
            break;
        }
    }

    EXPECT_TRUE(received);
    auto resp = future.get();
    EXPECT_FALSE(resp->success);
    EXPECT_EQ(resp->outcome, static_cast<uint8_t>(CommandOutcome::REJECTED));
    EXPECT_EQ(resp->error_code, static_cast<uint16_t>(ErrorCode::INVALID_ARGUMENT));
}

TEST_F(GatewayNodeTest, GuardConditionTriggersImmediateAlarmPublish) {
    StationConfig st_cfg;
    st_cfg.station_id = 1;
    st_cfg.plc_host = "127.0.0.1";
    st_cfg.plc_port = 5020;

    GatewayConfig cfg;
    cfg.instance_name = "test_node_guard";
    cfg.stations = {st_cfg};

    auto node = std::make_shared<GatewayNode>(cfg);
    auto test_sub_node = std::make_shared<rclcpp::Node>("guard_sub_node");

    rclcpp::QoS qos(1);
    qos.reliable();
    qos.transient_local();

    std::vector<SafetyAlarm> received_alarms;
    auto sub = test_sub_node->create_subscription<SafetyAlarm>(
        "/safety/alarm", qos,
        [&](const SafetyAlarm::SharedPtr msg) {
            received_alarms.push_back(*msg);
        });

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(test_sub_node);

    // Wait for startup alarm
    for (int i = 0; i < 30; ++i) {
        executor.spin_some();
        if (!received_alarms.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_FALSE(received_alarms.empty());

    // Trigger an alarm event manually
    AlarmEvent comm_timeout_ev;
    comm_timeout_ev.station_id = 1;
    comm_timeout_ev.sequence = 2;
    comm_timeout_ev.active = true;
    comm_timeout_ev.cause = AlarmCause::COMM_TIMEOUT;
    comm_timeout_ev.error = GatewayError(ErrorCode::IO_TIMEOUT, "Simulated 3x timeout");
    comm_timeout_ev.detected_ns = Clock::now_ns();

    node->on_alarm_event(comm_timeout_ev);

    // Spin executor to observe GuardCondition trigger
    for (int i = 0; i < 30; ++i) {
        executor.spin_some();
        if (received_alarms.size() >= 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_GE(received_alarms.size(), 2u);
    EXPECT_EQ(received_alarms.back().cause, static_cast<uint8_t>(AlarmCause::COMM_TIMEOUT));
    EXPECT_EQ(received_alarms.back().error_code, static_cast<uint16_t>(ErrorCode::IO_TIMEOUT));
}

TEST_F(GatewayNodeTest, TriggerCommandRejectsInvalidValueImmediately) {
    StationConfig st_cfg;
    st_cfg.station_id = 1;
    st_cfg.plc_host = "127.0.0.1";
    st_cfg.plc_port = 5020;

    GatewayConfig cfg;
    cfg.instance_name = "test_node_val_inv";
    cfg.stations = {st_cfg};

    auto node = std::make_shared<GatewayNode>(cfg);
    auto client_node = std::make_shared<rclcpp::Node>("val_client_node");

    auto client = client_node->create_client<TriggerCommand>("/plc/trigger_command");
    ASSERT_TRUE(client->wait_for_service(std::chrono::seconds(1)));

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(client_node);

    // Case 1: START command with non-zero value
    auto req1 = std::make_shared<TriggerCommand::Request>();
    req1->command = 1; // START
    req1->value = 100; // Must be 0!

    auto future1 = client->async_send_request(req1);
    for (int i = 0; i < 30; ++i) {
        executor.spin_some();
        if (future1.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready) break;
    }
    ASSERT_EQ(future1.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    auto resp1 = future1.get();
    EXPECT_FALSE(resp1->success);
    EXPECT_EQ(resp1->error_code, static_cast<uint16_t>(ErrorCode::INVALID_ARGUMENT));

    // Case 2: SET_SETPOINT command with value > 1000
    auto req2 = std::make_shared<TriggerCommand::Request>();
    req2->command = 4; // SET_SETPOINT
    req2->value = 1500; // Exceeds 1000!

    auto future2 = client->async_send_request(req2);
    for (int i = 0; i < 30; ++i) {
        executor.spin_some();
        if (future2.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready) break;
    }
    ASSERT_EQ(future2.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    auto resp2 = future2.get();
    EXPECT_FALSE(resp2->success);
    EXPECT_EQ(resp2->error_code, static_cast<uint16_t>(ErrorCode::INVALID_ARGUMENT));
}

TEST_F(GatewayNodeTest, MultiStationEndpointsCreated) {
    StationConfig st1;
    st1.station_id = 1;
    st1.plc_host = "127.0.0.1";
    st1.plc_port = 5020;

    StationConfig st2;
    st2.station_id = 2;
    st2.plc_host = "127.0.0.1";
    st2.plc_port = 5021;
    st2.unit_id = 2;

    GatewayConfig cfg;
    cfg.instance_name = "test_node_multi";
    cfg.stations = {st1, st2};

    auto node = std::make_shared<GatewayNode>(cfg);
    ASSERT_NE(node, nullptr);

    auto topic_names_and_types = node->get_topic_names_and_types();
    EXPECT_NE(topic_names_and_types.find("/station_1/plc/state"), topic_names_and_types.end());
    EXPECT_NE(topic_names_and_types.find("/station_2/plc/state"), topic_names_and_types.end());
    EXPECT_NE(topic_names_and_types.find("/station_1/safety/alarm"), topic_names_and_types.end());
    EXPECT_NE(topic_names_and_types.find("/station_2/safety/alarm"), topic_names_and_types.end());

    auto service_names_and_types = node->get_service_names_and_types();
    EXPECT_NE(service_names_and_types.find("/station_1/plc/trigger_command"), service_names_and_types.end());
    EXPECT_NE(service_names_and_types.find("/station_2/plc/trigger_command"), service_names_and_types.end());
    EXPECT_NE(service_names_and_types.find("/station_1/plc/clear_fault"), service_names_and_types.end());
    EXPECT_NE(service_names_and_types.find("/station_2/plc/clear_fault"), service_names_and_types.end());
}

