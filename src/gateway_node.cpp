#include "ros2_modbus_gateway/gateway_node.hpp"
#include <random>

namespace ros2_modbus_gateway {

GatewayNode::GatewayNode(const rclcpp::NodeOptions& options)
    : Node("gateway_node", options) {
    load_parameters();
    init_node();
}

GatewayNode::GatewayNode(const GatewayConfig& config, const rclcpp::NodeOptions& options)
    : Node("gateway_node", options), config_(config) {
    init_node();
}

GatewayNode::~GatewayNode() {
    if (alarm_waitable_) {
        this->get_node_waitables_interface()->remove_waitable(alarm_waitable_, nullptr);
        alarm_waitable_.reset();
    }
    if (runtime_) {
        runtime_->request_stop();
        runtime_->join();
        runtime_.reset();
    }
}

void GatewayNode::load_parameters() {
    this->declare_parameter<std::vector<int64_t>>("station_ids", std::vector<int64_t>{});
    auto station_ids = this->get_parameter("station_ids").as_integer_array();

    config_.instance_name = this->get_name();
    config_.stations.clear();

    if (!station_ids.empty()) {
        for (int64_t sid_int : station_ids) {
            StationId sid = static_cast<StationId>(sid_int);
            std::string prefix = "station_" + std::to_string(sid) + ".";

            this->declare_parameter<std::string>(prefix + "plc_host", "mock_plc");
            this->declare_parameter<int>(prefix + "plc_port", 5020);
            this->declare_parameter<int>(prefix + "unit_id", sid);
            this->declare_parameter<int>(prefix + "coil_base", 0);
            this->declare_parameter<int>(prefix + "holding_base", 0);
            this->declare_parameter<int>(prefix + "poll_period_ms", 20);
            this->declare_parameter<int>(prefix + "publish_period_ms", 20);
            this->declare_parameter<int>(prefix + "response_timeout_ms", 25);
            this->declare_parameter<int>(prefix + "connect_timeout_ms", 200);
            this->declare_parameter<int>(prefix + "consecutive_failures_limit", 3);
            this->declare_parameter<int>(prefix + "heartbeat_timeout_ms", 80);
            this->declare_parameter<int>(prefix + "command_timeout_ms", 300);
            this->declare_parameter<int>(prefix + "recovery_progress_count", 2);

            StationConfig station_cfg;
            station_cfg.station_id = sid;
            station_cfg.plc_host = this->get_parameter(prefix + "plc_host").as_string();
            station_cfg.plc_port = static_cast<uint16_t>(this->get_parameter(prefix + "plc_port").as_int());
            station_cfg.unit_id = static_cast<uint8_t>(this->get_parameter(prefix + "unit_id").as_int());
            station_cfg.coil_base = static_cast<uint16_t>(this->get_parameter(prefix + "coil_base").as_int());
            station_cfg.holding_base = static_cast<uint16_t>(this->get_parameter(prefix + "holding_base").as_int());
            station_cfg.poll_period_ms = static_cast<uint16_t>(this->get_parameter(prefix + "poll_period_ms").as_int());
            station_cfg.publish_period_ms = static_cast<uint16_t>(this->get_parameter(prefix + "publish_period_ms").as_int());
            station_cfg.response_timeout_ms = static_cast<uint16_t>(this->get_parameter(prefix + "response_timeout_ms").as_int());
            station_cfg.connect_timeout_ms = static_cast<uint16_t>(this->get_parameter(prefix + "connect_timeout_ms").as_int());
            station_cfg.consecutive_failures_limit = static_cast<uint8_t>(this->get_parameter(prefix + "consecutive_failures_limit").as_int());
            station_cfg.heartbeat_timeout_ms = static_cast<uint16_t>(this->get_parameter(prefix + "heartbeat_timeout_ms").as_int());
            station_cfg.command_timeout_ms = static_cast<uint16_t>(this->get_parameter(prefix + "command_timeout_ms").as_int());
            station_cfg.recovery_progress_count = static_cast<uint8_t>(this->get_parameter(prefix + "recovery_progress_count").as_int());

            config_.stations.push_back(station_cfg);
        }
    } else {
        // Default single station flat parameters
        this->declare_parameter<std::string>("plc_host", "mock_plc");
        this->declare_parameter<int>("plc_port", 5020);
        this->declare_parameter<int>("station_id", 1);
        this->declare_parameter<int>("unit_id", 1);
        this->declare_parameter<int>("coil_base", 0);
        this->declare_parameter<int>("holding_base", 0);
        this->declare_parameter<int>("poll_period_ms", 20);
        this->declare_parameter<int>("publish_period_ms", 20);
        this->declare_parameter<int>("response_timeout_ms", 25);
        this->declare_parameter<int>("connect_timeout_ms", 200);
        this->declare_parameter<int>("consecutive_failures_limit", 3);
        this->declare_parameter<int>("heartbeat_timeout_ms", 80);
        this->declare_parameter<int>("command_timeout_ms", 300);
        this->declare_parameter<int>("recovery_progress_count", 2);

        StationConfig station_cfg;
        station_cfg.plc_host = this->get_parameter("plc_host").as_string();
        station_cfg.plc_port = static_cast<uint16_t>(this->get_parameter("plc_port").as_int());
        station_cfg.station_id = static_cast<StationId>(this->get_parameter("station_id").as_int());
        station_cfg.unit_id = static_cast<uint8_t>(this->get_parameter("unit_id").as_int());
        station_cfg.coil_base = static_cast<uint16_t>(this->get_parameter("coil_base").as_int());
        station_cfg.holding_base = static_cast<uint16_t>(this->get_parameter("holding_base").as_int());
        station_cfg.poll_period_ms = static_cast<uint16_t>(this->get_parameter("poll_period_ms").as_int());
        station_cfg.publish_period_ms = static_cast<uint16_t>(this->get_parameter("publish_period_ms").as_int());
        station_cfg.response_timeout_ms = static_cast<uint16_t>(this->get_parameter("response_timeout_ms").as_int());
        station_cfg.connect_timeout_ms = static_cast<uint16_t>(this->get_parameter("connect_timeout_ms").as_int());
        station_cfg.consecutive_failures_limit = static_cast<uint8_t>(this->get_parameter("consecutive_failures_limit").as_int());
        station_cfg.heartbeat_timeout_ms = static_cast<uint16_t>(this->get_parameter("heartbeat_timeout_ms").as_int());
        station_cfg.command_timeout_ms = static_cast<uint16_t>(this->get_parameter("command_timeout_ms").as_int());
        station_cfg.recovery_progress_count = static_cast<uint8_t>(this->get_parameter("recovery_progress_count").as_int());

        config_.stations.push_back(station_cfg);
    }
}

void GatewayNode::init_node() {
    // Generate UUIDv4 instance ID
    std::random_device rd;
    for (auto& b : instance_id_) {
        b = static_cast<uint8_t>(rd() & 0xFF);
    }
    instance_id_[6] = (instance_id_[6] & 0x0F) | 0x40; // UUID v4
    instance_id_[8] = (instance_id_[8] & 0x3F) | 0x80; // Variant RFC 4122

    init_publishers_and_services();

    // Instantiate and start GatewayRuntime
    runtime_ = std::make_unique<GatewayRuntime>(config_);
    runtime_->set_alarm_callback([this](const AlarmEvent& ev) {
        on_alarm_event(ev);
    });
    runtime_->set_command_callback([this](const CommandResult& res) {
        on_command_completed(res);
    });
    runtime_->start();

    // Initial alarm state publish on startup conforming to Transient Local QoS and §2.6
    for (const auto& station_cfg : config_.stations) {
        AlarmEvent startup_alarm;
        startup_alarm.station_id = station_cfg.station_id;
        startup_alarm.sequence = 1;
        startup_alarm.active = true;
        startup_alarm.cause = AlarmCause::STARTUP;
        startup_alarm.detected_ns = Clock::now_ns();
        publish_alarm(startup_alarm);
    }
}

void GatewayNode::init_publishers_and_services() {
    rclcpp::QoS state_qos(1);
    state_qos.best_effort();
    state_qos.durability_volatile();

    rclcpp::QoS alarm_qos(1);
    alarm_qos.reliable();
    alarm_qos.transient_local();

    // GuardCondition for zero-delay event-driven alarm publishing
    alarm_guard_condition_ = std::make_shared<rclcpp::GuardCondition>(
        this->get_node_base_interface()->get_context());
    alarm_waitable_ = std::make_shared<AlarmWaitable>(
        alarm_guard_condition_,
        std::bind(&GatewayNode::on_alarm_guard_condition, this));
    this->get_node_waitables_interface()->add_waitable(alarm_waitable_, nullptr);

    // 20ms Periodic State Publish Timer
    uint16_t pub_period_ms = config_.stations.empty() ? 20 : config_.stations[0].publish_period_ms;
    publish_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(pub_period_ms),
        std::bind(&GatewayNode::on_publish_timer, this));

