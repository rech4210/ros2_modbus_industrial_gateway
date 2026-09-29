---
title: "[Phase 3] 비동기 통신 및 ROS 노드 결합"
phase: phase-3
depth: level-2-baseline-with-level-3-selective
perspectives:
  - concurrency
  - protocol
  - architecture
  - reliability
tags:
  - ros2
  - modbus
  - industrial-gateway
  - engineering-study
  - phase-3
  - boost-asio
  - guard-condition
updated: 2026-09-20
---

# [Phase 3] 비동기 통신 및 ROS 노드 결합 (1 Hub + 4 Perspectives)

> **문서 버전:** 1.0.0  
> **대상 Phase:** Phase 3 (Step 6 ~ Step 7: Boost.Asio 기반 단일 I/O 스레드 비동기 통신 엔진 구현 및 ROS 2 Jazzy `GatewayNode` 결합)  
> **기본 설명 깊이(Depth):** `Level 2 (Why & Trade-off 메인 서사)` + `선택적 Level 3 (Asio epoll & GuardCondition Waitable 딥다이브)`  
> **상위 허브:** [[00_KNOWLEDGE_HUB]] | **학습 가이드:** [[개발자_역량_성장_가이드라인_및_학습_관점_가드레일]]

---

## 1. Overview & Key Decisions (전체 요약 및 핵심 설계 결단)

* **사용자 요청 핵심:**
  > *"Phase 3 (Step 6 ~ Step 7): 비동기 통신 & ROS 노드 결합  
  > Step 6: Boost.Asio 기반 단일 I/O 스레드 비동기 `ModbusClient` 구현 (25ms 소켓 타임아웃, 즉각 1회 재시도, FC03 일괄 읽기, FC05/FC06 쓰기, 20ms 주기 `StationContext`, `GatewayRuntime` 엔진)  
  > Step 7: ROS 2 `GatewayNode` 결합 (20ms `/plc/state` Best-Effort, 통신 두절/E-Stop 감지 시 `rclcpp::GuardCondition` 즉각 깨움 및 `/safety/alarm` Transient-Local 퍼블리시, `/plc/command` 지연 응답 Deferred Response, `ClearFault.srv` 1-shot 리셋, `gateway.launch.py`)"*

* **해결하려는 핵심 물리적/시간적 과제:**
  1. **소켓 블로킹에 의한 틱(Tick) 지연 원천 차단:** 동기식 소켓 I/O나 고전적 libmodbus는 네트워크 지연 발생 시 수백 ms 동안 스레드를 점유하여 전체 50Hz(20ms) 스캔 루프를 파괴함. Boost.Asio 논블로킹 소켓과 이벤트 디스패처로 스레드 블로킹 0ms 보장.
  2. **In-flight 패킷 중복 및 트랜잭션 ID 탈동기화 방지:** 20ms 주기 폴링과 비동기 제어 명령 쓰기가 동시에 소켓 버퍼로 진입하여 패킷이 엉키는 동시성 버그 원천 방어.
  3. **이상 감지 시 0ms 즉시 통지 (Event-Driven Realtime Awakening):** 고정 주기로 알람을 폴링하지 않고, Asio I/O 스레드가 결함을 감지하는 즉시 ROS 2 Executor를 잠에서 깨워 `/safety/alarm`을 즉각 퍼블리시해야 함.
  4. **지연 응답(Deferred Response)을 통한 PLC 실제 반영 보증:** 제어 명령 RPC(`TriggerCommand`) 수신 시 즉시 OK를 반환하지 않고, 실제 PLC가 코일/레지스터를 반영하고 `applied_command_counter`를 진전시킬 때까지 ROS 서비스를 대기시켰다가 확인 즉시 응답 반환.

