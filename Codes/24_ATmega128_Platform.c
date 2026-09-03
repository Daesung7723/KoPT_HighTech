/*
 * ============================================================
 *  24_ATmega128_Platform.c
 *  ATmega128 통신 플랫폼 기본 코드  (학생 실습용)
 * ============================================================
 *
 * [목적]
 *   이 파일은 ATmega128 마이크로컨트롤러 실습을 위한 기본 플랫폼 코드입니다.
 *   23_UART_TCPIP_Repeater.py (PC 측 프로그램)와 통신하며, 학생들은 이
 *   코드를 기반으로 자신만의 기능을 구현합니다.
 *
 * [이 파일에서 제공하는 것]  ← 수정하지 않아도 됩니다
 *   - UART1 바이너리 프레임 송수신 (KPT-PROTO-001 프레임 프로토콜 준수)
 *   - ADC 초기화 및 채널별 읽기
 *   - Timer1 시스템 틱 (10ms 주기 인터럽트)
 *   - Timer3 PWM 출력 (부저/서보모터 등)
 *   - LCD(HD44780) 초기화 및 출력 함수
 *   - 4x4 키 매트릭스 스캔
 *   - SPI 마스터 초기화 및 전송
 *   - I2C(TWI) 마스터 초기화 및 통신
 *
 * [학생이 직접 구현해야 하는 것]  ← 여기를 작성하세요
 *   1. on_frame_received() 함수: PC에서 받은 CMD/DATA 처리
 *   2. app_periodic_task()  함수: 주기적 센서 읽기, DATA 전송 등
 *   3. main() 루프 내 TODO 영역: 키 입력 처리 등 추가 로직
 *
 * [사용 방법]
 *   1. 파일 하단의 "SECTION 15: 애플리케이션 계층"을 찾으세요.
 *   2. on_frame_received() 안의 TODO를 채워 CMD 처리를 구현하세요.
 *   3. app_periodic_task() 안의 TODO를 채워 주기적 동작을 구현하세요.
 *   4. main()의 while(1) 안에 키 입력 등 추가 이벤트를 작성하세요.
 *
 * [통신 프로토콜 요약]  (상세: docs/UART_Frame_Protocol_Spec.md)
 *
 *   UART 프레임 구조:
 *   ┌──────┬───────┬────────┬─────────────────┬──────┐
 *   │ SOF  │ TYPE  │ LENGTH │    PAYLOAD       │ CRC8 │
 *   │ 0xAA │ 1byte │  1byte │ 0 ~ 28 bytes    │1byte │
 *   └──────┴───────┴────────┴─────────────────┴──────┘
 *   - TYPE: 0x01=DATA  0x02=CMD  0x03=ACK  0x04=ERR
 *   - CRC8: TYPE XOR LENGTH XOR PAYLOAD[0] XOR ... XOR PAYLOAD[N-1]
 *
 * [하드웨어 핀 배치]
 *   LCD 데이터 (D0-D7) : PORTB (PB0-PB7)
 *   LCD RS             : PA0
 *   LCD RW             : PA1
 *   LCD E(Enable)      : PA2
 *   ADC 입력           : PF0~PF7  (ADC0~ADC7)
 *   UART1 TXD1/RXD1    : PD3/PD2
 *   부저(Buzzer)       : PE3  (OC3A, Timer3 PWM 출력)
 *   KeyMatrix 열 C0-C3 : PC0-PC3 (출력, 액티브 Low)
 *   KeyMatrix 행 L0-L3 : PC4-PC7 (입력, 내부 풀업)
 *   SPI SCK/MOSI/MISO  : PB1/PB2/PB3  ⚠ LCD와 핀 충돌 (주의사항 11절 참고)
 *   SPI SS             : PB0           ⚠ LCD와 핀 충돌
 *   I2C SDA/SCL (TWI)  : PD1/PD0
 *
 * Target  : ATmega128A
 * Crystal : 16MHz
 * Compiler: AVR-GCC (avr-libc)
 * Version : v1.0  (2026-05-22)
 * ============================================================
 */


/* ============================================================
 *  SECTION 1: 시스템 설정 및 헤더 포함
 * ============================================================ */

/*
 * F_CPU: ATmega128의 동작 클록 주파수 (Hz)
 * 이 값은 _delay_ms(), _delay_us(), UART 보레이트 계산 등에 사용됩니다.
 * #include 이전에 반드시 정의해야 합니다.
 */
#define F_CPU 16000000UL

#include <avr/io.h>          /* AVR 레지스터 정의 (PORTB, DDRB, TCCR1A 등) */
#include <avr/interrupt.h>   /* 인터럽트 매크로 (sei(), cli(), ISR()) */
#include <util/delay.h>      /* _delay_ms(), _delay_us() */
#include <util/twi.h>        /* I2C 상태 코드 (TW_START, TW_MT_SLA_ACK 등) */
#include <stdio.h>           /* sprintf(), snprintf() */
#include <string.h>          /* memcpy(), strlen(), strcpy() */
#include <stdint.h>          /* uint8_t, uint16_t, uint32_t 타입 */


/* ============================================================
 *  SECTION 2: 핀 정의 및 상수
 * ============================================================ */

/* --- LCD 핀 (HD44780, 8비트 모드) --- */
#define LCD_DATA_PORT  PORTB   /* LCD 데이터 버스 D0-D7 출력 포트 */
#define LCD_DATA_DDR   DDRB    /* LCD 데이터 버스 방향 레지스터 */
#define LCD_CTRL_PORT  PORTA   /* LCD 제어 핀 출력 포트 */
#define LCD_CTRL_DDR   DDRA    /* LCD 제어 핀 방향 레지스터 */
#define LCD_RS         0       /* PA0: Register Select (0=명령, 1=데이터) */
#define LCD_RW         1       /* PA1: Read/Write      (항상 0=쓰기 고정) */
#define LCD_E          2       /* PA2: Enable          (High→Low 엣지에서 데이터 래치) */
#define LCD_COLS       16      /* LCD 1줄 최대 문자 수 */
#define LCD_ROWS       2       /* LCD 줄 수 */

/* --- ADC --- */
#define ADC_VREF_AVCC  (1 << REFS0) /* 기준 전압: AVCC(5V) 사용 */

/* --- UART1 --- */
#define BAUD_RATE      9600UL  /* 보레이트 (PC Repeater와 동일해야 함) */

/* --- UART 프레임 프로토콜 상수 (KPT-PROTO-001) --- */
#define UART_SOF              0xAA  /* Start of Frame: 프레임 시작 식별 바이트 */
#define UART_FRAME_MAX_PAYLOAD  28  /* 최대 페이로드 크기 (ATmega128 버퍼 32B - 헤더 4B) */
#define UART_TYPE_DATA        0x01  /* 일반 데이터 (센서값, 문자열 등) */
#define UART_TYPE_CMD         0x02  /* 명령 (PC → ATmega128 동작 제어) */
#define UART_TYPE_ACK         0x03  /* 응답 (명령 수신 확인) */
#define UART_TYPE_ERR         0x04  /* 오류 (CRC 불일치, 알 수 없는 명령 등) */

/* --- Timer1 시스템 틱 --- */
/*
 * 시스템 틱 주기 계산:
 *   Prescaler = 64
 *   OCR1A = (F_CPU / Prescaler / 틱_주파수) - 1
 *         = (16,000,000 / 64 / 100) - 1 = 2499
 *   인터럽트 주기 = 64 × (2499+1) / 16,000,000 = 0.01초 = 10ms
 */
