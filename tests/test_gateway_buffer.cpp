#include <gtest/gtest.h>
#include "ros2_modbus_gateway/gateway_buffer.hpp"
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

using namespace ros2_modbus_gateway;

namespace {

PlcSample create_sample(Generation gen,
                        uint16_t heartbeat = 1,
                        bool ready = true,
                        bool running = false,
                        bool physical_estop = false,
                        uint16_t fault_code = 0,
                        uint16_t applied_counter = 0,
                        MonotonicNs timestamp = 1'000'000ULL,
                        bool alarm_tripped = false,
                        bool valid = true) {
    PlcSample s;
    s.generation = gen;
    s.sequence = 1;
    s.poll_started_ns = timestamp - 5'000'000ULL;
    s.sampled_ns = timestamp;
    s.read_rtt_ns = 5'000'000ULL;
    s.valid = valid;

    s.image.registers[0] = heartbeat;

    uint16_t flags = 0;
    if (ready) flags |= 0x0002;
    if (running) flags |= 0x0004;
    if (physical_estop) flags |= 0x0008;
    if (alarm_tripped) flags |= 0x0020;
    s.image.registers[1] = flags;

    s.image.registers[2] = 500;
    s.image.registers[3] = 500;
    s.image.registers[4] = fault_code;
    s.image.registers[5] = applied_counter;
    return s;
}

} // namespace

TEST(GatewayBufferTest, InitialStateIsDisconnectedWithAlarmActive) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    auto st = buffer.get_status();

    EXPECT_EQ(st.station_id, 1);
    EXPECT_EQ(st.link_state, LinkState::DISCONNECTED);
    EXPECT_TRUE(st.alarm_active);
    EXPECT_FALSE(st.data_valid);
    EXPECT_FALSE(st.last_sample.has_value());
    EXPECT_EQ(st.consecutive_failures, 0);
    EXPECT_EQ(st.generation, 0);
}

TEST(GatewayBufferTest, BeginConnectionAdvancesGenerationAndSetsConnecting) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;

    Generation gen1 = buffer.begin_connection(now);
    EXPECT_EQ(gen1, 1);
    EXPECT_EQ(buffer.link_state(), LinkState::CONNECTING);
    EXPECT_EQ(buffer.current_generation(), 1);

    Generation gen2 = buffer.begin_connection(now + 1'000'000ULL);
    EXPECT_EQ(gen2, 2);
    EXPECT_EQ(buffer.current_generation(), 2);
}

TEST(GatewayBufferTest, AcceptSampleAtomicSnapshotAndRecovery) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    // 1st sample in CONNECTING
    auto s1 = create_sample(gen, 1, true, false, false, 0, 0, now);
    auto ev1 = buffer.accept_sample(s1, now);
    EXPECT_FALSE(ev1.has_value());
    EXPECT_EQ(buffer.link_state(), LinkState::CONNECTING);
    EXPECT_TRUE(buffer.has_sample());

    // 2nd sample reaches recovery_progress_count (2) -> transitions to OPERATIONAL
    now += 20'000'000ULL;
    auto s2 = create_sample(gen, 2, true, false, false, 0, 0, now);
    auto ev2 = buffer.accept_sample(s2, now);
    EXPECT_FALSE(ev2.has_value());
    EXPECT_EQ(buffer.link_state(), LinkState::OPERATIONAL);

    // Even though alarm is active (from startup), data_valid recovers immediately for monitoring!
    EXPECT_TRUE(buffer.is_data_valid());

    auto st = buffer.get_status();
    ASSERT_TRUE(st.last_sample.has_value());
    EXPECT_EQ(st.last_sample->image.heartbeat(), 2);
    EXPECT_EQ(st.link_state, LinkState::OPERATIONAL);
    EXPECT_TRUE(st.data_valid);
}

