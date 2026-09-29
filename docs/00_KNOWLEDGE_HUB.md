---
title: "Industrial Gateway Engineering Knowledge Hub"
type: moc
tags:
  - obsidian-moc
  - ros2
  - modbus
  - industrial-gateway
  - engineering-study
updated: 2026-09-21
---

# 🌐 Industrial Gateway Engineering Knowledge Hub (MOC)

> **프로젝트:** ROS 2 Modbus-TCP Industrial Edge Gateway (양산 프로덕션 기준)  
> **아키텍처 베이스라인:** [[spec_production]] *(※ 레거시 참조: [[[LEGACY]spec_full]])*  
> **학습 가이드라인:** [[개발자_역량_성장_가이드라인_및_학습_관점_가드레일]]  
> **단계별 허브 템플릿:** [[TEMPLATE_PHASE_HUB]]

---

## 1. 지식 허브(MOC) 소개 및 학습 원칙

본 허브는 **ROS 2 Modbus Industrial Gateway** 프로젝트의 모든 엔지니어링 의사결정, 단계별 구현 과정, C++ 시스템 프로그래밍의 핵심 원리를 **옵시디언(Obsidian) 양방향 링크(`[[...]]`) 및 메타데이터 태그**를 기반으로 체계적으로 탐색할 수 있도록 구축된 중앙 색인(Map of Content)입니다.

### 💡 핵심 학습 및 문서화 원칙
1. **1 Phase = 1 통합 허브 (Single Integrated Hub):**
   * 한 Phase의 엔지니어링 문제를 다각도로 어떻게 풀었는지 인과관계(Trade-off)를 잃지 않도록 모든 4대 관점을 단일 문서에 통합합니다.
2. **이원화된 Depth 구조 (인지적 피로 최소화):**
   * **메인 서사 (Level 2):** 코드 및 아키텍처 수준에서 "문제가 왜 발생했고 어떻게 타협했는가(Why & Trade-off)"를 누구나 술술 읽히도록 전개합니다.
   * **심층 딥다이브 (Level 3):** OS 커널(Futex, CFS), CPU 메모리(MESI), 소켓 패킷 바이트열 등 바닥 메커니즘은 `> [!NOTE] 🔬 Level 3 Deep Dive (Under the Hood)` 블록으로 시각적 격리하여 선택적 집중 탐독을 지원합니다.
3. **AI 선제 제안 (Proactive Recommendation):**
   * 각 Phase 착수 시 AI가 해당 단계의 기술 승부처와 최적의 관점별 Level 구성을 먼저 제안하여, 사용자의 선택 피로를 제거합니다.

---

## 2. Phase별 학습 문서 로드맵 (Phases Roadmap)

| Phase | 단계명 및 범위 | 기본 심도 / 딥다이브 관점 | 주요 검증 산출물 및 핵심 기법 | 문서 링크 | 상태 |
|:---:|---|:---:|---|:---:|:---:|
| **Phase 1** | **기본 골격 및 가상 환경 구축**<br>(Step 1 ~ Step 3) | Level 2 기본 서사<br>+ Level 3 통신/OS 딥다이브 | • 6-Register Single Bulk Read (FC03)<br>• DIP 순수 C++ 코어 분리 (`types.hpp`)<br>• `CLOCK_MONOTONIC` 단조 시계 커널 타이머<br>• Docker Compose 격리 및 E-Stop 카운터 누수 방어 | [[ [Phase_1]_기본_골격_및_가상_환경_구축 ]] | ✅ 완료 |
| **Phase 2** | **도메인 코어 및 TDD 검증**<br>(Step 4 ~ Step 5) | Level 2 기본 서사<br>+ **Level 3 동시성 딥다이브** | • `GatewayBuffer` 락 점유 시간 `< 1µs`<br>• Linux Futex 진입 방지 & MESI 캐시라인 바운싱 극소화<br>• Unlock-Before-Dispatch 외계인 메서드 데드락 방어<br>• `SafetyMonitor` Modulo 65536 Heartbeat 정체(80ms) 감시<br>• 1-shot 리셋 머신 & 57개 단위 테스트 100% PASS | [[ [Phase_2]_도메인_코어_및_TDD_검증 ]] | ✅ 완료 |
| **Phase 3** | **비동기 통신 & ROS 노드 결합**<br>(Step 6 ~ Step 7) | Level 2 기본 서사<br>+ **Level 3 소켓/Asio/GuardCondition 딥다이브** | • Boost.Asio 단일 스레드 논블로킹 `ModbusClient`<br>• 25ms 소켓 I/O 타임아웃 & 즉각 1회 재시도<br>• `StationContext` & 1:N 격리 `GatewayRuntime`<br>• `rclcpp::GuardCondition` 결합 0ms 알람 통지<br>• 지연 응답(Deferred Response) & 67개 테스트 통과 | [[ [Phase_3]_비동기_통신_및_ROS_노드_결합 ]] | ✅ 완료 |
| **Phase 4** | **장애 주입 & 16대 시나리오 검증**<br>(Step 8 ~ Step 11) | Level 2 기본 서사<br>+ **Level 3 UDS/통계적 지터 딥다이브** | • UDS 기반 결함 주입 프록시 연동 (`/run/plc/control.sock`)<br>• 16대 양산 시나리오 자동화 검증 100% PASS<br>• 1,000회 지연/지터 실측 CSV, summary.json & 그래프 시각화 | [[ [Phase_4]_장애_주입_및_16대_시나리오_검증 ]] | ✅ 완료 |