#define TIMER1_PRESCALER_64   ((1 << CS11) | (1 << CS10))
#define TIMER1_OCR_10MS       2499
#define SYSTICK_PERIOD_MS     10    /* 틱 1회당 경과 시간 (ms) */
#define PERIODIC_INTERVAL_MS  500  /* app_periodic_task() 호출 주기 (ms) */

/* --- Timer3 PWM (부저/서보모터) --- */
/*
 * Fast PWM Mode 14 (TOP=ICR3), Prescaler=64, OC3A 출력 (PE3)
 * 주파수 F의 PWM: ICR3 = (F_CPU / 64 / F) - 1
 * 예) 1000Hz: ICR3 = (16,000,000 / 64 / 1000) - 1 = 249
 */
#define PWM_PRESCALER_64  ((1 << CS31) | (1 << CS30))

/* --- 키 매트릭스 --- */
#define KEY_NONE  255   /* 눌린 키 없음 */

/* --- I2C 속도 ---
 * 100kHz I2C: TWBR = (F_CPU / SCL_FREQ - 16) / 2 = (16MHz/100kHz - 16) / 2 = 72
 */
#define I2C_TWBR_100KHZ  72

/* --- LED (예시 포트, 학생이 수정 가능) --- */
/*
 * LED가 연결된 포트를 여기서 정의하세요.
 * 예) PORTD 하위 4비트를 LED로 사용하는 경우:
 * #define LED_PORT   PORTD
 * #define LED_DDR    DDRD
 * #define LED_PIN_0  4
 */


/* ============================================================
 *  SECTION 3: 데이터 구조 정의
 * ============================================================ */

/*
 * UART 프레임 구조체
 * PC와 주고받는 모든 데이터는 이 구조체에 담겨 처리됩니다.
 *
 * 사용 예시 (ACK 응답 전송):
 *   UartFrame ack;
 *   ack.type    = UART_TYPE_ACK;
 *   ack.length  = 2;
 *   ack.payload[0] = 'O';
 *   ack.payload[1] = 'K';
 *   uart_send_frame(&ack);
 */
typedef struct {
    uint8_t type;                            /* 프레임 타입 (UART_TYPE_DATA 등) */
    uint8_t length;                          /* payload 바이트 수 (0~28) */
    uint8_t payload[UART_FRAME_MAX_PAYLOAD]; /* 실제 데이터 */
} UartFrame;

/*
 * UART 수신 파서 상태 (상태 머신)
 * ISR 내부에서 바이트를 한 개씩 받아 상태를 전이합니다.
 */
typedef enum {
    RX_WAIT_SOF,      /* 0xAA를 기다리는 상태 (초기/재동기화 상태) */
    RX_WAIT_TYPE,     /* TYPE 바이트를 기다리는 상태 */
    RX_WAIT_LENGTH,   /* LENGTH 바이트를 기다리는 상태 */
    RX_WAIT_PAYLOAD,  /* PAYLOAD 바이트를 기다리는 상태 */
    RX_WAIT_CRC       /* CRC8 바이트를 기다리는 상태 */
} RxState;


/* ============================================================
 *  SECTION 4: 전역 변수
 * ============================================================ */

/*
 * volatile 키워드:
 *   ISR(인터럽트 서비스 루틴)과 main() 루프가 동시에 접근하는 변수는
 *   반드시 volatile로 선언해야 합니다.
 *   volatile이 없으면 컴파일러 최적화로 인해 ISR에서 변경한 값을
 *   main()이 인식하지 못하는 버그가 발생합니다.
 */

/* --- UART 수신 관련 --- */
volatile uint8_t  g_frame_ready = 0;  /* 완성된 프레임이 있을 때 1로 설정됨 */
volatile UartFrame g_rx_frame;         /* ISR이 완성한 프레임을 저장하는 버퍼 */

/* --- 시스템 틱 관련 --- */
volatile uint32_t g_systick = 0;       /* 10ms마다 1씩 증가하는 틱 카운터 */
volatile uint8_t  g_periodic_flag = 0; /* 500ms마다 1로 설정되는 플래그 */


/* ============================================================
 *  SECTION 5: 함수 프로토타입 (함수 선언)
 * ============================================================ */

/* UART1 프레임 통신 */
void    uart1_init(uint32_t baud);
void    uart1_putc(char c);
uint8_t uart_crc8(uint8_t type, uint8_t length, const uint8_t *payload);
void    uart_send_frame(const UartFrame *frame);
uint8_t uart_parse_byte(uint8_t byte, UartFrame *out_frame);

/* ADC */
void     adc_init(void);
uint16_t adc_read(uint8_t channel);

/* Timer */
void     timer1_init_systick(void);
uint32_t get_systick(void);

/* LCD (HD44780) */
void lcd_init(void);
void lcd_command(uint8_t cmd);
void lcd_data(uint8_t data);
void lcd_clear(void);
void lcd_goto_xy(uint8_t row, uint8_t col);
void lcd_print(const char *str);
void lcd_printf(uint8_t row, const char *fmt, ...);

/* PWM (Timer3, 부저/서보모터) */
void pwm_init(void);
void pwm_tone(uint16_t freq);
void pwm_off(void);

/* SPI 마스터 */
void    spi_init(void);
uint8_t spi_transfer(uint8_t data);

/* I2C/TWI 마스터 */
void    i2c_init(void);
uint8_t i2c_start(uint8_t addr);
void    i2c_stop(void);
uint8_t i2c_write(uint8_t data);
uint8_t i2c_read_ack(void);
uint8_t i2c_read_nack(void);

/* 키 매트릭스 */
void    keymatrix_init(void);
uint8_t keymatrix_scan(void);

/* 시스템 초기화 */
void ports_init(void);
void system_init(void);

/* 애플리케이션 계층 (학생 구현) */
void on_frame_received(const UartFrame *frame);
void app_periodic_task(void);


/* ============================================================
 *  SECTION 6: UART1 프레임 통신
 * ============================================================
 *
 * ATmega128에는 UART0, UART1 두 개의 UART가 있습니다.
 * 이 플랫폼은 PC 통신에 UART1 (TXD1=PD3, RXD1=PD2)을 사용합니다.
 *
 * 프레임 구조:
 *   [SOF=0xAA][TYPE][LENGTH][PAYLOAD 0~28B][CRC8]
 */

/*
 * USART1 수신 인터럽트 서비스 루틴
 *
 * UART1로 1바이트가 수신될 때마다 자동으로 호출됩니다.
 * 수신된 바이트를 uart_parse_byte()에 전달하여 프레임을 조립합니다.
 * 프레임이 완성되면 g_frame_ready = 1로 설정합니다.
 *
 * ⚠ ISR 내부에서는 처리를 최소화하고, 실제 처리는 main() 루프에서 합니다.
 *    (ISR이 길면 다른 인터럽트의 응답이 늦어집니다)
 */
ISR(USART1_RX_vect)
{
    uint8_t byte = UDR1;  /* UART1 수신 데이터 레지스터에서 바이트를 읽음 */
    UartFrame tmp;

    if (uart_parse_byte(byte, &tmp)) {
        /* 프레임이 완성됐을 때: 버퍼에 복사하고 플래그를 올림 */
        g_rx_frame    = tmp;
        g_frame_ready = 1;
    }
}

