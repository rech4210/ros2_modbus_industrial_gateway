---
title: "[Phase 1] 기본 골격 및 가상 환경 구축"
phase: phase-1
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
  - phase-1
updated: 2026-09-20
---

# [Phase 1] 기본 골격 및 가상 환경 구축 (1 Hub + 4 Perspectives)

> **문서 버전:** 1.2.0  
> **대상 Phase:** Phase 1 (Step 1 ~ Step 3: 패키지 골격, 인터페이스, 설정 도메인, 가상 PLC 및 컨테이너 환경)  
> **기본 설명 깊이(Depth):** `Level 2 (Why & Trade-off 메인 서사)` + `선택적 Level 3 (Under the Hood 딥다이브)`  
> **상위 허브:** [[00_KNOWLEDGE_HUB]] | **학습 가이드:** [[개발자_역량_성장_가이드라인_및_학습_관점_가드레일]]

---

## 1. Overview & Key Decisions (전체 요약 및 핵심 설계 결단)

* **사용자 요청 핵심:**
  > *"Phase 1 (Step 1 ~ 3): 환경 확인 후 기본 골격 및 가상 환경 구축  
  > ROS 패키지 생성, 메시지/서비스 4종 정의 (colcon build 확인)  
  > Python 가상 PLC (mock_plc) 6개 레지스터 모델 및 스캔 로직 구현 (pytest 확인)"*

* **해결하려는 핵심 물리적/시간적 과제:**
  1. 기존 연구용 명세(`spec_full.md`)의 10ms 주기 내 3회 트랜잭션(Seqlock)으로 인한 TCP RTT 지연 및 정상 표본 폐기 결함 해소.
  2. ROS 2 통신 미들웨어와 순수 C++ 비즈니스 로직이 강결합되어 단위 테스트가 불가능해지는 아키텍처 부채 차단.
  3. Windows 호스트의 소켓 지터 및 Python 환경 오염을 방지하고, 산업용 격리 네트워크(OT 망) 표준을 준수하는 가상 개발 환경 구축.

* **핵심 아키텍처 결정 (ADR - Architecture Decision Records):**
  1. **ADR 1-1 (Single Bulk Read):** 분산된 레지스터/코일 조회를 6개 연속 Holding Register 단 1회의 FC03 일괄 읽기(12바이트)로 압축.
  2. **ADR 1-2 (DIP Layering):** `types.hpp`, `register_map.hpp`, `config.hpp`에서 ROS 헤더(`rclcpp`)를 100% 배제하여 순수 C++ 코어로 격리.
  3. **ADR 1-3 (Monotonic Time):** 시스템 시계(`system_clock`)의 NTP 역행/점프로 인한 허위 트립(False Trip)을 차단하기 위해 `CLOCK_MONOTONIC` 나노초 단조 시계 채택.
  4. **ADR 1-4 (Production MTTR):** 가혹한 7단계 해제 절차 대신 통신 재연결 즉시 데이터 갱신 + 1-shot `ClearFault.srv`를 통한 신속한 라인 가동(MTTR 단축) 확립.

---

## 2. [View A: Concurrency & OS] 시스템 엔지니어의 눈 (Base: Level 2)

> **초점:** 단조 시각의 물리적 보장, NTP 점프 방어, Fast DDS 비동기 퍼블리시 스레드 모델, 차단 상한 설정

### 2.1 `CLOCK_MONOTONIC` 단조 시각의 필요성 (Level 2)
* **구현 파일:** [`include/ros2_modbus_gateway/monotonic_clock.hpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/include/ros2_modbus_gateway/monotonic_clock.hpp), [`src/monotonic_clock.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/monotonic_clock.cpp)
* **왜 `std::chrono::system_clock`을 쓰면 공장 라인이 멈추는가?**
  * 공장 현장의 산업용 PC(IPC)는 주기적으로 사내 NTP(Network Time Protocol) 서버와 시각을 동기화합니다.
  * 이때 시스템 시계는 1초 뒤로 점프(역행)하거나 수 초 앞으로 건너뛸 수 있습니다.
  * 만약 게이트웨이의 통신 타임아웃 감시(60ms)나 20ms 주기 루프에 시스템 시계를 사용하면, `now - last_rx` 계산 결과가 음수가 나오거나 1,000ms 이상으로 튀어 **멀쩡히 가동 중인 로봇과 설비에 허위 비상정지(False Trip)가 걸리는 대형 사고**가 발생합니다.
  * 따라서 OS 부팅 이후 오직 앞만 보고 일정하게 흐르는 나노초 단조 시계를 채택했습니다.

