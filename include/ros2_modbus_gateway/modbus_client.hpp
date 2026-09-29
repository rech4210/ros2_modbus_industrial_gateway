#pragma once

#include "ros2_modbus_gateway/types.hpp"
#include "ros2_modbus_gateway/config.hpp"

#include <boost/asio.hpp>
#include <array>
#include <memory>
#include <cstdint>
#include <atomic>

namespace ros2_modbus_gateway {

class ModbusClient : public std::enable_shared_from_this<ModbusClient> {
public:
    explicit ModbusClient(boost::asio::io_context& ioc);
    ~ModbusClient();

    // Non-copyable
    ModbusClient(const ModbusClient&) = delete;
    ModbusClient& operator=(const ModbusClient&) = delete;

    // Specification §2.5 Methods
    void async_connect(const StationConfig& cfg, ConnectCallback cb);
    void close() noexcept;
    void async_read_holding_bulk(uint16_t addr, uint16_t count, ReadCallback cb);
    void async_write_coil(uint16_t addr, bool val, WriteCallback cb);
    void async_write_register(uint16_t addr, uint16_t val, WriteCallback cb);

    // Configuration & State Helpers
    bool is_connected() const noexcept;
    void set_response_timeout_ms(uint16_t timeout_ms) noexcept;
    uint16_t response_timeout_ms() const noexcept;
    boost::asio::ip::tcp::socket& socket() noexcept { return socket_; }

private:
    struct OperationState {
        std::atomic<bool> completed{false};
        std::atomic<bool> timed_out{false};
        boost::asio::steady_timer timer;
        explicit OperationState(boost::asio::io_context& ioc) : timer(ioc) {}
    };


    void cancel_socket_and_close() noexcept;

    boost::asio::io_context& ioc_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver resolver_;
    StationConfig config_{};
    uint16_t response_timeout_ms_{25};
    uint16_t tx_id_{0};
    bool connected_{false};
    bool operation_in_progress_{false};

    // Frame Buffers
    std::array<uint8_t, 260> tx_buffer_{};
    std::array<uint8_t, 260> rx_buffer_{};
};

} // namespace ros2_modbus_gateway