/*
 * UART1 초기화
 *
 * 매개변수: baud - 보레이트 (예: 9600)
 *
 * UBRR(USART Baud Rate Register) 계산:
 *   UBRR = F_CPU / (16 × Baud) - 1
 *   예) 9600bps, 16MHz: UBRR = 16,000,000 / (16 × 9600) - 1 = 103
 */
void uart1_init(uint32_t baud)
{
    uint16_t ubrr = (uint16_t)(F_CPU / (16UL * baud) - 1);

    /* UBRR1H/L: 보레이트 설정 레지스터 (16비트, 상위/하위 분리 기록) */
    UBRR1H = (uint8_t)(ubrr >> 8);
    UBRR1L = (uint8_t)(ubrr);

    /*
     * UCSR1B: USART1 제어/상태 레지스터 B
     *   RXEN1  (bit4) = 1: 수신 활성화
     *   TXEN1  (bit3) = 1: 송신 활성화
     *   RXCIE1 (bit7) = 1: 수신 완료 인터럽트 활성화 → ISR(USART1_RX_vect) 호출
     */
    UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);

    /*
     * UCSR1C: USART1 제어/상태 레지스터 C
     *   UCSZ11, UCSZ10 = 1, 1 → 데이터 비트 8bit (8N1 포맷)
     *   UCSZ12(UCSR1B) = 0 (기본값): 8bit 완성
     *   정지 비트 1개, 패리티 없음 → 기본값 그대로 사용
     */
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

/*
 * UART1 1바이트 전송
 *
 * UDRE1(USART Data Register Empty) 비트가 1이 될 때까지 기다린 후
 * UDR1(USART Data Register)에 데이터를 씁니다.
 */
void uart1_putc(char c)
{
    while (!(UCSR1A & (1 << UDRE1)));  /* 송신 버퍼 비어있을 때까지 대기 */
    UDR1 = (uint8_t)c;
}

/*
 * CRC8 계산 (XOR 누적 방식)
 *
 * 계산 범위: TYPE + LENGTH + PAYLOAD 전체
 * 공식: CRC8 = TYPE XOR LENGTH XOR PAYLOAD[0] XOR ... XOR PAYLOAD[N-1]
 *
 * ATmega128에서 몇 줄의 코드로 구현 가능하며, 단일 비트 오류를 100% 감지합니다.
 */
uint8_t uart_crc8(uint8_t type, uint8_t length, const uint8_t *payload)
{
    uint8_t crc = type ^ length;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= payload[i];
    }
    return crc;
}

/*
 * UART 프레임 전송
 *
 * UartFrame 구조체를 받아 프레임 형식으로 인코딩하여 전송합니다.
 *
 * 사용 예시:
 *   UartFrame f;
 *   f.type   = UART_TYPE_DATA;
 *   f.length = 7;
 *   memcpy(f.payload, "A0:0512", 7);
 *   uart_send_frame(&f);
 */
void uart_send_frame(const UartFrame *frame)
{
    uint8_t crc = uart_crc8(frame->type, frame->length, frame->payload);

    uart1_putc((char)UART_SOF);      /* SOF 바이트 전송 */
    uart1_putc((char)frame->type);   /* TYPE 전송 */
    uart1_putc((char)frame->length); /* LENGTH 전송 */

    for (uint8_t i = 0; i < frame->length; i++) {
        uart1_putc((char)frame->payload[i]); /* PAYLOAD 바이트 전송 */
    }

    uart1_putc((char)crc); /* CRC8 전송 */
}

/*
 * UART 프레임 파서 (바이트 단위 상태 머신)
 *
 * ISR에서 수신된 바이트를 한 개씩 이 함수에 전달합니다.
 * 내부 상태를 유지하며 프레임을 조립합니다.
 *
 * 반환값: 1 = 프레임 완성 (out_frame에 결과 저장됨)
 *         0 = 아직 프레임 미완성 (계속 바이트 수신 중)
 *
 * 상태 전이:
 *   WAIT_SOF → WAIT_TYPE → WAIT_LENGTH → WAIT_PAYLOAD → WAIT_CRC
 *                              ↑ LENGTH=0이면 WAIT_PAYLOAD 건너뜀
 *   오류(CRC 불일치, LENGTH 초과) → 즉시 WAIT_SOF로 복귀 (재동기화)
 */
uint8_t uart_parse_byte(uint8_t byte, UartFrame *out_frame)
{
    /* static: 함수 호출 간에 상태가 유지됩니다 (전역 변수처럼 동작) */
    static RxState  state   = RX_WAIT_SOF;
    static UartFrame rx_buf;
    static uint8_t  pay_idx = 0;

    switch (state) {

        case RX_WAIT_SOF:
            /* 0xAA가 올 때까지 모든 바이트를 버립니다 (재동기화) */
            if (byte == UART_SOF) {
                state = RX_WAIT_TYPE;
            }
            break;

        case RX_WAIT_TYPE:
            rx_buf.type = byte;
            state = RX_WAIT_LENGTH;
            break;

        case RX_WAIT_LENGTH:
            if (byte > UART_FRAME_MAX_PAYLOAD) {
                /*
                 * LENGTH가 최대값(28)을 초과하면 잘못된 프레임입니다.
                 * 재동기화를 위해 WAIT_SOF로 복귀합니다.
                 */
                state = RX_WAIT_SOF;
            } else {
                rx_buf.length = byte;
                pay_idx = 0;
                /* PAYLOAD가 없으면(length=0) 바로 CRC 대기 상태로 */
                state = (byte == 0) ? RX_WAIT_CRC : RX_WAIT_PAYLOAD;
            }
            break;

        case RX_WAIT_PAYLOAD:
            rx_buf.payload[pay_idx++] = byte;
            if (pay_idx >= rx_buf.length) {
                state = RX_WAIT_CRC;
            }
            break;

        case RX_WAIT_CRC: {
            uint8_t expected = uart_crc8(rx_buf.type, rx_buf.length, rx_buf.payload);
            state = RX_WAIT_SOF; /* 다음 프레임 수신을 위해 초기화 */

            if (byte == expected) {
                /* CRC 일치: 프레임 완성 */
                *out_frame = rx_buf;
                return 1;
            } else {
                /* CRC 불일치: 오류 응답을 PC에 전송 */
                UartFrame err;
                err.type   = UART_TYPE_ERR;
                err.length = 5;
                err.payload[0] = 'E';
                err.payload[1] = ':';
                err.payload[2] = 'C';
                err.payload[3] = 'R';
                err.payload[4] = 'C';
                uart_send_frame(&err);
            }
            break;
        }
    }
    return 0;
}


/* ============================================================
 *  SECTION 7: ADC (아날로그-디지털 변환기)
 * ============================================================
 *
 * ATmega128의 ADC는 10비트 해상도를 가집니다.
 * 변환 결과: 0 (0V) ~ 1023 (AVCC = 5V)
 *
 * ADC 입력 핀: PF0(ADC0) ~ PF7(ADC7)
 * ※ PORTF는 ADC 전용 포트입니다. DDR 설정 없이 자동으로 아날로그 입력 모드가 됩니다.
 *   PORTA를 LCD로 사용하더라도 PORTF는 별개이므로 충돌이 없습니다.
 */

