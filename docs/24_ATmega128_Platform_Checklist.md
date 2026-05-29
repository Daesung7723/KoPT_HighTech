# 24_ATmega128_Platform 개발 체크리스트

> 프로젝트: KoPT_HighTech / ATmega128  
> 목적: 23번 Python Repeater와 통신하는 ATmega128 플랫폼 기본 코드 제공 (학생 실습용)  
> 작성일: 2026-05-22  
> 적용 프로세스: 소프트웨어 설계 프로세스 (요구사항 → 아키텍처 → 인터페이스 → 상세 설계 → 구현 → 검토)

---

## 산출물 목록

| 번호 | 파일 | 경로 | 상태 |
| :--: | ---- | ---- | ---- |
| 1 | `24_ATmega128_Platform_Checklist.md` | `docs/` | ✅ 완료 |
| 2 | `24_ATmega128_Platform.c` | `ATmega128/` | ✅ 완료 |

---

## Phase 1 — 요구사항 분석

### 1-1. 기능 요구사항 (FR)

| ID | 요구사항 | 우선순위 | 상태 |
| -- | -------- | :------: | ---- |
| FR-01 | 23번 Python Repeater와 UART 바이너리 프레임으로 통신 | 필수 | ✅ |
| FR-02 | UART_Frame_Protocol_Spec.md 정의 프레임 구조 그대로 사용 | 필수 | ✅ |
| FR-03 | 학생에게 기본 플랫폼 제공 (내부 Command/Protocol은 학생 구현) | 필수 | ✅ |
| FR-04 | ADC 초기화 및 채널별 읽기 함수 제공 | 필수 | ✅ |
| FR-05 | Timer 초기화 및 주기적 작업 트리거 제공 | 필수 | ✅ |
| FR-06 | LCD(HD44780) 초기화 및 출력 함수 제공 | 필수 | ✅ |
| FR-07 | 4x4 키 매트릭스 스캔 함수 제공 | 필수 | ✅ |
| FR-08 | PWM 출력 함수 제공 (부저/서보모터 등) | 필수 | ✅ |
| FR-09 | SPI 마스터 초기화 및 전송 함수 제공 | 필수 | ✅ |
| FR-10 | I2C(TWI) 마스터 초기화 및 통신 함수 제공 | 필수 | ✅ |
| FR-11 | 학생을 위한 상세 주석 포함 | 필수 | ✅ |

### 1-2. 제공 vs 학생 구현 경계

| 항목 | 구분 | 비고 |
| ---- | :--: | ---- |
| UART1 초기화 | 제공 | Baud Rate 설정, RXCIE 인터럽트 활성화 |
| UART1 RX ISR | 제공 | 바이트 수신 후 상태 머신 파서 호출 |
| 프레임 파서 (상태 머신) | 제공 | SOF 탐색 → CRC 검증까지 자동 처리 |
| 프레임 인코딩/전송 | 제공 | `uart_send_frame()` 완성형 제공 |
| CRC8 계산 | 제공 | XOR 누적 방식 |
| CMD 수신 후 분기 처리 | **골격만 제공** | `switch-case` 뼈대 + TODO 마커 |
| DATA 전송 예시 | 제공 | ADC 읽어 프레임 전송 1개 완성 예시 |
| 나머지 DATA/CMD 처리 | **학생 구현** | on_frame_received() 내부 |
| ACK/ERR 응답 | 제공 (예시 1개) | 나머지는 학생 구현 |
| ADC 초기화/읽기 | 제공 | |
| Timer 초기화/틱 | 제공 | 10ms CTC 인터럽트 |
| LCD 모든 함수 | 제공 | init, clear, goto_xy, print, printf |
| PWM (부저) 초기화/제어 | 제공 | Timer3 OC3A (PE3) |
| SPI 초기화/전송 | 제공 | 하드웨어 SPI (PORTB 충돌 주의) |
| I2C 초기화/통신 | 제공 | 하드웨어 TWI (PD0/PD1) |
| 키 매트릭스 스캔 | 제공 | Non-blocking, Edge 검출 |
| 응용 로직 전체 | **학생 구현** | |

### 1-3. 하드웨어 핀 배치 확정

