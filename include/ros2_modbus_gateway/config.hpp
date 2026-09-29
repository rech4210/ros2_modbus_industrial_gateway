#pragma once

#include "ros2_modbus_gateway/types.hpp"
#include <string>
#include <vector>

namespace ros2_modbus_gateway {

struct StationConfig {
    StationId station_id{1};
    std::string plc_host{"mock_plc"};
    uint16_t plc_port{5020};
    uint8_t unit_id{1};
    uint16_t coil_base{0};
    uint16_t holding_base{0};
    uint16_t poll_period_ms{20};
    uint16_t publish_period_ms{20};
    uint16_t response_timeout_ms{25};
    uint16_t connect_timeout_ms{200};
    uint8_t consecutive_failures_limit{3};
    uint16_t heartbeat_timeout_ms{80};
    uint16_t command_timeout_ms{300};
    uint8_t recovery_progress_count{2};
};

struct GatewayConfig {
    std::string instance_name{"gateway_node"};
    std::vector<StationConfig> stations;

    const StationConfig& default_station() const {
        if (stations.empty()) {
            throw std::runtime_error("No stations configured");
        }
        return stations.at(0);
    }
};

Result<void> validate_station_config(const StationConfig& cfg);
Result<void> validate_config(const GatewayConfig& config);

} // namespace ros2_modbus_gateway
