#include <gtest/gtest.h>
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/monotonic_clock.hpp"

using namespace ros2_modbus_gateway;

TEST(ConfigTest, DefaultStationConfigIsValid) {
    StationConfig cfg;
    auto res = validate_station_config(cfg);
    EXPECT_TRUE(res.is_ok());
}

TEST(ConfigTest, DefaultGatewayConfigIsValid) {
    GatewayConfig cfg;
    cfg.stations.push_back(StationConfig{});
    auto res = validate_config(cfg);
    EXPECT_TRUE(res.is_ok());
}

TEST(ConfigTest, EmptyStationsFails) {
    GatewayConfig cfg;
    auto res = validate_config(cfg);
    EXPECT_TRUE(res.is_err());
    EXPECT_EQ(res.error().code, static_cast<uint16_t>(ErrorCode::INVALID_CONFIG));
}

TEST(ConfigTest, InstanceNameBoundaries) {
    GatewayConfig cfg;
    cfg.stations.push_back(StationConfig{});

    cfg.instance_name = "";
    EXPECT_TRUE(validate_config(cfg).is_err());

    cfg.instance_name = std::string(64, 'a');
    EXPECT_TRUE(validate_config(cfg).is_ok());

    cfg.instance_name = std::string(65, 'a');
    EXPECT_TRUE(validate_config(cfg).is_err());
}

TEST(ConfigTest, StationIdBoundaries) {
    StationConfig cfg;
    cfg.station_id = 0;
    EXPECT_TRUE(validate_station_config(cfg).is_err());

    cfg.station_id = 1;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());

    cfg.station_id = 247;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());

    cfg.station_id = 248;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
}

TEST(ConfigTest, PlcHostBoundaries) {
    StationConfig cfg;
    cfg.plc_host = "";
    EXPECT_TRUE(validate_station_config(cfg).is_err());

    cfg.plc_host = std::string(253, 'a');
    EXPECT_TRUE(validate_station_config(cfg).is_ok());

    cfg.plc_host = std::string(254, 'a');
    EXPECT_TRUE(validate_station_config(cfg).is_err());
}

TEST(ConfigTest, PlcPortBoundaries) {
    StationConfig cfg;
    cfg.plc_port = 0;
    EXPECT_TRUE(validate_station_config(cfg).is_err());

    cfg.plc_port = 1;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());

    cfg.plc_port = 65535;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
}

TEST(ConfigTest, UnitIdBoundaries) {
    StationConfig cfg;
    cfg.unit_id = 0;
    EXPECT_TRUE(validate_station_config(cfg).is_err());

    cfg.unit_id = 1;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());

    cfg.unit_id = 247;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());

    cfg.unit_id = 248;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
}

TEST(ConfigTest, AddressBaseBoundaries) {
    StationConfig cfg;
    cfg.coil_base = 65534;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.coil_base = 65535;
    EXPECT_TRUE(validate_station_config(cfg).is_err());

    cfg.coil_base = 0;
    cfg.holding_base = 65530;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.holding_base = 65531;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
}

TEST(ConfigTest, TimingBoundaries) {
    StationConfig cfg;

    // poll_period_ms: 10~100
    cfg.poll_period_ms = 9;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.poll_period_ms = 10;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.poll_period_ms = 100;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.poll_period_ms = 101;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.poll_period_ms = 20;

    // publish_period_ms: 10~100
    cfg.publish_period_ms = 9;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.publish_period_ms = 10;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.publish_period_ms = 100;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.publish_period_ms = 101;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.publish_period_ms = 20;

    // response_timeout_ms: 10~100
    cfg.response_timeout_ms = 9;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.response_timeout_ms = 10;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.response_timeout_ms = 100;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.response_timeout_ms = 101;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.response_timeout_ms = 25;

    // connect_timeout_ms: 50~1000
    cfg.connect_timeout_ms = 49;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.connect_timeout_ms = 50;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.connect_timeout_ms = 1000;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.connect_timeout_ms = 1001;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.connect_timeout_ms = 200;

    // consecutive_failures_limit: 2~10
    cfg.consecutive_failures_limit = 1;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.consecutive_failures_limit = 2;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.consecutive_failures_limit = 10;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.consecutive_failures_limit = 11;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.consecutive_failures_limit = 3;

    // heartbeat_timeout_ms: 40~300
    cfg.heartbeat_timeout_ms = 39;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.heartbeat_timeout_ms = 40;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.heartbeat_timeout_ms = 300;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.heartbeat_timeout_ms = 301;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.heartbeat_timeout_ms = 80;

    // command_timeout_ms: 100~1000
    cfg.command_timeout_ms = 99;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.command_timeout_ms = 100;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.command_timeout_ms = 1000;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.command_timeout_ms = 1001;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.command_timeout_ms = 300;

    // recovery_progress_count: 1~5
    cfg.recovery_progress_count = 0;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.recovery_progress_count = 1;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.recovery_progress_count = 5;
    EXPECT_TRUE(validate_station_config(cfg).is_ok());
    cfg.recovery_progress_count = 6;
    EXPECT_TRUE(validate_station_config(cfg).is_err());
    cfg.recovery_progress_count = 2;
}

TEST(ConfigTest, MultiStationValidation) {
    GatewayConfig cfg;
    StationConfig st1;
    st1.station_id = 1;
    st1.plc_host = "mock_plc";
    st1.plc_port = 5020;
    st1.unit_id = 1;

    StationConfig st2;
    st2.station_id = 2;
    st2.plc_host = "plc_cell_2";
    st2.plc_port = 5020;
    st2.unit_id = 1;

    cfg.stations.push_back(st1);
    cfg.stations.push_back(st2);
    EXPECT_TRUE(validate_config(cfg).is_ok());

    // Duplicate station_id fails
    cfg.stations[1].station_id = 1;
    EXPECT_TRUE(validate_config(cfg).is_err());

    // Same host and port with same unit_id fails
    cfg.stations[1].station_id = 2;
    cfg.stations[1].plc_host = "mock_plc";
    EXPECT_TRUE(validate_config(cfg).is_err());

    // Same host and port with DIFFERENT unit_id passes
    cfg.stations[1].unit_id = 2;
    EXPECT_TRUE(validate_config(cfg).is_ok());
}

TEST(ClockTest, MonotonicClockNowNs) {
    MonotonicNs t1 = Clock::now_ns();
    EXPECT_GT(t1, 0ULL);

    MonotonicNs t2 = Clock::now_ns();
    EXPECT_GE(t2, t1);
}