---

## 3. 엔지니어링 4대 학습 관점 (4 Perspectives Matrix)

각 문서는 아래 4가지 관점을 교차하여 시스템을 다각도로 분석합니다:

```text
                     ┌──────────────────────────────────────────────┐
                     │          엔지니어링 4대 핵심 학습 관점       │
                     └──────────────────────┬───────────────────────┘
                                            │
         ┌──────────────────┬───────────────┴──────────────┬──────────────────┐
         ▼                  ▼                              ▼                  ▼
  [관점 A: 동시성/OS]   [관점 B: 산업통신/물리제어]   [관점 C: 아키텍처/클린코드]   [관점 D: 현장운영/MTTR]
   시스템 바닥 실력        OT 도메인 전문성           리드 엔지니어 설계력         프로덕션 생존력
```

* **[[개발자_역량_성장_가이드라인_및_학습_관점_가드레일#①-관점-a-동시성--os-커널-관점-concurrency--system-internals|관점 A (동시성 & OS 커널)]]:** C++17 멀티스레딩, Futex, 캐시라인 바운싱, 데드락 방어, POSIX 시각 모델.
* **[[개발자_역량_성장_가이드라인_및_학습_관점_가드레일#②-관점-b-산업-통신--물리-제어-관점-industrial-protocols--ot-domain|관점 B (산업 통신 & OT 도메인)]]:** Modbus-TCP 와이어 프레임, 6개 레지스터 압축, PLC 10ms 스캔 사이클, 하드웨어 E-Stop 인터록.
* **[[개발자_역량_성장_가이드라인_및_학습_관점_가드레일#③-관점-c-소프트웨어-아키텍처--디자인-패턴-관점-architecture--clean-code|관점 C (소프트웨어 아키텍처 & 클린 코드)]]:** 의존성 역전 원칙(DIP), 미들웨어 격리, ROS 2 QoS 전략, TDD 테스트 더블.
* **[[개발자_역량_성장_가이드라인_및_학습_관점_가드레일#④-관점-d-현장-운영--트러블슈팅-관점-reliability-engineering--mttr|관점 D (현장 운영 & 신뢰성/MTTR)]]:** 3회 연속 실패 필터링, 단일 명령 슬롯 BUSY 방어, 1-shot 리셋을 통한 MTTR 극소화, Fail-Fast 설정 검증.

---

## 4. 핵심 기술 베이스라인 (Technical Baselines)

1. **[`spec_production.md`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/spec_production.md):**
   * 프로젝트의 유일한 단일 진실 공급원(Single Source of Truth).
   * 5대 양산 개정안(6개 레지스터 FC03 Bulk Read, 이벤트 기반 알람, 20ms 주기/3회 실패 타임아웃, MTTR 단축 1-shot 리셋, Boost.Asio 비동기) 완벽 수록.
2. **[`[LEGACY]spec_full.md`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/%5BLEGACY%5Dspec_full.md):**
   * 프로젝트 초기 10ms Seqlock 구조를 담고 있는 보존용 원본 명세서 (수정 불가 보존 대상).
3. **[`docs/hmi/HMI_DESIGN_AND_IMPLEMENTATION_PLAN.md`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/docs/hmi/HMI_DESIGN_AND_IMPLEMENTATION_PLAN.md):**
   * ISA-101 고성능 HMI 설계, 1초 상황 인식, Linux/Windows OS 포터빌리티, i18n(`KO | EN`) 및 3계층 엔지니어링 테스트 벤치 규격.

---

## 5. 아키텍처 의사결정 기록 (Architecture Decision Records, ADR)

| 번호 | 결정 사항 및 주제 | 핵심 접근법 및 트레이드오프 | 문서 링크 | 상태 |
|:---:|---|---|:---:|:---:|
| **ADR 001** | **다중 언어 데이터 관리 및 계약 테스트** | • 오버엔지니어링(CodeGen / 런타임 핸드셰이크) 기각<br>• C++ / Python 레이어별 독립 SSOT 구축<br>• HMI의 Modbus 물리 레지스터 은닉<br>• CI 단계 20줄 초경량 계약 테스트(`test_contract.py`)로 100% 정합성 강제 | [[ADR_001_LAYERED_SSOT_AND_CONTRACT_TESTING]] | ✅ 승인 |

---

## 6. 옵시디언 Dataview 쿼리 (Obsidian Dataview Queries)

옵시디언의 `Dataview` 플러그인을 활성화하신 경우, 아래 코드 블록을 통해 태그 기반으로 실시간 문서를 자동 색인할 수 있습니다:

### Phase 문서 목록
```dataview
TABLE phase, depth, perspectives, updated
FROM "docs/phases"
SORT phase ASC
```

### 학습 가이드 및 MOC 문서 목록
```dataview
TABLE type, tags, updated
FROM "docs/learning"
SORT updated DESC
```