TEST(GatewayBufferTest, GenerationIsolationDropsStaleSamples) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    buffer.begin_connection(now); // Gen 1
    buffer.begin_connection(now + 10'000ULL); // Gen 2

    // Sample with old gen 1
    auto old_sample = create_sample(1, 100, true, false, false, 0, 0, now + 20'000ULL);
    auto ev = buffer.accept_sample(old_sample, now + 20'000ULL);
    EXPECT_FALSE(ev.has_value());
    EXPECT_FALSE(buffer.has_sample()); // Stale sample dropped!

    // Sample with matching gen 2
    auto valid_sample = create_sample(2, 100, true, false, false, 0, 0, now + 30'000ULL);
    buffer.accept_sample(valid_sample, now + 30'000ULL);
    EXPECT_TRUE(buffer.has_sample());
    EXPECT_EQ(buffer.last_sample()->generation, 2);
}

TEST(GatewayBufferTest, SubmitCommandAcceptanceRules) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;

    CommandRequest start_req{Command::START, 0};
    CommandRequest stop_req{Command::STOP, 0};

    // 1. Rejected when link is not operational
    auto res_not_ready = buffer.submit_command(start_req, now);
    EXPECT_TRUE(res_not_ready.is_err());
    EXPECT_EQ(res_not_ready.error().code, static_cast<uint16_t>(ErrorCode::NOT_READY));

    // Connect and transition to OPERATIONAL
    Generation gen = buffer.begin_connection(now);
    auto s1 = create_sample(gen, 1, true, false, false, 0, 0, now);
    buffer.accept_sample(s1, now);
    auto s2 = create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL);
    buffer.accept_sample(s2, now + 20'000'000ULL);
    EXPECT_EQ(buffer.link_state(), LinkState::OPERATIONAL);

    // 2. START rejected while alarm is active
    EXPECT_TRUE(buffer.is_alarm_active());
    auto res_alarm = buffer.submit_command(start_req, now + 21'000'000ULL);
    EXPECT_TRUE(res_alarm.is_err());
    EXPECT_EQ(res_alarm.error().code, static_cast<uint16_t>(ErrorCode::ALARM_ACTIVE));

    // 3. STOP is ALWAYS permitted during OPERATIONAL even while alarm is active
    auto res_stop = buffer.submit_command(stop_req, now + 22'000'000ULL);
    EXPECT_TRUE(res_stop.is_ok());

    // Clear command slot
    CommandResult stop_result;
    stop_result.id = res_stop.value();
    stop_result.outcome = CommandOutcome::CONFIRMED;
    buffer.complete_command(stop_result);

    // 4. Clear alarm via clear_fault
    auto clear_res = buffer.clear_fault(false, now + 23'000'000ULL);
    EXPECT_TRUE(clear_res.is_ok());
    EXPECT_FALSE(buffer.is_alarm_active());

    // 5. START now succeeds
    auto res_start_ok = buffer.submit_command(start_req, now + 24'000'000ULL);
    EXPECT_TRUE(res_start_ok.is_ok());
}

TEST(GatewayBufferTest, SingleCommandSlotSaturationEnforcesBUSY) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);
    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);
    buffer.clear_fault(false, now + 21'000'000ULL);

    CommandRequest req1{Command::START, 0};
    CommandRequest req2{Command::SET_SETPOINT, 500};

    // Slot empty: req1 accepted
    auto res1 = buffer.submit_command(req1, now + 22'000'000ULL);
    ASSERT_TRUE(res1.is_ok());

    // Slot occupied: req2 rejected with BUSY
    auto res2 = buffer.submit_command(req2, now + 23'000'000ULL);
    EXPECT_TRUE(res2.is_err());
    EXPECT_EQ(res2.error().code, static_cast<uint16_t>(ErrorCode::BUSY));

    // Complete req1: slot freed
    CommandResult done1;
    done1.id = res1.value();
    done1.outcome = CommandOutcome::CONFIRMED;
    buffer.complete_command(done1);

    // Now req2 succeeds
    auto res3 = buffer.submit_command(req2, now + 24'000'000ULL);
    EXPECT_TRUE(res3.is_ok());
}

TEST(GatewayBufferTest, TakeCommandLifecycleAndGenerationIsolation) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);
    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);

    CommandRequest stop_req{Command::STOP, 0};
    auto sub_res = buffer.submit_command(stop_req, now + 22'000'000ULL);
    ASSERT_TRUE(sub_res.is_ok());

    // Take with wrong generation -> nullopt
    auto wrong_task = buffer.take_command(gen + 1, now + 23'000'000ULL);
    EXPECT_FALSE(wrong_task.has_value());

    // Take with matching generation -> task extracted and state set to EXECUTING
    auto valid_task = buffer.take_command(gen, now + 24'000'000ULL);
    ASSERT_TRUE(valid_task.has_value());
    EXPECT_EQ(valid_task->id, sub_res.value());
    EXPECT_EQ(valid_task->phase, CommandPhase::EXECUTING);

    // Taking again while EXECUTING -> nullopt
    auto second_take = buffer.take_command(gen, now + 25'000'000ULL);
    EXPECT_FALSE(second_take.has_value());
}

