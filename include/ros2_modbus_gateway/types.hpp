#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "ros2_modbus_gateway/register_map.hpp"

namespace ros2_modbus_gateway {

using MonotonicNs    = uint64_t;
using DurationNs     = uint64_t;
using Generation     = uint64_t;
using CommandId      = uint64_t;
using SampleSequence = uint64_t;
using InstanceId     = std::array<uint8_t, 16>;
using StationId      = uint8_t;

template <typename T>
using Optional = std::optional<T>;

enum class LinkState : uint8_t {
    DISCONNECTED = 0,   // 소켓 닫힘, 재시도 대기 상태
    CONNECTING   = 1,   // TCP 연결 핸드셰이크 진행 중
    OPERATIONAL  = 2,   // 정상 통신 중, 폴링 및 제어 가능
    COMM_FAULT   = 3,   // 통신 단절 또는 연속 실패로 인한 오류 상태
    STOPPING     = 4    // 게이트웨이 노드 종료 진행 중
};

enum class Command : uint8_t {
    START        = 1,   // PLC 운전 시작 요청
    STOP         = 2,   // PLC 운전 정지 요청
    RESET        = 3,   // PLC 오류 리셋 요청
    SET_SETPOINT = 4    // 공정 설정값(Setpoint) 변경 요청
};

enum class CommandPhase : uint8_t {
    EMPTY                = 0,   // 슬롯 비어있음
    QUEUED               = 1,   // 서비스 수락 완료, 송신 대기
    EXECUTING            = 2,   // Modbus 쓰기 패킷 송신 완료
    WAITING_CONFIRMATION = 3,   // PLC 상태 반영 대기 중
    TERMINAL             = 4    // 완료 또는 실패 확정
};

enum class CommandOutcome : uint8_t {
    NOT_SENT  = 0,      // 검증 실패 또는 사전 연결 단절로 전송되지 않음
    CONFIRMED = 1,      // PLC 레지스터에 성공적으로 반영됨을 확인
    UNKNOWN   = 2,      // 쓰기 후 통신 두절 또는 타임아웃으로 반영 여부 불명
    REJECTED  = 3       // PLC가 Modbus Exception을 반환하거나 조건 미충족 거부
};

enum class AlarmCause : uint8_t {
    NONE            = 0, // 정상 (알람 없음)
    STARTUP         = 1, // 노드 초기 기동 시 안전 래치 활성화
    COMM_TIMEOUT    = 2, // 연속 N회 폴링 실패 또는 소켓 타임아웃
    HEARTBEAT_STALE = 3, // 응답 패킷은 오나 PLC heartbeat가 멈춤
    PLC_INTERLOCK   = 4, // 물리 E-Stop 또는 PLC 내부 공정 결함 감지
    MANUAL_STOP     = 5, // 상위 시스템 또는 오퍼레이터 수동 정지
    SHUTDOWN        = 6, // 게이트웨이 노드 정상 종료
    INTERNAL        = 7  // 시스템 내부 버퍼 오류 또는 예외
};

enum class ErrorCode : uint16_t {
    OK                        = 0,  // 성공
    INVALID_ARGUMENT          = 1,  // 요청 필드·값 오류
    INVALID_CONFIG            = 2,  // 시작 파라미터 오류
    NOT_READY                 = 3,  // 미연결·초기화 중·데이터 무효
    BUSY                      = 4,  // 명령 슬롯 점유 중
    ALARM_ACTIVE              = 5,  // 알람 래치 활성화로 제어 명령 금지
    INTERLOCK_ACTIVE          = 6,  // 물리 E-Stop 또는 PLC 준비 미충족
    CONNECT_FAILED            = 10, // TCP 소켓 연결 실패
    IO_TIMEOUT                = 11, // 읽기·쓰기 응답 타임아웃
    CONNECTION_LOST           = 12, // EOF, TCP RST, broken pipe
    PROTOCOL_ERROR            = 13, // MBAP 헤더 오류, 잘못된 응답 구조
    MODBUS_EXCEPTION          = 14, // PLC Modbus exception (0x01~0x04)
    HEARTBEAT_STALE           = 15, // 80ms 이상 heartbeat 무변화
    BULK_READ_MISMATCH        = 16, // 수신된 6개 레지스터 바이트 수 불일치
    COMMAND_TIMEOUT           = 17, // 300ms deadline 내 반영 미확인
    COMMAND_OUTCOME_UNKNOWN   = 18, // 쓰기 전송 후 단절로 반영 여부 불명
    GENERATION_EXPIRED        = 19, // 이전 세션의 지연 응답 유입
    SHUTTING_DOWN             = 20, // 노드 종료 진행 중
    INTERNAL_ERROR            = 21, // 내부 메모리 또는 시스템 예외
    PLC_FAULT                 = 22, // PLC 내부 공정 결함 코드 관측
    RESPONSE_DELIVERY_FAILED  = 23, // ROS 서비스 응답 발송 실패
    COMMAND_CONFLICT          = 24  // 기대치와 다른 반영 카운터 변화
};

struct GatewayError {
    uint16_t code{0};              // §4.1 표준 에러 코드
    int32_t system_errno{0};       // OS 소켓 errno (없으면 0)
    uint8_t modbus_exception{0};   // Modbus Exception 코드 (없으면 0)
    std::string detail;            // 진단용 메시지 (최대 160바이트)

