---
title: "[Phase X] 단계별 주제명"
phase: phase-x
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
  - phase-x
updated: 2026-09-20
---

# [Phase X] 단계별 주제명 (1 Hub + 4 Perspectives Hub)

> **문서 버전:** 1.2.0  
> **대상 Phase:** Phase X (Step A ~ Step B)  
> **기본 설명 깊이(Depth):** `Level 2 (Why & Trade-off 메인 서사)` + `선택적 Level 3 (Under the Hood 딥다이브)`  
> **상위 허브:** [[00_KNOWLEDGE_HUB]] | **학습 가이드:** [[개발자_역량_성장_가이드라인_및_학습_관점_가드레일]]

---

## 💡 문서 구성 원칙 (Authoring Principles)
1. **1 Phase = 1 통합 문서:** 4대 관점(A~D)을 분리하지 않고 단일 문서 내에 유기적인 인과관계로 서술하여 맥락 단절을 방지합니다.
2. **이원화된 Depth 구조 (인지적 피로 최소화):**
   * **본문 (Level 2):** 코드와 아키텍처 수준에서 "문제가 왜 발생했고 어떻게 타협하여 해결했는가(Why & Trade-off)"를 막힘없이 읽히도록 서술합니다.
   * **딥다이브 블록 (Level 3):** OS 커널(Futex, CFS), CPU 캐시(MESI), 소켓 버퍼/패킷 바이트열 등 바닥 메커니즘은 `> [!NOTE] 🔬 Level 3 Deep Dive (Under the Hood)` 콜아웃 블록으로 시각적으로 격리하여, 필요한 순간에만 선택적으로 깊이 학습할 수 있도록 합니다.

---

## 1. Overview & Key Decisions (전체 요약 및 핵심 설계 결단)

* **사용자 요청 핵심:**
* **해결하려는 핵심 물리적/시간적 과제:**
* **핵심 아키텍처 결정 (Architecture Decision Records - ADR):**
  1. *ADR X-1 (이름):* 결정 이유 및 대안과의 비교
  2. *ADR X-2 (이름):* 결정 이유 및 대안과의 비교

---

## 2. [View A: Concurrency & OS] 시스템 엔지니어의 눈 (Base: Level 2)

> **핵심 질문:** 스레드 간 경합을 어떻게 제어하고 데드락과 컨텍스트 스위칭 지터를 어떻게 방지했는가?

### 2.1 스레드 상호작용 및 임계 구역 설계 (Level 2)
* *동시성 모델, 뮤텍스 범위, 락 획득 순서 설명...*

> [!NOTE] 🔬 Level 3 Deep Dive: OS 커널 & 하드웨어 메커니즘 (Under the Hood)
> * **Linux Futex 시스템 콜 상태 전이:** 유저 공간 스핀과 커널 `sys_futex` 진입 경계...
> * **CPU 캐시라인(64B) 바운싱:** L1/L2 캐시 무효화 및 MESI 프로토콜 오버헤드 최소화 기법...

### 2.2 외계인 메서드 호출 및 데드락 방어 (Level 2)
* *Unlock-Before-Dispatch 등 콜백 호출 시 락 해제 원리...*

---

## 3. [View B: Industrial Protocol & OT] 제어 엔지니어의 눈 (Base: Level 2)

> **핵심 질문:** 산업용 필드버스(Modbus-TCP)와 PLC 스캔 루프의 물리적 한계를 어떻게 돌파했는가?

### 3.1 와이어 프레임 압축 및 단일 트랜잭션 (Level 2)
* *다중 조회 제거, Bulk Read를 통한 RTT 단축 및 정상 표본 보존...*

> [!NOTE] 🔬 Level 3 Deep Dive: 패킷 바이트 레이아웃 & 와이어 프레임 분석
> ```text
> ┌───────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
> │ Transaction ID│  Protocol ID │    Length    │    Unit ID   │Function Code │
> └───────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
> ```
> * *바이트 오프셋별 엔디언(Big Endian vs Little Endian) 변환 및 비트필드 패킹 상세...*

### 3.2 PLC 스캔 사이클 동기화 및 인터록 가드레일 (Level 2)
* *10ms 스캔 주기와 E-Stop/하드웨어 안전 인터록 소프트웨어 반영...*

---

## 4. [View C: Software Architecture & Clean Code] 소프트웨어 아키텍트의 눈 (Base: Level 2)

> **핵심 질문:** 미들웨어(ROS 2)와 도메인 코어를 어떻게 분리하여 테스트 가능성과 유지보수성을 극대화했는가?

### 4.1 의존성 역전 원칙(DIP)과 순수 C++ 코어 격리 (Level 2)
* *Zero ROS 헤더 원칙, 순수 C++ 구조체 및 도메인 인터페이스 설계...*

> [!NOTE] 🔬 Level 3 Deep Dive: ROS 2 Fast DDS 미들웨어 전송 최적화
> * *비동기 퍼블리시 스레드 격리, 공유 메모리(Shared Memory) 트랜스포트 동작 원리...*

### 4.2 TDD 설계 및 단위 테스트 더블 격리 (Level 2)
* *Google Test와 Mock 객체를 활용한 빠른 피드백 루프(0.01초 검증)...*

---

## 5. [View D: Reliability & Operations (MTTR)] 현장 운영자의 눈 (Base: Level 2)

> **핵심 질문:** 공장 라인 정지 시간을 줄이고(MTTR 단축), 오경보와 설비 트립을 어떻게 방지했는가?

### 5.1 오경보 방지 필터링 및 타임아웃 튜닝 (Level 2)
* *노이즈성 1회 패킷 드롭 무시, 연속 N회 실패 판정 알고리즘...*

### 5.2 1-shot 리셋 머신을 통한 MTTR 극소화 (Level 2)
* *가혹한 7단계 핸드셰이크 폐기, 자동 수신 복구 + 1회 서비스 호출을 통한 가동 재개...*

> [!NOTE] 🔬 Level 3 Deep Dive: 장애 전이 상태 머신 (FSM) 엄격 분석
> * *상태 전이표(State Transition Matrix) 및 예외 에지 케이스 핸들링 메커니즘...*

---

## 6. What Changed (코드 및 형상 변경 내역)

### 6.1 신규 및 수정 파일 목록
* `path/to/file1`: 역할 및 변경 사유
* `path/to/file2`: 역할 및 변경 사유

### 6.2 핵심 코드 변경 스니펫
```cpp
// 핵심 알고리즘 또는 인터페이스 코드
```

---

## 7. Hands-on Follow-up (사용자 직접 실습 가이드)

사용자가 터미널에서 직접 실행하며 이번 Phase의 산출물을 검증할 수 있는 명령어 세트:

```powershell
# 1. 빌드 및 컨테이너 상태 확인
docker compose ps

# 2. 단위 테스트 실행
docker compose exec mock_plc pytest -v
docker compose exec ros2_gateway /gateway-entrypoint.sh colcon test-result --verbose

# 3. 실시간 동작 및 인터페이스 검증
docker compose exec ros2_gateway /gateway-entrypoint.sh ros2 topic list
```

---

## 8. 다음 단계 연계 (Next Phase Roadmap)
* 다음 Phase 목표 및 해결 과제 예고...
