builtin_interfaces/Time stamp
unique_identifier_msgs/UUID instance_id
uint8 station_id
uint64 event_sequence         # 알람 상태 전이마다 1씩 증가
bool active                   # true: 알람 발생(통신 두절/결함), false: 알람 해제(정상)
uint8 cause                   # 0:NONE, 1:STARTUP, 2:COMM_TIMEOUT, 3:HEARTBEAT_STALE, 4:PLC_INTERLOCK, 5:MANUAL_STOP, 6:SHUTDOWN, 7:INTERNAL
uint16 error_code             # 상세 에러 코드 (§4.1 참조)
uint64 generation             # 알람 발생 세션 번호
uint64 detected_ns            # 알람 감지 단조 시각 (ns)
uint64 published_ns           # 토픽 발행 단조 시각 (ns)
uint64 last_progress_ns       # 마지막 heartbeat 정상 진행 단조 시각
