# 23_UART_TCPIP_Repeater 개발 체크리스트

> 프로젝트: KoPT_HighTech / ATmega128  
> 목적: ATmega128 간 데이터 교환을 PC에서 UART↔TCP/IP 중계로 지원  
> 작성일: 2026-05-22  

---

## 산출물 목록

| 번호 | 파일                          | 경로         | 상태      |
| :--: | ----------------------------- | ------------ | --------- |
| 1    | `UART_Frame_Protocol_Spec.md` | `docs/`      | ✅ 완료   |
| 2    | `23_UART_TCPIP_Repeater.py`   | `ATmega128/` | ✅ 완료   |

---

## Phase 1 — 프로토콜 명세서 (`UART_Frame_Protocol_Spec.md`)

| #   | 작업 항목                                                                                                    | 상태 |
| --- | ------------------------------------------------------------------------------------------------------------ | ---- |
| 1-1 | 문서 개요 및 버전 이력 작성                                                                                  | ✅   |
| 1-2 | UART 프레임 프로토콜 상세 명세 (SOF / TYPE / LENGTH / PAYLOAD / CRC8, 필드 정의, CRC 계산식)                | ✅   |
| 1-3 | TCP 프레임 프로토콜 상세 명세 (MAGIC / VERSION / TYPE / SRC_ID / DST_ID / SEQ_NUM / LENGTH / PAYLOAD / CRC16) | ✅   |
| 1-4 | ATmega128 C 코드 구현 예시 (송신 / 수신 / CRC8 함수 포함)                                                   | ✅   |
| 1-5 | Python 구현 예시 및 프레임 사용 시나리오 작성                                                                | ✅   |

---

## Phase 2 — Python Repeater 앱 (`23_UART_TCPIP_Repeater.py`)

### 설계 확정 사항 요약

- **TCP 모드**: Server / Client 수동 선택 (A안)
- **TCP 프레임**: MAGIC + VERSION + TYPE + SRC_ID + DST_ID + SEQ_NUM + LENGTH + PAYLOAD + CRC16 (최소 12B)
- **UART 프레임**: SOF(0xAA) + TYPE + LENGTH + PAYLOAD + CRC8 (최소 4B, 페이로드 최대 28B)
- **서버 최대 클라이언트**: 3대
- **브로드캐스트**: 전체 또는 특정 ID 선택
- **크로스 릴레이**: 미지원 (Server UART → TCP Clients 단방향만)
- **Heartbeat 주기**: 5초
- **ID 할당**: Server가 접속 순서로 자동 부여 (1~3)
- **로그**: 텍스트 파일 저장
- **설정 저장**: config.json

### 라우팅 규칙

| 이벤트                          | 동작                              | 지원 |
| ------------------------------- | --------------------------------- | :--: |
| Server UART RX → TCP            | 브로드캐스트 또는 특정 Client 전달 | ✅   |
| Client TCP RX → Client UART TX  | 수신 데이터를 로컬 ATmega128로 전달 | ✅   |
| Client UART RX → Server TCP TX  | 로컬 ATmega128 데이터를 Server로 전달 | ✅   |
| Server TCP RX → Server UART TX  | Server가 받은 TCP 데이터를 ATmega128로 전달 | ✅   |
| Client-N → Client-M 직접 중계   | TCP→TCP 포워딩 (서버가 수신 후 송신자 제외 브로드캐스트) | ✅   |

### 모듈별 체크리스트

