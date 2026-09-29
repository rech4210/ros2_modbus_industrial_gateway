#include <gtest/gtest.h>
#include "ros2_modbus_gateway/safety_monitor.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace ros2_modbus_gateway;

namespace {

PlcSample create_sample(uint16_t heartbeat = 1,
                        bool ready = true,
                        bool running = false,
                        bool physical_estop = false,
                        uint16_t fault_code = 0,
                        Generation gen = 1,
                        MonotonicNs timestamp = 1'000'000ULL,
                        bool alarm_tripped = false) {
    PlcSample s;
    s.generation = gen;
    s.sequence = 1;
    s.poll_started_ns = timestamp - 5'000'000ULL;
    s.sampled_ns = timestamp;
    s.read_rtt_ns = 5'000'000ULL;
    s.valid = true;

    // Registers: 0: heartbeat, 1: status_flags, 2: sensor, 3: setpoint, 4: fault_code, 5: applied_counter
    s.image.registers[0] = heartbeat;

    uint16_t flags = 0;
    if (ready) flags |= 0x0002;
    if (running) flags |= 0x0004;
    if (physical_estop) flags |= 0x0008;
    if (alarm_tripped) flags |= 0x0020;
    s.image.registers[1] = flags;

    s.image.registers[2] = 0;
    s.image.registers[3] = 0;
    s.image.registers[4] = fault_code;
    s.image.registers[5] = 0;
    return s;
}

} // namespace

TEST(SafetyMonitorTest, InitialStateIsAlarmActiveWithStartupCause) {
    SafetyMonitor sm(1, 3, 80);
    EXPECT_TRUE(sm.is_alarm_active());
    EXPECT_EQ(sm.current_cause(), AlarmCause::STARTUP);
    EXPECT_EQ(sm.consecutive_failures(), 0);
    EXPECT_FALSE(sm.last_progress_ns().has_value());
    EXPECT_FALSE(sm.last_heartbeat().has_value());
    EXPECT_EQ(sm.station_id(), 1);
}

TEST(SafetyMonitorTest, NormalHeartbeatProgressionDoesNotTripAlarm) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    auto s1 = create_sample(10, true, false, false, 0, 1, now);
    auto ev1 = sm.observe(s1, now);
    EXPECT_FALSE(ev1.has_value());
    EXPECT_EQ(sm.last_heartbeat().value(), 10);
    EXPECT_EQ(sm.last_progress_ns().value(), now);

    now += 20'000'000ULL; // +20ms
    auto s2 = create_sample(12, true, false, false, 0, 1, now);
    auto ev2 = sm.observe(s2, now);
    EXPECT_FALSE(ev2.has_value());
    EXPECT_EQ(sm.last_heartbeat().value(), 12);
    EXPECT_EQ(sm.last_progress_ns().value(), now);
    EXPECT_EQ(sm.consecutive_failures(), 0);
}

TEST(SafetyMonitorTest, HeartbeatWrapAroundIsHandledCleanly) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    auto s1 = create_sample(65535, true, false, false, 0, 1, now);
    sm.observe(s1, now);

    now += 20'000'000ULL;
    // 65535 -> 0 is delta = (0 - 65535) & 0xFFFF = 1
    auto s2 = create_sample(0, true, false, false, 0, 1, now);
    auto ev2 = sm.observe(s2, now);
    EXPECT_FALSE(ev2.has_value());
    EXPECT_EQ(sm.last_heartbeat().value(), 0);
    EXPECT_EQ(sm.last_progress_ns().value(), now);
}

TEST(SafetyMonitorTest, ConsecutiveFailuresThresholdTripsAlarm) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;
    GatewayError err(ErrorCode::IO_TIMEOUT, "Read timeout");

    // Failure 1
    auto ev1 = sm.on_io_failure(err, now);
    EXPECT_FALSE(ev1.has_value());
    EXPECT_EQ(sm.consecutive_failures(), 1);

    // Failure 2
    now += 25'000'000ULL;
    auto ev2 = sm.on_io_failure(err, now);
    EXPECT_FALSE(ev2.has_value());
    EXPECT_EQ(sm.consecutive_failures(), 2);

    // Failure 3: Limit reached!
    now += 25'000'000ULL;
    auto ev3 = sm.on_io_failure(err, now);
    ASSERT_TRUE(ev3.has_value());
    EXPECT_TRUE(ev3->active);
    EXPECT_EQ(ev3->cause, AlarmCause::COMM_TIMEOUT);
    EXPECT_EQ(ev3->error.code, static_cast<uint16_t>(ErrorCode::IO_TIMEOUT));
    EXPECT_EQ(sm.current_cause(), AlarmCause::COMM_TIMEOUT);
}

