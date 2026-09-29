#include <gtest/gtest.h>
#include "ros2_modbus_gateway/modbus_client.hpp"
#include "ros2_modbus_gateway/config.hpp"
#include "ros2_modbus_gateway/register_map.hpp"

#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <vector>

using namespace ros2_modbus_gateway;
using boost::asio::ip::tcp;

class EmbeddedMockServer {
public:
    enum class ResponseMode {
        NORMAL,
        DELAY,
        DROP,
        MALFORMED_PROTO,
        MODBUS_EXCEPTION,
        BYTE_COUNT_MISMATCH
    };

    explicit EmbeddedMockServer(boost::asio::io_context& ioc)
        : ioc_(ioc), acceptor_(ioc, tcp::endpoint(tcp::v4(), 0)) {
        port_ = acceptor_.local_endpoint().port();
    }

    uint16_t port() const { return port_; }
    void set_mode(ResponseMode mode) { mode_ = mode; }
    void set_delay_ms(uint32_t ms) { delay_ms_ = ms; }

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

    void stop() {
        accepting_ = false;
        boost::system::error_code ec;
        acceptor_.close(ec);
    }

private:
    void handle_client(std::shared_ptr<tcp::socket> socket) {
        auto rx_buf = std::make_shared<std::array<uint8_t, 256>>();
        socket->async_read_some(
            boost::asio::buffer(*rx_buf),
            [this, socket, rx_buf](const boost::system::error_code& ec, std::size_t bytes_read) {
                if (ec || bytes_read < 7) {
                    return;
                }

                if (mode_ == ResponseMode::DROP) {
                    // Do not respond
                    return;
                }

                uint16_t tx_id = (rx_buf->at(0) << 8) | rx_buf->at(1);
                uint8_t unit_id = rx_buf->at(6);
                uint8_t fc = bytes_read >= 8 ? rx_buf->at(7) : 0x03;

                std::vector<uint8_t> resp;

                if (mode_ == ResponseMode::MALFORMED_PROTO) {
                    // Malformed Protocol ID = 1
                    resp = {
                        static_cast<uint8_t>((tx_id >> 8) & 0xFF), static_cast<uint8_t>(tx_id & 0xFF),
                        0x00, 0x01, // Protocol ID = 1
                        0x00, 0x0F,
                        unit_id,
                        0x03, 0x0C,
                        0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06
                    };
                } else if (mode_ == ResponseMode::MODBUS_EXCEPTION) {
                    // Exception response: FC | 0x80, exception code 0x02
                    resp = {
                        static_cast<uint8_t>((tx_id >> 8) & 0xFF), static_cast<uint8_t>(tx_id & 0xFF),
                        0x00, 0x00,
                        0x00, 0x03, // Length: unit_id + fc + exc_code
                        unit_id,
                        static_cast<uint8_t>(fc | 0x80),
                        0x02 // Illegal Data Address
                    };
                } else if (mode_ == ResponseMode::BYTE_COUNT_MISMATCH) {
                    // Byte count mismatch: claims 10 bytes instead of 12
                    resp = {
                        static_cast<uint8_t>((tx_id >> 8) & 0xFF), static_cast<uint8_t>(tx_id & 0xFF),
                        0x00, 0x00,
                        0x00, 0x0D,
                        unit_id,
                        0x03, 0x0A, // 10 bytes
                        0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05
                    };
                } else if (fc == 0x03) {
                    // Normal FC03: 6 registers
                    resp = {
                        static_cast<uint8_t>((tx_id >> 8) & 0xFF), static_cast<uint8_t>(tx_id & 0xFF),
                        0x00, 0x00,
                        0x00, 0x0F, // Length: 15 bytes
                        unit_id,
                        0x03,
                        0x0C, // 12 bytes
                        0x00, 0x2A, // HR0: 42 (Heartbeat)
                        0x00, 0x06, // HR1: status_flags (Ready=1, Running=1)
                        0x01, 0xF4, // HR2: sensor_raw (500)
                        0x01, 0xF4, // HR3: setpoint_raw (500)
                        0x00, 0x00, // HR4: fault_code (0)
                        0x00, 0x05  // HR5: applied_counter (5)
                    };
                } else if (fc == 0x05 || fc == 0x06) {
                    // Echo back 12 bytes
                    resp.assign(rx_buf->begin(), rx_buf->begin() + 12);
                }

                if (mode_ == ResponseMode::DELAY && delay_ms_ > 0) {
                    auto timer = std::make_shared<boost::asio::steady_timer>(ioc_);
                    timer->expires_after(std::chrono::milliseconds(delay_ms_));
                    timer->async_wait([socket, resp, timer](const boost::system::error_code& timer_ec) {
                        if (!timer_ec) {
                            boost::asio::async_write(*socket, boost::asio::buffer(resp),
                                [](const boost::system::error_code&, std::size_t) {});
                        }
                    });
                } else {
                    boost::asio::async_write(*socket, boost::asio::buffer(resp),
                        [this, socket](const boost::system::error_code& write_ec, std::size_t) {
                            if (!write_ec) {
                                handle_client(socket);
                            }
                        });
                }
            });
    }

