#include "ros2_modbus_gateway/modbus_client.hpp"

#include <iostream>

namespace ros2_modbus_gateway {

ModbusClient::ModbusClient(boost::asio::io_context& ioc)
    : ioc_(ioc), socket_(ioc), resolver_(ioc) {}

ModbusClient::~ModbusClient() {
    close();
}

void ModbusClient::close() noexcept {
    cancel_socket_and_close();
}

void ModbusClient::cancel_socket_and_close() noexcept {
    boost::system::error_code ec;
    if (socket_.is_open()) {
        socket_.cancel(ec);
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
    connected_ = false;
    operation_in_progress_ = false;
}

bool ModbusClient::is_connected() const noexcept {
    return connected_ && socket_.is_open();
}

void ModbusClient::set_response_timeout_ms(uint16_t timeout_ms) noexcept {
    response_timeout_ms_ = timeout_ms;
}

uint16_t ModbusClient::response_timeout_ms() const noexcept {
    return response_timeout_ms_;
}

void ModbusClient::async_connect(const StationConfig& cfg, ConnectCallback cb) {
    if (operation_in_progress_) {
        cb(Result<void>::err(GatewayError(ErrorCode::BUSY, "Client is busy with an existing operation")));
        return;
    }

    config_ = cfg;
    response_timeout_ms_ = cfg.response_timeout_ms;
    cancel_socket_and_close();

    operation_in_progress_ = true;

    auto op = std::make_shared<OperationState>(ioc_);
    uint16_t timeout = cfg.connect_timeout_ms > 0 ? cfg.connect_timeout_ms : 200;
    op->timer.expires_after(std::chrono::milliseconds(timeout));

    op->timer.async_wait([this, op, cb](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }
        bool expected = false;
        if (op->completed.compare_exchange_strong(expected, true)) {
            operation_in_progress_ = false;
            cancel_socket_and_close();
            cb(Result<void>::err(GatewayError(ErrorCode::CONNECT_FAILED, "Connection attempt timed out")));
        }
    });

    boost::system::error_code resolve_ec;
    auto endpoints = resolver_.resolve(cfg.plc_host, std::to_string(cfg.plc_port), resolve_ec);
    if (resolve_ec) {
        bool expected = false;
        if (op->completed.compare_exchange_strong(expected, true)) {
            boost::system::error_code timer_ec;
            op->timer.cancel(timer_ec);
            operation_in_progress_ = false;
            cb(Result<void>::err(GatewayError(ErrorCode::CONNECT_FAILED,
                                              "DNS resolve failed: " + resolve_ec.message(),
                                              resolve_ec.value())));
        }
        return;
    }

    boost::asio::async_connect(
        socket_, endpoints,
        [this, op, cb = std::move(cb)](const boost::system::error_code& ec,
                                       const boost::asio::ip::tcp::endpoint& /*endpoint*/) {
            bool expected = false;
            if (!op->completed.compare_exchange_strong(expected, true)) {
                return;
            }
            boost::system::error_code timer_ec;
            op->timer.cancel(timer_ec);
            operation_in_progress_ = false;

            if (ec) {
                cancel_socket_and_close();
                cb(Result<void>::err(GatewayError(ErrorCode::CONNECT_FAILED,
                                                  "TCP connection failed: " + ec.message(),
                                                  ec.value())));
                return;
            }

            // Optimize for industrial low latency: TCP_NODELAY
            boost::system::error_code opt_ec;
            socket_.set_option(boost::asio::ip::tcp::no_delay(true), opt_ec);
            connected_ = true;
            cb(Result<void>::ok());
        });
}

void ModbusClient::async_read_holding_bulk(uint16_t addr, uint16_t count, ReadCallback cb) {
    if (!is_connected()) {
        cb(Result<std::array<uint16_t, 6>>::err(
            GatewayError(ErrorCode::CONNECTION_LOST, "Socket is not connected")));
        return;
    }

    if (operation_in_progress_) {
        cb(Result<std::array<uint16_t, 6>>::err(
            GatewayError(ErrorCode::BUSY, "Socket operation already in flight")));
        return;
    }

    if (count > 6) {
        cb(Result<std::array<uint16_t, 6>>::err(
            GatewayError(ErrorCode::INVALID_ARGUMENT, "Count exceeds bulk read maximum (6)")));
        return;
    }

    operation_in_progress_ = true;
    uint16_t req_tx_id = ++tx_id_;

    // Build Modbus-TCP FC03 Request (12 bytes)
    // MBAP Header (7 bytes)
    tx_buffer_[0] = static_cast<uint8_t>((req_tx_id >> 8) & 0xFF);
    tx_buffer_[1] = static_cast<uint8_t>(req_tx_id & 0xFF);
    tx_buffer_[2] = 0x00; // Protocol ID high
    tx_buffer_[3] = 0x00; // Protocol ID low (0 = Modbus)
    tx_buffer_[4] = 0x00; // Length high
    tx_buffer_[5] = 0x06; // Length low (6 bytes: Unit ID + FC + Addr(2) + Count(2))
    tx_buffer_[6] = config_.unit_id; // Unit ID

    // PDU (5 bytes)
    tx_buffer_[7] = 0x03; // Function Code: Read Holding Registers
    tx_buffer_[8] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    tx_buffer_[9] = static_cast<uint8_t>(addr & 0xFF);
    tx_buffer_[10] = static_cast<uint8_t>((count >> 8) & 0xFF);
    tx_buffer_[11] = static_cast<uint8_t>(count & 0xFF);

    auto op = std::make_shared<OperationState>(ioc_);
    op->timer.expires_after(std::chrono::milliseconds(response_timeout_ms_));

    op->timer.async_wait([this, op, cb](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }
        bool expected = false;
        if (op->completed.compare_exchange_strong(expected, true)) {
            op->timed_out = true;
            boost::system::error_code cancel_ec;
            socket_.cancel(cancel_ec);
        }
    });