/*
 * ADC 초기화
 *
 * ADMUX: ADC Multiplexer Selection Register
 *   REFS1=0, REFS0=1 → 기준 전압: AVCC(5V)
 *
 * ADCSRA: ADC Control and Status Register A
 *   ADEN=1             → ADC 활성화 (이 비트가 0이면 변환 불가)
 *   ADPS2,ADPS1,ADPS0  → Prescaler 선택
 *   Prescaler = 128: ADC 클록 = 16MHz / 128 = 125kHz (권장 범위: 50~200kHz)
 */
void adc_init(void)
{
    ADMUX  = ADC_VREF_AVCC;                                         /* AVCC 기준 전압 */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); /* ADC ON, Prescaler 128 */
}

/*
 * ADC 채널 읽기
 *
 * 매개변수: channel - 읽을 채널 번호 (0~7, PF0~PF7에 대응)
 * 반환값:  10비트 ADC 결과 (0~1023)
 *
 * 사용 예시:
 *   uint16_t adc0 = adc_read(0);   // PF0 핀의 아날로그 값 읽기
 *   uint16_t adc1 = adc_read(1);   // PF1 핀의 아날로그 값 읽기
 */
uint16_t adc_read(uint8_t channel)
{
    /* ADMUX 하위 5비트(MUX4:0)에 채널 번호를 설정, 상위 기준 전압 설정 유지 */
    ADMUX = (ADMUX & 0xE0) | (channel & 0x1F);

    ADCSRA |= (1 << ADSC);           /* ADSC=1: ADC 변환 시작 */
    while (ADCSRA & (1 << ADSC));    /* 변환 완료까지 대기 (하드웨어가 ADSC를 0으로 클리어함) */

    return ADC; /* ADC: ADCL+ADCH를 합친 16비트 레지스터 (실제 값은 10비트: 0~1023) */
}


/* ============================================================
 *  SECTION 8: Timer1 시스템 틱 (10ms 주기 인터럽트)
 * ============================================================
 *
 * 시스템 틱: 주기적인 작업을 위한 기준 시간을 제공합니다.
 * Timer1을 CTC 모드로 설정하여 10ms마다 인터럽트가 발생하도록 합니다.
 *
 * 활용 방법:
 *   - get_systick() 반환값을 이용해 경과 시간을 측정합니다.
 *   - g_periodic_flag가 1이 되면 주기적 작업(500ms)을 실행합니다.
 */

/*
 * Timer1 CTC 모드 초기화 (시스템 틱 10ms)
 *
 * Timer1 설정:
 *   WGM12=1 → CTC 모드: 카운터가 OCR1A 값에 도달하면 0으로 초기화
 *   CS11, CS10 → Prescaler 64
 *   OCR1A = 2499 → 인터럽트 주기 = 64 × 2500 / 16MHz = 10ms
 *   OCIE1A=1 → Compare Match A 인터럽트 활성화
 */
void timer1_init_systick(void)
{
    TCCR1B |= (1 << WGM12);              /* CTC 모드 (Compare Match로 카운터 초기화) */
    OCR1A   = TIMER1_OCR_10MS;           /* 비교값: 2499 (10ms 주기) */
    TIMSK  |= (1 << OCIE1A);             /* Timer1 Compare Match A 인터럽트 활성화 */
    TCCR1B |= TIMER1_PRESCALER_64;       /* Prescaler 64로 타이머 시작 */
}

/*
 * Timer1 Compare Match A 인터럽트 서비스 루틴 (10ms마다 호출)
 *
 * 역할:
 *   1. g_systick을 10ms마다 1씩 증가시킵니다.
 *   2. 500ms마다 g_periodic_flag를 1로 설정합니다.
 */
ISR(TIMER1_COMPA_vect)
{
    g_systick++;

    /* 500ms 주기 플래그 생성
     * PERIODIC_INTERVAL_MS(500) / SYSTICK_PERIOD_MS(10) = 50 틱마다 */
    static uint16_t cnt = 0;
    if (++cnt >= (PERIODIC_INTERVAL_MS / SYSTICK_PERIOD_MS)) {
        cnt = 0;
        g_periodic_flag = 1;
    }
}

/*
 * 현재 시스템 틱 값 반환 (원자적 읽기)
 *
 * g_systick은 32비트이므로 읽는 도중 인터럽트가 발생하면 값이 깨질 수 있습니다.
 * 읽는 동안 인터럽트를 잠시 비활성화합니다.
 *
 * 반환값: 현재 틱 카운트 (1 = 10ms)
 *
 * 경과 시간 측정 예시:
 *   uint32_t start = get_systick();
 *   ... 작업 ...
 *   uint32_t elapsed_ms = (get_systick() - start) * SYSTICK_PERIOD_MS;
 */
uint32_t get_systick(void)
{
    uint32_t tick;
    cli();          /* 인터럽트 비활성화 (Critical Section 시작) */
    tick = g_systick;
    sei();          /* 인터럽트 활성화  (Critical Section 종료) */
    return tick;
}


/* ============================================================
 *  SECTION 9: LCD (HD44780 호환, 16x2, 8비트 모드)
 * ============================================================
 *
 * 핀 연결:
 *   PORTB (PB0-PB7) → LCD D0-D7  (데이터 버스)
 *   PA0 (RS)         → 0=명령, 1=데이터
 *   PA1 (RW)         → 항상 0 (쓰기 전용)
 *   PA2 (E)          → Enable 펄스 (High→Low에서 데이터 래치)
 *
 * ⚠ SPI 사용 시: PORTB를 SPI가 사용하므로 LCD와 동시에 사용할 수 없습니다.
 *   SPI 사용이 필요하면 LCD 데이터 핀을 PORTA로, 제어 핀을 PORTB로 변경하세요.
 */

/*
 * LCD 명령 전송
 *
 * RS=0 (명령 모드)로 설정하고 E 핀을 펄스하여 명령을 래치합니다.
 */
void lcd_command(uint8_t cmd)
{
    LCD_DATA_PORT = cmd;
    LCD_CTRL_PORT &= ~((1 << LCD_RS) | (1 << LCD_RW)); /* RS=0, RW=0 */
    LCD_CTRL_PORT |=  (1 << LCD_E);                      /* E = High */
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << LCD_E);                      /* E = Low (하강 엣지에서 래치) */
    _delay_ms(2);                                        /* 명령 실행 대기 */
}

/*
 * LCD 데이터(문자) 전송
 *
 * RS=1 (데이터 모드)로 설정하고 E 핀을 펄스하여 문자를 표시합니다.
 */
void lcd_data(uint8_t data)
{
    LCD_DATA_PORT = data;
    LCD_CTRL_PORT |=  (1 << LCD_RS);  /* RS=1 (데이터 모드) */
    LCD_CTRL_PORT &= ~(1 << LCD_RW);  /* RW=0 (쓰기) */
    LCD_CTRL_PORT |=  (1 << LCD_E);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << LCD_E);
    _delay_us(50);
}

/*
 * LCD 초기화 (전원 인가 후 1회 호출)
 *
 * HD44780 데이터시트의 초기화 시퀀스를 따릅니다:
 *   1. 전원 안정화 대기 (50ms)
 *   2. Function Set: 8비트, 2줄, 5×8 폰트 (0x38)
 *   3. Display ON, Cursor OFF, Blink OFF (0x0C)
 *   4. 화면 전체 지우기 (0x01)
 *   5. Entry Mode: 커서 오른쪽 이동, 화면 이동 없음 (0x06)
 */