TEST(SafetyMonitorTest, ConsecutiveFailuresResetOnObserve) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;
    GatewayError err(ErrorCode::IO_TIMEOUT, "Read timeout");

    sm.on_io_failure(err, now);
    sm.on_io_failure(err, now + 25'000'000ULL);
    EXPECT_EQ(sm.consecutive_failures(), 2);

    // Valid sample resets consecutive failures
    auto s = create_sample(5, true, false, false, 0, 1, now + 50'000'000ULL);
    sm.observe(s, now + 50'000'000ULL);
    EXPECT_EQ(sm.consecutive_failures(), 0);

    // Next failure is count 1, not 3
    auto ev = sm.on_io_failure(err, now + 75'000'000ULL);
    EXPECT_FALSE(ev.has_value());
    EXPECT_EQ(sm.consecutive_failures(), 1);
}

TEST(SafetyMonitorTest, HeartbeatStallDetectionViaObserve) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    auto s1 = create_sample(100, true, false, false, 0, 1, now);
    sm.observe(s1, now);

    // Stalled heartbeat (delta = 0) within 80ms
    now += 50'000'000ULL; // +50ms
    auto s2 = create_sample(100, true, false, false, 0, 1, now);
    auto ev2 = sm.observe(s2, now);
    EXPECT_FALSE(ev2.has_value()); // not yet 80ms

    // Exceeding 80ms timeout (85ms from s1)
    now += 35'000'000ULL; // total 85ms
    auto s3 = create_sample(100, true, false, false, 0, 1, now);
    auto ev3 = sm.observe(s3, now);
    ASSERT_TRUE(ev3.has_value());
    EXPECT_EQ(ev3->cause, AlarmCause::HEARTBEAT_STALE);
    EXPECT_EQ(sm.current_cause(), AlarmCause::HEARTBEAT_STALE);
}

TEST(SafetyMonitorTest, HeartbeatStallDetectionViaEvaluateStale) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs start = 100'000'000ULL;

    auto s1 = create_sample(100, true, false, false, 0, 1, start);
    sm.observe(s1, start);

    // Within timeout (60ms elapsed)
    auto ev1 = sm.evaluate_stale(start + 60'000'000ULL);
    EXPECT_FALSE(ev1.has_value());

    // Beyond timeout (85ms elapsed)
    auto ev2 = sm.evaluate_stale(start + 85'000'000ULL);
    ASSERT_TRUE(ev2.has_value());
    EXPECT_EQ(ev2->cause, AlarmCause::HEARTBEAT_STALE);
}

TEST(SafetyMonitorTest, HeartbeatReversalDetection) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    auto s1 = create_sample(1000, true, false, false, 0, 1, now);
    sm.observe(s1, now);

    // Reversal: jump backwards to 10 (delta = (10 - 1000) & 0xFFFF = 55646 >= 32768)
    now += 20'000'000ULL;
    auto s2 = create_sample(10, true, false, false, 0, 1, now);
    auto ev2 = sm.observe(s2, now);
    ASSERT_TRUE(ev2.has_value());
    EXPECT_EQ(ev2->cause, AlarmCause::HEARTBEAT_STALE);
    EXPECT_EQ(sm.current_cause(), AlarmCause::HEARTBEAT_STALE);
}