```
┌─────────────────────────────────────────────────────┐
│          ATmega128A 핀 배치 (학생 실습 보드 기준)        │
├─────────────────┬───────────────────────────────────┤
│  주변장치        │  핀                                │
├─────────────────┼───────────────────────────────────┤
│  LCD 데이터     │  PORTB  (PB0-PB7 = D0-D7)          │
│  LCD RS         │  PA0                               │
│  LCD RW         │  PA1                               │
│  LCD E(Enable)  │  PA2                               │
├─────────────────┼───────────────────────────────────┤
│  ADC 입력       │  PF0~PF7  (ADC0~ADC7)             │
├─────────────────┼───────────────────────────────────┤
│  UART1 TXD1     │  PD3                               │
│  UART1 RXD1     │  PD2                               │
├─────────────────┼───────────────────────────────────┤
│  부저(OC3A)     │  PE3  (Timer3 PWM 출력)            │
│  서보/DC모터    │  PE3 또는 Timer1 OC1A(PB5) ※주의   │
├─────────────────┼───────────────────────────────────┤
│  KeyMatrix C0   │  PC0  (열 출력, 액티브 Low)        │
│  KeyMatrix C1   │  PC1                               │
│  KeyMatrix C2   │  PC2                               │
│  KeyMatrix C3   │  PC3                               │
│  KeyMatrix L0   │  PC4  (행 입력, 내부 풀업)         │
│  KeyMatrix L1   │  PC5                               │
│  KeyMatrix L2   │  PC6                               │
│  KeyMatrix L3   │  PC7                               │
├─────────────────┼───────────────────────────────────┤
│  SPI SCK        │  PB1  ⚠ LCD 데이터 포트와 겹침     │
│  SPI MOSI       │  PB2  ⚠ LCD와 동시 사용 불가       │
│  SPI MISO       │  PB3  ⚠ SPI 사용 시 LCD→PORTA    │
│  SPI SS         │  PB0  ⚠ 배선 변경 필요             │
├─────────────────┼───────────────────────────────────┤
│  I2C SDA (TWI)  │  PD1  (LCD, UART와 겹치지 않음)   │
│  I2C SCL (TWI)  │  PD0                               │
└─────────────────┴───────────────────────────────────┘
```

<table width="100%">
<tr><th colspan="2" align="center">ATmega128A 핀 배치 (학생 실습 보드 기준)</th></tr>
<tr><th>주변장치</th><th>핀</th></tr>
<tr><td>LCD 데이터</td><td>PORTB (PB0-PB7 = D0-D7)</td></tr>
<tr><td>LCD RS</td><td>PA0</td></tr>
<tr><td>LCD RW</td><td>PA1</td></tr>
<tr><td>LCD E(Enable)</td><td>PA2</td></tr>
<tr><th>ADC 입력</th><th>PF0~PF7 (ADC0~ADC7)</th></tr>
<tr><td>UART1 TXD1</td><td>PD3</td></tr>
<tr><td>UART1 RXD1</td><td>PD2</td></tr>
<tr><th>부저 (OC3A)</th><th>PE3 — Timer3 PWM 출력</th></tr>
<tr><td>서보/DC모터</td><td>PE3 또는 Timer1 OC1A (PB5) ⚠ 주의</td></tr>
<tr><th>KeyMatrix C0~C3</th><th>PC0~PC3 (열 출력, 액티브 Low)</th></tr>
<tr><td>KeyMatrix L0~L3</td><td>PC4~PC7 (행 입력, 내부 풀업)</td></tr>
<tr><th>SPI SCK</th><th>PB1 ⚠ LCD 데이터 포트와 겹침</th></tr>
<tr><td>SPI MOSI</td><td>PB2 ⚠ LCD와 동시 사용 불가</td></tr>
<tr><td>SPI MISO</td><td>PB3 ⚠ SPI 사용 시 LCD→PORTA</td></tr>
<tr><td>SPI SS</td><td>PB0 ⚠ 배선 변경 필요</td></tr>
<tr><th>I2C SDA (TWI)</th><th>PD1 (LCD, UART와 겹치지 않음)</th></tr>
<tr><td>I2C SCL (TWI)</td><td>PD0</td></tr>
</table>