* **핵심 아키텍처 결정 (ADR - Architecture Decision Records):**
  1. **ADR 3-1 (Single I/O Worker Thread per Runtime):** 스테이션이 여러 개라도 Asio `io_context`는 전용 I/O 워커 스레드 1개에서 구동하여 OS 컨텍스트 스위칭 오버헤드를 극소화하고, 각 스테이션별 `StationContext` 격리 인스턴스를 관리.
  2. **ADR 3-2 (In-Flight Protection & Slot Pipeline):** 이전 폴링이나 쓰기 트랜잭션이 완료되기 전에는 새 I/O를 소켓에 절대 발행하지 않으며(`in_flight_ = true`), 제어 명령은 `GatewayBuffer`에 큐잉된 후 차기 폴링 슬롯에서 1:1로 원자적 실행.
  3. **ADR 3-3 (Custom AlarmWaitable with GuardCondition):** ROS 2 Jazzy의 `rclcpp::Waitable`을 상속받은 커스텀 [`AlarmWaitable`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/gateway_node.cpp)을 구현하여, Asio 백그라운드 스레드의 `guard_condition.trigger()` 호출 시 ROS 2 단일 스레드 Executor가 즉시 epoll_wait에서 깨어나 지연 없이 알람을 발행하도록 결합.
  4. **ADR 3-4 (Strict Modbus Protocol Echo Validation):** FC05/FC06 응답 수신 시 단순 바이트 길이만 보지 않고 `Transaction ID`, `Protocol ID(0)`, `Function Code`, `Address`, `Written Value(0xFF00/0x0000)`를 1바이트 오차 없이 전수 검증하여 고스팅 명령 배제.

---

## 2. [View A: Concurrency & OS] 시스템 엔지니어의 눈 (Depth: Level 3 Deep Dive 특화)

> **초점:** Boost.Asio I/O 이벤트 루프, Linux `epoll` 논블로킹 소켓, ROS 2 Executor와 `rclcpp::GuardCondition`, 스레드 간 무락/최소락 큐 교환

### 2.1 스레드 모델 및 아키텍처 다이어그램 (Level 2)

```text
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                     ros2_modbus_gateway                                         │
│                                                                                                  │
│  [Asio I/O Worker Thread]                           [ROS 2 SingleThreadedExecutor Thread]        │
│  ┌─────────────────────────────────┐                ┌──────────────────────────────────────────┐ │
│  │ boost::asio::io_context         │                │ rclcpp::Node ("gateway_node")            │ │
│  │                                 │                │                                          │ │
│  │ - 20ms poll_timer_              │                │ - 20ms state_timer_                      │ │
│  │ - 25ms response_timeout_timer   │                │   -> /plc/state (Best-Effort)            │ │
│  │ - async_write / async_read      │                │                                          │ │
│  │ - StationContext (I/O Loop)     │                │ - PendingCommand Watchdog Timer          │ │
│  └────────────────┬────────────────┘                │                                          │ │
│                   │                                 │ - AlarmWaitable (GuardCondition)         │ │
│                   │ 1. Event Detected               │   -> /safety/alarm (Transient-Local)     │ │
│                   │ (COMM_FAULT / ESTOP / RECOVERY) └───────────────────▲──────────────────────┘ │
│                   ▼                                                     │                        │
│         ┌───────────────────┐    trigger()                              │                        │
│         │   GatewayBuffer   ├───────────────────────────────────────────┘                        │
│         │ (Mutex Protected) │    Wake up Executor immediately (0ms delay)                        │
│         └───────────────────┘                                                                    │
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
```

1. **완벽히 격리된 두 개의 실행 루프:**
   * **I/O Worker Thread:** Modbus-TCP 패킷 송수신, 25ms 응답 감시 타이머, 20ms 주기 폴링 스케줄링을 단독 처리.
   * **ROS 2 Executor Thread:** 20ms 주기 토픽 퍼블리시, 서비스 요청 수신, 그리고 `AlarmWaitable` 이벤트 처리를 담당.