TEST(GatewayBufferTest, ResetCommandClearsAlarmLatchOnConfirmed) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);
    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);

    // Alarm is active
    EXPECT_TRUE(buffer.is_alarm_active());

    // Path B: Submit RESET command
    CommandRequest reset_req{Command::RESET, 0};
    auto sub_res = buffer.submit_command(reset_req, now + 21'000'000ULL);
    ASSERT_TRUE(sub_res.is_ok());

    auto task = buffer.take_command(gen, now + 22'000'000ULL);
    ASSERT_TRUE(task.has_value());

    // Worker reports CONFIRMED outcome
    CommandResult res;
    res.id = task->id;
    res.outcome = CommandOutcome::CONFIRMED;
    res.completed_ns = now + 25'000'000ULL;
    buffer.complete_command(res);

    // RESET confirmation clears the alarm latch!
    EXPECT_FALSE(buffer.is_alarm_active());
}

TEST(GatewayBufferTest, AlienMethodCallDeadlockPrevention) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);
    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);

    std::atomic<bool> alarm_cb_called{false};
    std::atomic<bool> command_cb_called{false};

    // Callback that calls buffer methods (alien method calls)
    buffer.set_alarm_callback([&](const AlarmEvent& ev) {
        // Access buffer methods from INSIDE the callback:
        // If mutex was held across callback dispatch, this would DEADLOCK!
        auto st = buffer.get_status();
        EXPECT_EQ(st.station_id, 1);
        if (ev.active) {
            buffer.clear_fault(true, ev.detected_ns + 100'000ULL);
            alarm_cb_called = true;
        }
    });

    buffer.set_command_callback([&](const CommandResult& res) {
        // Access buffer methods from INSIDE command callback
        auto st = buffer.get_status();
        EXPECT_EQ(st.station_id, 1);
        EXPECT_EQ(res.outcome, CommandOutcome::CONFIRMED);
        command_cb_called = true;
    });

    // 1. Trigger alarm via report_failure (3 failures)
    GatewayError err(ErrorCode::IO_TIMEOUT, "Simulated failure");
    buffer.report_failure(gen, err, now + 30'000'000ULL);
    buffer.report_failure(gen, err, now + 40'000'000ULL);
    buffer.report_failure(gen, err, now + 50'000'000ULL);

    EXPECT_TRUE(alarm_cb_called.load());

    // 2. Submit and complete a command to test command callback alien method call safety
    buffer.set_link_state(LinkState::OPERATIONAL);
    auto sub = buffer.submit_command(CommandRequest{Command::STOP, 0}, now + 60'000'000ULL);
    ASSERT_TRUE(sub.is_ok());

    CommandResult cmd_res;
    cmd_res.id = sub.value();
    cmd_res.outcome = CommandOutcome::CONFIRMED;
    buffer.complete_command(cmd_res);

    EXPECT_TRUE(command_cb_called.load());
}

TEST(GatewayBufferTest, LockHoldTimeBenchmarkUnderOneMicrosecond) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    constexpr int ITERATIONS = 10000;
    auto sample = create_sample(gen, 10, true, false, false, 0, 0, now);

    // Benchmark accept_sample
    auto start_accept = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        sample.sampled_ns = now + i * 1'000'000ULL;
        sample.image.registers[0] = static_cast<uint16_t>(i & 0x7FFF);
        buffer.accept_sample(sample, sample.sampled_ns);
    }
    auto end_accept = std::chrono::high_resolution_clock::now();
    auto total_accept_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_accept - start_accept).count();
    double avg_accept_us = (total_accept_ns / static_cast<double>(ITERATIONS)) / 1000.0;

    // Benchmark get_status
    auto start_status = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        volatile auto st = buffer.get_status();
        (void)st;
    }
    auto end_status = std::chrono::high_resolution_clock::now();
    auto total_status_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_status - start_status).count();
    double avg_status_us = (total_status_ns / static_cast<double>(ITERATIONS)) / 1000.0;

    // Verifications: Critical section average hold time must be strictly < 1 microsecond (1µs)
    EXPECT_LT(avg_accept_us, 1.0);
    EXPECT_LT(avg_status_us, 1.0);
}