void lcd_init(void)
{
    LCD_DATA_DDR  = 0xFF;              /* PORTB 전체 출력 */
    LCD_CTRL_DDR |= (1 << LCD_RS) | (1 << LCD_RW) | (1 << LCD_E); /* 제어 핀 출력 */

    _delay_ms(50); /* LCD 전원 안정화 대기 */

    lcd_command(0x38); /* Function Set: 8비트, 2줄, 5×8 */
    lcd_command(0x0C); /* Display ON, Cursor OFF */
    lcd_command(0x01); /* Display Clear */
    lcd_command(0x06); /* Entry Mode: 커서 오른쪽 이동 */
}

/*
 * LCD 화면 지우기
 * 커서를 (0,0)으로 복귀시킵니다.
 */
void lcd_clear(void)
{
    lcd_command(0x01); /* Clear Display (실행 시간 최대 1.52ms) */
    _delay_ms(2);
}

/*
 * LCD 커서 위치 이동
 *
 * 매개변수:
 *   row - 줄 번호 (0=첫 번째 줄, 1=두 번째 줄)
 *   col - 열 번호 (0~15)
 *
 * HD44780 DDRAM 주소:
 *   1행: 0x00~0x0F
 *   2행: 0x40~0x4F
 */
void lcd_goto_xy(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0 ? 0x00 : 0x40) + (col % LCD_COLS);
    lcd_command(0x80 | addr); /* Set DDRAM Address 명령 (MSB=1) */
}

/*
 * LCD 문자열 출력
 *
 * 현재 커서 위치부터 null 문자('\0')를 만날 때까지 출력합니다.
 */
void lcd_print(const char *str)
{
    while (*str) {
        lcd_data((uint8_t)(*str++));
    }
}

/*
 * LCD 포맷 문자열 출력 (printf 스타일)
 *
 * 매개변수:
 *   row - 출력할 줄 (0 또는 1)
 *   fmt - printf 포맷 문자열
 *   ... - 가변 인수
 *
 * 사용 예시:
 *   lcd_printf(0, "ADC: %4d", adc_read(0));
 *   lcd_printf(1, "Hello %s", "World");
 *
 * ※ 16자 초과분은 자동으로 잘립니다.
 */
void lcd_printf(uint8_t row, const char *fmt, ...)
{
    char buf[LCD_COLS + 1];
    va_list args;

    /* va_list를 사용하려면 <stdarg.h>가 필요합니다. stdio.h가 포함되면 대부분 같이 포함됩니다. */
    extern int vsnprintf(char *, size_t, const char *, __builtin_va_list);
    __builtin_va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    __builtin_va_end(args);

    buf[LCD_COLS] = '\0'; /* 16자 이상이면 강제 종단 */

    /* 지정한 행의 맨 처음으로 커서 이동 후 출력 */
    lcd_goto_xy(row, 0);
    lcd_print(buf);

    /* 남은 칸을 공백으로 채워 이전 내용의 잔상을 제거 */
    for (uint8_t i = (uint8_t)strlen(buf); i < LCD_COLS; i++) {
        lcd_data(' ');
    }
}


/* ============================================================
 *  SECTION 10: PWM 출력 (Timer3, OC3A = PE3)
 * ============================================================
 *
 * Timer3 Fast PWM Mode 14 (TOP = ICR3)을 사용합니다.
 * OC3A 핀(PE3)으로 PWM 신호가 출력됩니다.
 *
 * 주파수 계산:
 *   F_PWM = F_CPU / (Prescaler × (ICR3 + 1))
 *   ↔ ICR3 = F_CPU / (Prescaler × F_PWM) - 1
 *   예) 1000Hz: ICR3 = 16,000,000 / (64 × 1000) - 1 = 249
 *
 * 활용 예시:
 *   - 부저: pwm_tone(880) → 라(A4) 음계 출력
 *   - 서보모터: 20ms 주기(50Hz), 듀티 1~2ms → pwm_tone(50) 후 OCR3A 직접 조정
 */

/*
 * PWM 초기화 (Timer3, OC3A)
 *
 * TCCR3A:
 *   COM3A1=1, COM3A0=0 → Non-inverting: OCR3A 도달 시 OC3A 출력 Low
 *   WGM31=1             → Fast PWM Mode 14 (일부)
 *
 * TCCR3B:
 *   WGM33=1, WGM32=1   → Fast PWM Mode 14 완성
 *   CS31=1, CS30=1      → Prescaler 64
 */
void pwm_init(void)
{
    DDRE |= (1 << PE3); /* OC3A 핀(PE3)을 출력으로 설정 */

    TCCR3A |= (1 << COM3A1) | (1 << WGM31);           /* Non-inverting, Mode 14 일부 */
    TCCR3B |= (1 << WGM33) | (1 << WGM32) | PWM_PRESCALER_64; /* Mode 14 완성, Prescaler 64 */

    pwm_off(); /* 초기 상태: 출력 없음 */
}

/*
 * PWM 주파수 출력
 *
 * 매개변수: freq - 출력 주파수 (Hz), 0이면 출력 중지
 *
 * 사용 예시:
 *   pwm_tone(440);  // 라(A4) 음계, 440Hz
 *   pwm_tone(523);  // 도(C5) 음계, 523Hz
 *   pwm_off();      // 소리 끄기
 */
void pwm_tone(uint16_t freq)
{
    if (freq == 0) {
        pwm_off();
        return;
    }
    /* ICR3 = TOP 값: 주기 결정 */
    ICR3  = (uint16_t)(F_CPU / 64UL / freq) - 1;
    /* OCR3A = 50% 듀티 사이클 (부저용, 서보모터는 다르게 설정) */
    OCR3A = ICR3 / 2;
}

/*
 * PWM 출력 중지
 * OCR3A를 0으로 설정하면 OC3A 출력이 항상 Low 상태가 됩니다.
 */
void pwm_off(void)
{
    OCR3A = 0;
}


/* ============================================================
 *  SECTION 11: SPI 마스터
 * ============================================================
 *
 * ⚠ 중요한 핀 충돌 경고:
 *   ATmega128의 SPI 하드웨어 핀은 PORTB를 공유합니다:
 *     PB0 = SS   (Slave Select)
 *     PB1 = SCK  (Clock)
 *     PB2 = MOSI (Master Out Slave In)
 *     PB3 = MISO (Master In Slave Out)
 *
 *   기본 설정에서 PORTB는 LCD 데이터 버스로 사용됩니다.
 *   따라서 SPI와 LCD를 동시에 사용할 수 없습니다.
 *
 * SPI 사용을 위한 배선 변경 방법:
 *   LCD 데이터 핀 → PORTA (PA0-PA7)
 *   LCD 제어 핀   → PORTB (PB4=RS, PB5=RW, PB6=E) 등으로 변경
 *   (DC_MTR_ADC_PWM_CTRL.c 참고)
 *
 * SPI 속도: F_CPU/4 = 4MHz (SPR0=0, SPR1=0, SPI2X=0)
 */

/*
 * SPI 마스터 초기화
 *
 * SPCR: SPI Control Register
 *   SPE  = 1: SPI 활성화
 *   MSTR = 1: 마스터 모드
 *   SPR0, SPR1: 속도 선택 (00 = F_CPU/4)
 *
 * DDRB: SPI 핀 방향 설정
 *   MOSI, SCK, SS → 출력
 *   MISO          → 입력 (SPI 모듈이 제어)
 */