> [!NOTE] 🔬 Level 3 Deep Dive: POSIX clock_gettime 및 CPU TSC 하드웨어 카운터 (Under the Hood)
> * **Linux 시스템 콜 메커니즘:**  
>   `src/monotonic_clock.cpp` 내부에서는 `clock_gettime(CLOCK_MONOTONIC, &ts)`을 호출합니다. 현대 x86-64 Linux 커널은 이를 vDSO(Virtual Dynamic Shared Object)를 통해 유저 공간 메모리 매핑 영역에서 처리하므로, Ring 0 시스템 콜 트랩 비용 없이 약 15~20ns 만에 나노초 정밀도 시각을 획득합니다.
> * **CPU TSC(Time Stamp Counter)와의 연동:**  
>   Linux 커널은 CPU 불변 TSC(Invariant TSC) 하드웨어 클럭 사이클을 읽어 단조 시각을 계산하므로, 전원 관리나 코어 간 주파수 스케일링(cpufreq) 환경에서도 시간 왜곡이나 역행이 물리적으로 발생하지 않습니다.

### 2.2 Fast DDS 비동기 퍼블리시 스레드 격리 (Level 2)
* **구현 파일:** [`config/fastdds.xml`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/config/fastdds.xml)
* **스레드 차단 방지 (Non-blocking Publish):**
  * 기본 동기(SYNCHRONOUS) 모드에서는 `publish()` 호출 시 네트워크 소켓 I/O가 완료될 때까지 호출 스레드가 블로킹됩니다.
  * Fast DDS 프로파일에 `ASYNCHRONOUS_PUBLISH_MODE`를 설정하여, 20ms 제어 스레드가 DDS 큐에 표본을 넣고 즉시 복귀하도록 격리했습니다.
  * 또한 `max_blocking_time = 1ms`를 강제하여 네트워크 정체 시에도 메인 제어 루프가 스케줄러에 묶이는 현상을 1ms 이내로 차단했습니다.

---

## 3. [View B: Industrial Protocol & OT] 제어 엔지니어의 눈 (Base: Level 2)

> **초점:** Modbus-TCP 와이어 프레임, 6개 Holding Register 압축, 비트필드(Bitfield), PLC 10ms 스캔 사이클 및 인터록 무결성

