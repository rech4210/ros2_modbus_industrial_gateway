builtin_interfaces/Time stamp
unique_identifier_msgs/UUID instance_id
uint8 station_id
uint64 publish_sequence
uint64 sample_sequence
uint64 generation
uint8 link_state              # 0:DISCONNECTED, 1:CONNECTING, 2:OPERATIONAL, 3:COMM_FAULT, 4:STOPPING
bool has_sample               # 정상 수신된 표본 존재 여부
bool data_valid               # 표본이 신선하고 유효하여 상위 제어기가 사용할 수 있는지 여부
bool alarm_active             # 통신 두절 또는 결함 알람 래치 여부

# 6개 Holding Register 필드 (Offset 0~5)
uint16 heartbeat              # Offset 0: 생존 카운터
uint16 status_flags           # Offset 1: 비트필드
bool run_requested            # status_flags Bit 0
bool ready                    # status_flags Bit 1
bool running                  # status_flags Bit 2
bool physical_estop           # status_flags Bit 3
bool reset_requested          # status_flags Bit 4
bool alarm_tripped            # status_flags Bit 5
uint16 sensor_raw             # Offset 2: 센서 계측값 (0~1000)
uint16 setpoint_raw           # Offset 3: 목표 설정값 (0~1000)
uint16 fault_code             # Offset 4: PLC 에러 코드
uint16 applied_command_counter # Offset 5: 반영된 제어 카운터

# 진단 및 벤치마크 지표
uint64 sample_age_ns          # 표본 수신 시점으로부터 발행 시점까지 경과한 ns
uint64 heartbeat_age_ns       # 마지막 heartbeat 진행 시점으로부터 경과한 ns
uint64 poll_started_ns        # FC03 요청 송신 시작 단조 시각
uint64 sampled_ns             # FC03 응답 수신 완료 단조 시각
uint64 published_ns           # ROS 토픽 발행 직전 단조 시각
uint64 read_rtt_ns            # 1회 FC03 왕복 지연 시간 (ns)
int64 poll_jitter_ns          # 폴링 간격 지터 (ns)
uint64 poll_attempt_count     # 누적 폴링 시도 횟수
uint64 io_error_count         # 누적 I/O 실패 횟수
uint8 consecutive_failures    # 현재 연속 실패 횟수
uint16 error_code             # 현재 상태 에러 코드 (0이면 정상)
