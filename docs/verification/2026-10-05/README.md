# 2026-10-05 기존 도구 재검증 기록

이 기록은 [프로젝트 README](../../../README.md)의 현재 수치와 확인 범위를 뒷받침합니다. 기존 테스트·측정 코드를 실행했으며, 구현 코드와 테스트 코드는 변경하지 않았습니다. 문서와 실행 증거만 추가했습니다.

검증한 소스 커밋: `bfd11426dbbbd7c2a6ec7d840da3737a9e4e306f`. 실행일은 한국 시간 2026-10-05이며, 측정 JSON의 UTC 타임스탬프는 `2026-10-05T14:19:11Z`입니다. 이후 문서 변경은 이 소스의 시험 결과를 설명합니다.

## 환경과 실행 조건

| 항목 | 확인 값 |
|---|---|
| 로컬 Python 시험 | Windows 10.0.19045 · Python 3.11.9 · pytest 7.4.3 |
| ROS 시험 | ROS 2 Jazzy · Docker Engine 28.1.1 · Linux 컨테이너 |
| 컨테이너 커널 | `Linux-6.18.40.1-microsoft-standard-WSL2-x86_64-with-glibc2.39` |
| 컨테이너 Python | 3.12.3 |
| DDS 도메인 | `ROS_DOMAIN_ID=42` |
| PLC | 저장소의 가상 PLC와 장애 주입 프록시 |
| 주요 설정 | 폴링·발행 20ms, 응답 타임아웃 25ms, 연속 실패 3회, heartbeat 정체 80ms, 명령 타임아웃 300ms |

[환경 출력](environment.log) · [설정 원본](../../../config/gateway.yaml) · [컨테이너 구성](../../../docker-compose.yml)

정상 상태 측정은 장애 주입 시험 뒤 NORMAL 상태에서 수행했습니다. 측정 도구는 초기 알람을 강제 해제하고 정상 상태 표본을 수집합니다. 이는 정상 복구 절차의 조건 검증과는 별개의 실행 경로입니다.

## 실행 명령과 결과

저장소 루트에서 실행한 명령입니다. `ros2_gateway` 컨테이너에는 `python3`를 사용했습니다. 기존 컨테이너가 기동된 상태에서 현재 소스를 다시 빌드했습니다.

```bash
python -m pytest tests/ -q

docker compose exec -T ros2_gateway /gateway-entrypoint.sh colcon build --packages-select ros2_modbus_gateway --symlink-install
docker compose exec -T ros2_gateway /gateway-entrypoint.sh colcon test --packages-select ros2_modbus_gateway --event-handlers console_direct+
docker compose exec -T ros2_gateway /gateway-entrypoint.sh colcon test-result --verbose

# 시험을 위해 별도로 기동한 게이트웨이
docker compose exec -d ros2_gateway /gateway-entrypoint.sh bash -c 'exec ros2 launch ros2_modbus_gateway gateway.launch.py > /tmp/readme-gateway-20261005.log 2>&1'

docker compose exec -T ros2_gateway /gateway-entrypoint.sh python3 /ros2_ws/src/ros2_modbus_gateway/tests/run_integration.py
docker compose exec -T ros2_gateway /gateway-entrypoint.sh python3 /ros2_ws/src/ros2_modbus_gateway/tests/run_fault_scenarios.py
docker compose exec -T ros2_gateway /gateway-entrypoint.sh python3 /ros2_ws/src/ros2_modbus_gateway/benchmark/measure_latency.py 3000
```

각 단계의 최종 명령은 종료 코드 0이었습니다. 처음 통합 시험을 `python` 명령으로 실행했을 때에는 컨테이너에 해당 명령이 없어 종료 코드 127이 나왔습니다. `python3`로 다시 실행했으며 소스 변경은 없었습니다.

| 단계 | 관측 결과 | 증거 |
|---|---|---|
| 로컬 Python | `40 passed in 2.30s` | 터미널 결과를 이 표에 기록 |
| C++·ROS 빌드 | 패키지 빌드 성공 | [build.log](build.log) |
| C++ 테스트 | GoogleTest 66개 통과 | [cpp-tests.log](cpp-tests.log) |
| colcon 집계 | `72 tests, 0 errors, 0 failures, 0 skipped` | [cpp-results.log](cpp-results.log) |
| ROS 서비스 통합 | 기존 스크립트 판정 통과 | [integration.log](integration.log) |
| 장애 시나리오 | 기존 스크립트 판정 16/16 PASS | [fault-scenarios.log](fault-scenarios.log) · [fault_events.csv](fault_events.csv) |
| 정상 상태 측정 | 3,000개 표본 수집 | [benchmark.log](benchmark.log) · [summary.json](summary.json) · [latency.csv](latency.csv) |

GoogleTest 항목은 설정 12개, 안전 모니터 16개, 버퍼 17개, Modbus 클라이언트 9개, 런타임 5개, ROS 노드 7개로 합계 66개입니다. colcon의 72개 집계에는 이 66개와 CTest 실행 대상 6개가 포함됩니다.

## 기능·장애 시험에서 확인한 내용

통합 시험에서는 SETPOINT 0과 1000의 반영, 1001의 거부, START/STOP 반영, 동시 요청 중 하나의 BUSY 반환을 확인했습니다. 가상 PLC에 E-Stop 상태 비트를 설정했을 때 START와 일반 `ClearFault(force_clear=false)`가 거부되는 것도 확인했습니다. 실제 비상정지 장치나 물리 안전 회로를 시험한 결과는 아닙니다.

장애 시나리오 CSV에는 다음 판정이 기록되어 있습니다. 범위 설명은 스크립트의 출력 문구와 실제 판정 코드를 함께 읽은 것입니다.

