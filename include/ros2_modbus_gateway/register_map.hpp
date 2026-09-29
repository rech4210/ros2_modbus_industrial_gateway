#pragma once

#include <cstdint>

namespace ros2_modbus_gateway {

// 6개 Holding Register 수 및 0-based PDU 오프셋 (Wire Address)
constexpr uint16_t HOLDING_REGISTER_COUNT      = 6;
constexpr uint16_t HR_HEARTBEAT                = 0;
constexpr uint16_t HR_STATUS_FLAGS             = 1;
constexpr uint16_t HR_SENSOR_RAW               = 2;
constexpr uint16_t HR_SETPOINT_RAW             = 3;
constexpr uint16_t HR_FAULT_CODE               = 4;
constexpr uint16_t HR_APPLIED_COMMAND_COUNTER  = 5;

// Status Flags 비트 마스크 상수 (HR Offset 1)
constexpr uint16_t STATUS_BIT_RUN_REQUESTED    = 0x0001; // Bit 0
constexpr uint16_t STATUS_BIT_READY            = 0x0002; // Bit 1
constexpr uint16_t STATUS_BIT_RUNNING          = 0x0004; // Bit 2
constexpr uint16_t STATUS_BIT_PHYSICAL_ESTOP   = 0x0008; // Bit 3
constexpr uint16_t STATUS_BIT_RESET_REQUESTED  = 0x0010; // Bit 4
constexpr uint16_t STATUS_BIT_ALARM_TRIPPED    = 0x0020; // Bit 5
constexpr uint16_t STATUS_BIT_RESERVED_MASK    = 0xFFC0; // Bits 6~15 (must be 0)

// 2개 제어 코일 수 및 0-based PDU 오프셋
constexpr uint16_t COIL_COUNT                  = 2;
constexpr uint16_t COIL_RUN_REQUESTED          = 0;
constexpr uint16_t COIL_RESET_REQUESTED        = 1;

// Modbus FC05 코일 쓰기 표준 값
constexpr uint16_t COIL_VALUE_ON               = 0xFF00;
constexpr uint16_t COIL_VALUE_OFF              = 0x0000;

// 레지스터 값 유효 범위
constexpr uint16_t SENSOR_RAW_MIN              = 0;
constexpr uint16_t SENSOR_RAW_MAX              = 1000;
constexpr uint16_t SETPOINT_RAW_MIN            = 0;
constexpr uint16_t SETPOINT_RAW_MAX            = 1000;

// PLC 결함 코드
constexpr uint16_t FAULT_CODE_NONE             = 0;
constexpr uint16_t FAULT_CODE_PROCESS          = 1;
constexpr uint16_t FAULT_CODE_ESTOP            = 2;

} // namespace ros2_modbus_gateway