> **⚠ SPI 사용 시**: PORTB를 SPI가 점유하므로 LCD 데이터 핀을 PORTA로 변경하고
> LCD 제어 핀은 PORTB 하위 3비트(PB0-PB2)로 변경해야 한다. (DC_MTR_ADC_PWM_CTRL.c 참고)

---

## Phase 2 — 아키텍처 설계

### 2-1. 단일 파일 계층 구조

```
┌─────────────────────────────────────────────────────────┐
│              Application Layer  (학생 구현 영역)           │
│   on_frame_received()  |  app_periodic_task()  |  main() │
├─────────────────────────────────────────────────────────┤
│              Platform API Layer  (제공)                    │
│  uart_send_frame()  |  lcd_printf()  |  adc_read()       │
│  pwm_tone()  |  spi_transfer()  |  i2c_write()  |  ...   │
├───────────────────────────────┬─────────────────────────┤
│   HAL (하드웨어 추상화)         │  ISR / 인터럽트 처리      │
│  uart1_init(), adc_init()     │  USART1_RX_vect          │
│  timer1_init(), lcd_init()    │  TIMER1_COMPA_vect        │
│  spi_init(), i2c_init()       │                          │
└───────────────────────────────┴─────────────────────────┘
```

<table width="100%">
<tr><td align="center" colspan="2"><b>Application Layer &nbsp;(학생 구현 영역)</b><br>
<code>on_frame_received()</code> &nbsp;|&nbsp; <code>app_periodic_task()</code> &nbsp;|&nbsp; <code>main()</code></td></tr>
<tr><td align="center" colspan="2"><b>Platform API Layer &nbsp;(제공)</b><br>
<code>uart_send_frame()</code> &nbsp;|&nbsp; <code>lcd_printf()</code> &nbsp;|&nbsp; <code>adc_read()</code><br>
<code>pwm_tone()</code> &nbsp;|&nbsp; <code>spi_transfer()</code> &nbsp;|&nbsp; <code>i2c_write()</code> &nbsp;|&nbsp; ...</td></tr>
<tr>
<td align="center" width="60%"><b>HAL (하드웨어 추상화)</b><br>
<code>uart1_init()</code>, <code>adc_init()</code><br>
<code>timer1_init()</code>, <code>lcd_init()</code><br>
<code>spi_init()</code>, <code>i2c_init()</code></td>
<td align="center" width="40%"><b>ISR / 인터럽트 처리</b><br>
<code>USART1_RX_vect</code><br>
<code>TIMER1_COMPA_vect</code></td>
</tr>
</table>

### 2-2. 섹션 구조 (단일 .c 파일 내부)

| # | 섹션 이름 | 설명 |
|---|----------|------|
| 0 | 파일 헤더 | 목적, 핀 배치, 사용법, 프로토콜 요약 |
| 1 | 시스템 설정 | F_CPU, includes, 버전 |
| 2 | 핀 & 상수 정의 | 매크로, 프레임 상수, 타이머 상수 |
| 3 | 데이터 구조 | UartFrame, RxState enum |
| 4 | 전역 변수 | 플래그, 버퍼, 카운터 |
| 5 | 함수 프로토타입 | 전체 함수 선언 |
| 6 | UART1 프레임 통신 | ISR + 파서 + 인코더 + 전송 |
| 7 | ADC | 초기화, 채널 읽기 |
| 8 | Timer (시스템 틱) | Timer1 CTC, ISR, 틱 카운터 |
| 9 | LCD (HD44780) | init, command, data, print, printf |
| 10 | PWM (Timer3) | 부저/서보 주파수 제어 |
| 11 | SPI 마스터 | 초기화, 바이트 전송 |
| 12 | I2C/TWI 마스터 | 초기화, Start/Stop/Write/Read |
| 13 | 키 매트릭스 | 초기화, Non-blocking 스캔 |
| 14 | 시스템 초기화 | ports_init(), system_init() |
| 15 | 애플리케이션 계층 | on_frame_received(), app_periodic_task() |
| 16 | main() | 이벤트 드리븐 메인 루프 |

---