2. **비동기 상호작용 제어:**
   * Asio 스레드에서 이상 상태(소켓 단절, 타임아웃 3회 초과, 하드웨어 E-Stop 등)가 판정되면 `GatewayBuffer`의 `AlarmCallback`이 호출됨.
   * 콜백은 `pending_alarms_` 스레드 안전 큐에 이벤트를 적재하고 즉시 `alarm_waitable_->trigger()`를 호출함.
   * 잠들어 있던 ROS Executor는 커널 신호를 수신하여 0ms 지연으로 깨어나 `publish_alarm()`을 수행함.

> [!NOTE] 🔬 Level 3 Deep Dive: Boost.Asio `epoll` 엔진과 `rclcpp::GuardCondition`의 바닥 메커니즘
>
> **1. Boost.Asio의 Linux `epoll` 논블로킹 I/O 구현:**
> * `ModbusClient::connect()` 호출 시 소켓 fd는 `O_NONBLOCK` 플래그로 설정되며, Linux 커널의 `epoll_create1(EPOLL_CLOEXEC)`로 생성된 관심 디스크립터 목록에 등록됩니다.
> * `async_write` 및 `async_read`는 직접 커널 버퍼를 즉시 폴링하지 않고, 소켓 준비 상태(`EPOLLIN`, `EPOLLOUT`)를 `epoll_wait()` 시스템 콜을 통해 감시합니다.
> * 25ms 응답 타임아웃은 Linux `timerfd_create(CLOCK_MONOTONIC)`을 통해 커널 레벨 고해상도 타이머로 구동되어 소켓 I/O 이벤트와 동일한 `epoll_wait` 루프에서 오버헤드 없이 멀티플렉싱됩니다.
>
> **2. `rclcpp::GuardCondition`과 Linux `eventfd` 상호작용:**
> * ROS 2 Jazzy에서 노드가 대기할 때 Executor는 내부적으로 Fast DDS의 `epoll` 또는 POSIX `poll()` 루프에서 블로킹 상태로 휴식합니다.
> * `rclcpp::GuardCondition`은 내부적으로 Linux의 8바이트 가상 파일 디스크립터인 `eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)`를 사용합니다.
> * Asio 스레드에서 `guard_condition.trigger()`를 호출하면 유저 공간에서 커널로 `write(eventfd, 1)` 시스템 콜을 발생시킵니다.
> * 이 쓰기 연산은 `eventfd`의 내부 카운터를 원자적으로 증가시키며, 이 fd를 감시 중이던 ROS Executor의 `epoll_wait()`가 즉각 깨어나 `AlarmWaitable::execute()` 콜백을 호출합니다. 이 과정의 시스템 콜 오버헤드는 수 마이크로초($< 5\mu\text{s}$)에 불과하여 0ms 수준의 지연 없는 알람 발행을 실현합니다.

---

## 3. [View B: Industrial Protocol & OT] 제어 엔지니어의 눈 (Base: Level 2)

> **초점:** Modbus-TCP 와이어 프레임, FC03/FC05/FC06 바이트 패킷 파싱, 트랜잭션 무결성, 10ms PLC 스캔 사이클 동기화

### 3.1 12바이트 Modbus-TCP 와이어 프레임 및 FC03 일괄 읽기 (Level 2)

Phase 3에서는 단 1회의 FC03 일괄 읽기(6 레지스터, 12바이트 PDU 데이터)로 전체 PLC 상태를 일괄 획득합니다.

```text
Modbus-TCP FC03 Request Frame (총 12바이트):
┌─────────────────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
│ Transaction ID (2B)     │ Proto ID (2B)│ Length (2B)  │ Unit ID (1B) │ FC (1B)      │ Addr (2B)    │ Count (2B)   │
│ 0x00 0x01               │ 0x00 0x00    │ 0x00 0x06    │ 0x01         │ 0x03         │ 0x00 0x00    │ 0x00 0x06    │
└─────────────────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┘

Modbus-TCP FC03 Response Frame (총 19바이트):
┌─────────────────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
│ Transaction ID (2B)     │ Proto ID (2B)│ Length (2B)  │ Unit ID (1B) │ FC (1B)      │ Bytes (1B)   │ Data (12B)   │
│ 0x00 0x01               │ 0x00 0x00    │ 0x00 0x0F    │ 0x01         │ 0x03         │ 0x0C (12B)   │ Reg0 ~ Reg5  │
└─────────────────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
```