void spi_init(void)
{
    /* MOSI(PB2), SCK(PB1), SS(PB0): 출력 / MISO(PB3): 입력 */
    DDRB |=  (1 << PB2) | (1 << PB1) | (1 << PB0);
    DDRB &= ~(1 << PB3);

    PORTB |= (1 << PB0); /* SS = High (비활성 상태) */

    /* SPCR: SPI 활성화, 마스터 모드, Mode 0 (CPOL=0, CPHA=0), F_CPU/4 */
    SPCR = (1 << SPE) | (1 << MSTR);
}

/*
 * SPI 1바이트 전송/수신 (전이중 통신)
 *
 * SPI는 송신과 수신이 동시에 이루어집니다.
 * data를 보내는 동안 상대방이 보낸 데이터를 동시에 수신합니다.
 *
 * 매개변수: data - 전송할 바이트
 * 반환값:   수신된 바이트 (MISO로 받은 데이터)
 *
 * 사용 예시:
 *   PORTB &= ~(1 << PB0);          // SS = Low (슬레이브 선택)
 *   uint8_t rx = spi_transfer(0x01); // 명령 전송, 응답 수신
 *   PORTB |=  (1 << PB0);          // SS = High (슬레이브 해제)
 */
uint8_t spi_transfer(uint8_t data)
{
    SPDR = data;                      /* 데이터 전송 시작 */
    while (!(SPSR & (1 << SPIF)));   /* 전송 완료 대기 (SPIF 비트) */
    return SPDR;                      /* 수신된 데이터 반환 */
}


/* ============================================================
 *  SECTION 12: I2C / TWI 마스터
 * ============================================================
 *
 * ATmega128 하드웨어 TWI(Two Wire Interface) = I2C 호환
 * 핀: PD0 = SCL (클록), PD1 = SDA (데이터)
 * → LCD(PORTB), UART1(PD2,PD3)과 겹치지 않아 충돌 없음
 *
 * 사용 절차 (쓰기):
 *   i2c_start(addr << 1 | 0);  // addr: 7비트 주소, 0=Write
 *   i2c_write(data);
 *   i2c_stop();
 *
 * 사용 절차 (읽기):
 *   i2c_start(addr << 1 | 1);  // 1=Read
 *   uint8_t d0 = i2c_read_ack();   // 중간 바이트 (더 읽을 게 있을 때)
 *   uint8_t d1 = i2c_read_nack();  // 마지막 바이트
 *   i2c_stop();
 */

/*
 * I2C(TWI) 초기화
 *
 * TWBR: Bit Rate Register
 *   100kHz: TWBR = (F_CPU / SCL - 16) / 2 = (16MHz/100kHz - 16) / 2 = 72
 *
 * TWSR: Status Register
 *   TWPS1=0, TWPS0=0 → Prescaler 1 (분주 없음)
 */
void i2c_init(void)
{
    TWBR = I2C_TWBR_100KHZ; /* 100kHz I2C 속도 */
    TWSR = 0x00;             /* Prescaler = 1 */
}

/*
 * I2C START 조건 + 슬레이브 주소 전송
 *
 * 매개변수: addr - (7비트 주소 << 1) | R/W 비트
 *   쓰기: addr = (슬레이브_주소 << 1) | 0
 *   읽기: addr = (슬레이브_주소 << 1) | 1
 *
 * 반환값: 0 = 성공, 1 = 실패
 *
 * 사용 예시 (주소 0x50 장치에 쓰기 시작):
 *   if (i2c_start(0x50 << 1 | 0) == 0) { ... }
 */
uint8_t i2c_start(uint8_t addr)
{
    /* TWINT=1(인터럽트 클리어), TWSTA=1(START), TWEN=1(TWI 활성화) */
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));  /* START 전송 완료 대기 */

    /* START 또는 Repeated START 상태 확인 */
    if ((TW_STATUS != TW_START) && (TW_STATUS != TW_REP_START)) {
        return 1; /* 오류 */
    }

    /* 슬레이브 주소 + R/W 비트 전송 */
    TWDR = addr;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));

    /* ACK 수신 확인 (쓰기: TW_MT_SLA_ACK, 읽기: TW_MR_SLA_ACK) */
    if ((TW_STATUS != TW_MT_SLA_ACK) && (TW_STATUS != TW_MR_SLA_ACK)) {
        return 1; /* 슬레이브 응답 없음 */
    }

    return 0; /* 성공 */
}

/*
 * I2C STOP 조건 전송
 * 통신 종료 후 반드시 호출해야 합니다.
 */
void i2c_stop(void)
{
    /* TWSTO=1: STOP 조건 생성 */
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    /* STOP은 TWINT를 클리어하지 않으므로 대기 방식이 다름 */
    while (TWCR & (1 << TWSTO)); /* STOP 전송 완료 대기 */
}

/*
 * I2C 1바이트 전송 (마스터 → 슬레이브)
 *
 * 반환값: 0 = 성공 (ACK 수신), 1 = 실패 (NACK 수신)
 */
uint8_t i2c_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));

    return (TW_STATUS == TW_MT_DATA_ACK) ? 0 : 1;
}

/*
 * I2C 1바이트 수신 후 ACK 반환 (더 읽을 데이터가 있을 때)
 * 슬레이브에게 "다음 바이트도 보내달라"는 신호를 보냅니다.
 */
uint8_t i2c_read_ack(void)
{
    /* TWEA=1: ACK 자동 반환 */
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}

/*
 * I2C 1바이트 수신 후 NACK 반환 (마지막 바이트)
 * 슬레이브에게 "이게 마지막이다"는 신호를 보냅니다.
 * 이후 반드시 i2c_stop()을 호출하세요.
 */
uint8_t i2c_read_nack(void)
{
    /* TWEA=0: NACK 반환 (ACK 없음) */
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}


/* ============================================================
 *  SECTION 13: 4x4 키 매트릭스
 * ============================================================
 *
 * 핀 연결:
 *   PC0-PC3: 열(Column) 출력 핀 (액티브 Low: 스캔할 열을 0으로 설정)
 *   PC4-PC7: 행(Row)    입력 핀 (내부 풀업 사용: 키 눌리면 Low)
 *
 * 스캔 방식 (Non-blocking):
 *   열을 하나씩 Low로 설정하고, 각 행의 입력을 확인합니다.
 *   키가 눌렸다 떼어졌을 때(상승 엣지)에만 키 값을 반환합니다.
 *   → 키를 계속 누르고 있어도 1회만 인식됩니다.
 *
 * 키 번호 배치 (표준 4x4, 실제 배선에 따라 달라질 수 있습니다):
 *
 *       C0(PC3) C1(PC2) C2(PC1) C3(PC0)
 *   L0(PC4) [ 1  ]  [ 2  ]  [ 3  ]  [ A=10 ]
 *   L1(PC5) [ 4  ]  [ 5  ]  [ 6  ]  [ B=11 ]
 *   L2(PC6) [ 7  ]  [ 8  ]  [ 9  ]  [ C=12 ]
 *   L3(PC7) [*=13]  [ 0  ]  [#=14]  [ D=15 ]
 *
 * ※ 이 배치가 실제 하드웨어와 다르면 keymap 배열을 수정하세요.
 */