## Phase 3 — 인터페이스 설계

### 3-1. 함수 시그니처 목록

| 함수 | 반환 | 매개변수 | 섹션 |
| ---- | ---- | -------- | ---- |
| `uart1_init(baud)` | void | uint32_t baud | 6 |
| `uart1_putc(c)` | void | char c | 6 |
| `uart_crc8(type, len, payload)` | uint8_t | - | 6 |
| `uart_send_frame(frame)` | void | const UartFrame* | 6 |
| `uart_parse_byte(byte, out)` | uint8_t | uint8_t, UartFrame* | 6 |
| `adc_init()` | void | - | 7 |
| `adc_read(ch)` | uint16_t | uint8_t ch | 7 |
| `timer1_init_systick()` | void | - | 8 |
| `get_systick()` | uint32_t | - | 8 |
| `lcd_init()` | void | - | 9 |
| `lcd_command(cmd)` | void | uint8_t cmd | 9 |
| `lcd_data(data)` | void | uint8_t data | 9 |
| `lcd_clear()` | void | - | 9 |
| `lcd_goto_xy(row, col)` | void | uint8_t, uint8_t | 9 |
| `lcd_print(str)` | void | const char* | 9 |
| `lcd_printf(row, fmt, ...)` | void | uint8_t, const char*, ... | 9 |
| `pwm_init()` | void | - | 10 |
| `pwm_tone(freq)` | void | uint16_t freq | 10 |
| `pwm_off()` | void | - | 10 |
| `spi_init()` | void | - | 11 |
| `spi_transfer(data)` | uint8_t | uint8_t data | 11 |
| `i2c_init()` | void | - | 12 |
| `i2c_start(addr)` | uint8_t | uint8_t addr | 12 |
| `i2c_stop()` | void | - | 12 |
| `i2c_write(data)` | uint8_t | uint8_t data | 12 |
| `i2c_read_ack()` | uint8_t | - | 12 |
| `i2c_read_nack()` | uint8_t | - | 12 |
| `keymatrix_init()` | void | - | 13 |
| `keymatrix_scan()` | uint8_t | - | 13 |
| `ports_init()` | void | - | 14 |
| `system_init()` | void | - | 14 |
| `on_frame_received(frame)` | void | const UartFrame* | 15 ★학생 |
| `app_periodic_task()` | void | - | 15 ★학생 |

### 3-2. 핵심 데이터 구조

```c
/* UART 프레임 구조체 */
typedef struct {
    uint8_t type;                        // 0x01=DATA, 0x02=CMD, 0x03=ACK, 0x04=ERR
    uint8_t length;                      // PAYLOAD 바이트 수 (0~28)
    uint8_t payload[UART_FRAME_MAX_PAYLOAD]; // 최대 28 bytes
} UartFrame;

/* 수신 파서 상태 */
typedef enum {
    RX_WAIT_SOF, RX_WAIT_TYPE, RX_WAIT_LENGTH,
    RX_WAIT_PAYLOAD, RX_WAIT_CRC
} RxState;
```

### 3-3. 주요 상수/매크로

| 상수 | 값 | 설명 |
|------|----|------|
| `UART_SOF` | `0xAA` | 프레임 시작 바이트 |
| `UART_TYPE_DATA` | `0x01` | 데이터 프레임 |
| `UART_TYPE_CMD` | `0x02` | 명령 프레임 |
| `UART_TYPE_ACK` | `0x03` | 응답 프레임 |
| `UART_TYPE_ERR` | `0x04` | 오류 프레임 |
| `UART_FRAME_MAX_PAYLOAD` | `28` | 최대 페이로드 크기 |
| `BAUD_RATE` | `9600` | UART 기본 속도 |
| `SYSTICK_PERIOD_MS` | `10` | 시스템 틱 주기 (ms) |
| `KEY_NONE` | `255` | 키 입력 없음 |

---

## Phase 4 — 상세 설계

### 4-1. ISR 설계

| ISR | 트리거 | 동작 |
|-----|--------|------|
| `USART1_RX_vect` | UART1 수신 완료 | `uart_parse_byte()` 호출 → 프레임 완성 시 `g_frame_ready=1` |
| `TIMER1_COMPA_vect` | Timer1 CTC (10ms) | `g_systick` 증가, `g_periodic_flag` 설정 (500ms 주기) |