    boost::asio::async_write(
        socket_, boost::asio::buffer(tx_buffer_.data(), 12),
        [this, op, cb, req_tx_id, count](const boost::system::error_code& write_ec, std::size_t /*written*/) {
            if (write_ec) {
                if (op->timed_out.load()) {
                    operation_in_progress_ = false;
                    cb(Result<std::array<uint16_t, 6>>::err(
                        GatewayError(ErrorCode::IO_TIMEOUT, "Read holding registers timed out after 25ms")));
                    return;
                }
                bool expected = false;
                if (op->completed.compare_exchange_strong(expected, true)) {
                    boost::system::error_code timer_ec;
                    op->timer.cancel(timer_ec);
                    cancel_socket_and_close();
                    cb(Result<std::array<uint16_t, 6>>::err(
                        GatewayError(ErrorCode::CONNECTION_LOST,
                                     "TCP write failed: " + write_ec.message(),
                                     write_ec.value())));
                }
                return;
            }

            // Step 1: Read MBAP Header (7 bytes)
            boost::asio::async_read(
                socket_, boost::asio::buffer(rx_buffer_.data(), 7),
                [this, op, cb, req_tx_id, count](const boost::system::error_code& mbap_ec, std::size_t /*bytes_read*/) {
                    if (mbap_ec) {
                        if (op->timed_out.load()) {
                            operation_in_progress_ = false;
                            cb(Result<std::array<uint16_t, 6>>::err(
                                GatewayError(ErrorCode::IO_TIMEOUT, "Read holding registers timed out after 25ms")));
                            return;
                        }
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<std::array<uint16_t, 6>>::err(
                                GatewayError(ErrorCode::CONNECTION_LOST,
                                             "TCP read MBAP failed: " + mbap_ec.message(),
                                             mbap_ec.value())));
                        }
                        return;
                    }

                    // Validate MBAP Header
                    uint16_t rx_tx_id = (static_cast<uint16_t>(rx_buffer_[0]) << 8) | rx_buffer_[1];
                    uint16_t rx_proto_id = (static_cast<uint16_t>(rx_buffer_[2]) << 8) | rx_buffer_[3];
                    uint16_t rx_len = (static_cast<uint16_t>(rx_buffer_[4]) << 8) | rx_buffer_[5];

                    if (rx_proto_id != 0x0000) {
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<std::array<uint16_t, 6>>::err(
                                GatewayError(ErrorCode::PROTOCOL_ERROR,
                                             "MBAP Protocol ID is not 0 (malformed packet)")));
                        }
                        return;
                    }

