#pragma once

#include "ros2_modbus_gateway/types.hpp"

namespace ros2_modbus_gateway {

class Clock {
public:
    static MonotonicNs now_ns() noexcept;
};

inline MonotonicNs now_ns() noexcept {
    return Clock::now_ns();
}

} // namespace ros2_modbus_gateway
