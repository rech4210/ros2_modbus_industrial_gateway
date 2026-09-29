#include "ros2_modbus_gateway/monotonic_clock.hpp"
#include <chrono>
#include <ctime>

namespace ros2_modbus_gateway {

MonotonicNs Clock::now_ns() noexcept {
#if defined(__linux__) || defined(CLOCK_MONOTONIC)
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return static_cast<MonotonicNs>(ts.tv_sec) * 1'000'000'000ULL + static_cast<MonotonicNs>(ts.tv_nsec);
    }
#endif
    return static_cast<MonotonicNs>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace ros2_modbus_gateway