TEST(SafetyMonitorTest, InterlockDetectionEstopAndFaultCode) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    // Physical E-stop active in sample
    auto s_estop = create_sample(1, true, false, true, 0, 1, now);
    auto ev = sm.observe(s_estop, now);
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->cause, AlarmCause::PLC_INTERLOCK);
    EXPECT_EQ(ev->error.code, static_cast<uint16_t>(ErrorCode::INTERLOCK_ACTIVE));

    // Clear alarm with force
    sm.clear_alarm(true, now + 1'000'000ULL);
    EXPECT_FALSE(sm.is_alarm_active());

    // Fault code active in sample
    auto s_fault = create_sample(2, true, false, false, 1, 1, now + 2'000'000ULL);
    auto ev2 = sm.observe(s_fault, now + 2'000'000ULL);
    ASSERT_TRUE(ev2.has_value());
    EXPECT_EQ(ev2->cause, AlarmCause::PLC_INTERLOCK);
}

TEST(SafetyMonitorTest, ClearAlarmFailsUnderFaultOrCommFailure) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    // 1. Fails when consecutive failures >= 3
    GatewayError err(ErrorCode::IO_TIMEOUT, "Timeout");
    sm.on_io_failure(err, now);
    sm.on_io_failure(err, now + 20'000'000ULL);
    sm.on_io_failure(err, now + 40'000'000ULL);

    auto res1 = sm.clear_alarm(false, now + 50'000'000ULL);
    EXPECT_TRUE(res1.is_err());
    EXPECT_EQ(res1.error().code, static_cast<uint16_t>(ErrorCode::NOT_READY));

    // 2. Fails when sample has E-stop
    auto s_estop = create_sample(10, true, false, true, 0, 1, now + 60'000'000ULL);
    sm.observe(s_estop, now + 60'000'000ULL);
    auto res2 = sm.clear_alarm(false, now + 70'000'000ULL);
    EXPECT_TRUE(res2.is_err());
    EXPECT_EQ(res2.error().code, static_cast<uint16_t>(ErrorCode::INTERLOCK_ACTIVE));

    // 3. Forced clear succeeds anyway
    auto res_force = sm.clear_alarm(true, now + 80'000'000ULL);
    EXPECT_TRUE(res_force.is_ok());
    EXPECT_FALSE(sm.is_alarm_active());
}

TEST(SafetyMonitorTest, ClearAlarmSucceedsWhenHealthy) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    // Feed healthy sample
    auto s = create_sample(10, true, false, false, 0, 1, now);
    sm.observe(s, now);

    // Initial startup alarm is active
    EXPECT_TRUE(sm.is_alarm_active());

    // 1-shot clear alarm
    auto res = sm.clear_alarm(false, now + 1'000'000ULL);
    ASSERT_TRUE(res.is_ok());
    EXPECT_FALSE(res.value().active);
    EXPECT_EQ(res.value().cause, AlarmCause::NONE);
    EXPECT_FALSE(sm.is_alarm_active());
    EXPECT_EQ(sm.current_cause(), AlarmCause::NONE);
}

TEST(SafetyMonitorTest, ConcurrentStressSafetyMonitor) {
    SafetyMonitor sm(1, 3, 80);
    constexpr int THREAD_OPS = 2000;
    std::atomic<bool> start_flag{false};

    std::thread t1([&]() {
        while (!start_flag) {}
        for (int i = 0; i < THREAD_OPS; ++i) {
            auto s = create_sample(static_cast<uint16_t>(i), true, false, false, 0, 1, i * 1'000'000ULL);
            sm.observe(s, i * 1'000'000ULL);
        }
    });

    std::thread t2([&]() {
        while (!start_flag) {}
        GatewayError err(ErrorCode::IO_TIMEOUT, "Stress err");
        for (int i = 0; i < THREAD_OPS; ++i) {
            sm.on_io_failure(err, i * 1'000'000ULL);
        }
    });

    std::thread t3([&]() {
        while (!start_flag) {}
        for (int i = 0; i < THREAD_OPS; ++i) {
            sm.evaluate_stale(i * 1'000'000ULL);
        }
    });

    std::thread t4([&]() {
        while (!start_flag) {}
        for (int i = 0; i < THREAD_OPS; ++i) {
            sm.clear_alarm(true, i * 1'000'000ULL);
        }
    });

    start_flag = true;
    t1.join();
    t2.join();
    t3.join();
    t4.join();

    // Verification: Object remains in coherent state, no deadlocks, no memory corruptions
    EXPECT_NO_FATAL_FAILURE(sm.is_alarm_active());
    EXPECT_NO_FATAL_FAILURE(sm.consecutive_failures());
}

