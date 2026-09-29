#include "ros2_modbus_gateway/gateway_node.hpp"
#include <rclcpp/rclcpp.hpp>
#include <csignal>
#include <iostream>

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);

    try {
        auto node = std::make_shared<ros2_modbus_gateway::GatewayNode>();
        RCLCPP_INFO(node->get_logger(), "Starting GatewayNode with Boost.Asio I/O engine...");
        rclcpp::spin(node);
    } catch (const std::exception& e) {
        std::cerr << "Fatal exception in GatewayNode: " << e.what() << std::endl;
        rclcpp::shutdown();
        return 1;
    }

    rclcpp::shutdown();
    return 0;
}