    GatewayError() = default;
    GatewayError(uint16_t c, std::string d = "", int32_t sys_errno = 0, uint8_t mb_exc = 0)
        : code(c), system_errno(sys_errno), modbus_exception(mb_exc), detail(std::move(d)) {}
    GatewayError(ErrorCode ec, std::string d = "", int32_t sys_errno = 0, uint8_t mb_exc = 0)
        : code(static_cast<uint16_t>(ec)), system_errno(sys_errno), modbus_exception(mb_exc), detail(std::move(d)) {}
};

template <typename T>
class Result {
public:
    Result(T val) : is_ok_(true), val_(std::move(val)), err_{} {}
    Result(GatewayError err) : is_ok_(false), val_{}, err_(std::move(err)) {}

    static Result<T> ok(T val) { return Result<T>(std::move(val)); }
    static Result<T> err(GatewayError err) { return Result<T>(std::move(err)); }

    bool is_ok() const noexcept { return is_ok_; }
    bool is_err() const noexcept { return !is_ok_; }
    explicit operator bool() const noexcept { return is_ok_; }

    const T& value() const {
        if (!is_ok_) {
            throw std::runtime_error("Result has no value: " + err_.detail);
        }
        return val_;
    }
    T& value() {
        if (!is_ok_) {
            throw std::runtime_error("Result has no value: " + err_.detail);
        }
        return val_;
    }

    const GatewayError& error() const noexcept { return err_; }

private:
    bool is_ok_{false};
    T val_{};
    GatewayError err_{};
};

template <>
class Result<void> {
public:
    Result() : is_ok_(true), err_{} {}
    Result(GatewayError err) : is_ok_(false), err_(std::move(err)) {}

    static Result<void> ok() { return Result<void>(); }
    static Result<void> err(GatewayError err) { return Result<void>(std::move(err)); }

    bool is_ok() const noexcept { return is_ok_; }
    bool is_err() const noexcept { return !is_ok_; }
    explicit operator bool() const noexcept { return is_ok_; }

    const GatewayError& error() const noexcept { return err_; }

private:
    bool is_ok_{true};
    GatewayError err_{};
};

struct RawPlcImage {
    std::array<uint16_t, HOLDING_REGISTER_COUNT> registers{}; // offset 0~5의 6개 Holding Register 값

    // 도메인 필드 헬퍼 접근자
    uint16_t heartbeat() const noexcept { return registers[HR_HEARTBEAT]; }
    uint16_t status_flags() const noexcept { return registers[HR_STATUS_FLAGS]; }
    uint16_t sensor_raw() const noexcept { return registers[HR_SENSOR_RAW]; }
    uint16_t setpoint_raw() const noexcept { return registers[HR_SETPOINT_RAW]; }
    uint16_t fault_code() const noexcept { return registers[HR_FAULT_CODE]; }
    uint16_t applied_command_counter() const noexcept { return registers[HR_APPLIED_COMMAND_COUNTER]; }