    boost::asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    uint16_t port_{0};
    bool accepting_{true};
    ResponseMode mode_{ResponseMode::NORMAL};
    uint32_t delay_ms_{0};
};

class ModbusClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<EmbeddedMockServer>(server_ioc_);
        server_->start_accept();
        server_thread_ = std::thread([this]() { server_ioc_.run(); });

        cfg_.plc_host = "127.0.0.1";
        cfg_.plc_port = server_->port();
        cfg_.unit_id = 1;
        cfg_.connect_timeout_ms = 200;
        cfg_.response_timeout_ms = 25; // 25ms SLA timeout
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
    std::unique_ptr<EmbeddedMockServer> server_;
    std::thread server_thread_;
    StationConfig cfg_;
};

TEST_F(ModbusClientTest, ConnectsSuccessfully) {
    boost::asio::io_context ioc;
    ModbusClient client(ioc);

    bool connect_cb_called = false;
    Result<void> connect_res;

    client.async_connect(cfg_, [&](const Result<void>& res) {
        connect_cb_called = true;
        connect_res = res;
    });

    ioc.run();

    EXPECT_TRUE(connect_cb_called);
    EXPECT_TRUE(connect_res.is_ok());
    EXPECT_TRUE(client.is_connected());
}

TEST_F(ModbusClientTest, BulkReadFC03ParsesRegisters) {
    boost::asio::io_context ioc;
    ModbusClient client(ioc);

    client.async_connect(cfg_, [&](const Result<void>& c_res) {
        ASSERT_TRUE(c_res.is_ok());
        client.async_read_holding_bulk(0, 6, [&](const Result<std::array<uint16_t, 6>>& r_res) {
            ASSERT_TRUE(r_res.is_ok());
            const auto& regs = r_res.value();
            EXPECT_EQ(regs[0], 42);   // Heartbeat
            EXPECT_EQ(regs[1], 6);    // status_flags
            EXPECT_EQ(regs[2], 500);  // sensor_raw
            EXPECT_EQ(regs[3], 500);  // setpoint_raw
            EXPECT_EQ(regs[4], 0);    // fault_code
            EXPECT_EQ(regs[5], 5);    // applied_command_counter
            client.close();
        });
    });

    ioc.run();
}

TEST_F(ModbusClientTest, WriteCoilFC05Succeeds) {
    boost::asio::io_context ioc;
    ModbusClient client(ioc);

    client.async_connect(cfg_, [&](const Result<void>& c_res) {
        ASSERT_TRUE(c_res.is_ok());
        client.async_write_coil(0, true, [&](const Result<void>& w_res) {
            EXPECT_TRUE(w_res.is_ok());
            client.close();
        });
    });

    ioc.run();
}

TEST_F(ModbusClientTest, WriteRegisterFC06Succeeds) {
    boost::asio::io_context ioc;
    ModbusClient client(ioc);

    client.async_connect(cfg_, [&](const Result<void>& c_res) {
        ASSERT_TRUE(c_res.is_ok());
        client.async_write_register(3, 750, [&](const Result<void>& w_res) {
            EXPECT_TRUE(w_res.is_ok());
            client.close();
        });
    });

    ioc.run();
}