### 4-2. 메인 루프 이벤트 처리 순서

```
while(1) {
    ① UART 프레임 수신 확인  → on_frame_received() 호출
    ② 주기적 작업 플래그 확인 → app_periodic_task() 호출
    ③ 키 입력 스캔           → 학생 처리 코드 (TODO)
    ④ 기타 이벤트           → 학생 추가 코드 (TODO)
}
```

### 4-3. Timer 설계

| Timer | 모드 | Prescaler | OCR/ICR | 주기/용도 |
|-------|------|-----------|---------|---------|
| Timer1 | CTC (Mode 4) | 64 | OCR1A=2499 | 10ms 시스템 틱 |
| Timer3 | Fast PWM (Mode 14) | 64 | ICR3=가변 | 부저/서보 PWM 출력 |

> **Timer 충돌 주의**: DC 모터 PWM(OC1A=PB5)에 Timer1을 사용하면 시스템 틱이 동작하지 않음.  
> DC 모터 제어가 필요하면 Timer3 외에 다른 OC 핀을 검토하거나 Timer1 모드를 변경할 것.

### 4-4. UART 파서 상태 전이

```
RX_WAIT_SOF ──[0xAA]──→ RX_WAIT_TYPE
RX_WAIT_TYPE ──[any]──→ RX_WAIT_LENGTH
RX_WAIT_LENGTH ──[0]──→ RX_WAIT_CRC
RX_WAIT_LENGTH ──[1-28]──→ RX_WAIT_PAYLOAD
RX_WAIT_LENGTH ──[>28]──→ RX_WAIT_SOF (재동기화)
RX_WAIT_PAYLOAD ──[N bytes 수신 완료]──→ RX_WAIT_CRC
RX_WAIT_CRC ──[CRC 일치]──→ g_frame_ready=1, RX_WAIT_SOF
RX_WAIT_CRC ──[CRC 불일치]──→ ERR 응답 전송, RX_WAIT_SOF
```

---

## Phase 5 — 구현 체크리스트

### 5-1. 섹션별 구현 항목

| # | 섹션 | 항목 | 상태 |
|---|------|------|------|
| 6-1 | UART1 | `uart1_init()` 구현 (UBRR 계산, 인터럽트 설정) | ✅ |
| 6-2 | UART1 | `uart1_putc()` 구현 (UDRE 폴링 전송) | ✅ |
| 6-3 | UART1 | `uart_crc8()` 구현 (XOR 누적) | ✅ |
| 6-4 | UART1 | `uart_send_frame()` 구현 (인코딩+전송) | ✅ |
| 6-5 | UART1 | `uart_parse_byte()` 상태 머신 구현 | ✅ |
| 6-6 | UART1 | `ISR(USART1_RX_vect)` 구현 | ✅ |
| 7-1 | ADC | `adc_init()` 구현 (ADMUX, ADCSRA) | ✅ |
| 7-2 | ADC | `adc_read(ch)` 구현 (채널 선택, ADSC, 결과 반환) | ✅ |
| 8-1 | Timer | `timer1_init_systick()` 구현 (CTC, OCR1A=2499) | ✅ |
| 8-2 | Timer | `get_systick()` 구현 (원자적 읽기) | ✅ |
| 8-3 | Timer | `ISR(TIMER1_COMPA_vect)` 구현 (500ms 주기 플래그) | ✅ |
| 9-1 | LCD | `lcd_init()` 구현 (HD44780 초기화 시퀀스) | ✅ |
| 9-2 | LCD | `lcd_command()` / `lcd_data()` 구현 | ✅ |
| 9-3 | LCD | `lcd_clear()` / `lcd_goto_xy()` 구현 | ✅ |
| 9-4 | LCD | `lcd_print()` 구현 | ✅ |
| 9-5 | LCD | `lcd_printf()` 구현 (sprintf 래퍼) | ✅ |
| 10-1 | PWM | `pwm_init()` 구현 (Timer3 Fast PWM) | ✅ |
| 10-2 | PWM | `pwm_tone(freq)` / `pwm_off()` 구현 | ✅ |
| 11-1 | SPI | `spi_init()` 구현 (SPCR, 핀 방향 설정) | ✅ |
| 11-2 | SPI | `spi_transfer()` 구현 (SPDR 폴링) | ✅ |
| 12-1 | I2C | `i2c_init()` 구현 (TWBR, 100kHz) | ✅ |
| 12-2 | I2C | `i2c_start()` / `i2c_stop()` 구현 | ✅ |
| 12-3 | I2C | `i2c_write()` / `i2c_read_ack()` / `i2c_read_nack()` 구현 | ✅ |
| 13-1 | KeyMatrix | `keymatrix_init()` 구현 (DDRC, PORTC 설정) | ✅ |
| 13-2 | KeyMatrix | `keymatrix_scan()` Non-blocking 구현 | ✅ |
| 14-1 | Init | `ports_init()` 구현 (모든 포트 방향/초기값) | ✅ |
| 14-2 | Init | `system_init()` 구현 (모든 초기화 함수 호출) | ✅ |
| 15-1 | App | `on_frame_received()` 골격 + TODO 마커 작성 | ✅ |
| 15-2 | App | `app_periodic_task()` 골격 + ADC 전송 예시 + TODO | ✅ |
| 16-1 | Main | `main()` 이벤트 드리븐 루프 작성 | ✅ |