TEST(GatewayBufferTest, MultiThreadedStressConcurrency) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs start_time = 100'000'000ULL;
    Generation gen = buffer.begin_connection(start_time);

    constexpr int OPS = 3000;
    std::atomic<bool> go{false};

    // Thread 1: Asio Polling Worker
    std::thread worker_poll([&]() {
        while (!go) {}
        for (int i = 1; i <= OPS; ++i) {
            auto s = create_sample(gen, static_cast<uint16_t>(i & 0x7FFF), true, false, false, 0, 0,
                                   start_time + i * 20'000'000ULL);
            buffer.accept_sample(s, start_time + i * 20'000'000ULL);
        }
    });

    // Thread 2: ROS Publisher reading status snapshots
    std::thread ros_publisher([&]() {
        while (!go) {}
        for (int i = 0; i < OPS; ++i) {
            auto st = buffer.get_status();
            EXPECT_EQ(st.station_id, 1);
        }
    });

    // Thread 3: ROS Service Client submitting commands
    std::thread ros_client([&]() {
        while (!go) {}
        for (int i = 0; i < OPS; ++i) {
            CommandRequest req{Command::STOP, 0};
            buffer.submit_command(req, start_time + i * 1'000'000ULL);
        }
    });

    // Thread 4: Worker executing and completing commands
    std::thread worker_exec([&]() {
        while (!go) {}
        for (int i = 0; i < OPS; ++i) {
            auto task = buffer.take_command(gen, start_time + i * 1'000'000ULL);
            if (task.has_value()) {
                CommandResult res;
                res.id = task->id;
                res.outcome = CommandOutcome::CONFIRMED;
                res.completed_ns = start_time + i * 1'000'000ULL;
                buffer.complete_command(res);
            }
        }
    });

    go = true;
    worker_poll.join();
    ros_publisher.join();
    ros_client.join();
    worker_exec.join();

    // Verification: Buffer remained consistent, no memory corruption, no deadlocks
    auto final_st = buffer.get_status();
    EXPECT_EQ(final_st.station_id, 1);
    EXPECT_EQ(final_st.generation, gen);
}

TEST(GatewayBufferTest, SubmitCommandRejectsStaleSample) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    // Warm up to OPERATIONAL
    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);
    buffer.clear_fault(false, now + 21'000'000ULL);
    EXPECT_EQ(buffer.link_state(), LinkState::OPERATIONAL);

    // Command submitted 70ms after last sample (exceeding 3 * 20ms = 60ms limit)
    MonotonicNs stale_time = now + 20'000'000ULL + 70'000'000ULL;
    CommandRequest req{Command::START, 0};
    auto res = buffer.submit_command(req, stale_time);
    EXPECT_TRUE(res.is_err());
    EXPECT_EQ(res.error().code, static_cast<uint16_t>(ErrorCode::NOT_READY));
}

TEST(GatewayBufferTest, SubmitCommandRejectsInvalidSample) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    // Sample marked as invalid (e.g. register range check failed in Modbus unpack)
    auto invalid_s = create_sample(gen, 1, true, false, false, 0, 0, now, false, false);
    buffer.accept_sample(invalid_s, now);
    buffer.set_link_state(LinkState::OPERATIONAL);

    CommandRequest req{Command::STOP, 0};
    auto res = buffer.submit_command(req, now + 1'000'000ULL);
    EXPECT_TRUE(res.is_err());
    EXPECT_EQ(res.error().code, static_cast<uint16_t>(ErrorCode::NOT_READY));
}

TEST(GatewayBufferTest, EvaluateStaleUpdatesBufferStateAndDispatchesAlarmCallback) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    buffer.accept_sample(create_sample(gen, 10, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 11, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);
    buffer.clear_fault(false, now + 21'000'000ULL);
    EXPECT_EQ(buffer.link_state(), LinkState::OPERATIONAL);
    EXPECT_FALSE(buffer.is_alarm_active());

    std::atomic<bool> alarm_cb_invoked{false};
    AlarmEvent received_ev{};
    buffer.set_alarm_callback([&](const AlarmEvent& ev) {
        // Alien method safety: read buffer status from callback
        auto st = buffer.get_status();
        EXPECT_TRUE(st.alarm_active);
        EXPECT_EQ(st.link_state, LinkState::COMM_FAULT);
        received_ev = ev;
        alarm_cb_invoked = true;
    });

    // Evaluate stale after 85ms without heartbeat progress
    MonotonicNs stale_time = now + 20'000'000ULL + 85'000'000ULL;
    auto ev = buffer.evaluate_stale(stale_time);
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->cause, AlarmCause::HEARTBEAT_STALE);
    EXPECT_TRUE(buffer.is_alarm_active());
    EXPECT_EQ(buffer.link_state(), LinkState::COMM_FAULT);
    EXPECT_FALSE(buffer.is_data_valid());
    EXPECT_TRUE(alarm_cb_invoked.load());
}