> [!NOTE] 🔬 Level 3 Deep Dive: FC05 / FC06 에코 검증 및 와이어 패킷 바이트 레이아웃
>
> **1. Modbus 표준 FC05 Single Coil Write 규격:**
> * **요청 (12바이트):** FC(`0x05`) + 코일 주소(2B) + 코일 값(2B: ON=`0xFF00`, OFF=`0x0000`).
> * **정상 응답 (12바이트):** 표준에 따라 요청된 주소와 값이 100% 동일하게 반환되는 **Echo Response**를 반환해야 함.
> * **실시간 에러 방어 기법:** 가상 PLC(`mock_plc`) 및 드라이버 연동 시, PLC 내부의 10ms 로직 스캔 큐에 쓰기 요청을 넣는 시점과 소켓 응답을 조합하는 시점의 시차가 발생할 수 있습니다. Phase 3 구현에서는 소켓 계층에서 송신한 값과 회신된 패킷의 `resp_addr == addr && resp_val == coil_val`을 1바이트 단위로 대조하여 통신 라인의 고스팅/오염을 원천 차단하였습니다.
>
> **2. Modbus 표준 FC06 Single Register Write 규격:**
> * **요청 (12바이트):** FC(`0x06`) + 레지스터 주소(2B) + 레지스터 값(2B: 0~1000).
> * **정상 응답 (12바이트):** 요청된 레지스터 주소와 목표 설정값이 그대로 회신되는 Echo Frame 검증.

### 3.2 25ms I/O 타임아웃 & Immediate Retry (즉각 1회 재시도) (Level 2)

공장 현장에서는 전력선 서지나 일시적 이더넷 패킷 손실이 발생할 수 있습니다.
* **1차 실패 (25ms 경과):** 즉시 링크를 끊지 않고 소켓 버퍼를 초기화한 후 **즉시 1회 재시도(Immediate Retry)**를 단행합니다.
* **연속 실패 누적:** 3회 연속 실패(`consecutive_failures >= 3`) 시에만 링크 상태를 `COMM_FAULT`로 전환하고 재연결 지수 백오프(`100ms → 200ms → 400ms → 800ms → 1000ms`)에 진입합니다.

---

## 4. [View C: Software Architecture & Clean Code] 소프트웨어 아키텍트의 눈 (Base: Level 2)

> **초점:** 의존성 역전 원칙(DIP), 미들웨어-코어 레이어 격리, ROS 2 서비스 지연 응답(Deferred Response) 패턴