    // 비트필드 플래그 파싱
    bool run_requested() const noexcept { return (registers[HR_STATUS_FLAGS] & STATUS_BIT_RUN_REQUESTED) != 0; }
    bool ready() const noexcept { return (registers[HR_STATUS_FLAGS] & STATUS_BIT_READY) != 0; }
    bool running() const noexcept { return (registers[HR_STATUS_FLAGS] & STATUS_BIT_RUNNING) != 0; }
    bool physical_estop() const noexcept { return (registers[HR_STATUS_FLAGS] & STATUS_BIT_PHYSICAL_ESTOP) != 0; }
    bool reset_requested() const noexcept { return (registers[HR_STATUS_FLAGS] & STATUS_BIT_RESET_REQUESTED) != 0; }
    bool alarm_tripped() const noexcept { return (registers[HR_STATUS_FLAGS] & STATUS_BIT_ALARM_TRIPPED) != 0; }
};

struct PlcSample {
    Generation generation{0};
    SampleSequence sequence{0};
    RawPlcImage image{};
    MonotonicNs poll_started_ns{0};
    MonotonicNs sampled_ns{0};
    DurationNs read_rtt_ns{0};     // 단 1회 FC03 일괄 읽기의 왕복 RTT (ns)
    int64_t poll_jitter_ns{0};     // 이전 폴링 시작과의 간격 차이
    bool valid{false};             // 레지스터 값 유효 범위 충족 여부
};

struct CommandRequest {
    Command command{Command::START};
    uint16_t value{0}; // SET_SETPOINT일 때 0~1000, 그 외 0
};

struct CommandTask {
    CommandId id{0};
    StationId station_id{1};       // 대상 스테이션 ID
    CommandRequest request{};
    Generation generation{0};
    MonotonicNs accepted_ns{0};
    MonotonicNs deadline_ns{0};    // accepted_ns + command_timeout_ms * 1'000'000ULL
    CommandPhase phase{CommandPhase::EMPTY};
    std::optional<uint16_t> expected_counter{};
};

struct CommandResult {
    CommandId id{0};
    StationId station_id{1};       // 대상 스테이션 ID
    Generation generation{0};
    CommandOutcome outcome{CommandOutcome::NOT_SENT};
    GatewayError error{};
    MonotonicNs completed_ns{0};
    uint64_t confirmed_sample_sequence{0};
};

struct GatewayStatus {
    StationId station_id{1};                      // 스테이션 고유 ID
    LinkState link_state{LinkState::DISCONNECTED};
    Generation generation{0};
    bool alarm_active{true};                      // 초기값은 true (안전 인터록)
    std::optional<PlcSample> last_sample{};       // 최신 표본
    std::optional<MonotonicNs> last_progress_ns{};// 마지막 heartbeat 진행 시각
    bool data_valid{false};                       // 상위 제어기가 신뢰 가능한지 여부
    GatewayError last_error{};
    uint64_t poll_attempt_count{0};               // 누적 시도 횟수
    uint64_t io_error_count{0};                   // 누적 I/O 오류 횟수
    uint8_t consecutive_failures{0};              // 현재 연속 실패 횟수
};

struct AlarmEvent {
    StationId station_id{1};                      // 대상 스테이션 ID
    uint64_t sequence{0};                         // 1부터 단조 증가하는 알람 이벤트 번호
    bool active{true};                            // true: 알람 발생, false: 알람 해제
    AlarmCause cause{AlarmCause::NONE};
    GatewayError error{};
    Generation generation{0};
    MonotonicNs detected_ns{0};
    std::optional<MonotonicNs> last_progress_ns{};
};

// 콜백 함수 타입 정의
using ConnectCallback = std::function<void(const Result<void>&)>;
using ReadCallback    = std::function<void(const Result<std::array<uint16_t, 6>>&)>;
using WriteCallback   = std::function<void(const Result<void>&)>;
using AlarmCallback   = std::function<void(const AlarmEvent&)>;
using CommandCallback = std::function<void(const CommandResult&)>;

} // namespace ros2_modbus_gateway