                    if (rx_tx_id != req_tx_id) {
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<std::array<uint16_t, 6>>::err(
                                GatewayError(ErrorCode::PROTOCOL_ERROR,
                                             "Transaction ID mismatch in Modbus response")));
                        }
                        return;
                    }

                    if (rx_len < 2 || rx_len > 250) {
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<std::array<uint16_t, 6>>::err(
                                GatewayError(ErrorCode::PROTOCOL_ERROR,
                                             "Invalid MBAP Length: " + std::to_string(rx_len))));
                        }
                        return;
                    }

                    // Step 2: Read PDU bytes (rx_len - 1 bytes, as Unit ID was already read in MBAP)
                    std::size_t pdu_len = rx_len - 1;
                    boost::asio::async_read(
                        socket_, boost::asio::buffer(rx_buffer_.data() + 7, pdu_len),
                        [this, op, cb, count, pdu_len](const boost::system::error_code& pdu_ec, std::size_t /*pdu_bytes*/) {
                            if (pdu_ec) {
                                if (op->timed_out.load()) {
                                    operation_in_progress_ = false;
                                    cb(Result<std::array<uint16_t, 6>>::err(
                                        GatewayError(ErrorCode::IO_TIMEOUT, "Read holding registers timed out after 25ms")));
                                    return;
                                }
                                bool expected = false;
                                if (op->completed.compare_exchange_strong(expected, true)) {
                                    boost::system::error_code timer_ec;
                                    op->timer.cancel(timer_ec);
                                    cancel_socket_and_close();
                                    cb(Result<std::array<uint16_t, 6>>::err(
                                        GatewayError(ErrorCode::CONNECTION_LOST,
                                                     "TCP read PDU failed: " + pdu_ec.message(),
                                                     pdu_ec.value())));
                                }
                                return;
                            }

                            bool expected = false;
                            if (!op->completed.compare_exchange_strong(expected, true)) {
                                return;
                            }
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            operation_in_progress_ = false;

                            uint8_t fc = rx_buffer_[7];
                            if ((fc & 0x80) != 0) {
                                uint8_t exc_code = (pdu_len >= 2) ? rx_buffer_[8] : 0;
                                cb(Result<std::array<uint16_t, 6>>::err(
                                    GatewayError(ErrorCode::MODBUS_EXCEPTION,
                                                 "Modbus exception response received",
                                                 0, exc_code)));
                                return;
                            }

                            if (fc != 0x03) {
                                cb(Result<std::array<uint16_t, 6>>::err(
                                    GatewayError(ErrorCode::PROTOCOL_ERROR,
                                                 "Unexpected function code: " + std::to_string(fc))));
                                return;
                            }

                            uint8_t byte_count = rx_buffer_[8];
                            if (byte_count != count * 2 || pdu_len != static_cast<std::size_t>(byte_count + 2)) {
                                cb(Result<std::array<uint16_t, 6>>::err(
                                    GatewayError(ErrorCode::BULK_READ_MISMATCH,
                                                 "Bulk read byte count mismatch")));
                                return;
                            }

                            std::array<uint16_t, 6> registers{};
                            for (std::size_t i = 0; i < count && i < 6; ++i) {
                                std::size_t offset = 9 + i * 2;
                                registers[i] = (static_cast<uint16_t>(rx_buffer_[offset]) << 8) |
                                               static_cast<uint16_t>(rx_buffer_[offset + 1]);
                            }

                            cb(Result<std::array<uint16_t, 6>>::ok(registers));
                        });
                });
        });
}

