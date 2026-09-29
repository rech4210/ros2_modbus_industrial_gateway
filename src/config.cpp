#include "ros2_modbus_gateway/config.hpp"
#include <unordered_set>
#include <string>

namespace ros2_modbus_gateway {

Result<void> validate_station_config(const StationConfig& cfg) {
    if (cfg.station_id < 1 || cfg.station_id > 247) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "station_id must be between 1 and 247"));
    }
    if (cfg.plc_host.empty() || cfg.plc_host.size() > 253) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "plc_host must be between 1 and 253 characters"));
    }
    if (cfg.plc_port == 0) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "plc_port must be between 1 and 65535"));
    }
    if (cfg.unit_id < 1 || cfg.unit_id > 247) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "unit_id must be between 1 and 247"));
    }
    if (cfg.coil_base > 65534) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "coil_base + 1 exceeds 65535"));
    }
    if (cfg.holding_base > 65530) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "holding_base + 5 exceeds 65535"));
    }
    if (cfg.poll_period_ms < 10 || cfg.poll_period_ms > 100) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "poll_period_ms must be between 10 and 100"));
    }
    if (cfg.publish_period_ms < 10 || cfg.publish_period_ms > 100) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "publish_period_ms must be between 10 and 100"));
    }
    if (cfg.response_timeout_ms < 10 || cfg.response_timeout_ms > 100) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "response_timeout_ms must be between 10 and 100"));
    }
    if (cfg.connect_timeout_ms < 50 || cfg.connect_timeout_ms > 1000) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "connect_timeout_ms must be between 50 and 1000"));
    }
    if (cfg.consecutive_failures_limit < 2 || cfg.consecutive_failures_limit > 10) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "consecutive_failures_limit must be between 2 and 10"));
    }
    if (cfg.heartbeat_timeout_ms < 40 || cfg.heartbeat_timeout_ms > 300) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "heartbeat_timeout_ms must be between 40 and 300"));
    }
    if (cfg.command_timeout_ms < 100 || cfg.command_timeout_ms > 1000) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "command_timeout_ms must be between 100 and 1000"));
    }
    if (cfg.recovery_progress_count < 1 || cfg.recovery_progress_count > 5) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "recovery_progress_count must be between 1 and 5"));
    }

    return Result<void>::ok();
}

Result<void> validate_config(const GatewayConfig& config) {
    if (config.instance_name.empty() || config.instance_name.size() > 64) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "instance_name must be between 1 and 64 characters"));
    }
    if (config.stations.empty()) {
        return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "stations list cannot be empty"));
    }

    std::unordered_set<StationId> seen_station_ids;
    std::unordered_set<std::string> seen_endpoints;

    for (const auto& station : config.stations) {
        auto station_res = validate_station_config(station);
        if (!station_res) {
            return station_res;
        }

        if (seen_station_ids.find(station.station_id) != seen_station_ids.end()) {
            return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "Duplicate station_id: " + std::to_string(station.station_id)));
        }
        seen_station_ids.insert(station.station_id);

        // Host:Port:UnitID uniqueness check
        std::string endpoint = station.plc_host + ":" + std::to_string(station.plc_port) + ":" + std::to_string(station.unit_id);
        if (seen_endpoints.find(endpoint) != seen_endpoints.end()) {
            return Result<void>::err(GatewayError(ErrorCode::INVALID_CONFIG, "Duplicate host:port and unit_id: " + endpoint));
        }
        seen_endpoints.insert(endpoint);
    }

    return Result<void>::ok();
}

} // namespace ros2_modbus_gateway