/*
 * 키 매트릭스 핀 설정
 * ports_init()에서 호출됩니다. 별도 호출 불필요.
 */
void keymatrix_init(void)
{
    /* PC0-PC3: 출력 (열 스캔 신호) / PC4-PC7: 입력 (행 감지) */
    DDRC  = 0x0F;   /* 하위 4비트 출력, 상위 4비트 입력 */
    PORTC = 0xF0;   /* 하위 4비트 High(비활성), 상위 4비트 내부 풀업 활성화 */
}

/*
 * 4x4 키 매트릭스 스캔 (Non-blocking, Edge 검출)
 *
 * 반환값: 눌렸다 떼어진 키의 번호 (0~15)
 *         눌린 키 없으면 KEY_NONE(255) 반환
 *
 * 사용 예시:
 *   uint8_t key = keymatrix_scan();
 *   if (key != KEY_NONE) {
 *       // key 값으로 동작 처리
 *   }
 */
uint8_t keymatrix_scan(void)
{
    /* 키 배치 맵 [row][col]: keymap[row][col] = 키 번호 */
    static const uint8_t keymap[4][4] = {
        { 1,  2,  3, 10}, /* L0(PC4) × C0-C3 */
        { 4,  5,  6, 11}, /* L1(PC5) × C0-C3 */
        { 7,  8,  9, 12}, /* L2(PC6) × C0-C3 */
        {13,  0, 14, 15}  /* L3(PC7) × C0-C3 */
    };

    static uint8_t last_key = KEY_NONE; /* 이전 스캔에서 눌린 키 */
    uint8_t current_key = KEY_NONE;

    /* 열(Column)을 하나씩 Low로 활성화하며 행(Row) 입력 확인 */
    for (uint8_t col = 0; col < 4; col++) {
        /* col=0 → PC3 Low (3-col=3), col=1 → PC2 Low (3-col=2), ... */
        PORTC = 0xFF;
        PORTC &= ~(1 << (3 - col)); /* 해당 열만 Low로 설정 */
        _delay_us(5);               /* 신호 안정화 대기 */

        for (uint8_t row = 0; row < 4; row++) {
            /* 행 입력 핀: PC4~PC7 → (row+4)번 비트 */
            if (!(PINC & (1 << (row + 4)))) {
                current_key = keymap[row][col];
                break;
            }
        }
        if (current_key != KEY_NONE) break;
    }

    /* 상승 엣지 검출: 이전에 눌렸다가(last_key != KEY_NONE) 지금 떼어짐 */
    if (current_key == KEY_NONE && last_key != KEY_NONE) {
        uint8_t pressed = last_key;
        last_key = KEY_NONE;
        return pressed; /* 떼어진 키 번호 반환 */
    }
    last_key = current_key;
    return KEY_NONE;
}


/* ============================================================
 *  SECTION 14: 시스템 초기화
 * ============================================================ */

/*
 * 포트 방향 및 초기값 설정
 *
 * 모든 포트의 입출력 방향(DDR)과 초기값(PORT)을 여기서 설정합니다.
 * 포트가 추가되면 이 함수를 수정하세요.
 */
void ports_init(void)
{
    /* LCD 데이터 포트 (PORTB): 전체 출력 */
    LCD_DATA_DDR = 0xFF;
    LCD_DATA_PORT = 0x00;

    /* LCD 제어 포트 (PORTA, RS/RW/E): 출력 */
    LCD_CTRL_DDR |= (1 << LCD_RS) | (1 << LCD_RW) | (1 << LCD_E);

    /* UART1 (PD3=TXD1 출력, PD2=RXD1 입력) */
    DDRD |= (1 << PD3);
    DDRD &= ~(1 << PD2);

    /* PWM 출력 핀 (PE3 = OC3A): 출력 */
    DDRE |= (1 << PE3);

    /* ADC 입력 핀 (PORTF): 방향 설정 불필요 (자동 아날로그 입력) */

    /* 키 매트릭스 (PORTC): keymatrix_init()에서 설정 */
    keymatrix_init();

    /* I2C 핀 (PD0=SCL, PD1=SDA): TWI 모듈이 자동 제어, DDR 설정 불필요 */

    /*
     * 학생 추가: 필요한 포트가 있으면 아래에 추가하세요.
     * 예) LED 포트 설정:
     *   DDRD  |= 0x0F;   // PD4-PD7 출력 (LED)
     *   PORTD &= ~0x0F;  // 초기값: 꺼짐
     */
}

/*
 * 전체 시스템 초기화
 * main() 시작 시 반드시 호출해야 합니다.
 */
void system_init(void)
{
    ports_init();           /* 포트 설정 */
    adc_init();             /* ADC 초기화 */
    uart1_init(BAUD_RATE);  /* UART1 초기화 */
    lcd_init();             /* LCD 초기화 */
    pwm_init();             /* PWM(Timer3) 초기화 */
    spi_init();             /* SPI 초기화 (LCD와 충돌 시 제거) */
    i2c_init();             /* I2C(TWI) 초기화 */
    timer1_init_systick();  /* Timer1 시스템 틱 초기화 */
    sei();                  /* 전역 인터럽트 활성화 (모든 초기화 완료 후 마지막에 호출) */

    /* 시작 메시지 출력 */
    lcd_clear();
    lcd_printf(0, "Platform Ready");
    lcd_printf(1, "KoPT v1.0");
    _delay_ms(1000);
    lcd_clear();
}


/* ============================================================
 *  SECTION 15: 애플리케이션 계층 ← 학생 구현 영역
 * ============================================================
 *
 * 이 섹션이 학생 여러분이 채워야 할 핵심 영역입니다.
 *
 * 규칙:
 *   1. 이 섹션 이외의 코드는 수정하지 않아도 됩니다.
 *   2. TODO 주석을 찾아 아래에 코드를 작성하세요.
 *   3. 새로운 함수가 필요하면 이 섹션 안에 추가하세요.
 *   4. 전역 변수가 필요하면 SECTION 4 아래에 추가하세요.
 */

/*
 * UART 프레임 수신 처리 함수
 *
 * PC에서 프레임이 도착할 때마다 main() 루프가 이 함수를 호출합니다.
 * frame->type에 따라 처리를 분기합니다.
 *
 * 매개변수: frame - 수신된 완성 프레임 (읽기 전용)
 *
 * 처리해야 할 주요 TYPE:
 *   UART_TYPE_DATA (0x01): PC가 보낸 데이터 (필요에 따라 처리)
 *   UART_TYPE_CMD  (0x02): PC가 보낸 명령 → 여기서 구현!
 */