### 3.1 1회 일괄 읽기(FC03 Single Bulk Read)의 필연성 (Level 2)
* **구현 파일:** [`include/ros2_modbus_gateway/register_map.hpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/include/ros2_modbus_gateway/register_map.hpp)
* **기존 연구용 명세(`spec_full.md`)의 문제점:**
  * 10ms 폴링 주기 내에 `HR_before(FC03) → Coil(FC01) → HR_after(FC03)` 3개의 독립된 TCP 트랜잭션을 전송하고, 전후 HR의 Seqlock 카운터가 일치하지 않으면 데이터를 통째로 폐기했습니다.
  * 스위칭 허브와 PLC 스캔 사이클이 얽힌 공장 현장에서 TCP 3회 왕복 시간(RTT)은 수십 ms에 달해 패킷이 누적되고, 정상 데이터를 계속 버려 제어가 불가능해지는 치명적 결함(오버엔지니어링)이었습니다.
* **해결책: 6개 레지스터 단 1회 읽기 (12바이트 PDU)**
  * 단 1회의 트랜잭션으로 설비의 전 상태를 12바이트 안에 원자적으로 포획하므로 네트워크 지터가 대폭 감소합니다.

> [!NOTE] 🔬 Level 3 Deep Dive: Modbus-TCP 와이어 프레임 및 빅엔디언 패킷 바이트 구조 (Under the Hood)
> ```text
> [ Modbus-TCP FC03 Request Frame: 12 Bytes ]
> ┌───────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
> │ Transaction ID│  Protocol ID │    Length    │    Unit ID   │Function Code │ Start & Qty  │
> │    (2 Bytes)  │0x0000(2 Bytes│0x0006(2 Bytes│  0x01(1 Byte)│  0x03(1 Byte)│0x0000, 0x0006│
> └───────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
> 
> [ Modbus-TCP FC03 Response Frame: 21 Bytes ]
> ┌───────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
> │ Transaction ID│  Protocol ID │    Length    │    Unit ID   │Function Code │  Byte Count  │ Register Data│
> │    (2 Bytes)  │0x0000(2 Bytes│0x000F(2 Bytes│  0x01(1 Byte)│  0x03(1 Byte)│ 0x0C(12 Bytes│   6 Words    │
> └───────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
> ```
> * **엔디언 변환 (Byte Swapping):**  
>   Modbus 프로토콜은 Big-Endian(네트워크 바이트 오더)으로 레지스터 데이터를 전송합니다.  
>   x86-64 호스트(Little-Endian) 게이트웨이는 수신된 12바이트 페이로드를 읽을 때 `(byte[0] << 8) | byte[1]` 형태로 16비트 워드를 조립하여 메모리 오염을 원천 차단합니다.

### 3.2 16비트 `status_flags` 비트필드(Bitfield) 설계 (Level 2)
* 6개 레지스터 중 Offset 1에 위치한 `status_flags`는 16개의 불리언 상태를 비트 단위로 패킹합니다:
  * `Bit 0 (0x0001)`: `RUN_REQUESTED` (운전 기동 요청 중)
  * `Bit 1 (0x0002)`: `READY` (설비 운전 준비 완료)
  * `Bit 2 (0x0004)`: `RUNNING` (실제 모터/액추에이터 회전 중)
  * `Bit 3 (0x0008)`: `PHYSICAL_ESTOP` (하드웨어 비상정지 버튼 눌림)
  * `Bit 4 (0x0010)`: `RESET_REQUESTED` (오류 리셋 진행 중)
  * `Bit 5 (0x0020)`: `ALARM_TRIPPED` (PLC 내부 공정 알람 활성화)
* **이점:** 개별 코일(Coil)을 따로 읽을 필요 없이 단 1개의 워드로 설비의 논리적/물리적 인터록 상태를 한 번에 검증할 수 있습니다.

### 3.3 [핵심 검증] 인터록 상태에서의 명령 카운터 누수 방어 (Level 2)
* **구현 파일:** [`mock_plc/datastore.py`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/mock_plc/datastore.py), [`tests/test_datastore.py`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/tests/test_datastore.py)
* **실제 PLC 스캔 사이클 모사:**
  1. 입력 및 쓰기 큐 처리 $\to$ 2. 안전 인터록 및 래더 로직 연산 $\to$ 3. 출력 갱신 및 Heartbeat 전진.
* **버그 사례와 해결:**
  * 물리 E-Stop이 눌린 상태에서 게이트웨이가 RESET이나 START 명령을 전송했을 때, PLC가 명령을 실제로 수행하지 않았음에도 `applied_command_counter`를 올려버리면 상위 제어기는 명령이 성공했다고 착각(거짓 성공)하는 치명적 안전 불일치가 발생합니다.
  * 따라서 `datastore.py`에서는 **"물리 비상정지와 공정 결함이 실제로 해제되어 정상 상태로 전이되었을 때만"** 카운터를 1 증가시키도록 엄격한 인터록 가드레일을 적용했습니다.

---

## 4. [View C: Software Architecture & Clean Code] 소프트웨어 아키텍트의 눈 (Base: Level 2)

> **초점:** 의존성 역전 원칙(DIP), 미들웨어 격리, ROS 2 메시지 QoS 설계, Docker 환경 격리

### 4.1 의존성 역전 원칙(DIP)과 순수 C++ 코어 분리 (Level 2)
```text
  [ ROS 2 Framework Layer ]
     - rclcpp, Node, Executors
     - msg/PlcState, srv/TriggerCommand
                 │
                 ▼ (의존성 단방향 주입)
  ┌────────────────────────────────────────────────────────┐
  │            Pure C++ Core Domain Layer                  │
  │  - types.hpp (LinkState, Command, ErrorCode, Sample)   │
  │  - register_map.hpp (PDU 주소, 비트마스크, 코일 주소)  │
  │  - config.hpp (GatewayConfig, StationConfig)           │
  │  - monotonic_clock.hpp (단조 시각 취득 함수)           │
  │                                                        │
  │  ※ 규칙: #include <rclcpp/rclcpp.hpp> 절대 금지!        │
  └────────────────────────────────────────────────────────┘
```
* **Why?:** 
  * ROS 2 미들웨어는 교체되거나 버전이 올라갈 수 있는 외부 I/O 프레임워크일 뿐입니다.
  * 도메인 코어에서 ROS 의존성을 100% 걷어냄으로써, ROS 노드를 띄우지 않고도 Google Test(GTest)를 0.01초 만에 실행할 수 있는 완벽한 테스트 가능성(Testability)을 확보했습니다.

### 4.2 ROS 2 통신 인터페이스 4종의 QoS 설계 (Level 2)
* **`msg/PlcState.msg` (Best Effort, Volatile, Depth 1):** 50Hz(20ms) 고속 계측 데이터는 네트워크가 지연될 때 과거 데이터를 버퍼링하면 안 되며, 항상 가장 최신 표본 1개만 전달되어야 합니다.
* **`msg/SafetyAlarm.msg` (Reliable, Transient Local, Depth 1):** 안전 경보는 1건이라도 유실되면 안 되며, 늦게 켜진 모니터링 노드도 마지막 알람 상태를 즉시 수신(DDS 래치 기능)해야 합니다.
* **`srv/TriggerCommand.srv` (Reliable, Volatile):** 제어 명령은 비동기로 처리되며, PLC의 실제 수락/반영 여부를 추적하기 위한 메타데이터(`command_id`, `outcome`, `elapsed_ms`)를 갖춥니다.
* **`srv/ClearFault.srv` (Reliable, Volatile):** 통신 복구 후 안전 래치를 해제하여 생산 라인을 즉시 재기동하는 전용 1-shot 서비스입니다.

### 4.3 Docker Compose 기반 격리 개발 환경 (Level 2)
* **구현 파일:** [`docker-compose.yml`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/docker-compose.yml), [`docker/Dockerfile.gateway`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/docker/Dockerfile.gateway), [`docker/Dockerfile.plc`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/docker/Dockerfile.plc)
* **Security by Design:** Modbus 포트(5020)를 호스트 머신에 노출하지 않고 컨테이너 내부 전용 브리지 네트워크(`gateway_net`)에 가두어, 호스트 포트 충돌 및 산업망 사이버 공격을 원천 차단했습니다.
* **자동화 엔트리포인트:** 컨테이너 기동 시 빌드 산출물이 없으면 자동으로 `colcon build --symlink-install`을 수행하여 작업자의 환경 설정 실수를 방지했습니다.

---

## 5. [View D: Reliability & Operations (MTTR)] 현장 운영자의 눈 (Base: Level 2)

> **초점:** 설비종합효율(OEE), 복구 시간(MTTR) 단축, 설정 경계값 검증을 통한 Fail-Fast

### 5.1 가혹한 7단계 인터록 폐기와 MTTR 단축 (Level 2)
* **기존 명세의 문제:** 통신이 1회 단절되면 통신이 재개되어도 7단계의 복잡한 핸드셰이크를 통과해야만 설비가 돌아가 현장 작업자의 극심한 불만과 OEE 저하를 초래했습니다.
* **양산형 복구 정책:**
  1. 소켓이 재연결되고 정상 Modbus 응답이 수신되면 계측 표본(`PlcState`) 발행을 즉시 재개합니다.
  2. 제어 기능은 여전히 안전 래치로 보호되지만, 상위 PLC/로봇 제어기가 단 1회의 `ClearFault` 서비스 호출만으로 제어권을 즉각 복구할 수 있어 설비 정지 시간(MTTR)을 분 단위에서 밀리초 단위로 단축시켰습니다.

### 5.2 파라미터 경계값 검증과 Fail-Fast 원칙 (Level 2)
* **구현 파일:** [`src/config.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/src/config.cpp), [`tests/test_config.cpp`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/tests/test_config.cpp)
* 잘못된 설정값(주기 0ms, 타임아웃 0ms, 비어있는 IP, 중복된 스테이션 ID)으로 게이트웨이가 기동되면 런타임에 알 수 없는 크래시를 유발합니다.
* 기동 단계에서 파라미터를 즉각 검증하고 `ErrorCode::INVALID_CONFIG`로 명확한 원인을 로그에 남긴 뒤 안전하게 프로세스를 종료(Fail-Fast)하도록 13개의 경계값 단위 테스트를 통과시켰습니다.

---

## 6. What Changed (코드 및 형상 변경 내역)

### 6.1 신규 구축 파일 목록 및 역할

```text
ros2_modbus_industrial_gateway/
├── CMakeLists.txt                    # [NEW] C++17, ament_cmake, Boost 의존성, rosidl 인터페이스 생성
├── package.xml                       # [NEW] rclcpp, rosidl, 의존성 선언
├── docker-compose.yml                # [NEW] mock_plc 및 ros2_gateway 격리 컨테이너 정의
├── docker/
│   ├── Dockerfile.gateway            # [NEW] Ubuntu 24.04 (Noble), ROS Jazzy, Boost 빌드 환경
│   ├── Dockerfile.plc                # [NEW] Python 3.11-slim, pymodbus==3.8.6 가상 PLC 환경
│   └── gateway-entrypoint.sh         # [NEW] 자동 colcon build 및 Fast DDS 환경 자동 로드
├── config/
│   ├── gateway.yaml                  # [NEW] 20ms 주기 및 1:N 스테이션 파라미터 정의
│   └── fastdds.xml                   # [NEW] 비동기 퍼블리시 및 1ms 차단 상한 설정
├── msg/
│   ├── PlcState.msg                  # [NEW] 6개 레지스터, 16비트 플래그, 지연시간 측정 메시지
│   └── SafetyAlarm.msg               # [NEW] 통신 두절 및 안전 경보 이벤트 메시지
├── srv/
│   ├── TriggerCommand.srv            # [NEW] 4대 제어 명령 비동기 처리 서비스
│   └── ClearFault.srv                # [NEW] 1-shot 안전 래치 해제 및 복구 서비스
├── include/ros2_modbus_gateway/
│   ├── types.hpp                     # [NEW] 순수 C++ 열거형(LinkState, Command, ErrorCode) 및 구조체
│   ├── register_map.hpp              # [NEW] 6개 HR PDU 주소, 비트마스크, 코일 주소 상수
│   ├── config.hpp                    # [NEW] GatewayConfig / StationConfig 도메인 모델
│   └── monotonic_clock.hpp           # [NEW] CLOCK_MONOTONIC 나노초 시각 취득 함수
├── src/
│   ├── config.cpp                    # [NEW] 파라미터 경계값 검증 구현체
│   └── monotonic_clock.cpp           # [NEW] 단조 시각 구현체
├── tests/
│   ├── test_config.cpp               # [NEW] GTest 기반 13개 설정 검증 단위 테스트
│   └── test_datastore.py             # [NEW] pytest 기반 12개 가상 PLC 스캔/인터록 단위 테스트
└── mock_plc/
    ├── requirements.txt              # [NEW] pymodbus==3.8.6, pytest
    ├── datastore.py                  # [NEW] 6개 HR + 2개 Coil 데이터스토어 및 10ms 스캔 엔진
    └── mock_plc_server.py            # [NEW] asyncio 기반 Modbus-TCP 서버 구동기
```

---

## 7. Hands-on Follow-up (사용자 직접 실습 가이드)

사용자가 호스트 터미널에서 아래 명령어들을 순서대로 실행하며 Phase 1 산출물을 직접 검증할 수 있습니다:

### 실습 1: Docker 컨테이너 상태 확인
```powershell
docker compose ps
```
* **기대 결과:** `mock_plc`와 `ros2_gateway` 두 컨테이너가 모두 `Up` 상태로 표시됩니다.

### 실습 2: 가상 PLC 12개 단위 테스트 실행 (인터록 검증)
```powershell
docker compose exec mock_plc pytest -v tests/test_datastore.py
```
* **기대 결과:** Heartbeat 전진, Modbus 소켓 통신, 물리 E-Stop 인터록 등 12개 테스트가 **100% PASS**됩니다.

### 실습 3: C++ 도메인 설정 및 시각 단위 테스트 실행 (GTest)
```powershell
docker compose exec ros2_gateway /gateway-entrypoint.sh colcon test-result --verbose
```
* **기대 결과:** `Summary: 13 tests, 0 errors, 0 failures, 0 skipped`로 모든 C++ 단위 테스트가 통과합니다.

### 실습 4: ROS 2 메시지 및 서비스 인터페이스 정의 확인
```powershell
docker compose exec ros2_gateway /gateway-entrypoint.sh ros2 interface package ros2_modbus_gateway
```
* **기대 결과:** `msg/PlcState`, `msg/SafetyAlarm`, `srv/TriggerCommand`, `srv/ClearFault` 4종 인터페이스가 표시됩니다.

---

## 8. 다음 단계 (Phase 2) 연계

Phase 1에서 완벽한 도메인 계약과 격리된 가상 환경이 마련되었으므로, **[[ [Phase_2]_도메인_코어_및_TDD_검증 ]]**에서는 다음 핵심 구현으로 진입합니다:
* **`GatewayBuffer`**: 락 점유 시간 `< 1µs`의 원자적 스냅샷 덮어쓰기 및 외계인 메서드 호출 데드락 방어.
* **`SafetyMonitor`**: 연속 3회 실패(~60ms) 누적 판정, Heartbeat 80ms 정체 감시 및 1-shot 리셋 머신.
* **순수 C++ 동시성 TDD**: 멀티스레드 경합 하에서의 무결성 검증.