| 번호 | 조건 | 이번 실행의 판정·관측 범위 |
|---:|---|---|
| 1 | 정상 START → STOP | 명령 반영과 running 상태 전이 |
| 2 | SETPOINT 경계값 | 0/1000 수락, 1001 거부 |
| 3 | 알람 래치 중 START | 요청 거부 |
| 4 | 동시 요청 | 명령 슬롯 BUSY 응답 |
| 5 | DROP | 알람 수신까지 94.19ms |
| 6 | DISCONNECT | 알람 수신까지 1.24ms |
| 7 | DELAY 200ms | 알람 수신까지 77.87ms |
| 8 | FREEZE | 알람 수신까지 82.10ms |
| 9 | MALFORMED | 변조 응답 감지 |
| 10 | DROP_ONE | 일회 유실 뒤 알람 없이 회복 |
| 11 | 쓰기 ACK 유실 | UNKNOWN 응답 |
| 12 | NORMAL 복구 | 유효 표본 회복 |
| 13 | 복구 후 래치 | 알람 유지와 START 거부 |
| 14 | ClearFault | 강제 해제 요청 후 래치 해제 |
| 15 | 늦은 구독 | 보존된 알람 수신 |
| 16 | 노드 정지·재개 | 스크립트의 경과 시간 판정. 실제 watchdog 동작은 검증하지 못함 |

번호 5~8은 각각 한 번 실행한 장애 주입 시각부터 시험 구독자의 ROS 알람 수신 시각까지의 관측값입니다. 반복 시험의 최댓값이나 모든 현장에서의 100ms 이내 검출을 보장하지 않습니다. 스크립트 자체의 `sla_pass`도 해당 실행에서 정한 조건의 판정입니다.

번호 16의 `last_rx_mono`는 시작 시 한 번 설정되고, 상태 콜백은 이 수신 시각을 갱신하지 않습니다. 노드를 `SIGSTOP/SIGCONT`로 정지·재개하면서 이 초기 시각에서 100ms가 지났는지를 확인합니다. 따라서 원시 CSV의 “Controlled Deceleration Stop 자율 발동” 문구와 PASS는 실제 상위 제어기의 watchdog·설비 감속·정지 증거로 해석할 수 없습니다. 사용자 요청에 따라 시험 코드를 수정하지 않고 이 한계를 기록했습니다.

통합·시나리오 시험 일부와 측정 준비 과정은 `force_clear=true`를 사용합니다. 해당 경로의 성공을 일반 해제 경로의 모든 인터록 충족으로 해석하지 않습니다.

## 정상 상태 측정과 지표 정의

유효 표본 3,000개를 60.04초 동안 수집했으며 관측 주파수는 49.97Hz였습니다. 다음 값은 [summary.json](summary.json)에서 읽었습니다.

| 지표 | 평균 | p50 | p95 | p99 | 관측 최댓값 |
|---|---:|---:|---:|---:|---:|
| FC03 RTT (ms) | 0.3850 | 0.3611 | 0.5466 | 0.6629 | 1.1025 |
| 폴링 지터 절댓값 (ms) | 0.0099 | 0.0049 | 0.0351 | 0.0917 | 0.2048 |
| 표본 → ROS 수신 (ms) | 14.4086 | 14.3820 | 14.6615 | 14.8046 | 15.1052 |

게이트웨이 프로세스 CPU 사용률은 1.68%였습니다. 측정 도구는 `/proc`에서 읽은 프로세스 CPU 시간 증가량을 측정 구간 시간으로 나누어 계산합니다. 머신 전체 사용률이나 12개 CPU로 정규화한 비율을 의미하지 않습니다.

- RTT는 게이트웨이가 상태 메시지에 기록한 Modbus 읽기 왕복 시간입니다.
- 지터는 상태 메시지의 `poll_jitter_ns` 절댓값을 ms로 바꾼 값입니다.
- 표본 → ROS 수신은 시험 구독자 콜백의 `time.monotonic_ns()`와 메시지 `sampled_ns`의 차이입니다. 일반 상태 전달 지연이며, 알람 이벤트 지연과 구분합니다.
- `overall_pass`는 p99 RTT ≤ 5ms와 p99 지터 ≤ 2ms의 결합 판정입니다. CPU ≤ 2% 판정은 별도 필드입니다. 이번 실행에서는 세 기준 모두 충족했습니다.

측정 stdout의 일부 고정 제목에는 “1,000”이 남아 있지만, 명령 인수·JSON의 목표/수집 수·원시 CSV는 3,000개입니다. 출력 내용은 보존했으며, C++ 로그의 줄 끝 공백만 정리했습니다. 기존 `benchmark/summary.json`은 실행 전에 보관한 원본으로 복원했으며, 이번 실행 결과는 이 날짜 폴더에 따로 저장했습니다.

## 결과의 적용 범위

이번 실행은 WSL2 Docker의 가상 PLC 환경에서 수행했습니다. 실측 정보가 부족한 상황에서 설정과 실패·복구 조건을 명시하고 재현 가능한 시험으로 확인한 기록입니다. 실제 PLC 스캔 정합성, 현장 네트워크 지연 분포, 물리 인터록, 상위 제어기의 독립 watchdog, 네이티브 실시간 커널 동작은 추가 확인 대상입니다.

C++ 속도 테스트는 버퍼 함수 반복 호출의 평균 시간을 측정합니다. 통과 결과를 순수 mutex 보유 시간이나 최악 지연의 상한으로 해석하지 않습니다. 원시 로그에 포함된 기존 완료·보장 표현도 위 확인 범위를 넘어서는 증거로 사용하지 않습니다.