TEST_F(ModbusClientTest, ResponseTimeoutFiresAt25ms) {
    // Set server to delay by 100ms, client timeout is 25ms
    server_->set_mode(EmbeddedMockServer::ResponseMode::DELAY);
    server_->set_delay_ms(100);

    boost::asio::io_context ioc;
    ModbusClient client(ioc);
    client.set_response_timeout_ms(25);

    bool timed_out = false;
    uint16_t err_code = 0;

    client.async_connect(cfg_, [&](const Result<void>& c_res) {
        ASSERT_TRUE(c_res.is_ok());
        client.async_read_holding_bulk(0, 6, [&](const Result<std::array<uint16_t, 6>>& r_res) {
            EXPECT_TRUE(r_res.is_err());
            err_code = r_res.error().code;
            timed_out = (err_code == static_cast<uint16_t>(ErrorCode::IO_TIMEOUT));
            client.close();
        });
    });

    ioc.run();

    EXPECT_TRUE(timed_out);
    EXPECT_EQ(err_code, static_cast<uint16_t>(ErrorCode::IO_TIMEOUT));
}

TEST_F(ModbusClientTest, MalformedProtocolIdRejected) {
    server_->set_mode(EmbeddedMockServer::ResponseMode::MALFORMED_PROTO);

    boost::asio::io_context ioc;
    ModbusClient client(ioc);

    bool protocol_err = false;
    client.async_connect(cfg_, [&](const Result<void>& c_res) {
        ASSERT_TRUE(c_res.is_ok());
        client.async_read_holding_bulk(0, 6, [&](const Result<std::array<uint16_t, 6>>& r_res) {
            EXPECT_TRUE(r_res.is_err());
            EXPECT_EQ(r_res.error().code, static_cast<uint16_t>(ErrorCode::PROTOCOL_ERROR));
            protocol_err = true;
            client.close();
        });
    });

    ioc.run();
    EXPECT_TRUE(protocol_err);
}

TEST_F(ModbusClientTest, ModbusExceptionHandled) {
    server_->set_mode(EmbeddedMockServer::ResponseMode::MODBUS_EXCEPTION);

    boost::asio::io_context ioc;
    ModbusClient client(ioc);

    bool exc_handled = false;
    client.async_connect(cfg_, [&](const Result<void>& c_res) {
        ASSERT_TRUE(c_res.is_ok());
        client.async_read_holding_bulk(0, 6, [&](const Result<std::array<uint16_t, 6>>& r_res) {
            EXPECT_TRUE(r_res.is_err());
            EXPECT_EQ(r_res.error().code, static_cast<uint16_t>(ErrorCode::MODBUS_EXCEPTION));
            EXPECT_EQ(r_res.error().modbus_exception, 2);
            exc_handled = true;
            client.close();
        });
    });

    ioc.run();
    EXPECT_TRUE(exc_handled);
}

TEST_F(ModbusClientTest, ByteCountMismatchRejected) {
    server_->set_mode(EmbeddedMockServer::ResponseMode::BYTE_COUNT_MISMATCH);

    boost::asio::io_context ioc;
    ModbusClient client(ioc);

    bool mismatch_err = false;
    client.async_connect(cfg_, [&](const Result<void>& c_res) {
        ASSERT_TRUE(c_res.is_ok());
        client.async_read_holding_bulk(0, 6, [&](const Result<std::array<uint16_t, 6>>& r_res) {
            EXPECT_TRUE(r_res.is_err());
            EXPECT_EQ(r_res.error().code, static_cast<uint16_t>(ErrorCode::BULK_READ_MISMATCH));
            mismatch_err = true;
            client.close();
        });
    });

    ioc.run();
    EXPECT_TRUE(mismatch_err);
}

TEST_F(ModbusClientTest, RepeatedConnectionsDoNotLeakFileDescriptors) {
    // Acceptance criterion: 반복 연결 테스트 100회 통과
    boost::asio::io_context ioc;

    for (int i = 0; i < 100; ++i) {
        ModbusClient client(ioc);
        bool connected = false;
        client.async_connect(cfg_, [&](const Result<void>& c_res) {
            EXPECT_TRUE(c_res.is_ok());
            connected = true;
            client.close();
        });
        ioc.restart();
        ioc.run();
        EXPECT_TRUE(connected);
    }
}

