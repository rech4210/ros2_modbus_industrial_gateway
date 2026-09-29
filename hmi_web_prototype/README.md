# 🚀 ROS 2 Industrial Gateway HMI Prototype (for bolt.new)

이 폴더는 **[bolt.new](https://bolt.new)**에 그대로 복사하여 브라우저에서 즉시 인터랙티브하게 체험할 수 있도록 제작된 **ISA-101 기반 고성능 산업용 HMI 프로토타입**입니다.

---

## 🛠️ bolt.new에서 사용하는 2가지 방법

### 방법 A: bolt.new 프롬프트 창에 바로 붙여넣기 (가장 빠름)
bolt.new 메인 화면의 채팅창에 아래 문구를 그대로 복사해서 붙여넣으세요:

```markdown
Create an ISA-101 High-Performance Industrial HMI for a ROS 2 Modbus-TCP Edge Gateway with the following specifications:
1. Dark Slate ISA-101 Theme:
   - Background #1E1F22, Card panels #2B2D30, borders #3E4247, text #CFD8DC.
   - Muted Palette: Silence means normal. Color is strictly reserved for abnormalities.
   - Full-window flashing red border during active alarms/E-Stop.
2. 1-Second Situational Awareness:
   - Station Status Badge: "[설비 정상 가동 중 (RUNNING)]" (muted green), "[운전 준비 완료 (READY)]" (slate blue), "[비상 정지 (PHYSICAL E-STOP)]" (red).
   - In-Range Analog Bar: 0 to 1000 scale with a clearly shaded "Normal Operating Band (400 to 600)" and an animated pointer indicating current value (e.g. 520, 52.0%).
   - QoS Telemetry: Modbus FC03 Read RTT (0.42 ms), Polling Jitter (0.01 ms), Heartbeat counter.
3. 2-Step Safety Interlock:
   - START and RESET buttons trigger a safety confirmation modal.
   - Modal requires a 1.5-second Long-Press (filling progress bar) to execute actions.
4. Interactive Fault Simulator Panel:
   - Buttons to toggle [NORMAL], [DROP 3x], [DISCONNECT], [PHYSICAL E-STOP].
   - When E-Stop is active, lock the Clear Fault button and display guidance: "Please physically reset the hardware E-Stop first".
   - When E-Stop is cleared, allow Reset to restore normal running.
5. Simulated 50Hz Background Engine:
   - Internal state updates at 50Hz (every 20ms) and UI rendering is throttled/decimated to 10Hz (every 100ms).
```

---

### 방법 B: 코드 파일 직접 복사
`hmi_web_prototype/` 안의 파일들을 bolt.new 프로젝트 트리에 그대로 붙여넣으시면 됩니다:
* `package.json`
* `index.html`
* `src/types.ts`
* `src/App.tsx`
* `src/main.tsx`

---

## 🎮 구현된 인터랙티브 체험 기능

1. **1초 상황 인지 (1-Second Awareness):**
   * 상단 상태 캡슐 배지만 보고도 설비의 정상/대기/비상 여부를 1초 만에 파악.
   * 아날로그 바에서 바늘이 음영 밴드(400~600) 안에 안착해 있는 모습을 시각적으로 즉시 확인.
2. **QoS 결정론적 품질 실시간 계측:**
   * 게이트웨이의 Modbus FC03 RTT(0.42ms)와 주기 지터(0.01ms) 표시.
3. **2-Step 안전 인터록 (롱프레스 확인):**
   * `[설비 기동 (START)]` 또는 `[이상 해제 (Clear Fault)]` 클릭 시 팝업이 뜨며, 버튼을 1.5초간 꾹 눌러야만 게이지가 차오르며 실행됨.
4. **하단 결함 시뮬레이터 (Fault Testing Drawer):**
   * `[물리 비상정지 (E-STOP)]` 클릭 시 화면 전체 테두리가 붉은색으로 점멸하며 긴급 복구 가이드 배너 출력.
   * 물리 비상정지 중에는 리셋 버튼이 자동으로 잠기며, 하드웨어 복구 후에만 래치 해제 가능.