TEST(GatewayBufferTest, TakeCommandTimeoutDispatchesCommandCallback) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);

    CommandRequest req{Command::STOP, 0};
    auto sub_res = buffer.submit_command(req, now + 21'000'000ULL);
    ASSERT_TRUE(sub_res.is_ok());

    std::atomic<bool> cmd_cb_invoked{false};
    CommandResult timeout_res{};
    buffer.set_command_callback([&](const CommandResult& res) {
        // Alien method safety: read buffer status
        auto st = buffer.get_status();
        EXPECT_EQ(st.station_id, 1);
        timeout_res = res;
        cmd_cb_invoked = true;
    });

    // Worker attempts take_command beyond 300ms deadline (accepted_ns + 300ms)
    MonotonicNs expired_time = now + 21'000'000ULL + 305'000'000ULL;
    auto task = buffer.take_command(gen, expired_time);
    EXPECT_FALSE(task.has_value()); // Expired command not dispatched to worker
    EXPECT_TRUE(cmd_cb_invoked.load()); // Deferred response callback notified!
    EXPECT_EQ(timeout_res.outcome, CommandOutcome::NOT_SENT);
    EXPECT_EQ(timeout_res.error.code, static_cast<uint16_t>(ErrorCode::COMMAND_TIMEOUT));
}

TEST(GatewayBufferTest, ResetCommandConfirmationDispatchesAlarmClearedCallback) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);
    EXPECT_TRUE(buffer.is_alarm_active());

    std::atomic<bool> alarm_cleared_invoked{false};
    buffer.set_alarm_callback([&](const AlarmEvent& ev) {
        if (!ev.active) {
            alarm_cleared_invoked = true;
        }
    });

    CommandRequest reset_req{Command::RESET, 0};
    auto sub = buffer.submit_command(reset_req, now + 21'000'000ULL);
    ASSERT_TRUE(sub.is_ok());

    auto task = buffer.take_command(gen, now + 22'000'000ULL);
    ASSERT_TRUE(task.has_value());

    CommandResult res;
    res.id = task->id;
    res.outcome = CommandOutcome::CONFIRMED;
    res.completed_ns = now + 25'000'000ULL;
    buffer.complete_command(res);

    EXPECT_FALSE(buffer.is_alarm_active());
    EXPECT_TRUE(alarm_cleared_invoked.load());
}

TEST(GatewayBufferTest, ConcurrentClearFaultAndIoFailureBoundary) {
    GatewayBuffer buffer(1, 20, 3, 80, 300, 2);
    MonotonicNs now = 100'000'000ULL;
    Generation gen = buffer.begin_connection(now);

    buffer.accept_sample(create_sample(gen, 1, true, false, false, 0, 0, now), now);
    buffer.accept_sample(create_sample(gen, 2, true, false, false, 0, 0, now + 20'000'000ULL), now + 20'000'000ULL);
    buffer.clear_fault(false, now + 21'000'000ULL);
    EXPECT_FALSE(buffer.is_alarm_active());

    constexpr int ITERS = 1000;
    std::atomic<bool> go{false};

    // Thread 1 drives failures to the 3rd failure boundary
    std::thread t1([&]() {
        while (!go) {}
        GatewayError err(ErrorCode::IO_TIMEOUT, "Simulated boundary failure");
        for (int i = 0; i < ITERS; ++i) {
            buffer.report_failure(gen, err, now + 30'000'000ULL + i * 1'000'000ULL);
        }
    });

    // Thread 2 continuously attempts clear_fault
    std::thread t2([&]() {
        while (!go) {}
        for (int i = 0; i < ITERS; ++i) {
            buffer.clear_fault(false, now + 30'000'000ULL + i * 1'000'000ULL);
        }
    });

    go = true;
    t1.join();
    t2.join();

    auto st = buffer.get_status();
    EXPECT_EQ(st.station_id, 1);
    EXPECT_EQ(st.generation, gen);
}

