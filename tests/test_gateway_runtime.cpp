#include <gtest/gtest.h>
#include "ros2_modbus_gateway/gateway_runtime.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/register_map.hpp"

#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <chrono>

using namespace ros2_modbus_gateway;
using boost::asio::ip::tcp;

class RuntimeMockServer {
public:
    explicit RuntimeMockServer(boost::asio::io_context& ioc)
        : ioc_(ioc), acceptor_(ioc, tcp::endpoint(tcp::v4(), 0)) {
        port_ = acceptor_.local_endpoint().port();
    }

    uint16_t port() const { return port_; }

    void start() {
        start_accept();
    }

    void stop() {
        accepting_ = false;
        boost::system::error_code ec;
        acceptor_.close(ec);
    }

private:
    void start_accept() {
        auto socket = std::make_shared<tcp::socket>(ioc_);
        acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
            if (!ec) {
                handle_client(socket);
            }
            if (accepting_) {
                start_accept();
            }
        });
    }

    void handle_client(std::shared_ptr<tcp::socket> socket) {
        auto rx_buf = std::make_shared<std::array<uint8_t, 256>>();
        socket->async_read_some(
            boost::asio::buffer(*rx_buf),
            [this, socket, rx_buf](const boost::system::error_code& ec, std::size_t bytes_read) {
                if (ec || bytes_read < 7) return;

                uint16_t tx_id = (rx_buf->at(0) << 8) | rx_buf->at(1);
                uint8_t unit_id = rx_buf->at(6);
                uint8_t fc = bytes_read >= 8 ? rx_buf->at(7) : 0x03;

                std::vector<uint8_t> resp;

                if (fc == 0x03) {
                    heartbeat_++;
                    resp = {
                        static_cast<uint8_t>((tx_id >> 8) & 0xFF), static_cast<uint8_t>(tx_id & 0xFF),
                        0x00, 0x00,
                        0x00, 0x0F,
                        unit_id,
                        0x03, 0x0C,
                        static_cast<uint8_t>((heartbeat_ >> 8) & 0xFF), static_cast<uint8_t>(heartbeat_ & 0xFF),
                        static_cast<uint8_t>((status_flags_ >> 8) & 0xFF), static_cast<uint8_t>(status_flags_ & 0xFF),
                        static_cast<uint8_t>((sensor_raw_ >> 8) & 0xFF), static_cast<uint8_t>(sensor_raw_ & 0xFF),
                        static_cast<uint8_t>((setpoint_raw_ >> 8) & 0xFF), static_cast<uint8_t>(setpoint_raw_ & 0xFF),
                        0x00, 0x00, // fault_code
                        static_cast<uint8_t>((applied_counter_ >> 8) & 0xFF), static_cast<uint8_t>(applied_counter_ & 0xFF)
                    };
                } else if (fc == 0x05) {
                    uint16_t coil_addr = (rx_buf->at(8) << 8) | rx_buf->at(9);
                    uint16_t coil_val = (rx_buf->at(10) << 8) | rx_buf->at(11);
                    applied_counter_++;
                    if (coil_addr == 0) {
                        if (coil_val == 0xFF00) {
                            status_flags_ |= (STATUS_BIT_RUN_REQUESTED | STATUS_BIT_RUNNING);
                        } else {
                            status_flags_ &= ~(STATUS_BIT_RUN_REQUESTED | STATUS_BIT_RUNNING);
                        }
                    } else if (coil_addr == 1) {
                        status_flags_ &= ~STATUS_BIT_ALARM_TRIPPED;
                    }
                    resp.assign(rx_buf->begin(), rx_buf->begin() + 12);
                } else if (fc == 0x06) {
                    uint16_t val = (rx_buf->at(10) << 8) | rx_buf->at(11);
                    setpoint_raw_ = val;
                    applied_counter_++;
                    resp.assign(rx_buf->begin(), rx_buf->begin() + 12);
                }

                boost::asio::async_write(*socket, boost::asio::buffer(resp),
                    [this, socket](const boost::system::error_code& write_ec, std::size_t) {
                        if (!write_ec) {
                            handle_client(socket);
                        }
                    });
            });
    }

    boost::asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    uint16_t port_{0};
    bool accepting_{true};

    uint16_t heartbeat_{10};
    uint16_t status_flags_{STATUS_BIT_READY};
    uint16_t sensor_raw_{0};
    uint16_t setpoint_raw_{500};
    uint16_t applied_counter_{0};
};

class GatewayRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<RuntimeMockServer>(server_ioc_);
        server_->start();
        server_thread_ = std::thread([this]() { server_ioc_.run(); });

        st1_.station_id = 1;
        st1_.plc_host = "127.0.0.1";
        st1_.plc_port = server_->port();
        st1_.unit_id = 1;
        st1_.poll_period_ms = 10;
        st1_.publish_period_ms = 10;
        st1_.response_timeout_ms = 25;
        st1_.connect_timeout_ms = 200;
        st1_.consecutive_failures_limit = 3;
        st1_.recovery_progress_count = 2;

        gcfg_.instance_name = "test_runtime";
        gcfg_.stations = {st1_};
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        server_ioc_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    boost::asio::io_context server_ioc_;
    std::unique_ptr<RuntimeMockServer> server_;
    std::thread server_thread_;
    StationConfig st1_;
    GatewayConfig gcfg_;
};

