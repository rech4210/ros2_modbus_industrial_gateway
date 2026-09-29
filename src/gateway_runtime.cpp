#include "ros2_modbus_gateway/gateway_runtime.hpp"
#include "ros2_modbus_gateway/monotonic_clock.hpp"

#include <iostream>

namespace ros2_modbus_gateway {

GatewayRuntime::GatewayRuntime(const GatewayConfig& config)
    : config_(config) {
    work_guard_.emplace(boost::asio::make_work_guard(io_context_));

    for (const auto& station_cfg : config_.stations) {
        auto station = std::make_unique<StationContext>(station_cfg, io_context_);
        stations_[station_cfg.station_id] = std::move(station);
    }
}

GatewayRuntime::~GatewayRuntime() {
    request_stop();
    join();
}

Result<void> GatewayRuntime::start() {
    if (running_.load()) {
        return Result<void>::ok();
    }

    // Wire callbacks to all stations
    for (auto& [sid, station] : stations_) {
        station->set_alarm_callback([this](const AlarmEvent& ev) {
            if (alarm_callback_) {
                alarm_callback_(ev);
            }
        });

        station->set_command_callback([this](const CommandResult& res) {
            if (command_callback_) {
                command_callback_(res);
            }
        });

        station->start();
    }

    running_.store(true);
    io_thread_ = std::thread([this]() {
        try {
            io_context_.run();
        } catch (const std::exception& e) {
            std::cerr << "[GatewayRuntime] io_context exception: " << e.what() << std::endl;
        }
    });

    return Result<void>::ok();
}

void GatewayRuntime::set_alarm_callback(AlarmCallback cb) {
    alarm_callback_ = std::move(cb);
}

void GatewayRuntime::set_command_callback(CommandCallback cb) {
    command_callback_ = std::move(cb);
}

Result<CommandId> GatewayRuntime::submit(StationId sid, const CommandRequest& req) {
    if (!running_.load()) {
        return Result<CommandId>::err(
            GatewayError(ErrorCode::SHUTTING_DOWN, "Gateway runtime is not running"));
    }

    auto it = stations_.find(sid);
    if (it == stations_.end()) {
        return Result<CommandId>::err(
            GatewayError(ErrorCode::INVALID_ARGUMENT, "Station ID not found: " + std::to_string(sid)));
    }

    return it->second->submit_command(req, Clock::now_ns());
}

Result<void> GatewayRuntime::clear_fault(StationId sid, bool force) {
    if (!running_.load()) {
        return Result<void>::err(
            GatewayError(ErrorCode::SHUTTING_DOWN, "Gateway runtime is not running"));
    }

    auto it = stations_.find(sid);
    if (it == stations_.end()) {
        return Result<void>::err(
            GatewayError(ErrorCode::INVALID_ARGUMENT, "Station ID not found: " + std::to_string(sid)));
    }

    auto res = it->second->clear_fault(force, Clock::now_ns());
    if (res.is_ok()) {
        return Result<void>::ok();
    }
    return Result<void>::err(res.error());
}

void GatewayRuntime::request_stop() noexcept {
    if (!running_.exchange(false)) {
        return;
    }

    for (auto& [sid, station] : stations_) {
        station->stop();
    }

    if (work_guard_.has_value()) {
        work_guard_->reset();
        work_guard_.reset();
    }

    io_context_.stop();
}

void GatewayRuntime::join() {
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

GatewayStatus GatewayRuntime::get_status(StationId sid) const {
    auto it = stations_.find(sid);
    if (it != stations_.end()) {
        return it->second->get_status();
    }
    GatewayStatus empty;
    empty.station_id = sid;
    return empty;
}

std::vector<GatewayStatus> GatewayRuntime::get_all_statuses() const {
    std::vector<GatewayStatus> list;
    list.reserve(stations_.size());
    for (const auto& [sid, station] : stations_) {
        list.push_back(station->get_status());
    }
    return list;
}

} // namespace ros2_modbus_gateway