| #   | 모듈                | 작업 항목                                             | 상태 |
| --- | ------------------- | ----------------------------------------------------- | :--: |
| 2-1 | `ProtocolCodec`     | UART 프레임 인코딩/디코딩, CRC8 계산                  | ✅   |
| 2-1 | `ProtocolCodec`     | TCP 프레임 인코딩/디코딩, CRC16 계산                  | ✅   |
| 2-2 | `UartManager`       | COM 포트 연결 / 해제                                  | ✅   |
| 2-2 | `UartManager`       | 수신 스레드 (SOF 탐색, 프레임 파싱)                   | ✅   |
| 2-2 | `UartManager`       | 송신 함수 (프레임 인코딩 후 전송)                     | ✅   |
| 2-3 | `TcpManager`        | Server 모드: bind / listen / accept loop              | ✅   |
| 2-3 | `TcpManager`        | Server 모드: Client ID 자동 할당 (1~3)                | ✅   |
| 2-3 | `TcpManager`        | Client 모드: connect / 재연결 처리                    | ✅   |
| 2-3 | `TcpManager`        | 수신 스레드 (프레임 파싱, 큐 투입)                    | ✅   |
| 2-3 | `TcpManager`        | Heartbeat 송수신 (5초 주기)                           | ✅   |
| 2-3 | `TcpManager`        | 강제 클라이언트 해제 기능                             | ✅   |
| 2-4 | `RelayEngine`       | UART RX → TCP 라우팅 (브로드캐스트 / 특정 ID)         | ✅   |
| 2-4 | `RelayEngine`       | TCP RX → UART TX 라우팅                               | ✅   |
| 2-4 | `RelayEngine`       | 중계 ON/OFF 제어 (UART→TCP, TCP→UART 개별)            | ✅   |
| 2-4 | `RelayEngine`       | 통계 카운터 (RX/TX 패킷 수, 바이트 수, CRC 오류)      | ✅   |
| 2-5 | `RepeaterApp` (GUI) | UART 설정 패널 (COM 포트, Baud Rate, 연결/해제)       | ✅   |
| 2-5 | `RepeaterApp` (GUI) | TCP 설정 패널 (모드, 내 IP, 상대 IP, 포트)            | ✅   |
| 2-5 | `RepeaterApp` (GUI) | 클라이언트 목록 패널 (ID, IP, 연결 상태, 강제 해제)   | ✅   |
| 2-5 | `RepeaterApp` (GUI) | 중계 설정 패널 (UART↔TCP ON/OFF, 브로드캐스트 대상)   | ✅   |
| 2-5 | `RepeaterApp` (GUI) | 통신 로그 뷰 (방향 표시, 자동 스크롤, 지우기)         | ✅   |
| 2-5 | `RepeaterApp` (GUI) | 통계 패널 (패킷/바이트 수, CRC 오류, Heartbeat 상태)  | ✅   |
| 2-5 | `RepeaterApp` (GUI) | 수동 송신 패널 (UART / TCP 특정 ID 선택, 전송)        | ✅   |
| 2-6 | 공통                | config.json 저장 / 불러오기                           | ✅   |
| 2-6 | 공통                | 텍스트 로그 파일 저장 기능                            | ✅   |
| 2-6 | 공통                | 앱 종료 시 정상 종료 처리 (소켓/포트 해제)            | ✅   |

---

## GUI 레이아웃 참고