    bool multi_station = config_.stations.size() > 1;

    if (!multi_station) {
        StationId sid = config_.stations.empty() ? 1 : config_.stations[0].station_id;

        // Default un-namespaced endpoints conforming to spec §2.6 single station mode
        default_plc_state_pub_ = this->create_publisher<PlcState>("/plc/state", state_qos);
        default_safety_alarm_pub_ = this->create_publisher<SafetyAlarm>("/safety/alarm", alarm_qos);

        StationEndpoints ep;
        ep.station_id = sid;
        ep.plc_state_pub = default_plc_state_pub_;
        ep.safety_alarm_pub = default_safety_alarm_pub_;

        auto trigger_cb = [this, sid](std::shared_ptr<rmw_request_id_t> hdr,
                                     std::shared_ptr<TriggerCommand::Request> req) {
            auto it = station_endpoints_.find(sid);
            if (it != station_endpoints_.end() && it->second.trigger_command_service) {
                on_trigger_command_with_service(sid, it->second.trigger_command_service, hdr, req);
            }
        };
        ep.trigger_command_service = this->create_service<TriggerCommand>("/plc/trigger_command", trigger_cb);

        auto alias_cb = [this, sid](std::shared_ptr<rmw_request_id_t> hdr,
                                   std::shared_ptr<TriggerCommand::Request> req) {
            auto it = station_endpoints_.find(sid);
            if (it != station_endpoints_.end() && it->second.command_service_alias) {
                on_trigger_command_with_service(sid, it->second.command_service_alias, hdr, req);
            }
        };
        ep.command_service_alias = this->create_service<TriggerCommand>("/plc/command", alias_cb);

        auto clear_cb = [this, sid](std::shared_ptr<rmw_request_id_t> hdr,
                                   std::shared_ptr<ClearFault::Request> req) {
            auto it = station_endpoints_.find(sid);
            if (it != station_endpoints_.end() && it->second.clear_fault_service) {
                on_clear_fault_with_service(sid, it->second.clear_fault_service, hdr, req);
            }
        };
        ep.clear_fault_service = this->create_service<ClearFault>("/plc/clear_fault", clear_cb);

        station_endpoints_[sid] = ep;
    } else {
        // Multi-station mode conforming to spec §2.6: /station_<id>/...
        for (const auto& st_cfg : config_.stations) {
            StationId sid = st_cfg.station_id;
            std::string prefix = "/station_" + std::to_string(sid);

            StationEndpoints ep;
            ep.station_id = sid;
            ep.plc_state_pub = this->create_publisher<PlcState>(prefix + "/plc/state", state_qos);
            ep.safety_alarm_pub = this->create_publisher<SafetyAlarm>(prefix + "/safety/alarm", alarm_qos);

            auto trigger_cb = [this, sid](std::shared_ptr<rmw_request_id_t> hdr,
                                         std::shared_ptr<TriggerCommand::Request> req) {
                auto it = station_endpoints_.find(sid);
                if (it != station_endpoints_.end() && it->second.trigger_command_service) {
                    on_trigger_command_with_service(sid, it->second.trigger_command_service, hdr, req);
                }
            };
            ep.trigger_command_service = this->create_service<TriggerCommand>(prefix + "/plc/trigger_command", trigger_cb);

            auto alias_cb = [this, sid](std::shared_ptr<rmw_request_id_t> hdr,
                                       std::shared_ptr<TriggerCommand::Request> req) {
                auto it = station_endpoints_.find(sid);
                if (it != station_endpoints_.end() && it->second.command_service_alias) {
                    on_trigger_command_with_service(sid, it->second.command_service_alias, hdr, req);
                }
            };
            ep.command_service_alias = this->create_service<TriggerCommand>(prefix + "/plc/command", alias_cb);

            auto clear_cb = [this, sid](std::shared_ptr<rmw_request_id_t> hdr,
                                       std::shared_ptr<ClearFault::Request> req) {
                auto it = station_endpoints_.find(sid);
                if (it != station_endpoints_.end() && it->second.clear_fault_service) {
                    on_clear_fault_with_service(sid, it->second.clear_fault_service, hdr, req);
                }
            };
            ep.clear_fault_service = this->create_service<ClearFault>(prefix + "/plc/clear_fault", clear_cb);

            station_endpoints_[sid] = ep;
        }
    }
}

