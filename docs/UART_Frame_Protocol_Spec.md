# UART / TCP 프레임 프로토콜 명세서

> **프로젝트**: KoPT_HighTech — ATmega128 UART↔TCP/IP Repeater  
> **문서 번호**: KPT-PROTO-001  
> **작성일**: 2026-05-22  
> **작성자**: KoPT_HighTech  
> **적용 파일 (Python)**: `ATmega128/23_UART_TCPIP_Repeater.py`  
> **적용 파일 (C)**:     `ATmega128/24_ATmega128_Platform.c`  
> **관련 하드웨어**: ATmega128A (16MHz, UART1 사용)

---

## 목차

1. [개요](#1-개요)
2. [프로토콜 구분](#2-프로토콜-구분)
3. [UART 프레임 프로토콜 (PC ↔ ATmega128)](#3-uart-프레임-프로토콜-pc--atmega128)
4. [TCP 프레임 프로토콜 (PC ↔ PC)](#4-tcp-프레임-프로토콜-pc--pc)
5. [ATmega128 C 코드 구현 예시](#5-atmega128-c-코드-구현-예시)
6. [Python 구현 예시](#6-python-구현-예시)
7. [통신 시나리오](#7-통신-시나리오)
8. [23↔24 연동 가이드 (학생용)](#8-2324-연동-가이드-학생용)
9. [변경 이력](#9-변경-이력)

---

## 1. 개요

본 문서는 ATmega128 마이크로컨트롤러와 PC 간 UART 통신, 및 PC 간 TCP/IP 통신에 사용되는 **바이너리 프레임 프로토콜**을 정의한다.

### 1.1 시스템 구성

```
ATmega128-S ──[UART Frame]──[PC-Server]──[TCP Frame]──[PC-Client-1]──[UART Frame]──ATmega128-1
                                       └──────────────[PC-Client-2]──[UART Frame]──ATmega128-2
                                       └──────────────[PC-Client-3]──[UART Frame]──ATmega128-3
```

### 1.2 설계 목표

- ATmega128의 **제한된 RAM/Flash** 환경에서 구현 가능한 경량 프로토콜
- **프레임 동기화**: SOF 바이트로 수신 오류 후 재동기화 가능
- **오류 감지**: CRC 검사로 데이터 무결성 확인
- **확장성**: TYPE 필드로 향후 새로운 데이터 종류 추가 가능

---

## 2. 프로토콜 구분

| 구분 | 적용 구간 | 헤더 크기 | 오류 감지 | 최대 페이로드 |
|------|----------|-----------|-----------|--------------|
| UART 프레임 | PC ↔ ATmega128 | 3 bytes | CRC8 (1B) | 28 bytes |
| TCP 프레임 | PC ↔ PC | 10 bytes | CRC16 (2B) | 65,535 bytes |

> **UART 페이로드 최대 28B 제한 근거**: ATmega128 UART 수신 버퍼 32B 제약에서 헤더(SOF+TYPE+LENGTH = 3B) + CRC8(1B) = 4B를 제외한 값.

---

## 3. UART 프레임 프로토콜 (PC ↔ ATmega128)

### 3.1 프레임 구조

```
 Byte 0      Byte 1    Byte 2      Byte 3 ~ (2+N)    Byte (3+N)
┌──────────┬─────────┬──────────┬──────────────────┬───────────┐
│  SOF     │  TYPE   │  LENGTH  │    PAYLOAD        │   CRC8    │
│  1 byte  │  1 byte │  1 byte  │   0 ~ 28 bytes    │  1 byte   │
│  0xAA    │         │   N      │                   │           │
└──────────┴─────────┴──────────┴──────────────────┴───────────┘
← ──────────────────── 최소 4 bytes (PAYLOAD 없을 때) ──────────────────── →
```

<table width="100%">
<tr>
<th align="center">Byte 0</th>
<th align="center">Byte 1</th>
<th align="center">Byte 2</th>
<th align="center">Byte 3 ~ (2+N)</th>
<th align="center">Byte (3+N)</th>
</tr>
<tr>
<td align="center"><b>SOF</b><br>1 byte<br><code>0xAA</code></td>
<td align="center"><b>TYPE</b><br>1 byte</td>
<td align="center"><b>LENGTH</b><br>1 byte<br>= N</td>
<td align="center"><b>PAYLOAD</b><br>0 ~ 28 bytes</td>
<td align="center"><b>CRC8</b><br>1 byte</td>
</tr>
<tr><td colspan="5" align="center">← &nbsp; 최소 4 bytes (PAYLOAD 없을 때) &nbsp; →</td></tr>
</table>

### 3.2 필드 정의

#### SOF (Start of Frame)

| 항목 | 값 |
|------|----|
| 크기 | 1 byte |
| 고정값 | `0xAA` |
| 역할 | 프레임 시작 식별. 수신 오류 후 이 값이 나올 때까지 바이트를 버려 재동기화. |

#### TYPE (프레임 유형)

| 값 | 상수명 | 설명 |
|----|--------|------|
| `0x01` | `UART_TYPE_DATA` | 일반 데이터 (센서값, 문자열 등) |
| `0x02` | `UART_TYPE_CMD`  | 명령 (ATmega128 동작 제어) |
| `0x03` | `UART_TYPE_ACK`  | 응답 (명령 수신 확인) |
| `0x04` | `UART_TYPE_ERR`  | 오류 응답 (CRC 불일치, 알 수 없는 명령 등) |

#### LENGTH

| 항목 | 값 |
|------|----|
| 크기 | 1 byte |
| 범위 | `0x00` ~ `0x1C` (0 ~ 28) |
| 역할 | PAYLOAD 바이트 수. 0이면 PAYLOAD 없음. |

#### PAYLOAD

| 항목 | 값 |
|------|----|
| 크기 | 0 ~ 28 bytes |
| 인코딩 | UTF-8 문자열 또는 바이너리 데이터 |
| 내용 | TYPE에 따라 정의 (§3.3 참고) |

#### CRC8

| 항목 | 값 |
|------|----|
| 크기 | 1 byte |
| 계산 범위 | `TYPE` + `LENGTH` + `PAYLOAD` 전체 |
| 알고리즘 | **XOR 누적** (아래 수식 참고) |

**CRC8 계산식:**

```
CRC8 = TYPE XOR LENGTH XOR PAYLOAD[0] XOR PAYLOAD[1] XOR ... XOR PAYLOAD[N-1]
```

> XOR 누적 방식은 ATmega128에서 단 몇 줄의 C 코드로 구현 가능하며, 단일 비트 오류를 100% 감지한다.

### 3.3 PAYLOAD 포맷 (TYPE별)

#### TYPE = 0x01 (DATA) — ATmega128 → PC

| 데이터 종류 | PAYLOAD 예시 | 설명 |
|------------|-------------|------|
| ADC 채널 0 | `A0:0512` (7B) | ADC0 측정값 (0~1023) |
| ADC 채널 1 | `A1:0300` (7B) | ADC1 측정값 (0~1023) |
| LCD 1행    | `L0:Hello World  ` (최대 19B) | LCD 1행 텍스트 (16자) |
| LCD 2행    | `L1:Playing...   ` (최대 19B) | LCD 2행 텍스트 (16자) |

> **포맷 규칙**: `[2자 태그]:[데이터]` — 태그는 2자 고정, 콜론(`:`) 구분자

#### TYPE = 0x02 (CMD) — PC → ATmega128

| 명령 | PAYLOAD 예시 | 설명 |
|------|-------------|------|
| 곡 재생 | `S:1` ~ `S:9` | 주크박스 곡 번호 재생 |
| LCD 표시 | `D:Hello` (최대 28B) | ATmega128 LCD에 문자열 표시 |
| 리셋 | `R:` | 소프트 리셋 요청 |

#### TYPE = 0x03 (ACK) — ATmega128 → PC

| PAYLOAD | 설명 |
|---------|------|
| `OK` (2B) | 명령 정상 수신 및 처리 완료 |

#### TYPE = 0x04 (ERR) — ATmega128 → PC

| PAYLOAD | 설명 |
|---------|------|
| `E:CRC` (5B) | CRC8 불일치 오류 |
| `E:CMD` (5B) | 알 수 없는 명령 |
| `E:LEN` (5B) | LENGTH 범위 초과 |

### 3.4 프레임 예시

**예시 1 — ATmega128 → PC: ADC0 값 512 전송**

```
SOF    TYPE   LENGTH  PAYLOAD          CRC8
0xAA   0x01   0x07    41 30 3A 30 35 31 32   ??

PAYLOAD (ASCII): A0:0512
CRC8 = 0x01 XOR 0x07 XOR 0x41 XOR 0x30 XOR 0x3A XOR 0x30 XOR 0x35 XOR 0x31 XOR 0x32
     = 0x01 ^ 0x07 ^ 0x41 ^ 0x30 ^ 0x3A ^ 0x30 ^ 0x35 ^ 0x31 ^ 0x32 = 0x63

완성 프레임: AA 01 07 41 30 3A 30 35 31 32 63
```

**예시 2 — PC → ATmega128: 곡 1번 재생 명령**

```
SOF    TYPE   LENGTH  PAYLOAD   CRC8
0xAA   0x02   0x03    53 3A 31  ??

PAYLOAD (ASCII): S:1
CRC8 = 0x02 XOR 0x03 XOR 0x53 XOR 0x3A XOR 0x31 = 0x65

완성 프레임: AA 02 03 53 3A 31 65
```

**예시 3 — ATmega128 → PC: ACK 응답**

```
SOF    TYPE   LENGTH  PAYLOAD   CRC8
0xAA   0x03   0x02    4F 4B     ??

PAYLOAD (ASCII): OK
CRC8 = 0x03 XOR 0x02 XOR 0x4F XOR 0x4B = 0x47

완성 프레임: AA 03 02 4F 4B 47
```

---

## 4. TCP 프레임 프로토콜 (PC ↔ PC)

### 4.1 프레임 구조

```
 Byte 0-1    Byte 2     Byte 3    Byte 4    Byte 5    Byte 6-7   Byte 8-9    Byte 10~(9+N)  Last 2B
┌──────────┬──────────┬─────────┬─────────┬─────────┬──────────┬──────────┬───────────────┬────────┐
│  MAGIC   │ VERSION  │  TYPE   │ SRC_ID  │ DST_ID  │ SEQ_NUM  │  LENGTH  │    PAYLOAD    │ CRC16  │
│  2 bytes │  1 byte  │  1 byte │  1 byte │  1 byte │  2 bytes │  2 bytes │  0~65535 B    │ 2 bytes│
│ AB CD    │  0x01    │         │         │         │ Big-End. │ Big-End. │               │Big-End.│
└──────────┴──────────┴─────────┴─────────┴─────────┴──────────┴──────────┴───────────────┴────────┘
← ──────────────────────────── 최소 12 bytes (PAYLOAD 없을 때) ──────────────────────────────────── →
```

<table width="100%">
<tr>
<th align="center">Byte 0-1</th>
<th align="center">Byte 2</th>
<th align="center">Byte 3</th>
<th align="center">Byte 4</th>
<th align="center">Byte 5</th>
<th align="center">Byte 6-7</th>
<th align="center">Byte 8-9</th>
<th align="center">Byte 10~(9+N)</th>
<th align="center">Last 2B</th>
</tr>
<tr>
<td align="center"><b>MAGIC</b><br>2 bytes<br><code>AB CD</code></td>
<td align="center"><b>VERSION</b><br>1 byte<br><code>0x01</code></td>
<td align="center"><b>TYPE</b><br>1 byte</td>
<td align="center"><b>SRC_ID</b><br>1 byte</td>
<td align="center"><b>DST_ID</b><br>1 byte</td>
<td align="center"><b>SEQ_NUM</b><br>2 bytes<br>Big-End.</td>
<td align="center"><b>LENGTH</b><br>2 bytes<br>Big-End.</td>
<td align="center"><b>PAYLOAD</b><br>0~65535 B</td>
<td align="center"><b>CRC16</b><br>2 bytes<br>Big-End.</td>
</tr>
<tr><td colspan="9" align="center">← &nbsp; 최소 12 bytes (PAYLOAD 없을 때) &nbsp; →</td></tr>
</table>

### 4.2 필드 정의

#### MAGIC

| 항목 | 값 |
|------|----|
| 크기 | 2 bytes |
| 고정값 | `0xAB 0xCD` |
| 역할 | TCP 스트림에서 프레임 시작 식별 |

#### VERSION

| 항목 | 값 |
|------|----|
| 크기 | 1 byte |
| 현재값 | `0x01` |
| 역할 | 프로토콜 버전. 하위 호환성 관리. |

#### TYPE (프레임 유형)

| 값 | 상수명 | 설명 |
|----|--------|------|
| `0x01` | `TCP_TYPE_DATA`      | 일반 데이터 중계 |
| `0x02` | `TCP_TYPE_HEARTBEAT` | 연결 유지 확인 (5초 주기, PAYLOAD 없음) |
| `0x03` | `TCP_TYPE_SYSTEM`    | 시스템 메시지 (접속/해제/ID 할당) |

#### SRC_ID (송신 측 ID)

| 값 | 의미 |
|----|------|
| `0x00` | Server |
| `0x01` | Client 1 |
| `0x02` | Client 2 |
| `0x03` | Client 3 |

#### DST_ID (수신 측 ID)

| 값 | 의미 |
|----|------|
| `0xFF` | **브로드캐스트** (모든 Client에게 전달) |
| `0x00` | Server |
| `0x01` | Client 1 |
| `0x02` | Client 2 |
| `0x03` | Client 3 |

#### SEQ_NUM (순번)

| 항목 | 값 |
|------|----|
| 크기 | 2 bytes (Big-Endian) |
| 범위 | `0x0000` ~ `0xFFFF`, 오버플로우 시 `0x0000`으로 순환 |
| 역할 | 패킷 손실 감지. 수신 측에서 순번 불연속 시 드롭 카운터 증가. |

#### LENGTH

| 항목 | 값 |
|------|----|
| 크기 | 2 bytes (Big-Endian) |
| 범위 | `0x0000` ~ `0xFFFF` (0 ~ 65,535 bytes) |
| 역할 | PAYLOAD 바이트 수 |

#### PAYLOAD

| 항목 | 값 |
|------|----|
| 크기 | LENGTH bytes |
| 내용 | TYPE에 따라 정의 (§4.3 참고) |

#### CRC16

| 항목 | 값 |
|------|----|
| 크기 | 2 bytes (Big-Endian) |
| 계산 범위 | `TYPE` + `SRC_ID` + `DST_ID` + `SEQ_NUM` + `LENGTH` + `PAYLOAD` 전체 |
| 알고리즘 | **CRC-16/CCITT-FALSE** (다항식 `0x1021`, 초기값 `0xFFFF`) |

**CRC16 계산 예시 (Python):**

```python
def calc_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
    return crc & 0xFFFF
```

### 4.3 PAYLOAD 포맷 (TYPE별)

#### TYPE = 0x01 (DATA)

UART 프레임에서 수신한 PAYLOAD를 그대로 캡슐화하여 전달한다.

```
TCP PAYLOAD = UART PAYLOAD (원본 데이터)
```

예: ATmega128이 `A0:0512`를 보내면 → TCP PAYLOAD도 `A0:0512` (7 bytes)

#### TYPE = 0x02 (HEARTBEAT)

PAYLOAD 없음 (LENGTH = 0). 5초마다 송신. 상대방이 응답하지 않으면 연결 해제 처리.

#### TYPE = 0x03 (SYSTEM)

| PAYLOAD 문자열 | 의미 |
|---------------|------|
| `CONN:01` | Client 1 접속 알림 |
| `DISC:02` | Client 2 해제 알림 |
| `ASID:03` | Server → Client: "당신의 ID는 3입니다" |

### 4.4 연결 수립 절차

```
Client                          Server
  │                               │
  │──── TCP connect ─────────────→│
  │                               │ (ID 자동 할당: 1~3 중 빈 번호)
  │←─── SYSTEM "ASID:0N" ─────────│
  │                               │
  │←─── SYSTEM "CONN:0N" ─────────│ (다른 Client들에게 브로드캐스트)
  │                               │
  │←──→ HEARTBEAT (5초 주기) ←──→│
  │                               │
  │──── DATA ────────────────────→│ (UART에서 수신한 데이터)
  │                               │──→ UART TX (ATmega128으로 전달)
  │                               │──→ Broadcast (DST_ID = 0xFF 시)
```

### 4.5 프레임 예시

**예시 — Client-1이 ADC0 데이터를 Server로 전송**

```
MAGIC   VER  TYPE  SRC  DST  SEQ    LEN    PAYLOAD            CRC16
AB CD   01   01    01   00   00 01  00 07  41 30 3A 30 35 31 32  ????

PAYLOAD (ASCII): A0:0512  (7 bytes)
CRC16 계산 범위: 01 01 00 00 01 00 07 41 30 3A 30 35 31 32
```

**예시 — Server HEARTBEAT 브로드캐스트**

```
MAGIC   VER  TYPE  SRC  DST  SEQ    LEN    PAYLOAD  CRC16
AB CD   01   02    00   FF   00 0A  00 00  (없음)   ????
```

---

## 5. ATmega128 C 코드 구현 예시

### 5.1 상수 및 구조체 정의

```c
/* uart_frame.h */
#ifndef UART_FRAME_H
#define UART_FRAME_H

#include <stdint.h>

/* 프레임 상수 */
#define UART_FRAME_SOF          0xAA
#define UART_FRAME_MAX_PAYLOAD  28
#define UART_FRAME_HEADER_SIZE  3   /* SOF + TYPE + LENGTH */
#define UART_FRAME_TOTAL_MAX    (UART_FRAME_HEADER_SIZE + UART_FRAME_MAX_PAYLOAD + 1)  /* +1: CRC8 */

/* TYPE 정의 */
#define UART_TYPE_DATA  0x01
#define UART_TYPE_CMD   0x02
#define UART_TYPE_ACK   0x03
#define UART_TYPE_ERR   0x04

/* 프레임 구조체 */
typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t payload[UART_FRAME_MAX_PAYLOAD];
} UartFrame;

/* 함수 선언 */
uint8_t  uart_crc8(uint8_t type, uint8_t length, const uint8_t *payload);
void     uart_send_frame(const UartFrame *frame);
uint8_t  uart_parse_byte(uint8_t byte, UartFrame *out_frame);

#endif /* UART_FRAME_H */
```

### 5.2 CRC8 계산 함수

```c
/* uart_frame.c */
#include "uart_frame.h"

uint8_t uart_crc8(uint8_t type, uint8_t length, const uint8_t *payload) {
    uint8_t crc = type ^ length;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= payload[i];
    }
    return crc;
}
```

### 5.3 프레임 송신 함수

```c
/* uart1_putc 는 기존 구현 재사용 */
extern void uart1_putc(char c);

void uart_send_frame(const UartFrame *frame) {
    uint8_t crc = uart_crc8(frame->type, frame->length, frame->payload);

    uart1_putc(UART_FRAME_SOF);
    uart1_putc(frame->type);
    uart1_putc(frame->length);
    for (uint8_t i = 0; i < frame->length; i++) {
        uart1_putc(frame->payload[i]);
    }
    uart1_putc(crc);
}

/* 사용 예시: ADC0 값 512 전송 */
void send_adc0(uint16_t adc_val) {
    UartFrame f;
    f.type   = UART_TYPE_DATA;
    f.length = 7;
    /* PAYLOAD: "A0:XXXX" 포맷 */
    f.payload[0] = 'A';
    f.payload[1] = '0';
    f.payload[2] = ':';
    f.payload[3] = '0' + ((adc_val / 1000) % 10);
    f.payload[4] = '0' + ((adc_val /  100) % 10);
    f.payload[5] = '0' + ((adc_val /   10) % 10);
    f.payload[6] = '0' + ( adc_val         % 10);
    uart_send_frame(&f);
}
```

### 5.4 프레임 수신 파서 (상태 머신)

```c
/* 수신 상태 */
typedef enum {
    RX_WAIT_SOF,
    RX_WAIT_TYPE,
    RX_WAIT_LENGTH,
    RX_WAIT_PAYLOAD,
    RX_WAIT_CRC
} RxState;

/*
 * 바이트 단위 상태 머신 파서.
 * UART ISR에서 수신한 바이트를 하나씩 전달.
 * 반환값: 1 = 프레임 완성, 0 = 계속 수신 중
 */
uint8_t uart_parse_byte(uint8_t byte, UartFrame *out) {
    static RxState  state      = RX_WAIT_SOF;
    static UartFrame rx_buf;
    static uint8_t  pay_idx   = 0;

    switch (state) {
        case RX_WAIT_SOF:
            if (byte == UART_FRAME_SOF) state = RX_WAIT_TYPE;
            break;

        case RX_WAIT_TYPE:
            rx_buf.type = byte;
            state = RX_WAIT_LENGTH;
            break;

        case RX_WAIT_LENGTH:
            if (byte > UART_FRAME_MAX_PAYLOAD) {
                state = RX_WAIT_SOF;  /* 범위 초과 → 재동기화 */
            } else {
                rx_buf.length = byte;
                pay_idx = 0;
                state = (byte == 0) ? RX_WAIT_CRC : RX_WAIT_PAYLOAD;
            }
            break;

        case RX_WAIT_PAYLOAD:
            rx_buf.payload[pay_idx++] = byte;
            if (pay_idx >= rx_buf.length) state = RX_WAIT_CRC;
            break;

        case RX_WAIT_CRC: {
            uint8_t expected = uart_crc8(rx_buf.type, rx_buf.length, rx_buf.payload);
            state = RX_WAIT_SOF;
            if (byte == expected) {
                *out = rx_buf;
                return 1;  /* 프레임 완성 */
            }
            /* CRC 불일치 → ERR 응답 */
            UartFrame err = { UART_TYPE_ERR, 5, {'E',':','C','R','C'} };
            uart_send_frame(&err);
            break;
        }
    }
    return 0;
}
```

### 5.5 main 루프 통합 예시

```c
/* UART ISR에서 파서 호출 */
volatile uint8_t  g_frame_ready = 0;
volatile UartFrame g_rx_frame;

ISR(USART1_RX_vect) {
    uint8_t byte = UDR1;
    UartFrame tmp;
    if (uart_parse_byte(byte, &tmp)) {
        g_rx_frame   = tmp;
        g_frame_ready = 1;
    }
}

/* main 루프에서 프레임 처리 */
int main(void) {
    /* ... 초기화 ... */
    while (1) {
        if (g_frame_ready) {
            g_frame_ready = 0;
            UartFrame f = g_rx_frame;

            if (f.type == UART_TYPE_CMD) {
                /* PAYLOAD 예: "S:1" → 곡 1번 재생 */
                if (f.payload[0] == 'S' && f.payload[1] == ':') {
                    int song = f.payload[2] - '0';
                    /* 곡 재생 처리 */
                    UartFrame ack = { UART_TYPE_ACK, 2, {'O','K'} };
                    uart_send_frame(&ack);
                }
            }
        }
    }
}
```

---

## 6. Python 구현 예시

### 6.1 UART 프레임 인코딩

```python
def encode_uart_frame(frame_type: int, payload: bytes) -> bytes:
    """UART 프레임을 바이트열로 인코딩."""
    length = len(payload)
    assert length <= 28, "PAYLOAD 최대 28 bytes 초과"

    crc = frame_type ^ length
    for b in payload:
        crc ^= b
    crc &= 0xFF

    return bytes([0xAA, frame_type, length]) + payload + bytes([crc])
```

### 6.2 UART 프레임 디코딩 (상태 머신)

```python
class UartFrameParser:
    """바이트 스트림에서 UART 프레임을 추출하는 상태 머신."""

    SOF = 0xAA

    def __init__(self):
        self._reset()

    def _reset(self):
        self._state   = 'WAIT_SOF'
        self._type    = 0
        self._length  = 0
        self._payload = bytearray()

    def feed(self, byte: int):
        """바이트 하나를 입력. 완성된 프레임 dict 반환, 아직이면 None."""
        if self._state == 'WAIT_SOF':
            if byte == self.SOF:
                self._state = 'WAIT_TYPE'

        elif self._state == 'WAIT_TYPE':
            self._type  = byte
            self._state = 'WAIT_LENGTH'

        elif self._state == 'WAIT_LENGTH':
            if byte > 28:
                self._reset()
            else:
                self._length  = byte
                self._payload = bytearray()
                self._state   = 'WAIT_CRC' if byte == 0 else 'WAIT_PAYLOAD'

        elif self._state == 'WAIT_PAYLOAD':
            self._payload.append(byte)
            if len(self._payload) >= self._length:
                self._state = 'WAIT_CRC'

        elif self._state == 'WAIT_CRC':
            expected = self._type ^ self._length
            for b in self._payload:
                expected ^= b
            expected &= 0xFF
            frame = None
            if byte == expected:
                frame = {
                    'type':    self._type,
                    'length':  self._length,
                    'payload': bytes(self._payload),
                }
            self._reset()
            return frame

        return None
```

### 6.3 TCP 프레임 인코딩

```python
import struct

TCP_MAGIC   = b'\xAB\xCD'
TCP_VERSION = 0x01

def calc_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
    return crc & 0xFFFF

def encode_tcp_frame(frame_type: int, src_id: int, dst_id: int,
                     seq_num: int, payload: bytes) -> bytes:
    """TCP 프레임을 바이트열로 인코딩."""
    length = len(payload)
    header_body = struct.pack('>BBBHH', frame_type, src_id, dst_id, seq_num, length)
    crc_data     = header_body + payload
    crc          = calc_crc16(crc_data)
    return TCP_MAGIC + bytes([TCP_VERSION]) + crc_data + struct.pack('>H', crc)
```

---

## 7. 통신 시나리오

### 시나리오 1 — ATmega128-S의 ADC 데이터를 모든 Client에 브로드캐스트

```
[ATmega128-S]
    │  UART Frame: AA 01 07 41 30 3A 30 35 31 32 63  (A0:0512)
    ↓
[PC-Server: UartManager]
    │  PAYLOAD 추출: "A0:0512"
    ↓
[PC-Server: RelayEngine]
    │  TCP Frame 생성: TYPE=DATA, SRC=0x00, DST=0xFF (Broadcast)
    ↓
[PC-Client-1] → UART TX → [ATmega128-1]
[PC-Client-2] → UART TX → [ATmega128-2]
[PC-Client-3] → UART TX → [ATmega128-3]
```

### 시나리오 2 — Client-1이 명령을 Server ATmega128로 전달

```
[PC-Client-1: 수동 송신]
    │  TCP Frame: TYPE=DATA, SRC=0x01, DST=0x00, PAYLOAD="S:1"
    ↓
[PC-Server: RelayEngine]
    │  TCP RX → UART TX
    ↓
[PC-Server: UartManager]
    │  UART Frame: AA 02 03 53 3A 31 65  (CMD: S:1)
    ↓
[ATmega128-S] → 곡 1번 재생
    │  UART Frame: AA 03 02 4F 4B 47  (ACK: OK)
    ↓
[PC-Server: UartManager] → 로그 표시
```

### 시나리오 3 — Heartbeat 타임아웃으로 Client 해제

```
[PC-Server]
    │  5초마다 HEARTBEAT 프레임 전송 (TYPE=0x02)
    │
    │  [Client-2 응답 없음 → 10초 경과]
    ↓
[PC-Server: TcpManager]
    │  Client-2 소켓 강제 해제
    │  SYSTEM "DISC:02" 나머지 Client에게 브로드캐스트
    ↓
[GUI] ID:2 상태 → 대기 중 ○
```

---

## 8. 23↔24 연동 가이드 (학생용)

> 이 섹션은 `23_UART_TCPIP_Repeater.py` (Python)와
> `24_ATmega128_Platform.c` (ATmega128) 간의 실제 동작 흐름을
> 학생 관점에서 설명합니다.  
> 프레임 구조 자체는 §3을 참고하세요.

### 8.1 통신 흐름 개요

```
[23_UART_TCPIP_Repeater.py]            [24_ATmega128_Platform.c]
        Python (PC)                         ATmega128 (MCU)
             │                                    │
             │◄─── DATA 프레임 (센서/ADC 값) ──────│ uart_send_frame()
             │      TYPE=0x01                     │ ← app_periodic_task()에서 호출
             │                                    │
             │──── CMD  프레임 (제어 명령)  ───────►│ on_frame_received()
             │      TYPE=0x02                     │ ← 학생이 구현하는 영역
             │                                    │
             │◄─── ACK  프레임 (정상 처리 응답) ────│ uart_send_frame()
             │      TYPE=0x03                     │ ← CMD 처리 후 학생이 전송
             │                                    │
             │◄─── ERR  프레임 (오류 응답) ─────────│ uart_send_frame()
             │      TYPE=0x04                     │ ← CRC 불일치 시 자동 전송
```

#### 역할 분담 요약

| 방향 | TYPE | 누가 보내나 | 어디서 처리하나 |
|------|------|------------|----------------|
| ATmega128 → Python | DATA (0x01) | 24번 C 코드 | Repeater 로그/릴레이 |
| Python → ATmega128 | CMD  (0x02) | Repeater GUI | `on_frame_received()` |
| ATmega128 → Python | ACK  (0x03) | 24번 C 코드 | Repeater 로그 |
| ATmega128 → Python | ERR  (0x04) | 24번 C 코드 (자동) | Repeater 로그 |

---

### 8.2 Python → ATmega128 : CMD 전송 흐름

#### Step 1 — Python Repeater 수동 송신 패널 입력

```
┌─────────────────────────────────────┐
│  수동 송신 (테스트용)                 │
│  대상: ● UART  ○ TCP [ID:1 ▼]       │
│  입력: [L:ON        ]  [전송]        │
└─────────────────────────────────────┘
```

<table width="60%">
<tr><th colspan="2">수동 송신 (테스트용)</th></tr>
<tr><td>대상</td><td>● UART &nbsp; ○ TCP &nbsp; <code>ID:1 ▼</code></td></tr>
<tr><td>입력</td><td><code>L:ON&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</code> &nbsp; <code>[전송]</code></td></tr>
</table>

Python Repeater는 입력한 문자열을 자동으로 **CMD 타입 UART 프레임**으로
인코딩합니다.

#### Step 2 — Python가 생성하는 실제 UART 바이트

페이로드 `L:ON` (4 bytes) 예시:

```
PAYLOAD (ASCII) : L    :    O    N
PAYLOAD (HEX)   : 4C   3A   4F   4E

CRC8 = 0x02 XOR 0x04 XOR 0x4C XOR 0x3A XOR 0x4F XOR 0x4E = 0x71

SOF    TYPE   LENGTH  PAYLOAD          CRC8
0xAA   0x02   0x04    4C 3A 4F 4E      0x71

완성 프레임 (8 bytes): AA 02 04 4C 3A 4F 4E 71
```

#### Step 3 — ATmega128에서 수신하는 값

프레임이 완성되면 `on_frame_received()`가 호출됩니다.

```c
void on_frame_received(const UartFrame *frame)
{
    // frame->type       = 0x02  (UART_TYPE_CMD)
    // frame->length     = 4
    // frame->payload[0] = 'L'  (0x4C)
    // frame->payload[1] = ':'  (0x3A)
    // frame->payload[2] = 'O'  (0x4F)
    // frame->payload[3] = 'N'  (0x4E)

    // ↓ 학생이 이 아래를 구현합니다
    switch (frame->type) {
        case UART_TYPE_CMD:
            if (frame->payload[0] == 'L') { /* LED 제어 */ }
            break;
    }
}
```

---

### 8.3 ATmega128 → Python : DATA 전송 흐름

#### Step 1 — C 코드에서 프레임 전송

`app_periodic_task()` 안에서 `uart_send_frame()`을 호출합니다.

```c
/* 제공된 예시 코드 (ADC0 값 전송) */
uint16_t adc0 = adc_read(0);   /* 예: 512 */

UartFrame f;
f.type      = UART_TYPE_DATA;
f.length    = 7;
f.payload[0] = 'A';            /* 태그 첫 글자 */
f.payload[1] = '0';            /* 채널 번호    */
f.payload[2] = ':';            /* 구분자       */
f.payload[3] = '0' + ((adc0 / 1000) % 10);   /* 천의 자리 */
f.payload[4] = '0' + ((adc0 /  100) % 10);   /* 백의 자리 */
f.payload[5] = '0' + ((adc0 /   10) % 10);   /* 십의 자리 */
f.payload[6] = '0' + ( adc0         % 10);   /* 일의 자리 */
uart_send_frame(&f);
```

#### Step 2 — 실제 UART 전송 바이트

```
PAYLOAD (ASCII) : A    0    :    0    5    1    2
PAYLOAD (HEX)   : 41   30   3A   30   35   31   32

CRC8 = 0x01 XOR 0x07 XOR 0x41 XOR 0x30 XOR 0x3A
            XOR 0x30 XOR 0x35 XOR 0x31 XOR 0x32 = 0x63

SOF    TYPE   LENGTH  PAYLOAD                      CRC8
0xAA   0x01   0x07    41 30 3A 30 35 31 32         0x63

완성 프레임 (11 bytes): AA 01 07 41 30 3A 30 35 31 32 63
```

#### Step 3 — Python Repeater 로그 표시

Python의 `UartFrameParser`가 프레임을 완성하면 `RelayEngine`으로 전달되고
GUI 로그에 표시됩니다.

```
[14:23:01.123] UART→TCP   DATA   A0:0512   → Broadcast
[14:23:01.624] UART→TCP   DATA   A0:0498   → Broadcast
[14:23:02.125] UART→TCP   DATA   A0:0501   → Broadcast
```

---

### 8.4 ACK / ERR 응답 패턴

#### 시나리오 A — CMD 정상 처리 후 ACK 응답

```
Python 전송:      AA 02 04 4C 3A 4F 4E 71   ("L:ON"  CMD)
ATmega128 처리:   LED ON 동작 실행
ATmega128 응답:   AA 03 02 4F 4B 47          ("OK"    ACK)

Python 로그:
  [14:23:01] ← UART   CMD    L:ON
  [14:23:01] → UART   ACK    OK
```

C 코드 작성 예시:

```c
case UART_TYPE_CMD:
    if (strncmp((char*)frame->payload, "L:ON", 4) == 0) {
        /* LED ON 처리 */
        UartFrame ack = { UART_TYPE_ACK, 2, {'O','K'} };
        uart_send_frame(&ack);
    }
    break;
```

#### 시나리오 B — 알 수 없는 CMD → ERR 응답

```
Python 전송:      AA 02 03 58 3A 31 6E       ("X:1"   CMD, 미정의)
ATmega128 응답:   AA 04 05 45 3A 43 4D 44 34 ("E:CMD" ERR)

Python 로그:
  [14:23:01] ← UART   CMD    X:1
  [14:23:01] → UART   ERR    E:CMD
```

C 코드 작성 예시:

```c
default: {
    UartFrame err = { UART_TYPE_ERR, 5,
                      {'E',':','C','M','D'} };
    uart_send_frame(&err);
    break;
}
```

#### 시나리오 C — CRC 불일치 → ERR 자동 응답

CRC8이 맞지 않으면 `uart_parse_byte()` 내부에서 **자동으로** ERR를 전송합니다.
학생이 별도로 처리하지 않아도 됩니다.

```
수신 (CRC 손상): AA 02 04 4C 3A 4F 4E FF    (마지막 FF가 잘못된 CRC)
자동 응답:       AA 04 05 45 3A 43 52 43 ??  ("E:CRC" ERR)
```

---

### 8.5 DATA 페이로드 포맷 규약

#### ATmega128 → Python (TYPE = DATA)

기본 제공 예시에서 사용하는 포맷입니다. **태그 2자 + `:` + 데이터** 규칙을
따르며, 학생이 새 태그를 추가할 수 있습니다.

| 태그 | PAYLOAD 예시 | 설명 | 총 바이트 |
|------|-------------|------|:---------:|
| `A0`~`A7` | `A0:0512` | ADC 채널 n 측정값 (0~1023, 4자리 고정) | 7 |
| `L0` | `L0:Hello World` | LCD 1행 텍스트 (최대 16자, 공백 패딩) | 19 |
| `L1` | `L1:Playing...` | LCD 2행 텍스트 (최대 16자, 공백 패딩) | 19 |
| *(학생 정의)* | `T0:0235` | 온도 센서 등 학생이 직접 추가 | 가변 |

> **포맷 규칙**: `[2자 태그]:[데이터]` — 최대 28 bytes, null 종단 문자 없음

#### Python → ATmega128 (TYPE = CMD)

CMD 페이로드 포맷은 **학생이 직접 정의**합니다. `on_frame_received()` 안에서
`frame->payload`를 파싱하는 코드를 작성하면 됩니다.

| 태그 예시 | PAYLOAD 예시 | 의미 (학생 정의 예시) |
|----------|-------------|----------------------|
| `L:` | `L:ON` / `L:OFF` | LED 켜기/끄기 |
| `S:` | `S:1` ~ `S:9` | 부저 음계 번호 |
| `D:` | `D:Hello` | LCD에 문자열 표시 |
| `R:` | `R:` | 소프트 리셋 |

> 위 표는 예시일 뿐입니다. 실제 CMD는 학생이 자유롭게 설계하세요.

---

### 8.6 학생 테스트 절차

아래 순서로 통신 동작을 확인합니다.

#### 환경 준비

| # | 항목 | 확인 |
|---|------|:----:|
| 1 | ATmega128에 `24_ATmega128_Platform.c` 플래시 | ☐ |
| 2 | USB-UART 변환기로 ATmega128 UART1(PD2/PD3) 연결 | ☐ |
| 3 | PC에서 `23_UART_TCPIP_Repeater.py` 실행 | ☐ |
| 4 | UART 설정 패널: COM 포트 선택, Baud=**9600** | ☐ |
| 5 | [연결] 버튼 클릭 → 상태: ● 연결됨 | ☐ |

#### STEP 1 — ATmega128 → Python DATA 수신 확인

| # | 기대 동작 | 확인 |
|---|----------|:----:|
| 6 | 로그에 `UART→  DATA  A0:XXXX` 가 500ms마다 표시됨 | ☐ |
| 7 | 가변저항을 돌리면 ADC 값이 변하는지 확인 | ☐ |

#### STEP 2 — Python → ATmega128 CMD 전송 확인

| # | 기대 동작 | 확인 |
|---|----------|:----:|
| 8 | 수동 송신 패널에서 정의한 CMD 입력 후 [전송] | ☐ |
| 9 | ATmega128에서 해당 동작 실행 (LED, 부저 등) | ☐ |
| 10 | 로그에 `UART←  ACK  OK` 표시됨 | ☐ |

#### STEP 3 — 오류 처리 확인

| # | 기대 동작 | 확인 |
|---|----------|:----:|
| 11 | 미정의 CMD 전송 시 로그에 `UART←  ERR  E:CMD` 표시됨 | ☐ |
| 12 | 통신 중 연결 해제/재연결 후 DATA 수신 재개됨 | ☐ |

---

## 9. 변경 이력

| 버전 | 날짜 | 내용 | 작성자 |
|------|------|------|--------|
| v1.0 | 2026-05-22 | 최초 작성 | KoPT_HighTech |
| v1.1 | 2026-05-22 | §8 23↔24 연동 가이드 추가, 적용 파일에 24번 C 코드 추가 | KoPT_HighTech |