void ModbusClient::async_write_coil(uint16_t addr, bool val, WriteCallback cb) {
    if (!is_connected()) {
        cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST, "Socket is not connected")));
        return;
    }

    if (operation_in_progress_) {
        cb(Result<void>::err(GatewayError(ErrorCode::BUSY, "Socket operation already in flight")));
        return;
    }

    operation_in_progress_ = true;
    uint16_t req_tx_id = ++tx_id_;
    uint16_t coil_val = val ? 0xFF00 : 0x0000;

    // Modbus-TCP FC05 Request (12 bytes)
    tx_buffer_[0] = static_cast<uint8_t>((req_tx_id >> 8) & 0xFF);
    tx_buffer_[1] = static_cast<uint8_t>(req_tx_id & 0xFF);
    tx_buffer_[2] = 0x00;
    tx_buffer_[3] = 0x00;
    tx_buffer_[4] = 0x00;
    tx_buffer_[5] = 0x06; // Length: 6 bytes
    tx_buffer_[6] = config_.unit_id;

    // PDU: FC05
    tx_buffer_[7] = 0x05;
    tx_buffer_[8] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    tx_buffer_[9] = static_cast<uint8_t>(addr & 0xFF);
    tx_buffer_[10] = static_cast<uint8_t>((coil_val >> 8) & 0xFF);
    tx_buffer_[11] = static_cast<uint8_t>(coil_val & 0xFF);

    auto op = std::make_shared<OperationState>(ioc_);
    op->timer.expires_after(std::chrono::milliseconds(response_timeout_ms_));

    op->timer.async_wait([this, op, cb](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) return;
        bool expected = false;
        if (op->completed.compare_exchange_strong(expected, true)) {
            op->timed_out = true;
            boost::system::error_code cancel_ec;
            socket_.cancel(cancel_ec);
        }
    });

    boost::asio::async_write(
        socket_, boost::asio::buffer(tx_buffer_.data(), 12),
        [this, op, cb, req_tx_id, addr, coil_val](const boost::system::error_code& write_ec, std::size_t /*written*/) {
            if (write_ec) {
                if (op->timed_out.load()) {
                    operation_in_progress_ = false;
                    cb(Result<void>::err(GatewayError(ErrorCode::IO_TIMEOUT, "Write coil timed out after 25ms")));
                    return;
                }
                bool expected = false;
                if (op->completed.compare_exchange_strong(expected, true)) {
                    boost::system::error_code timer_ec;
                    op->timer.cancel(timer_ec);
                    cancel_socket_and_close();
                    cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                      "TCP write failed: " + write_ec.message(),
                                                      write_ec.value())));
                }
                return;
            }

            // Read MBAP Header (7 bytes)
            boost::asio::async_read(
                socket_, boost::asio::buffer(rx_buffer_.data(), 7),
                [this, op, cb, req_tx_id, addr, coil_val](const boost::system::error_code& mbap_ec, std::size_t /*bytes*/) {
                    if (mbap_ec) {
                        if (op->timed_out.load()) {
                            operation_in_progress_ = false;
                            cb(Result<void>::err(GatewayError(ErrorCode::IO_TIMEOUT, "Write coil timed out after 25ms")));
                            return;
                        }
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                              "TCP read MBAP failed: " + mbap_ec.message(),
                                                              mbap_ec.value())));
                        }
                        return;
                    }

                    uint16_t rx_tx_id = (static_cast<uint16_t>(rx_buffer_[0]) << 8) | rx_buffer_[1];
                    uint16_t rx_proto_id = (static_cast<uint16_t>(rx_buffer_[2]) << 8) | rx_buffer_[3];
                    uint16_t rx_len = (static_cast<uint16_t>(rx_buffer_[4]) << 8) | rx_buffer_[5];

                    if (rx_proto_id != 0x0000 || rx_tx_id != req_tx_id || rx_len < 2) {
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<void>::err(GatewayError(ErrorCode::PROTOCOL_ERROR, "Invalid MBAP header")));
                        }
                        return;
                    }

                    std::size_t pdu_len = rx_len - 1;
                    boost::asio::async_read(
                        socket_, boost::asio::buffer(rx_buffer_.data() + 7, pdu_len),
                        [this, op, cb, addr, coil_val, pdu_len](const boost::system::error_code& pdu_ec, std::size_t /*pdu_bytes*/) {
                            if (pdu_ec) {
                                if (op->timed_out.load()) {
                                    operation_in_progress_ = false;
                                    cb(Result<void>::err(GatewayError(ErrorCode::IO_TIMEOUT, "Write coil timed out after 25ms")));
                                    return;
                                }
                                bool expected = false;
                                if (op->completed.compare_exchange_strong(expected, true)) {
                                    boost::system::error_code timer_ec;
                                    op->timer.cancel(timer_ec);
                                    cancel_socket_and_close();
                                    cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                                      "TCP read PDU failed: " + pdu_ec.message(),
                                                                      pdu_ec.value())));
                                }
                                return;
                            }

                            bool expected = false;
                            if (!op->completed.compare_exchange_strong(expected, true)) {
                                return;
                            }
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            operation_in_progress_ = false;

                            if (pdu_ec) {
                                cancel_socket_and_close();
                                cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                                  "TCP read PDU failed: " + pdu_ec.message(),
                                                                  pdu_ec.value())));
                                return;
                            }

                            uint8_t fc = rx_buffer_[7];
                            if ((fc & 0x80) != 0) {
                                uint8_t exc = (pdu_len >= 2) ? rx_buffer_[8] : 0;
                                cb(Result<void>::err(GatewayError(ErrorCode::MODBUS_EXCEPTION,
                                                                  "Modbus exception on FC05 write", 0, exc)));
                                return;
                            }

                            if (fc != 0x05) {
                                cb(Result<void>::err(GatewayError(ErrorCode::PROTOCOL_ERROR, "Unexpected FC")));
                                return;
                            }

                            uint16_t resp_addr = (static_cast<uint16_t>(rx_buffer_[8]) << 8) | rx_buffer_[9];
                            uint16_t resp_val = (static_cast<uint16_t>(rx_buffer_[10]) << 8) | rx_buffer_[11];
                            if (resp_addr != addr || resp_val != coil_val) {
                                cb(Result<void>::err(GatewayError(ErrorCode::PROTOCOL_ERROR,
                                    "Echo mismatch in FC05: resp_addr=" + std::to_string(resp_addr) +
                                    " vs " + std::to_string(addr) + ", resp_val=" + std::to_string(resp_val) +
                                    " vs " + std::to_string(coil_val))));
                                return;
                            }

                            cb(Result<void>::ok());
                        });
                });
        });
}