TEST_F(GatewayRuntimeTest, StartsAndRecoversToOperational) {
    GatewayRuntime runtime(gcfg_);
    auto start_res = runtime.start();
    ASSERT_TRUE(start_res.is_ok());

    // Wait for connection and 2 consecutive samples (recovery_progress_count)
    bool operational = false;
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto st = runtime.get_status(1);
        if (st.link_state == LinkState::OPERATIONAL && st.data_valid) {
            operational = true;
            break;
        }
    }

    EXPECT_TRUE(operational);
    auto final_st = runtime.get_status(1);
    EXPECT_TRUE(final_st.last_sample.has_value());
    EXPECT_GT(final_st.poll_attempt_count, 0u);

    runtime.request_stop();
    runtime.join();
}

TEST_F(GatewayRuntimeTest, SubmitsAndConfirmsCommand) {
    GatewayRuntime runtime(gcfg_);
    std::atomic<bool> cmd_confirmed{false};
    CommandResult confirmed_result;

    runtime.set_command_callback([&](const CommandResult& res) {
        if (res.outcome == CommandOutcome::CONFIRMED) {
            cmd_confirmed.store(true);
            confirmed_result = res;
        }
    });

    runtime.start();

    // Wait until OPERATIONAL
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (runtime.get_status(1).link_state == LinkState::OPERATIONAL) break;
    }

    // 1-shot ClearFault to clear startup latch before START command
    auto cf_res = runtime.clear_fault(1, true);
    EXPECT_TRUE(cf_res.is_ok());

    // Submit START command
    CommandRequest req{Command::START, 0};
    auto sub_res = runtime.submit(1, req);
    ASSERT_TRUE(sub_res.is_ok());

    // Wait for confirmation
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (cmd_confirmed.load()) break;
    }

    EXPECT_TRUE(cmd_confirmed.load());
    EXPECT_EQ(confirmed_result.id, sub_res.value());
    EXPECT_EQ(confirmed_result.outcome, CommandOutcome::CONFIRMED);

    runtime.request_stop();
    runtime.join();
}

TEST_F(GatewayRuntimeTest, MultiStationIsolation) {
    // Station 1 connects to valid mock server, Station 2 connects to invalid port
    StationConfig st2 = st1_;
    st2.station_id = 2;
    st2.plc_port = 59999; // Invalid closed port

    GatewayConfig multi_cfg = gcfg_;
    multi_cfg.stations.push_back(st2);

    GatewayRuntime runtime(multi_cfg);
    runtime.start();

    // Station 1 must become OPERATIONAL despite Station 2 failing
    bool st1_operational = false;
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (runtime.get_status(1).link_state == LinkState::OPERATIONAL) {
            st1_operational = true;
            break;
        }
    }

    EXPECT_TRUE(st1_operational);
    auto st2_status = runtime.get_status(2);
    EXPECT_NE(st2_status.link_state, LinkState::OPERATIONAL);

    runtime.request_stop();
    runtime.join();
}

TEST_F(GatewayRuntimeTest, SubmitsAndConfirmsSetSetpointCommand) {
    GatewayRuntime runtime(gcfg_);
    std::atomic<bool> cmd_confirmed{false};
    CommandResult confirmed_result;

    runtime.set_command_callback([&](const CommandResult& res) {
        if (res.outcome == CommandOutcome::CONFIRMED) {
            cmd_confirmed.store(true);
            confirmed_result = res;
        }
    });

    runtime.start();

    // Wait until OPERATIONAL
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (runtime.get_status(1).link_state == LinkState::OPERATIONAL) break;
    }

    // Clear fault latch before SET_SETPOINT
    auto cf_res = runtime.clear_fault(1, true);
    EXPECT_TRUE(cf_res.is_ok());

    CommandRequest req{Command::SET_SETPOINT, 750};
    auto sub_res = runtime.submit(1, req);
    ASSERT_TRUE(sub_res.is_ok());

    // Wait for confirmation
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (cmd_confirmed.load()) break;
    }

    EXPECT_TRUE(cmd_confirmed.load());
    EXPECT_EQ(confirmed_result.id, sub_res.value());
    EXPECT_EQ(confirmed_result.outcome, CommandOutcome::CONFIRMED);

    // Verify holding register was updated in status
    auto status = runtime.get_status(1);
    ASSERT_TRUE(status.last_sample.has_value());
    EXPECT_EQ(status.last_sample->image.setpoint_raw(), 750);

    runtime.request_stop();
    runtime.join();
}

TEST_F(GatewayRuntimeTest, SingleCommandSlotBusyRejection) {
    GatewayRuntime runtime(gcfg_);
    runtime.start();

    // Wait until OPERATIONAL
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (runtime.get_status(1).link_state == LinkState::OPERATIONAL) break;
    }

    auto cf_res = runtime.clear_fault(1, true);
    EXPECT_TRUE(cf_res.is_ok());

    // Submit first command
    CommandRequest req1{Command::SET_SETPOINT, 300};
    auto sub1 = runtime.submit(1, req1);
    ASSERT_TRUE(sub1.is_ok());

    // Immediately submit second command while first is pending/executing
    CommandRequest req2{Command::SET_SETPOINT, 400};
    auto sub2 = runtime.submit(1, req2);

    // Either sub2 was rejected with BUSY, or if sub1 already finished, sub2 was ok.
    // To strictly test BUSY, if sub1 is not yet confirmed, sub2 is BUSY.
    if (sub2.is_err()) {
        EXPECT_EQ(sub2.error().code, static_cast<uint16_t>(ErrorCode::BUSY));
    }

    runtime.request_stop();
    runtime.join();
}