void GatewayNode::on_publish_timer() {
    MonotonicNs now = Clock::now_ns();

    // 1. Publish latest state for each station
    for (const auto& station_cfg : config_.stations) {
        GatewayStatus st = runtime_->get_status(station_cfg.station_id);
        publish_state(st, now);
    }

    // 2. Watchdog: check pending commands for 300ms deadline expiration
    std::vector<PendingCommand> timed_out_commands;
    {
        std::lock_guard<std::mutex> lock(pending_commands_mutex_);
        for (auto it = pending_commands_.begin(); it != pending_commands_.end(); ) {
            if (now > it->second.deadline_ns) {
                timed_out_commands.push_back(it->second);
                it = pending_commands_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const auto& pending : timed_out_commands) {
        TriggerCommand::Response resp;
        resp.success = false;
        resp.command_id = pending.id;
        resp.outcome = static_cast<uint8_t>(CommandOutcome::UNKNOWN);
        resp.error_code = static_cast<uint16_t>(ErrorCode::COMMAND_TIMEOUT);
        resp.message = "Command timed out (300ms watchdog expired)";
        resp.elapsed_ms = static_cast<uint32_t>((now - pending.submitted_ns) / 1'000'000ULL);
        pending.service->send_response(*pending.header, resp);
    }
}

void GatewayNode::publish_state(const GatewayStatus& st, MonotonicNs pub_ns) {
    PlcState msg;
    msg.stamp = this->get_clock()->now();
    for (size_t i = 0; i < 16; ++i) {
        msg.instance_id.uuid[i] = instance_id_[i];
    }
    msg.station_id = st.station_id;
    msg.publish_sequence = ++publish_sequence_;
    msg.generation = st.generation;
    msg.link_state = static_cast<uint8_t>(st.link_state);
    msg.has_sample = st.last_sample.has_value();
    msg.data_valid = st.data_valid;
    msg.alarm_active = st.alarm_active;

    if (st.last_sample.has_value()) {
        const auto& sample = *st.last_sample;
        msg.sample_sequence = sample.sequence;
        msg.heartbeat = sample.image.heartbeat();
        msg.status_flags = sample.image.status_flags();
        msg.run_requested = sample.image.run_requested();
        msg.ready = sample.image.ready();
        msg.running = sample.image.running();
        msg.physical_estop = sample.image.physical_estop();
        msg.reset_requested = sample.image.reset_requested();
        msg.alarm_tripped = sample.image.alarm_tripped();
        msg.sensor_raw = sample.image.sensor_raw();
        msg.setpoint_raw = sample.image.setpoint_raw();
        msg.fault_code = sample.image.fault_code();
        msg.applied_command_counter = sample.image.applied_command_counter();

        msg.sample_age_ns = pub_ns >= sample.sampled_ns ? (pub_ns - sample.sampled_ns) : 0;
        msg.poll_started_ns = sample.poll_started_ns;
        msg.sampled_ns = sample.sampled_ns;
        msg.read_rtt_ns = sample.read_rtt_ns;
        msg.poll_jitter_ns = sample.poll_jitter_ns;
    } else {
        msg.sample_sequence = 0;
        msg.sample_age_ns = 0;
    }

    if (st.last_progress_ns.has_value() && pub_ns >= *st.last_progress_ns) {
        msg.heartbeat_age_ns = pub_ns - *st.last_progress_ns;
    } else {
        msg.heartbeat_age_ns = 0;
    }

    msg.published_ns = pub_ns;
    msg.poll_attempt_count = st.poll_attempt_count;
    msg.io_error_count = st.io_error_count;
    msg.consecutive_failures = st.consecutive_failures;
    msg.error_code = st.last_error.code;

    auto it = station_endpoints_.find(st.station_id);
    if (it != station_endpoints_.end() && it->second.plc_state_pub) {
        it->second.plc_state_pub->publish(msg);
    } else if (default_plc_state_pub_) {
        default_plc_state_pub_->publish(msg);
    }
}

void GatewayNode::on_alarm_event(const AlarmEvent& event) {
    {
        std::lock_guard<std::mutex> lock(alarm_queue_mutex_);
        pending_alarms_.push_back(event);
    }
    if (alarm_waitable_) {
        alarm_waitable_->trigger();
    }
}

void GatewayNode::on_alarm_guard_condition() {
    std::vector<AlarmEvent> alarms;
    {
        std::lock_guard<std::mutex> lock(alarm_queue_mutex_);
        alarms.swap(pending_alarms_);
    }

    for (const auto& ev : alarms) {
        publish_alarm(ev);
    }
}

void GatewayNode::publish_alarm(const AlarmEvent& ev) {
    SafetyAlarm msg;
    msg.stamp = this->get_clock()->now();
    for (size_t i = 0; i < 16; ++i) {
        msg.instance_id.uuid[i] = instance_id_[i];
    }
    msg.station_id = ev.station_id;
    msg.event_sequence = ev.sequence;
    msg.active = ev.active;
    msg.cause = static_cast<uint8_t>(ev.cause);
    msg.error_code = ev.error.code;
    msg.generation = ev.generation;
    msg.detected_ns = ev.detected_ns;
    msg.published_ns = Clock::now_ns();
    msg.last_progress_ns = ev.last_progress_ns.value_or(0);

    auto it = station_endpoints_.find(ev.station_id);
    if (it != station_endpoints_.end() && it->second.safety_alarm_pub) {
        it->second.safety_alarm_pub->publish(msg);
    } else if (default_safety_alarm_pub_) {
        default_safety_alarm_pub_->publish(msg);
    }
}

void GatewayNode::on_trigger_command(std::shared_ptr<rmw_request_id_t> hdr,
                                    std::shared_ptr<TriggerCommand::Request> req) {
    StationId sid = config_.stations.empty() ? 1 : config_.stations[0].station_id;
    auto it = station_endpoints_.find(sid);
    if (it != station_endpoints_.end() && it->second.trigger_command_service) {
        on_trigger_command_with_service(sid, it->second.trigger_command_service, hdr, req);
    }
}

void GatewayNode::on_trigger_command_with_service(
    StationId sid,
    std::shared_ptr<rclcpp::Service<TriggerCommand>> srv,
    std::shared_ptr<rmw_request_id_t> hdr,
    std::shared_ptr<TriggerCommand::Request> req) {

    MonotonicNs now = Clock::now_ns();

    // Command validation conforming to spec §2.6 and §3.5
    if (req->command < 1 || req->command > 4) {
        TriggerCommand::Response resp;
        resp.success = false;
        resp.command_id = 0;
        resp.outcome = static_cast<uint8_t>(CommandOutcome::REJECTED);
        resp.error_code = static_cast<uint16_t>(ErrorCode::INVALID_ARGUMENT);
        resp.message = "Invalid command value (must be 1..4)";
        srv->send_response(*hdr, resp);
        return;
    }

    if (req->command == 4 && req->value > 1000) {
        TriggerCommand::Response resp;
        resp.success = false;
        resp.command_id = 0;
        resp.outcome = static_cast<uint8_t>(CommandOutcome::REJECTED);
        resp.error_code = static_cast<uint16_t>(ErrorCode::INVALID_ARGUMENT);
        resp.message = "Setpoint value exceeds maximum allowable range (0~1000)";
        srv->send_response(*hdr, resp);
        return;
    }

    if (req->command != 4 && req->value != 0) {
        TriggerCommand::Response resp;
        resp.success = false;
        resp.command_id = 0;
        resp.outcome = static_cast<uint8_t>(CommandOutcome::REJECTED);
        resp.error_code = static_cast<uint16_t>(ErrorCode::INVALID_ARGUMENT);
        resp.message = "Value must be 0 for non-SET_SETPOINT commands";
        srv->send_response(*hdr, resp);
        return;
    }

    CommandRequest creq;
    creq.command = static_cast<Command>(req->command);
    creq.value = req->value;

    auto res = runtime_->submit(sid, creq);
    if (res.is_err()) {
        TriggerCommand::Response resp;
        resp.success = false;
        resp.command_id = 0;
        resp.outcome = static_cast<uint8_t>(CommandOutcome::REJECTED);
        resp.error_code = res.error().code;
        resp.system_errno = res.error().system_errno;
        resp.modbus_exception = res.error().modbus_exception;
        resp.message = res.error().detail;
        srv->send_response(*hdr, resp);
        return;
    }

    CommandId cid = res.value();
    uint16_t timeout_ms = config_.stations.empty() ? 300 : config_.stations[0].command_timeout_ms;

    PendingCommand pending;
    pending.id = cid;
    pending.header = hdr;
    pending.service = srv;
    pending.station_id = sid;
    pending.submitted_ns = now;
    pending.deadline_ns = now + (static_cast<DurationNs>(timeout_ms) * 1'000'000ULL);

    {
        std::lock_guard<std::mutex> lock(pending_commands_mutex_);
        pending_commands_[cid] = pending;
    }
    RCLCPP_INFO(get_logger(), "[Node] TriggerCommand accepted: cid=%lu sid=%u", cid, sid);
}

void GatewayNode::on_command_completed(const CommandResult& res) {
    send_command_response(res);
}

void GatewayNode::send_command_response(const CommandResult& res) {
    PendingCommand pending;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(pending_commands_mutex_);
        auto it = pending_commands_.find(res.id);
        if (it != pending_commands_.end()) {
            pending = it->second;
            pending_commands_.erase(it);
            found = true;
        }
    }

    if (!found) {
        return;
    }

    TriggerCommand::Response resp;
    resp.success = (res.outcome == CommandOutcome::CONFIRMED);
    resp.command_id = res.id;
    resp.outcome = static_cast<uint8_t>(res.outcome);
    resp.error_code = res.error.code;
    resp.system_errno = res.error.system_errno;
    resp.modbus_exception = res.error.modbus_exception;
    resp.message = res.error.detail.empty()
        ? (resp.success ? "Command executed successfully" : "Command execution failed")
        : res.error.detail;
    resp.generation = res.generation;
    resp.confirmed_sample_sequence = res.confirmed_sample_sequence;

    MonotonicNs now = Clock::now_ns();
    resp.elapsed_ms = static_cast<uint32_t>((now - pending.submitted_ns) / 1'000'000ULL);

    pending.service->send_response(*pending.header, resp);
}

void GatewayNode::on_clear_fault(std::shared_ptr<rmw_request_id_t> hdr,
                                std::shared_ptr<ClearFault::Request> req) {
    StationId sid = config_.stations.empty() ? 1 : config_.stations[0].station_id;
    auto it = station_endpoints_.find(sid);
    if (it != station_endpoints_.end() && it->second.clear_fault_service) {
        on_clear_fault_with_service(sid, it->second.clear_fault_service, hdr, req);
    }
}

void GatewayNode::on_clear_fault_with_service(
    StationId sid,
    std::shared_ptr<rclcpp::Service<ClearFault>> srv,
    std::shared_ptr<rmw_request_id_t> hdr,
    std::shared_ptr<ClearFault::Request> req) {

    MonotonicNs start = Clock::now_ns();
    auto res = runtime_->clear_fault(sid, req->force_clear);
    MonotonicNs finish = Clock::now_ns();

    ClearFault::Response resp;
    resp.success = res.is_ok();
    if (res.is_ok()) {
        resp.error_code = 0;
        resp.message = "Fault cleared successfully";
    } else {
        resp.error_code = res.error().code;
        resp.message = res.error().detail;
    }
    resp.elapsed_ms = static_cast<uint32_t>((finish - start) / 1'000'000ULL);

    srv->send_response(*hdr, resp);
}

} // namespace ros2_modbus_gateway