void ModbusClient::async_write_register(uint16_t addr, uint16_t val, WriteCallback cb) {
    if (!is_connected()) {
        cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST, "Socket is not connected")));
        return;
    }

    if (operation_in_progress_) {
        cb(Result<void>::err(GatewayError(ErrorCode::BUSY, "Socket operation already in flight")));
        return;
    }

    operation_in_progress_ = true;
    uint16_t req_tx_id = ++tx_id_;

    // Modbus-TCP FC06 Request (12 bytes)
    tx_buffer_[0] = static_cast<uint8_t>((req_tx_id >> 8) & 0xFF);
    tx_buffer_[1] = static_cast<uint8_t>(req_tx_id & 0xFF);
    tx_buffer_[2] = 0x00;
    tx_buffer_[3] = 0x00;
    tx_buffer_[4] = 0x00;
    tx_buffer_[5] = 0x06; // Length: 6 bytes
    tx_buffer_[6] = config_.unit_id;

    // PDU: FC06
    tx_buffer_[7] = 0x06;
    tx_buffer_[8] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    tx_buffer_[9] = static_cast<uint8_t>(addr & 0xFF);
    tx_buffer_[10] = static_cast<uint8_t>((val >> 8) & 0xFF);
    tx_buffer_[11] = static_cast<uint8_t>(val & 0xFF);

    auto op = std::make_shared<OperationState>(ioc_);
    op->timer.expires_after(std::chrono::milliseconds(response_timeout_ms_));

    op->timer.async_wait([this, op, cb](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) return;
        bool expected = false;
        if (op->completed.compare_exchange_strong(expected, true)) {
            op->timed_out = true;
            boost::system::error_code cancel_ec;
            socket_.cancel(cancel_ec);
        }
    });

    boost::asio::async_write(
        socket_, boost::asio::buffer(tx_buffer_.data(), 12),
        [this, op, cb, req_tx_id, addr, val](const boost::system::error_code& write_ec, std::size_t /*written*/) {
            if (write_ec) {
                if (op->timed_out.load()) {
                    operation_in_progress_ = false;
                    cb(Result<void>::err(GatewayError(ErrorCode::IO_TIMEOUT, "Write register timed out after 25ms")));
                    return;
                }
                bool expected = false;
                if (op->completed.compare_exchange_strong(expected, true)) {
                    boost::system::error_code timer_ec;
                    op->timer.cancel(timer_ec);
                    cancel_socket_and_close();
                    cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                      "TCP write failed: " + write_ec.message(),
                                                      write_ec.value())));
                }
                return;
            }

            // Read MBAP Header (7 bytes)
            boost::asio::async_read(
                socket_, boost::asio::buffer(rx_buffer_.data(), 7),
                [this, op, cb, req_tx_id, addr, val](const boost::system::error_code& mbap_ec, std::size_t /*bytes*/) {
                    if (mbap_ec) {
                        if (op->timed_out.load()) {
                            operation_in_progress_ = false;
                            cb(Result<void>::err(GatewayError(ErrorCode::IO_TIMEOUT, "Write register timed out after 25ms")));
                            return;
                        }
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                              "TCP read MBAP failed: " + mbap_ec.message(),
                                                              mbap_ec.value())));
                        }
                        return;
                    }

                    uint16_t rx_tx_id = (static_cast<uint16_t>(rx_buffer_[0]) << 8) | rx_buffer_[1];
                    uint16_t rx_proto_id = (static_cast<uint16_t>(rx_buffer_[2]) << 8) | rx_buffer_[3];
                    uint16_t rx_len = (static_cast<uint16_t>(rx_buffer_[4]) << 8) | rx_buffer_[5];

                    if (rx_proto_id != 0x0000 || rx_tx_id != req_tx_id || rx_len < 2) {
                        bool expected = false;
                        if (op->completed.compare_exchange_strong(expected, true)) {
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            cancel_socket_and_close();
                            cb(Result<void>::err(GatewayError(ErrorCode::PROTOCOL_ERROR, "Invalid MBAP header")));
                        }
                        return;
                    }

                    std::size_t pdu_len = rx_len - 1;
                    boost::asio::async_read(
                        socket_, boost::asio::buffer(rx_buffer_.data() + 7, pdu_len),
                        [this, op, cb, addr, val, pdu_len](const boost::system::error_code& pdu_ec, std::size_t /*pdu_bytes*/) {
                            if (pdu_ec) {
                                if (op->timed_out.load()) {
                                    operation_in_progress_ = false;
                                    cb(Result<void>::err(GatewayError(ErrorCode::IO_TIMEOUT, "Write register timed out after 25ms")));
                                    return;
                                }
                                bool expected = false;
                                if (op->completed.compare_exchange_strong(expected, true)) {
                                    boost::system::error_code timer_ec;
                                    op->timer.cancel(timer_ec);
                                    cancel_socket_and_close();
                                    cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                                      "TCP read PDU failed: " + pdu_ec.message(),
                                                                      pdu_ec.value())));
                                }
                                return;
                            }

                            bool expected = false;
                            if (!op->completed.compare_exchange_strong(expected, true)) {
                                return;
                            }
                            boost::system::error_code timer_ec;
                            op->timer.cancel(timer_ec);
                            operation_in_progress_ = false;

                            if (pdu_ec) {
                                cancel_socket_and_close();
                                cb(Result<void>::err(GatewayError(ErrorCode::CONNECTION_LOST,
                                                                  "TCP read PDU failed: " + pdu_ec.message(),
                                                                  pdu_ec.value())));
                                return;
                            }

                            uint8_t fc = rx_buffer_[7];
                            if ((fc & 0x80) != 0) {
                                uint8_t exc = (pdu_len >= 2) ? rx_buffer_[8] : 0;
                                cb(Result<void>::err(GatewayError(ErrorCode::MODBUS_EXCEPTION,
                                                                  "Modbus exception on FC06 write", 0, exc)));
                                return;
                            }

                            if (fc != 0x06) {
                                cb(Result<void>::err(GatewayError(ErrorCode::PROTOCOL_ERROR, "Unexpected FC")));
                                return;
                            }

                            uint16_t resp_addr = (static_cast<uint16_t>(rx_buffer_[8]) << 8) | rx_buffer_[9];
                            uint16_t resp_val = (static_cast<uint16_t>(rx_buffer_[10]) << 8) | rx_buffer_[11];
                            if (resp_addr != addr || resp_val != val) {
                                cb(Result<void>::err(GatewayError(ErrorCode::PROTOCOL_ERROR,
                                    "Echo mismatch in FC06: resp_addr=" + std::to_string(resp_addr) +
                                    " vs " + std::to_string(addr) + ", resp_val=" + std::to_string(resp_val) +
                                    " vs " + std::to_string(val))));
                                return;
                            }

                            cb(Result<void>::ok());
                        });
                });
        });
}

} // namespace ros2_modbus_gateway