```text
┌────────────────────────────────────────────────────────────┐
│                UART ↔ TCP/IP Repeater  v1.0                │
├───────────────────────┬────────────────────────────────────┤
│  UART 설정             │  TCP/IP 설정                       │
│  COM: [COM3 ▼][새로고침]│  모드: ● Server  ○ Client         │
│  Baud:[9600  ▼]       │  내 IP: [192.168.1.100]            │
│  [연결]  상태: ● 연결됨 │  포트:  [54321  ]                  │
│                       │  상대IP: [___________] (Client만)   │
│                       │  [서버 시작 / 서버에 연결]           │
│                       │  상태: 2/3 클라이언트 연결됨 ●       │
│                       ├────────────────────────────────────┤
│                       │  연결된 클라이언트 목록             │
│                       │  ID:1  192.168.1.101  ●  [강제해제] │
│                       │  ID:2  192.168.1.102  ●  [강제해제] │
│                       │  ID:3  - 대기 중 -    ○            │
├───────────────────────┴────────────────────────────────────┤
│  중계 설정                                                  │
│  [✓] UART→TCP 중계    [✓] TCP→UART 중계                    │
│  브로드캐스트 대상: ● 전체  ○ 선택                         │
│  선택 시: [✓]ID:1  [✓]ID:2  [ ]ID:3                        │
├────────────────────────────────────────────────────────────┤
│  통신 로그              [✓]자동스크롤  [지우기]  [저장...]  │
│  [14:23:01.123] UART→TCP  [ADC0]512         → Broadcast   │
│  [14:23:02.456] TCP→UART  [Song-1]          ← ID:1        │
│  [14:23:04.012] SYSTEM    클라이언트 연결됨  ID:3          │
├────────────────────────────────────────────────────────────┤
│  통계                                                       │
│  UART RX: 1,234 pkts (12,340 B)  TCP TX: 3,702 pkts       │
│  UART TX:   456 pkts  (4,560 B)  TCP RX:   456 pkts       │
│  드롭: 0 pkts  CRC 오류: 0 pkts  Heartbeat: 정상 (2/2)    │
├────────────────────────────────────────────────────────────┤
│  수동 송신 (테스트용)                                       │
│  대상: ● UART  ○ TCP [ID:1 ▼]  [입력____________] [전송]  │
└────────────────────────────────────────────────────────────┘
```

---

## GUI 레이아웃 — Ver.B (HTML 테이블)

<table width="100%">
<tr><th colspan="2" align="center">UART ↔ TCP/IP Repeater &nbsp; v1.0</th></tr>
<tr>
<td width="40%" valign="top">

**UART 설정**

COM: `COM3 ▼` `새로고침`
Baud: `9600 ▼`
`연결` &nbsp; 상태: 🟢 연결됨

</td>
<td width="60%" valign="top">

**TCP/IP 설정**

모드: ● Server &nbsp; ○ Client
내 IP: `192.168.1.100` &nbsp; 포트: `54321`
`서버 시작` &nbsp; 상태: 2/3 클라이언트 연결됨 🟢

**클라이언트 목록**

| ID | IP | 상태 | |
|:--:|---|:--:|---|
| 1 | 192.168.1.101 | 🟢 | `강제해제` |
| 2 | 192.168.1.102 | 🟢 | `강제해제` |
| 3 | - 대기 중 - | ⚪ | |

</td>
</tr>
<tr><td colspan="2">

**중계 설정**
`☑` UART→TCP 중계 &nbsp;&nbsp; `☑` TCP→UART 중계 &nbsp;&nbsp; 브로드캐스트 대상: ● 전체 &nbsp; ○ 선택

</td></tr>
<tr><td colspan="2">

**통신 로그** &nbsp; `☑ 자동스크롤` &nbsp; `지우기` &nbsp; `저장...`

```text
[14:23:01.123] UART→TCP  [ADC0] 512        → Broadcast
[14:23:02.456] TCP→UART  [Song-1]           ← ID:1
[14:23:04.012] SYSTEM    클라이언트 연결됨  ID:3
```

</td></tr>
<tr><td colspan="2">

**통계**
UART RX: `1,234 pkts` (12,340 B) &nbsp; TCP TX: `3,702 pkts`
UART TX: `456 pkts` (4,560 B) &nbsp;&nbsp;&nbsp; TCP RX: `456 pkts`
드롭: `0` &nbsp; CRC 오류: `0` &nbsp; Heartbeat: 🟢 정상 (2/2)

</td></tr>
<tr><td colspan="2">

**수동 송신 (테스트용)**
대상: ● UART &nbsp; ○ TCP &nbsp; `ID:1 ▼` &nbsp; `입력____________` &nbsp; `전송`

</td></tr>
</table>

---

## 변경 이력

| 버전 | 날짜       | 내용                   |
| ---- | ---------- | ---------------------- |
| v0.1 | 2026-05-22 | 초기 체크리스트 작성   |
| v0.2 | 2026-05-22 | Phase 1 완료 반영      |
| v0.3 | 2026-05-22 | Phase 2 전체 완료      |