TEST(SafetyMonitorTest, ClearAlarmRejectedBeforeAnySampleReceived) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    EXPECT_TRUE(sm.is_alarm_active());
    EXPECT_FALSE(sm.has_sample());

    // 1-shot clear must be rejected before observing at least one sample
    auto res = sm.clear_alarm(false, now);
    EXPECT_TRUE(res.is_err());
    EXPECT_EQ(res.error().code, static_cast<uint16_t>(ErrorCode::NOT_READY));
    EXPECT_TRUE(sm.is_alarm_active());

    // Force clear succeeds anyway
    auto force_res = sm.clear_alarm(true, now);
    EXPECT_TRUE(force_res.is_ok());
    EXPECT_FALSE(sm.is_alarm_active());
}

TEST(SafetyMonitorTest, ClearAlarmRejectedWhenAlarmTrippedBitActive) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    // Sample where physical_estop=false, fault_code=0, but alarm_tripped=true (Bit 5)
    auto s_alarm = create_sample(10, true, false, false, 0, 1, now, true);
    auto ev = sm.observe(s_alarm, now);
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->cause, AlarmCause::PLC_INTERLOCK);
    EXPECT_EQ(ev->error.code, static_cast<uint16_t>(ErrorCode::INTERLOCK_ACTIVE));

    // Clear alarm (force=false) MUST be rejected while alarm_tripped bit remains active
    auto res = sm.clear_alarm(false, now + 1'000'000ULL);
    EXPECT_TRUE(res.is_err());
    EXPECT_EQ(res.error().code, static_cast<uint16_t>(ErrorCode::INTERLOCK_ACTIVE));
    EXPECT_TRUE(sm.is_alarm_active());
}

TEST(SafetyMonitorTest, HeartbeatStallExactBoundaryAt80ms) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs start = 100'000'000ULL;

    auto s1 = create_sample(100, true, false, false, 0, 1, start);
    sm.observe(s1, start);

    // Exact boundary: 80'000'000 ns (80ms)
    // spec §1.1 & §4.1: "80ms 이상 heartbeat 무변화" -> must trip at >= 80ms
    auto ev_exact = sm.evaluate_stale(start + 80'000'000ULL);
    ASSERT_TRUE(ev_exact.has_value());
    EXPECT_EQ(ev_exact->cause, AlarmCause::HEARTBEAT_STALE);
    EXPECT_EQ(ev_exact->error.code, static_cast<uint16_t>(ErrorCode::HEARTBEAT_STALE));
}

TEST(SafetyMonitorTest, ConcurrentClearAlarmAndIoFailureBoundary) {
    SafetyMonitor sm(1, 3, 80);
    MonotonicNs now = 100'000'000ULL;

    // Feed a valid sample first
    auto s = create_sample(10, true, false, false, 0, 1, now);
    sm.observe(s, now);
    sm.clear_alarm(false, now + 1'000'000ULL);
    EXPECT_FALSE(sm.is_alarm_active());

    constexpr int ITERATIONS = 1000;
    std::atomic<bool> start_flag{false};

    // Thread 1 drives failures to the 3-failure boundary repeatedly
    std::thread t1([&]() {
        while (!start_flag) {}
        GatewayError err(ErrorCode::IO_TIMEOUT, "Timeout boundary");
        for (int i = 0; i < ITERATIONS; ++i) {
            sm.on_io_failure(err, now + i * 10'000ULL);
        }
    });

    // Thread 2 continuously attempts to clear the alarm
    std::thread t2([&]() {
        while (!start_flag) {}
        for (int i = 0; i < ITERATIONS; ++i) {
            sm.clear_alarm(false, now + i * 10'000ULL);
        }
    });

    start_flag = true;
    t1.join();
    t2.join();

    // Verify system state integrity
    EXPECT_NO_FATAL_FAILURE(sm.is_alarm_active());
}