### 5-2. 주석 완성도 기준

| 항목 | 기준 |
|------|------|
| 각 함수 | 목적, 매개변수, 반환값 설명 |
| 레지스터 설정 | 비트 이름, 역할, 계산 근거 포함 |
| ISR | 트리거 조건, 처리 내용 설명 |
| TODO 마커 | 학생이 채울 내용을 구체적으로 명시 |
| 프로토콜 연동 | 프레임 구조 도식 또는 예시 포함 |

---

## Phase 6 — 검토 항목

| # | 검토 항목 | 상태 |
|---|----------|------|
| 6-1 | UART 프레임 구조가 KPT-PROTO-001 명세서와 일치하는지 확인 | ✅ |
| 6-2 | CRC8 계산 방식이 명세서(XOR 누적)와 동일한지 확인 | ✅ |
| 6-3 | 핀 배치가 기존 코드(22번 등)와 일치하는지 확인 | ✅ |
| 6-4 | Timer1 CTC OCR1A=2499 → 10ms 계산 검증 | ✅ |
| 6-5 | 모든 TODO 마커에 구체적인 작성 가이드가 포함됐는지 확인 | ✅ |
| 6-6 | 전역 변수 volatile 선언 누락 없는지 확인 | ✅ |
| 6-7 | ISR 내부에서 무거운 연산 없는지 확인 (플래그 방식 사용) | ✅ |
| 6-8 | SPI/LCD 핀 충돌 주의사항이 코드 내 주석으로 명시됐는지 확인 | ✅ |

---

## 핵심 설계 결정 사항

| 결정 | 이유 |
|------|------|
| 단일 .c 파일 | 학생 배포 단순화, 빌드 설정 최소화 |
| 이벤트 드리븐 메인 루프 | 폴링 방식 대비 반응성 우수, 기존 코드 패턴과 일치 |
| Timer1 = 시스템 틱 전용 | 기존 코드(22번)와 동일, Timer3로 PWM 분리 |
| on_frame_received() 분리 | 통신 계층과 응용 로직의 명확한 분리, 학생이 채울 위치 명확 |
| App Layer를 별도 섹션으로 | 학생이 수정해야 할 코드 범위를 한눈에 파악 가능 |
| I2C는 하드웨어 TWI | PD0/PD1 사용, LCD/UART 핀과 충돌 없음 |
| SPI 충돌 경고 포함 | PB0-PB3가 LCD PORTB와 겹치므로 주석으로 명시 |

---

## 변경 이력

| 버전 | 날짜 | 내용 |
| ---- | ---- | ---- |
| v0.1 | 2026-05-22 | 초기 체크리스트 작성 (Phase 1~4) |
| v0.2 | 2026-05-22 | Phase 5~6 완료, 산출물 확정 |