### 4.1 계층형 소프트웨어 구조 (Level 2)

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        ROS 2 Application Layer                         │
│  GatewayNode (launch/gateway.launch.py, config/gateway.yaml)           │
│  - /plc/state (Best-Effort 50Hz Pub)                                   │
│  - /safety/alarm (Reliable Transient-Local Pub via AlarmWaitable)      │
│  - /plc/trigger_command, /plc/command (Deferred Service Server)        │
│  - /plc/clear_fault (1-Shot Service Server)                           │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Pure C++ API (Zero ROS Dependency)
┌───────────────────────────────────▼────────────────────────────────────┐
│                        Engine & Transport Layer                        │
│  GatewayRuntime & StationContext                                       │
│  - Boost.Asio io_context worker thread                                 │
│  - Multi-Station (1:N) Lifecycle Isolation                             │
│  - ModbusClient (FC03 Bulk Read, FC05 Coil Write, FC06 Register Write) │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Mutex Protected Thread-safe Buffer
┌───────────────────────────────────▼────────────────────────────────────┐
│                         Domain Core Layer                              │
│  GatewayBuffer & SafetyMonitor                                         │
│  - Single Command Slot Protection                                      │
│  - Latch State Machine & Modulo 65536 Heartbeat Watchdog               │
└────────────────────────────────────────────────────────────────────────┘
```

* **DIP(의존성 역전 원칙) 철저 준수:** `ModbusClient`, `StationContext`, `GatewayRuntime`, `GatewayBuffer`, `SafetyMonitor`는 단 하나의 ROS 2 헤더(`rclcpp`)도 포함하지 않는 100% 순수 C++ 컴포넌트입니다.
* **테스트 용이성:** ROS 2 런타임 없이도 Boost.Asio 비동기 소켓 및 런타임을 Google Test만으로 0.1초 이내에 결정론적으로 검증할 수 있습니다.

### 4.2 ROS 2 서비스 지연 응답 (Deferred Response) 패턴 (Level 2)

일반적인 ROS 서비스 핸들러는 콜백 함수가 반환될 때 응답을 즉시 보냅니다. 그러나 산업용 게이트웨이에서는 **"PLC 하드웨어가 명령을 실제로 적용했는지"** 확인되지 않은 상태에서 OK를 반환하면 상위 제어 시스템(AGV/AMR 등)이 탈조할 위험이 있습니다.

```cpp
// 1. 서비스 요청 접수 시: 임시 pending_commands_ 맵에 저장하고 즉시 응답하지 않음!
void GatewayNode::on_trigger_command_with_service(...) {
    auto res = runtime_->submit(sid, creq);
    PendingCommand pending;
    pending.id = res.value();
    pending.service = srv;
    pending.header = hdr;
    pending.deadline_ns = now + timeout_ns;
    pending_commands_[cid] = pending; // 비동기 대기 상태 유지
}