void on_frame_received(const UartFrame *frame)
{
    switch (frame->type) {

        /* ---- DATA 프레임 수신 (PC → ATmega128) ---- */
        case UART_TYPE_DATA:
            /*
             * TODO: PC가 DATA 타입으로 보낸 데이터를 처리하세요.
             *
             * 프레임 내용 확인 방법:
             *   frame->length      : 페이로드 바이트 수
             *   frame->payload[0]  : 첫 번째 바이트
             *   (char*)frame->payload : 문자열로 해석 (null 종단 아님, 주의!)
             *
             * 예시 — 페이로드가 "L0:Hello"이면 LCD 1행에 표시:
             *   if (frame->length >= 3 && frame->payload[0]=='L' && frame->payload[1]=='0') {
             *       char text[17] = {0};
             *       memcpy(text, frame->payload + 3, frame->length - 3);
             *       lcd_goto_xy(0, 0);
             *       lcd_print(text);
             *   }
             */
            break;

        /* ---- CMD 프레임 수신 (PC → ATmega128) ---- */
        case UART_TYPE_CMD:
            /*
             * TODO: PC가 CMD 타입으로 보낸 명령을 처리하세요.
             *
             * 명령 포맷은 여러분이 직접 정의합니다.
             * 23번 Python Repeater의 수동 송신 패널에서 이 명령을 테스트할 수 있습니다.
             *
             * ---- 구현 예시 ----
             * 페이로드 "L:ON" → LED 켜기
             * 페이로드 "L:OFF"→ LED 끄기
             *
             * if (frame->payload[0] == 'L' && frame->payload[1] == ':') {
             *     if (strncmp((char*)frame->payload + 2, "ON", 2) == 0) {
             *         PORTD |= (1 << PD4);   // LED ON
             *     } else if (strncmp((char*)frame->payload + 2, "OFF", 3) == 0) {
             *         PORTD &= ~(1 << PD4);  // LED OFF
             *     }
             *     // ACK 응답 전송
             *     UartFrame ack;
             *     ack.type = UART_TYPE_ACK;
             *     ack.length = 2;
             *     ack.payload[0] = 'O'; ack.payload[1] = 'K';
             *     uart_send_frame(&ack);
             * }
             * ---- 예시 끝 ----
             *
             * 명령을 인식하지 못한 경우 ERR 응답을 보내는 것이 좋습니다:
             *   UartFrame err;
             *   err.type = UART_TYPE_ERR;
             *   err.length = 5;
             *   err.payload[0]='E'; err.payload[1]=':';
             *   err.payload[2]='C'; err.payload[3]='M'; err.payload[4]='D';
             *   uart_send_frame(&err);
             */

            /* 아래에 TODO를 채우세요 */

            break;

        /* ---- ACK 프레임 수신 (PC가 응답을 보낸 경우) ---- */
        case UART_TYPE_ACK:
            /* TODO: 필요하면 처리 추가 */
            break;

        /* ---- ERR 프레임 수신 ---- */
        case UART_TYPE_ERR:
            /* TODO: 필요하면 처리 추가 */
            break;

        default:
            /* 알 수 없는 TYPE → 무시 */
            break;
    }
}

/*
 * 주기적 작업 함수 (500ms마다 호출)
 *
 * Timer1 인터럽트가 g_periodic_flag를 올리면 main()이 이 함수를 호출합니다.
 * ADC 읽기, LCD 갱신, 정기 데이터 전송 등의 작업을 여기에 작성하세요.
 *
 * ---- 완성 예시 (ADC0 값을 PC로 전송) ----
 * 아래 코드는 ADC0 값을 "A0:XXXX" 형식의 DATA 프레임으로 PC에 전송합니다.
 * 이 예시는 그대로 두고, 나머지 기능을 TODO 아래에 추가하세요.
 */
void app_periodic_task(void)
{
    /* ---- [예시] ADC0 값 읽어서 DATA 프레임으로 PC 전송 ---- */
    {
        uint16_t adc0 = adc_read(0);

        /* PAYLOAD 포맷: "A0:XXXX" (7 bytes, 명세서 §3.3 형식) */
        UartFrame f;
        f.type   = UART_TYPE_DATA;
        f.length = 7;
        f.payload[0] = 'A';
        f.payload[1] = '0';
        f.payload[2] = ':';
        f.payload[3] = '0' + ((adc0 / 1000) % 10);
        f.payload[4] = '0' + ((adc0 /  100) % 10);
        f.payload[5] = '0' + ((adc0 /   10) % 10);
        f.payload[6] = '0' + ( adc0         % 10);
        uart_send_frame(&f);

        /* LCD 1행에 ADC0 값 표시 */
        lcd_printf(0, "ADC0: %4d", (int)adc0);
    }
    /* ---- 예시 끝 ---- */

    /*
     * TODO: 아래에 추가 작업을 구현하세요.
     *
     * 구현 아이디어:
     *   - ADC1 값도 읽어서 전송
     *   - LCD 2행에 상태 메시지 표시
     *   - 다른 센서 값 전송
     *   - 정기적인 상태 보고
     *
     * ADC 다른 채널 읽기 예시:
     *   uint16_t adc1 = adc_read(1);
     *   lcd_printf(1, "ADC1: %4d", (int)adc1);
     */
}


/* ============================================================
 *  SECTION 16: main() — 메인 루프
 * ============================================================
 *
 * 이벤트 드리븐(Event-Driven) 방식:
 *   플래그(Flag)가 설정될 때만 해당 처리를 실행합니다.
 *   폴링(Polling) 방식보다 CPU를 효율적으로 사용합니다.
 *
 * 처리 우선순위:
 *   ① UART 프레임 수신 (즉각 처리)
 *   ② 주기적 작업 (500ms)
 *   ③ 키 입력 처리 (학생 구현)
 *   ④ 기타 추가 이벤트 (학생 구현)
 */
int main(void)
{
    system_init(); /* 모든 주변장치 초기화 */

    while (1) {

        /* ① UART 프레임 수신 처리
         *    ISR에서 g_frame_ready를 1로 설정하면 프레임을 처리합니다.
         *    처리 전에 플래그를 먼저 0으로 내려 새 프레임 수신을 놓치지 않습니다. */
        if (g_frame_ready) {
            g_frame_ready = 0;
            /* g_rx_frame의 로컬 복사본을 만들어 처리합니다.
             * (복사 중 ISR이 g_rx_frame을 덮어쓰는 경우를 방지) */
            UartFrame frame_copy;
            cli();
            frame_copy = g_rx_frame;
            sei();
            on_frame_received(&frame_copy);
        }

        /* ② 주기적 작업 처리 (500ms마다) */
        if (g_periodic_flag) {
            g_periodic_flag = 0;
            app_periodic_task();
        }

        /* ③ 키 입력 처리
         *    keymatrix_scan()은 Non-blocking 함수입니다.
         *    키가 눌렸다 떼어지면 키 번호를, 아니면 KEY_NONE을 반환합니다. */
        uint8_t key = keymatrix_scan();
        if (key != KEY_NONE) {
            /*
             * TODO: 키 번호(key)에 따른 동작을 구현하세요.
             *
             * 예시:
             *   switch (key) {
             *       case 1:  // '1' 키
             *           pwm_tone(262);    // 도(C4) 출력
             *           lcd_printf(1, "Key: 1 - Do");
             *           break;
             *       case 2:  // '2' 키
             *           pwm_tone(330);    // 미(E4) 출력
             *           lcd_printf(1, "Key: 2 - Mi");
             *           break;
             *       case 15: // 'D' 키 → 소리 끄기
             *           pwm_off();
             *           lcd_printf(1, "Sound OFF");
             *           break;
             *       default:
             *           break;
             *   }
             */
        }

        /* ④ TODO: 추가 이벤트 처리를 여기에 작성하세요.
         *
         * 예시 아이디어:
         *   - 특정 조건에서 LED 점멸
         *   - 버튼(스위치) 입력 처리
         *   - 상태 머신 업데이트
         */

    } /* while(1) 끝 */

    return 0; /* ATmega128에서는 도달하지 않지만 컴파일러 경고 방지 */
}
