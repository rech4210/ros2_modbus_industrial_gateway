---
title: "작업자 친화적 산업용 관제 HMI 설계 및 구현 계획서 (개정판)"
type: design-plan
tags:
  - hmi
  - ui-ux
  - isa-101
  - ros2
  - pyqt6
  - bolt-new
  - cross-platform
  - i18n
  - tooltip-system
updated: 2026-10-06
---

# 🖥️ 작업자 친화적 산업용 관제 HMI 설계 및 구현 계획서 (개정판)

> **문서 버전:** 1.2.0 (2026-10-06, ISA-101 회색 화면 반영)  
> **설계 목적:** 기계를 모르는 현장 작업자도 1초 만에 정상/이상을 인지하고 안전하게 조작할 수 있는 ISA-101 기반 HMI 아키텍처, UI/UX 레이아웃, 인터랙션 툴팁, 및 다국어(한/영) 지원 체계 수립  
> **사양서 격리 원칙:** 본 문서는 사용자 요청에 따라 핵심 엔진 명세서([`spec_production.md`](file:///c:/Users/QWER/Git_indie/ros2_modbus_industrial_gateway/spec_production.md))와 철저히 분리된 독립 계획서로 관리되며, 기존 백엔드 사양 및 인터페이스를 일체 변경하지 않습니다.

---

## 0. 구현 반영 현황 (v1.2.0)

1~4장은 bolt.new 프로토타입을 출발점으로 한 **설계 초안**입니다. 실제 구현은 아래 규칙으로 정리되어 있으며, 초안과 다른 부분은 이 장이 우선합니다. 기능(버튼, 명령, 결함 주입, 다국어, 툴팁)은 초안과 같고 화면 표현만 달라졌습니다.

![ISA-101 회색 화면 HMI](../assets/hmi_bolt_preview.png)

### 0.1 적용한 ISA-101 규칙

| 규칙 | 구현 |
|---|---|
| 정상 상태는 무채색 | 중간 회색 바탕(`#CDCDCD`)과 회색 패널. 운전 중·준비·대기 모두 색 없이 표시 |
| 색은 경보에만 사용 | 1순위(비상정지) 빨강 `#C80000`, 2순위(안전 경보·결함·범위 이탈) 주황 `#D97500`, 3순위 노랑 `#C9A800`(마커만 정의, 현재 발생 조건 없음). 변경 중인 설정값과 선택 상태에만 파랑 `#1F4E9C` |
| 색에만 의존하지 않음 | 경보 순위를 색·모양·번호로 함께 표시(■1 빨강 사각형, ▲2 주황 삼각형, ◆3 노랑 마름모). 설비 상태는 운전 중=채운 사각형, 준비=빈 사각형, 대기=점선 사각형 |
| 깜빡임·번짐 효과 없음 | 이전 초안의 화면 점멸, 글로우, 바운스 애니메이션 제거. 경보 시 화면 둘레에 정적 테두리만 표시 |
| 이모지·장식 아이콘 없음 | 안내 문구는 글자로만 표시. 아이콘 라이브러리(lucide) 사용 중단 |
| 정보 우선순위 | 경보 요약·조치 → 설비 상태 → 공정값(PV)·설정값(SP) → 통신 상태 → 구성 정보 → 엔지니어링 시험 패널 |

### 0.2 화면 구성

```text
┌ 제목줄(어두운 회색): 게이트웨이 · ST01 │ DDS · HB │ [Test Bench] [KO|EN] ┐
├ 경보 요약 (왼쪽 굵은 띠: 정상 회색 / 주황 / 빨강) + 조치 1·2단계 ───────┤
├ 설비 상태 ──────────────────────────┬ 조작: 기동 / 정지 / 이상 해제 ──┤
│ 공정값 PV (400~600 정상 대역 막대)   │ 구성 정보 (전송, FC, 기준값)     │
│ 설정값 SP (슬라이더·입력·프리셋)     │                                  │
│ 통신 상태 (RTT, 지터, heartbeat, 링크) │                                │
├ 엔지니어링 시험 패널 (사선 줄무늬 테두리, 제목줄 Test Bench 로 표시/숨김) ┤
└─────────────────────────────────────────────────────────────────────────┘
```

### 0.3 초안과 달라진 점

| 항목 | 초안(1~4장) | 구현 |
|---|---|---|
| 평상시 안내 | 녹색 안심 문구 | 회색 "경보 없음" |
| 비상정지 표시 | 화면 전체 테두리 점멸 | 빨간 정적 테두리 + 빨간 ■1 + 조치 단계 |
| 상태 배지 | 녹회색·청회색·적색 | 색 없이 모양과 글자로 구분, 경보 시에만 마커와 띠 |
| 리셋 버튼 | 활성 시 강조 색 | 해제할 경보가 있을 때만 주황 테두리, 비상정지 중 `LOCKED` 표시 |
| 시험 패널 격리 | 하단 서랍(Drawer) | 제목줄의 `Test Bench` 토글로 표시/숨김, 사선 줄무늬 테두리와 어두운 제목줄로 운전 화면과 구분 |
| 문구 | 이모지와 설명형 문장 | 이모지 제거, "기동 / 정지 / 이상 해제"처럼 짧은 현장 용어 |
| 서버 색상 값 | `statusColor`, `statusBadgeBg` 사용 | 응답에는 남아 있으나 화면은 사용하지 않고 상태 값으로 직접 스타일 결정 |

### 0.4 알려진 한계

- 정상 상태 안내 문구(`alarmGuideMsg`)는 백엔드가 영어로 보내므로 한국어 화면에서도 영어로 표시됩니다.
- 이 화면은 ISA-101의 원칙을 참고해 설계했으며, 표준 적합성 평가나 사용성 시험을 거치지 않았습니다.
- 3순위(노랑) 경보는 마커만 정의되어 있고 발생 조건은 아직 없습니다.

---

## 1. bolt.new 프로토타입 기반 UI/UX 종합 분석

사용자께서 bolt.new를 통해 도출하신 프로토타입 인터페이스를 바탕으로, 현장 작업자의 인지 공학적 관점에서 **[알아야 할 데이터]**, **[관리 지침]**, 그리고 **[인터랙션 & 툴팁]**을 심층 분석합니다.

```text
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
│ [HMI Information Architecture Matrix]                                                            │
├───────────────────┬──────────────────────────────────────────┬───────────────────────────────────┤
│ 구분              │ 데이터 항목                              │ 작업자 인지 목적 및 처리 방식     │
├───────────────────┼──────────────────────────────────────────┼───────────────────────────────────┤
│ 1순위: 즉각 인지  │ • 설비 상태 (RUNNING, READY, E-STOP)     │ 1초 이내 정상/비상 여부 판별      │
│ (Primary Safety)  │ • 비상정지 및 통신 단선 알람             │ 화면 테두리 표시(정적) 및 경보 배너│
├───────────────────┼──────────────────────────────────────────┼───────────────────────────────────┤
│ 2순위: 공정 제어  │ • 공정 계측값 (Process Value: 497 / 1000)│ 정상 운전 밴드(400~600) 안착 확인 │
│ (Process Control) │ • 조작 버튼 (START, STOP, RESET)         │ 2-Step 안전 롱프레스 인터록 조작  │
├───────────────────┼──────────────────────────────────────────┼───────────────────────────────────┤
│ 3순위: 통신 건전성│ • FC03 Read RTT (0.45 ms)                │ 결정론적 통신 품질 실시간 검증    │
│ (QoS Telemetry)   │ • Polling Jitter (0.03 ms)               │ (SLA 기준: RTT < 5ms, Jitter < 2ms│
│                   │ • Heartbeat 카운터 & Link 온라인 여부    │                                   │
├───────────────────┼──────────────────────────────────────────┼───────────────────────────────────┤
│ 4순위: 엔지니어링 │ • 프로토콜 (Modbus-TCP, FC03)            │ 유지보수 및 디버깅용 정적 정보    │
│ (System Info)     │ • 전송 계층 (ROS 2 Bridge, 50Hz/10Hz)    │ (평상시 작업자 시야 분산 방지)    │
└───────────────────┴──────────────────────────────────────────┴───────────────────────────────────┘
```

### 1.1 알아야 할 데이터 vs 불필요한 노이즈 데이터의 분리
* **작업자가 반드시 알아야 할 핵심 데이터:**
  1. **설비 가동 상태 배지:** 기계를 모르는 작업자에게 16진수 레지스터는 무의미합니다. 오직 `[운전 중]`, `[운전 준비]`, `[비상정지]`의 단 3가지 직관적 상태만 인지시키면 충분합니다. (구현에서는 정상 상태를 색이 아닌 모양과 글자로 구분하며, 경보일 때만 색을 씁니다. 0장 참고)
  2. **아날로그 인-레인지 바 (In-Range Analog Bar):** 현재 측정값 `497`이 정상인지 여부를 기억할 필요 없이, **초록색 정상 대역(400~600) 음영 안에 바늘이 머물고 있는지**만 1초 만에 확인하도록 유도합니다.
* **시각적 노이즈(Visual Noise) 절제:**
  * 프로토콜 이름(`Modbus-TCP`), 전송 주기(`50Hz/10Hz`) 등은 현장 작업자에게 시야를 어지럽히는 배경 잡음입니다. 우측 하단 카드에 작고 차분하게 배치하거나 접을 수 있는 서브 패널로 격리합니다.

---

### 1.2 관리해야 하는 방법 및 지침 (Actionable Operational Guidelines)
* **문제점:** 현재 프로토타입에는 "No active fault. System running normally"라는 수동적 상태 문구만 존재합니다.
* **개선 지침:** 이상 상황 발생 시 작업자에게 **"지금 즉시 무엇을 해야 하는가?"**를 단계별 행동 지침(Actionable Guide)으로 명령형 제공해야 합니다.
  * **시나리오 A: 현장 물리 비상정지(E-Stop) 감지 시:**
    * *가이드 배너:* `🚨 [긴급 조치 1단계] 현장 안전을 확보한 후, 제어반의 물리 비상정지(E-Stop) 버튼을 우측으로 돌려 복구하십시오.`
    * *인터록:* HMI의 [RESET] 버튼은 하드웨어 E-Stop이 풀릴 때까지 물리적으로 잠김(Disabled).
  * **시나리오 B: E-Stop 해제 후 통신 래치 상태:**
    * *가이드 배너:* `ℹ️ [조치 2단계] 물리 버튼이 복구되었습니다. 우측의 [RESET] 버튼을 1.5초간 길게 눌러 안전 래치를 해제하십시오.`
  * **시나리오 C: 통신 패킷 단선 (DISCONNECT):**
    * *가이드 배너:* `⚠️ [통신 점검] PLC 통신 케이블 연결 상태 및 스위칭 허브 전원을 확인하십시오.`

---

### 1.3 인터랙션 가능한 요소 및 문맥 기반 UI 툴팁 (Context-Aware Tooltip System)
모든 터치 가능 요소와 상태 카드에 마우스를 올리거나 터치 유지 시, 작동 원리와 비활성화 사유를 설명하는 **지능형 툴팁 시스템**을 도입합니다.

| UI 요소 | 상호작용 상태 | 툴팁 내용 (한글 / 영문) |
|---|---|---|
| **START 버튼** | **활성화 (Ready)** | "클릭 후 1.5초간 롱프레스하여 설비를 정상 가동 모드로 전환합니다."<br>(Hold for 1.5s to start equipment) |
| **START 버튼** | **비활성화 (Disabled)** | "설비가 이미 가동 중이거나, 비상정지 상태에서는 기동할 수 없습니다."<br>(Cannot start while running or under E-Stop) |
| **RESET 버튼** | **활성화 (Alarm)** | "통신 결함 및 소프트웨어 래치를 1회 해제하여 설비를 정상 대기 상태로 복구합니다."<br>(Clear software fault latch) |
| **RESET 버튼** | **비활성화 (Locked)** | "현장 물리 비상정지(E-Stop) 버튼이 복구되지 않아 리셋할 수 없습니다."<br>(Hardware E-Stop active. Reset locked) |
| **아날로그 공정 바** | 마우스 호버 | "공정 계측값: 497. 정상 제어 범위는 400 ~ 600입니다."<br>(Process Value: 497. Normal Operating Band: 400~600) |
| **FC03 Read RTT** | 마우스 호버 | "Modbus 패킷 왕복 시간입니다. 산업 안전 SLA 기준 한도: 5.0 ms 미만"<br>(Packet RTT. SLA Limit: < 5.0 ms) |
| **Polling Jitter** | 마우스 호버 | "20ms 주기 오차 지터입니다. 결정론적 제어 SLA 기준 한도: 2.0 ms 미만"<br>(Period jitter. SLA Limit: < 2.0 ms) |

---

## 2. UX 관점의 데이터 우선순위 및 레이아웃 최적화 점검

### 2.1 시선 흐름(Eye-Tracking)과 F-패턴에 따른 레이아웃 검증

```text
┌──────────────────────────────────────────────────────────────────────────────────────────────────┐
│ [헤더 영역] 로고 / 설비명 (좌측) ─────────────> [언어 전환: KO|EN] ──> [★ 1초 인지 상태 배지 (우측)]│
├───────────────────────────────────────────────────────┬──────────────────────────────────────────┤
│ [좌측 2컬럼: 주 관제 모니터링 영역 (Visual Monitoring)] │ [우측 1컬럼: 현장 조작 영역 (Touch Actions)]│
│                                                       │                                          │
│  1. [공정 계측 아날로그 바 (Process Variable)]         │  1. [조작 패널 (Operator Controls)]       │
│     - 0 ~ 1000 스케일                                 │     - [START (설비 기동)]                 │
│     - 400 ~ 600 정상 운전 대역 음영 시각화            │     - [STOP (설비 정지)]  <-- [보완 추가] │
│     - 현재 포인터 및 수치 표기                        │     - [RESET (이상 해제)]                │
│                                                       │                                          │
│  2. [QoS 통신 건전성 카드 (QoS Telemetry)]            │  2. [상태 요약 테이블 (State Summary)]    │
│     - FC03 Read RTT (<5ms) / Polling Jitter (<2ms)    │     - Fault Mode, Link, Analog, Heartbeat│
│     - Heartbeat 카운터 / Link 온라인 인디케이터       │                                          │
│                                                       │  3. [시스템 정보 카드 (System Info)]      │
│  3. [작업자 조치 가이드 배너 (Actionable Banner)]      │     - 프로토콜 및 아키텍처 정적 메타데이터│
│     - 평상시: 회색 "경보 없음"                          │       (양산 시 필요에 따라 접기/숨김 지원) │
│     - 결함 시: 1-2단계 조치 매뉴얼                    │                                          │
├───────────────────────────────────────────────────────┴──────────────────────────────────────────┤
│ [하단 영역: 결함 시험기 (Fault Simulator)] <-- [양산 환경에서는 '관리자 모드 토글'로 격리/숨김]  │
│  [NORMAL]  |  [DROP 3X]  |  [DISCONNECT]  |  [PHYSICAL E-STOP]                                   │
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 레이아웃 보완 및 개선 사항
1. **`STOP (설비 정지)` 버튼의 추가:**
   * bolt.new 프로토타입에는 `START`와 `RESET`만 존재합니다. 정상 가동 중 안전하게 설비를 멈출 수 있는 **`STOP` 버튼**이 조작 패널 중앙에 반드시 포함되어야 합니다.
2. **조작 패널의 우측 배치 적합성:**
   * 대다수의 작업자가 오른손잡이이며, 터치스크린 Kiosk 환경에서 오른손으로 버튼을 누르고 왼손으로 작업일지를 작성하는 현장 동선상 **우측 1열 조작 패널 배치는 매우 이상적**입니다.
3. **결함 시뮬레이터(Fault Simulator)의 양산 모드 분리:**
   * 프로토타입에서는 시뮬레이터가 상시 노출되어 테스트가 편리하지만, **실제 공장 배포 시에는 작업자가 시뮬레이터 버튼을 실수로 터치하면 안 됩니다.**
   * 따라서 양산 빌드에서는 시뮬레이터 영역을 `isSimulatorVisible` 플래그로 감추거나, 상단 관리자 톱니바퀴 아이콘을 눌렀을 때만 열리는 **하단 서랍(Drawer)** 형태로 격리하는 설계를 적용합니다.

---

## 3. 다국어(한글 / English) 취사 선택 환경 (i18n Architecture)

국내 공장 오퍼레이터(한글)와 외국인 엔지니어/글로벌 양산 라인(영문)을 모두 만족할 수 있도록, **원클릭 실시간 언어 전환 체계**를 구축합니다.

### 3.1 언어 전환 UX 설계
* 상단 헤더 우측 상단에 직관적인 `[ KO | EN ]` 토글 스위치 배치.
* 페이지 새로고침(Reload) 없이 **React State / Context 기반으로 0.01초 만에 전 화면 라벨 및 툴팁 텍스트 실시간 치환**.

### 3.2 i18n 텍스트 사전 명세 (`locales/ko.ts` & `locales/en.ts`)

```typescript
// locales/types.ts
export interface HmiLocale {
  header: {
    title: string;
    subTitle: string;
    station: string;
    engineStatus: string;
  };
  statusBadge: {
    running: string;
    ready: string;
    eStop: string;
    commFault: string;
    idle: string;
  };
  process: {
    title: string;
    normalBand: string;
    currentValue: string;
    inRange: string;
    outOfRange: string;
    scaleLow: string;
    scaleNormalStart: string;
    scaleTarget: string;
    scaleNormalEnd: string;
    scaleHigh: string;
    tooltip: string;
  };
  qos: {
    title: string;
    readRtt: string;
    pollingJitter: string;
    heartbeat: string;
    linkStatus: string;
    online: string;
    offline: string;
    rttTooltip: string;
    jitterTooltip: string;
  };
  controls: {
    title: string;
    start: string;
    stop: string;
    reset: string;
    startTooltip: string;
    stopTooltip: string;
    resetTooltip: string;
    disabledEStopTooltip: string;
  };
  guidance: {
    normal: string;
    eStopStep1: string;
    eStopStep2: string;
    commFault: string;
    readyToStart: string;
  };
  modal: {
    startTitle: string;
    startDesc: string;
    stopTitle: string;
    stopDesc: string;
    resetTitle: string;
    resetDesc: string;
    holdToConfirm: string;
    cancel: string;
  };
}
```

> 아래 사전은 **설계 초안**입니다. 실제 키와 문구는 `hmi_web_prototype/src/locales/ko.ts`, `en.ts`를 기준으로 하며, 이모지는 제거되었습니다.

```typescript
// locales/ko.ts (한국어 사전 발췌, 초안)
export const koLocale: HmiLocale = {
  header: {
    title: "ROS 2 Modbus-TCP 에지 게이트웨이",
    subTitle: "ISA-101 고성능 HMI / 산업용 공정 관제반",
    station: "스테이션 #01",
    engineStatus: "50Hz 엔진 / 10Hz UI",
  },
  statusBadge: {
    running: "설비 정상 가동 중 (RUNNING)",
    ready: "운전 준비 완료 (READY)",
    eStop: "비상 정지 (PHYSICAL E-STOP)",
    commFault: "통신 결함 (COMM FAULT)",
    idle: "대기 상태 (IDLE)",
  },
  process: {
    title: "공정 계측값 — Modbus FC03 홀딩 레지스터",
    normalBand: "정상 가동 대역 (400 - 600)",
    currentValue: "현재 측정값",
    inRange: "정상 대역 안착",
    outOfRange: "대역 이탈 주의",
    scaleLow: "0 (하한)",
    scaleNormalStart: "400 (정상 시작)",
    scaleTarget: "500 (목표치)",
    scaleNormalEnd: "600 (정상 종료)",
    scaleHigh: "1000 (상한)",
    tooltip: "실시간 계측값입니다. 음영 대역(400~600) 내에 안착해야 정상 운전입니다.",
  },
  controls: {
    title: "현장 조작 패널",
    start: "설비 기동 (START)",
    stop: "설비 정지 (STOP)",
    reset: "이상 해제 (RESET)",
    startTooltip: "설비를 가동 모드로 전환합니다. (1.5초 롱프레스 확인 필요)",
    stopTooltip: "설비 회전을 정지합니다.",
    resetTooltip: "소프트웨어 결함 래치를 해제하고 정상 상태를 복구합니다.",
    disabledEStopTooltip: "현장 물리 비상정지 버튼이 눌려 있어 조작이 차단되었습니다.",
  },
  guidance: {
    normal: "설비가 정상 운전 중입니다. 전 항목 공정 규격을 충족합니다.",
    eStopStep1: "⚠️ [긴급] 현장 제어반의 물리 비상정지(E-Stop) 버튼이 눌려 있습니다. 현장 확인 후 물리 버튼을 먼저 복구하십시오.",
    eStopStep2: "ℹ️ 물리 버튼이 복구되었습니다. [이상 해제 (RESET)] 버튼을 눌러 안전 래치를 해제하십시오.",
    commFault: "⚠️ 통신 패킷 단절이 발생했습니다. 케이블 및 스위칭 허브 상태를 점검하십시오.",
    readyToStart: "기동 준비가 완료되었습니다. [설비 기동] 버튼을 1.5초간 눌러 가동하십시오.",
  },
  modal: {
    startTitle: "설비 기동 안전 확인",
    startDesc: "설비를 기동하여 RUNNING 상태로 전이합니다. 작업 반경 내 인원 안전을 확인했습니까?",
    stopTitle: "설비 정지 확인",
    stopDesc: "운전 중인 설비를 정지하시겠습니까?",
    resetTitle: "결함 해제 (Clear Fault)",
    resetDesc: "통신 오류 및 인터록 래치를 해제합니다. 결함 원인이 완전히 조치되었습니까?",
    holdToConfirm: "1.5초간 누르면 승인됩니다",
    cancel: "취소 (닫기)",
  },
  // ... 기타 QoS 및 시스템 정보 번역
};
```

---

## 4. 프론트엔드 코드 개선 규격 (Component Architecture)

### 4.1 툴팁 컴포넌트 (`components/Tooltip.tsx`)
* 외부 의존성 없는 순수 Tailwind 기반 툴팁입니다. 현재 구현은 직각 모서리, 어두운 회색 바탕, 흰 글자이며 그림자·블러·화살표를 쓰지 않습니다. `position`(top/bottom/left/right)으로 위치를 고르고 마우스 올림·키보드 포커스에 표시됩니다.
* 소스: [`Tooltip.tsx`](../../hmi_web_prototype/src/components/Tooltip.tsx)

### 4.2 다국어 Provider (`context/I18nContext.tsx`)
```tsx
import React, { createContext, useContext, useState } from 'react';
import { koLocale } from '../locales/ko';
import { enLocale } from '../locales/en';
import { HmiLocale } from '../locales/types';

type Lang = 'KO' | 'EN';

const I18nContext = createContext<{
  lang: Lang;
  setLang: (lang: Lang) => void;
  t: HmiLocale;
}>({
  lang: 'KO',
  setLang: () => {},
  t: koLocale,
});

export const I18nProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [lang, setLang] = useState<Lang>('KO');
  const t = lang === 'KO' ? koLocale : enLocale;

  return (
    <I18nContext.Provider value={{ lang, setLang, t }}>
      {children}
    </I18nContext.Provider>
  );
};

export const useI18n = () => useContext(I18nContext);
```

---

## 5. 최종 구현 로드맵 (Action Items)

1. **Sprint 1 (완료): i18n 언어팩 및 Tooltip 시스템 통합**
   * `locales/ko.ts`, `locales/en.ts` 구축 및 상단 `[KO | EN]` 스위처 연동.
   * 조작 버튼, 아날로그 바, QoS 지표 카드에 문맥 툴팁 장착.
2. **Sprint 2 (완료): Operator Controls에 `STOP` 버튼 보완 및 2-Step 모달 연동**
   * `START`, `STOP`, `RESET`의 3-State 안전 제어 체계 확립.
   * 물리 E-Stop 시 비활성화 및 안내 툴팁 연동.
3. **Sprint 3 (완료): Fault Simulator의 양산 분리** (구현: 서랍 대신 제목줄 `Test Bench` 토글)
   * 시험 패널을 숨길 수 있게 하고, 표시 중에는 사선 줄무늬 테두리로 운전 화면과 구분.
4. **Sprint 4 (미착수): PyQt6 / C++ Qt6 포팅 가이드라인 검증**
   * 웹 프로토타입에서 검증된 UI/UX 규격을 독립 PyQt6 노드로 1:1 이식.