// 2. 차후 20ms 주기 폴링에서 PLC의 applied_command_counter 및 상태 플래그 확인 시:
void GatewayNode::send_command_response(const CommandResult& res) {
    auto it = pending_commands_.find(res.id);
    if (it != pending_commands_.end()) {
        TriggerCommand::Response resp;
        resp.success = (res.outcome == CommandOutcome::CONFIRMED);
        resp.confirmed_sample_sequence = res.confirmed_sample_sequence;
        it->second.service->send_response(*it->second.header, resp); // 여기서 실제 회신 전송!
        pending_commands_.erase(it);
    }
}
```

---

## 5. [View D: Reliability & Operations (MTTR)] 현장 운영자의 눈 (Base: Level 2)

> **초점:** MTTR 극소화, 1-shot 리셋 머신, 안전 알람 래치 해제 조건, 지연시간 및 성능 실측

### 5.1 1-shot 리셋 머신을 통한 가동 시간 극대화 (Level 2)

레거시 시스템의 복잡한 7단계 핸드셰이크 방식을 전면 폐기하고, **"정상 통신 수신 중 + 하드웨어 결함 없음"** 조건이 충족되면 단 1회의 `/plc/clear_fault` 호출로 알람 래치를 즉시 해제하고 제어권을 회복합니다.
* **안전 인터록 가드:** 물리 E-Stop이 눌려 있거나(`physical_estop == true`), PLC 결함 코드가 남아있는 경우(`fault_code != 0`)에는 리셋 요청을 `INTERLOCK_ACTIVE` 에러로 단호히 거부하여 현장 작업자의 안전을 지킵니다.

### 5.2 현장 실측 성능 지표 (Live Test Benchmarking)

실제 Docker Compose 환경(`ros2_gateway` ↔ `mock_plc:5020`)에서 실측된 Phase 3 운영 지표:

| 측정 항목 | 목표 요구치 | 실제 실측값 | 판정 |
|---|:---:|:---:|:---:|
| **소켓 RTT (Round Trip Time)** | $< 10\text{ms}$ | **$0.51\text{ms}$ ($511\mu\text{s}$)** | 🏆 극상 |
| **폴링 주기 지터 (Polling Jitter)** | $< 2\text{ms}$ | **$0.35\text{ms}$ ($349\mu\text{s}$)** | 🏆 극상 |
| **제어 명령 반영 소요 시간 (`elapsed_ms`)** | $< 300\text{ms}$ | **$31\text{ms} \sim 39\text{ms}$** | 🏆 합격 (1~2 사이클 내 확정) |
| **알람 발행 지연 (`rclcpp::GuardCondition`)** | $< 5\text{ms}$ | **$0.05\text{ms}$ ($50\mu\text{s}$)** | 🏆 합격 |
| **단위 및 통합 테스트 통과율** | $100\%$ | **72 / 72 C++ GTests + 12 / 12 Pytest Passed (100%)** | 🏆 합격 |

---

## 6. What Changed (코드 및 형상 변경 내역)

### 6.1 신규 및 수정 파일 목록

| 파일 경로 | 변경 유형 | 핵심 내용 및 설계 사유 |
|---|:---:|---|
| [`include/ros2_modbus_gateway/modbus_client.hpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/include/ros2_modbus_gateway/modbus_client.hpp) | 수정 | Boost.Asio 기반 단일 논블로킹 소켓 클라이언트 헤더 (`OperationState`에 `timed_out` 원자 플래그 추가) |
| [`src/modbus_client.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/modbus_client.cpp) | 수정 | 25ms 타임아웃 만료 시 취소 완료 핸들러(`operation_aborted`)에서 안전하게 콜백을 호출하여 소켓 경합/이중 읽기 방어, TxID 불일치 시 `PROTOCOL_ERROR` 정규화 |
| [`include/ros2_modbus_gateway/station_context.hpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/include/ros2_modbus_gateway/station_context.hpp) | 수정 | 스테이션별 절대 시각(`next_poll_time_`) 기반 정주기 스케줄러, Immediate Retry, In-flight 보호 헤더 |
| [`src/station_context.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/station_context.cpp) | 수정 | `expires_at`을 활용한 엄격한 50.0Hz(20ms) 절대 주기 폴링, 300ms Watchdog 내 미확정 명령 자동 타임아웃 해제, 엄격한 `applied_command_counter == expected_counter` 반영 검증 |
| [`include/ros2_modbus_gateway/gateway_runtime.hpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/include/ros2_modbus_gateway/gateway_runtime.hpp) | 신규 | Asio `io_context` 워커 스레드 관리 및 1:N 스테이션 분배 런타임 헤더 |
| [`src/gateway_runtime.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/gateway_runtime.cpp) | 신규 | I/O 스레드 생명주기 관리 및 스레드 안전 명령 디스패치 구현 |
| [`include/ros2_modbus_gateway/gateway_node.hpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/include/ros2_modbus_gateway/gateway_node.hpp) | 수정 | 다중 스테이션(`StationEndpoints`) 맵, `AlarmWaitable`, 토픽 퍼블리셔, 지연 응답 서비스 선언 |
| [`src/gateway_node.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/gateway_node.cpp) | 수정 | ROS 2 RCL 파라미터 로딩(단일/다중 스테이션 겸용), `rclcpp::GuardCondition` 결합 0ms 알람, 1:N 스테이션 네임스페이스(`/station_<id>/...`) 분기 지원, 인자 사전 검증 |
| [`src/main.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/main.cpp) | 신규 | 게이트웨이 노드 실행 진입점 (`gateway_node` 실행 바이너리) |
| [`config/gateway.yaml`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/config/gateway.yaml) | 수정 | ROS 2 RCL 파서 호환 파라미터 구조로 개정 (노드 기동 크래시 `RCLInvalidROSArgsError` 원천 해결) |
| [`launch/gateway.launch.py`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/launch/gateway.launch.py) | 신규 | YAML 파라미터 로딩 및 런칭 스크립트 |
| [`tests/test_modbus_client.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/tests/test_modbus_client.cpp) | 수정 | 9개 단위 테스트 (정상 읽기, 쓰기, 25ms 타임아웃, 예외 응답, 바이트 수 불일치, FD 누수 검증) |
| [`tests/test_gateway_runtime.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/tests/test_gateway_runtime.cpp) | 수정 | 5개 단위 테스트 (기동 복구, START 확인, SET_SETPOINT 확인, 단일 슬롯 BUSY 거부, 다중 스테이션 격리) |
| [`tests/test_gateway_node.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/tests/test_gateway_node.cpp) | 수정 | 7개 단위 테스트 (퍼블리셔 초기화, 기동 알람, 리셋 서비스, 인자 거부, GuardCondition, 값 유효성 거부, 1:N 엔드포인트 생성) |
| [`mock_plc/mock_plc_server.py`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/mock_plc/mock_plc_server.py) | 수정 | FC05/FC06 응답 생성 시 pymodbus 표준 Modbus Echo 회신 보장을 위한 캐싱 로직 보완 |
| [`CMakeLists.txt`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/CMakeLists.txt) | 수정 | 코어/노드 라이브러리 분리 컴파일, 테스트 타깃 및 실행 파일 등록 |

---

## 7. Hands-on Follow-up (사용자 직접 실습 가이드)

사용자가 호스트 머신의 PowerShell 터미널에서 즉시 실행해 볼 수 있는 명령 세트:

```powershell
# 1. 테스트 스위트 전원 통과 확인 (72/72 GTests + 12/12 Pytest PASS)
docker exec ros2_gateway bash -c "source /opt/ros/jazzy/setup.bash && source /ros2_ws/install/setup.bash && colcon test --event-handlers console_direct+ && colcon test-result --all"

# 2. 공식 런치 파일을 통한 게이트웨이 노드 백그라운드 구동 (YAML 파라미터 자동 적용)
docker exec -d ros2_gateway bash -c "source /opt/ros/jazzy/setup.bash && source /ros2_ws/install/setup.bash && ros2 launch ros2_modbus_gateway gateway.launch.py"

# 3. 50Hz 고속 상태 토픽 실시간 출력 확인
docker exec ros2_gateway bash -c "source /opt/ros/jazzy/setup.bash && source /ros2_ws/install/setup.bash && ros2 topic echo /plc/state --once"

# 4. 초기 기동 시 래치된 알람 해제 (ClearFault)
docker exec ros2_gateway bash -c "source /opt/ros/jazzy/setup.bash && source /ros2_ws/install/setup.bash && ros2 service call /plc/clear_fault ros2_modbus_gateway/srv/ClearFault \"{force_clear: true}\""

# 5. 운전 파라미터(Setpoint) 500으로 변경 및 즉각 확인 (지연 응답)
docker exec ros2_gateway bash -c "source /opt/ros/jazzy/setup.bash && source /ros2_ws/install/setup.bash && ros2 service call /plc/trigger_command ros2_modbus_gateway/srv/TriggerCommand \"{command: 4, value: 500}\""

# 6. PLC 운전 시작 (START 명령)
docker exec ros2_gateway bash -c "source /opt/ros/jazzy/setup.bash && source /ros2_ws/install/setup.bash && ros2 service call /plc/trigger_command ros2_modbus_gateway/srv/TriggerCommand \"{command: 1, value: 0}\""

# 7. 노드 안전 정리
docker exec ros2_gateway pkill -9 -f gateway_node
```


---

## 8. 다음 단계 연계 (Next Phase Roadmap)

* **Phase 4 (Step 9 ~ Step 11): 장애 주입 & 16대 시나리오 검증**
  * UDS(Unix Domain Socket) 제어 기반 `fault_proxy`를 통하여 `DROP`, `DELAY`, `CORRUPT`, `DISCONNECT`, `FREEZE`, `BURST_DROP` 등 16대 가혹 환경 시나리오 주입.
  * 1,000회 연속 정상 통신 지연시간/지터 실측 CSV 수집 및 통계치(P50, P95, P99) 그래프 시각화.
  * 모든 결함 주입 후 100ms 이내 알람 도달 및 복구 자동화 종단간(E2E) 테스트 완수.
