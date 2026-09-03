/*
 * ============================================================
 *  25_GUI_Test.c
 *  ATmega128 통신 플랫폼 — 주기 전송 ON/OFF 제어 실습
 * ============================================================
 *
 * [목적]
 *   24_ATmega128_Platform.c 기본 플랫폼을 기반으로,
 *   PC(23_UART_TCPIP_Repeater.py)에서 CMD 프레임을 전송하여
 *   ATmega128의 주기적 데이터 전송을 시작/중지할 수 있도록 구현합니다.
 *
 * [추가 기능]
 *   - g_tx_running 플래그: 주기 전송 활성화 여부를 관리
 *   - CMD "STOP"  수신 → 주기 전송 중지, LCD 2행에 "TX: STOP" 표시
 *   - CMD "START" 수신 → 주기 전송 재개, LCD 2행에 "TX: RUN" 표시
 *   - 인식되지 않은 CMD → ERR 프레임 응답
 *   - 키 '1' → 전송 시작 / 키 '2' → 전송 중지 (로컬 제어)
 *
 * [23번 Python 수동 송신 사용법]
 *   수동 송신 패널에서 대상: "UART" 선택 후
 *     STOP  → 입력 후 전송  (주기 전송 중지)
 *     START → 입력 후 전송  (주기 전송 재개)
 *
 * Target  : ATmega128A
 * Crystal : 16MHz
 * Version : v1.0  (2026-05-29)
 * ============================================================
 */


/* ============================================================
 *  SECTION 1: 시스템 설정 및 헤더 포함
 * ============================================================ */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/twi.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>


/* ============================================================
 *  SECTION 2: 핀 정의 및 상수
 * ============================================================ */

#define LCD_DATA_PORT  PORTB
#define LCD_DATA_DDR   DDRB
#define LCD_CTRL_PORT  PORTA
#define LCD_CTRL_DDR   DDRA
#define LCD_RS         0
#define LCD_RW         1
#define LCD_E          2
#define LCD_COLS       16
#define LCD_ROWS       2

#define ADC_VREF_AVCC  (1 << REFS0)

#define BAUD_RATE      9600UL

#define UART_SOF              0xAA
#define UART_FRAME_MAX_PAYLOAD  28
#define UART_TYPE_DATA        0x01
#define UART_TYPE_CMD         0x02
#define UART_TYPE_ACK         0x03
#define UART_TYPE_ERR         0x04

#define TIMER1_PRESCALER_64   ((1 << CS11) | (1 << CS10))
#define TIMER1_OCR_10MS       2499
#define SYSTICK_PERIOD_MS     10
#define PERIODIC_INTERVAL_MS  500

#define PWM_PRESCALER_64  ((1 << CS31) | (1 << CS30))

#define KEY_NONE  255

#define I2C_TWBR_100KHZ  72


/* ============================================================
 *  SECTION 3: 데이터 구조 정의
 * ============================================================ */

typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t payload[UART_FRAME_MAX_PAYLOAD];
} UartFrame;

typedef enum {
    RX_WAIT_SOF,
    RX_WAIT_TYPE,
    RX_WAIT_LENGTH,
    RX_WAIT_PAYLOAD,
    RX_WAIT_CRC
} RxState;


/* ============================================================
 *  SECTION 4: 전역 변수
 * ============================================================ */

volatile uint8_t   g_frame_ready  = 0;
volatile UartFrame g_rx_frame;

volatile uint32_t  g_systick      = 0;
volatile uint8_t   g_periodic_flag = 0;

/* 주기 전송 활성화 플래그 (1=전송 중, 0=중지) */
volatile uint8_t   g_tx_running   = 1;


/* ============================================================
 *  SECTION 5: 함수 프로토타입
 * ============================================================ */

void    uart1_init(uint32_t baud);
void    uart1_putc(char c);
uint8_t uart_crc8(uint8_t type, uint8_t length, const uint8_t *payload);
void    uart_send_frame(const UartFrame *frame);
uint8_t uart_parse_byte(uint8_t byte, UartFrame *out_frame);

void     adc_init(void);
uint16_t adc_read(uint8_t channel);

void     timer1_init_systick(void);
uint32_t get_systick(void);

void lcd_init(void);
void lcd_command(uint8_t cmd);
void lcd_data(uint8_t data);
void lcd_clear(void);
void lcd_goto_xy(uint8_t row, uint8_t col);
void lcd_print(const char *str);
void lcd_printf(uint8_t row, const char *fmt, ...);

void pwm_init(void);
void pwm_tone(uint16_t freq);
void pwm_off(void);

void    spi_init(void);
uint8_t spi_transfer(uint8_t data);

void    i2c_init(void);
uint8_t i2c_start(uint8_t addr);
void    i2c_stop(void);
uint8_t i2c_write(uint8_t data);
uint8_t i2c_read_ack(void);
uint8_t i2c_read_nack(void);

void    keymatrix_init(void);
uint8_t keymatrix_scan(void);

void ports_init(void);
void system_init(void);

void on_frame_received(const UartFrame *frame);
void app_periodic_task(void);
void send_ack(void);
void send_err_cmd(void);
void update_status_lcd(void);


/* ============================================================
 *  SECTION 6: UART1 프레임 통신
 * ============================================================ */

ISR(USART1_RX_vect)
{
    uint8_t byte = UDR1;
    UartFrame tmp;

    if (uart_parse_byte(byte, &tmp)) {
        g_rx_frame    = tmp;
        g_frame_ready = 1;
    }
}

void uart1_init(uint32_t baud)
{
    uint16_t ubrr = (uint16_t)(F_CPU / (16UL * baud) - 1);
    UBRR1H = (uint8_t)(ubrr >> 8);
    UBRR1L = (uint8_t)(ubrr);
    UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

void uart1_putc(char c)
{
    while (!(UCSR1A & (1 << UDRE1)));
    UDR1 = (uint8_t)c;
}

uint8_t uart_crc8(uint8_t type, uint8_t length, const uint8_t *payload)
{
    uint8_t crc = type ^ length;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= payload[i];
    }
    return crc;
}

void uart_send_frame(const UartFrame *frame)
{
    uint8_t crc = uart_crc8(frame->type, frame->length, frame->payload);
    uart1_putc((char)UART_SOF);
    uart1_putc((char)frame->type);
    uart1_putc((char)frame->length);
    for (uint8_t i = 0; i < frame->length; i++) {
        uart1_putc((char)frame->payload[i]);
    }
    uart1_putc((char)crc);
}

uint8_t uart_parse_byte(uint8_t byte, UartFrame *out_frame)
{
    static RxState   state   = RX_WAIT_SOF;
    static UartFrame rx_buf;
    static uint8_t   pay_idx = 0;

    switch (state) {
        case RX_WAIT_SOF:
            if (byte == UART_SOF) state = RX_WAIT_TYPE;
            break;
        case RX_WAIT_TYPE:
            rx_buf.type = byte;
            state = RX_WAIT_LENGTH;
            break;
        case RX_WAIT_LENGTH:
            if (byte > UART_FRAME_MAX_PAYLOAD) {
                state = RX_WAIT_SOF;
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
                *out_frame = rx_buf;
                return 1;
            } else {
                UartFrame err;
                err.type       = UART_TYPE_ERR;
                err.length     = 5;
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
 *  SECTION 7: ADC
 * ============================================================ */

void adc_init(void)
{
    ADMUX  = ADC_VREF_AVCC;
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t adc_read(uint8_t channel)
{
    ADMUX = (ADMUX & 0xE0) | (channel & 0x1F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}


/* ============================================================
 *  SECTION 8: Timer1 시스템 틱
 * ============================================================ */

void timer1_init_systick(void)
{
    TCCR1B |= (1 << WGM12);
    OCR1A   = TIMER1_OCR_10MS;
    TIMSK  |= (1 << OCIE1A);
    TCCR1B |= TIMER1_PRESCALER_64;
}

ISR(TIMER1_COMPA_vect)
{
    g_systick++;
    static uint16_t cnt = 0;
    if (++cnt >= (PERIODIC_INTERVAL_MS / SYSTICK_PERIOD_MS)) {
        cnt = 0;
        g_periodic_flag = 1;
    }
}

uint32_t get_systick(void)
{
    uint32_t tick;
    cli();
    tick = g_systick;
    sei();
    return tick;
}


/* ============================================================
 *  SECTION 9: LCD (HD44780)
 * ============================================================ */

void lcd_command(uint8_t cmd)
{
    LCD_DATA_PORT = cmd;
    LCD_CTRL_PORT &= ~((1 << LCD_RS) | (1 << LCD_RW));
    LCD_CTRL_PORT |=  (1 << LCD_E);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << LCD_E);
    _delay_ms(2);
}

void lcd_data(uint8_t data)
{
    LCD_DATA_PORT = data;
    LCD_CTRL_PORT |=  (1 << LCD_RS);
    LCD_CTRL_PORT &= ~(1 << LCD_RW);
    LCD_CTRL_PORT |=  (1 << LCD_E);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << LCD_E);
    _delay_us(50);
}

void lcd_init(void)
{
    LCD_DATA_DDR  = 0xFF;
    LCD_CTRL_DDR |= (1 << LCD_RS) | (1 << LCD_RW) | (1 << LCD_E);
    _delay_ms(50);
    lcd_command(0x38);
    lcd_command(0x0C);
    lcd_command(0x01);
    lcd_command(0x06);
}

void lcd_clear(void)
{
    lcd_command(0x01);
    _delay_ms(2);
}

void lcd_goto_xy(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0 ? 0x00 : 0x40) + (col % LCD_COLS);
    lcd_command(0x80 | addr);
}

void lcd_print(const char *str)
{
    while (*str) {
        lcd_data((uint8_t)(*str++));
    }
}

void lcd_printf(uint8_t row, const char *fmt, ...)
{
    char buf[LCD_COLS + 1];
    extern int vsnprintf(char *, size_t, const char *, __builtin_va_list);
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    __builtin_va_end(args);
    buf[LCD_COLS] = '\0';
    lcd_goto_xy(row, 0);
    lcd_print(buf);
    for (uint8_t i = (uint8_t)strlen(buf); i < LCD_COLS; i++) {
        lcd_data(' ');
    }
}


/* ============================================================
 *  SECTION 10: PWM (Timer3)
 * ============================================================ */

void pwm_init(void)
{
    DDRE |= (1 << PE3);
    TCCR3A |= (1 << COM3A1) | (1 << WGM31);
    TCCR3B |= (1 << WGM33) | (1 << WGM32) | PWM_PRESCALER_64;
    pwm_off();
}

void pwm_tone(uint16_t freq)
{
    if (freq == 0) { pwm_off(); return; }
    ICR3  = (uint16_t)(F_CPU / 64UL / freq) - 1;
    OCR3A = ICR3 / 2;
}

void pwm_off(void)
{
    OCR3A = 0;
}


/* ============================================================
 *  SECTION 11: SPI 마스터
 * ============================================================ */

void spi_init(void)
{
    DDRB |=  (1 << PB2) | (1 << PB1) | (1 << PB0);
    DDRB &= ~(1 << PB3);
    PORTB |= (1 << PB0);
    SPCR = (1 << SPE) | (1 << MSTR);
}

uint8_t spi_transfer(uint8_t data)
{
    SPDR = data;
    while (!(SPSR & (1 << SPIF)));
    return SPDR;
}


/* ============================================================
 *  SECTION 12: I2C / TWI 마스터
 * ============================================================ */

void i2c_init(void)
{
    TWBR = I2C_TWBR_100KHZ;
    TWSR = 0x00;
}

uint8_t i2c_start(uint8_t addr)
{
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    if ((TW_STATUS != TW_START) && (TW_STATUS != TW_REP_START)) return 1;
    TWDR = addr;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    if ((TW_STATUS != TW_MT_SLA_ACK) && (TW_STATUS != TW_MR_SLA_ACK)) return 1;
    return 0;
}

void i2c_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    while (TWCR & (1 << TWSTO));
}

uint8_t i2c_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    return (TW_STATUS == TW_MT_DATA_ACK) ? 0 : 1;
}

uint8_t i2c_read_ack(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}

uint8_t i2c_read_nack(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}


/* ============================================================
 *  SECTION 13: 4x4 키 매트릭스
 * ============================================================ */

void keymatrix_init(void)
{
    DDRC  = 0x0F;
    PORTC = 0xF0;
}

uint8_t keymatrix_scan(void)
{
    static const uint8_t keymap[4][4] = {
        { 1,  2,  3, 10},
        { 4,  5,  6, 11},
        { 7,  8,  9, 12},
        {13,  0, 14, 15}
    };
    static uint8_t last_key = KEY_NONE;
    uint8_t current_key = KEY_NONE;

    for (uint8_t col = 0; col < 4; col++) {
        PORTC = 0xFF;
        PORTC &= ~(1 << (3 - col));
        _delay_us(5);
        for (uint8_t row = 0; row < 4; row++) {
            if (!(PINC & (1 << (row + 4)))) {
                current_key = keymap[row][col];
                break;
            }
        }
        if (current_key != KEY_NONE) break;
    }

    if (current_key == KEY_NONE && last_key != KEY_NONE) {
        uint8_t pressed = last_key;
        last_key = KEY_NONE;
        return pressed;
    }
    last_key = current_key;
    return KEY_NONE;
}


/* ============================================================
 *  SECTION 14: 시스템 초기화
 * ============================================================ */

void ports_init(void)
{
    LCD_DATA_DDR  = 0xFF;
    LCD_DATA_PORT = 0x00;
    LCD_CTRL_DDR |= (1 << LCD_RS) | (1 << LCD_RW) | (1 << LCD_E);
    DDRD |= (1 << PD3);
    DDRD &= ~(1 << PD2);
    DDRE |= (1 << PE3);
    keymatrix_init();
}

void system_init(void)
{
    ports_init();
    adc_init();
    uart1_init(BAUD_RATE);
    lcd_init();
    pwm_init();
    /* spi_init() 제거: PORTB는 LCD 데이터 버스로 사용 중이며 SPI와 동시 사용 불가 */
    i2c_init();
    timer1_init_systick();
    sei();

    lcd_clear();
    lcd_printf(0, "GUI Test Ready");
    lcd_printf(1, "TX: RUN");
    _delay_ms(1000);

    /* lcd_clear() 이후 TX 상태를 복원하지 않으면 row 1이 공백이 됨 */
    lcd_clear();
    update_status_lcd();
}


/* ============================================================
 *  SECTION 15: 애플리케이션 계층
 * ============================================================ */

/* ACK 응답 전송 헬퍼 */
void send_ack(void)
{
    UartFrame ack;
    ack.type       = UART_TYPE_ACK;
    ack.length     = 2;
    ack.payload[0] = 'O';
    ack.payload[1] = 'K';
    uart_send_frame(&ack);
}

/* 알 수 없는 CMD에 대한 ERR 응답 */
void send_err_cmd(void)
{
    UartFrame err;
    err.type       = UART_TYPE_ERR;
    err.length     = 5;
    err.payload[0] = 'E';
    err.payload[1] = ':';
    err.payload[2] = 'C';
    err.payload[3] = 'M';
    err.payload[4] = 'D';
    uart_send_frame(&err);
}

/* LCD 2행에 현재 전송 상태 표시 */
void update_status_lcd(void)
{
    if (g_tx_running) {
        lcd_printf(1, "TX: RUN");
    } else {
        lcd_printf(1, "TX: STOP");
    }
}

/*
 * UART 프레임 수신 처리
 *
 * CMD 프레임 payload 규칙:
 *   "STOP"    → 주기 전송 중지
 *   "START"   → 주기 전송 재개
 *   "ECHO:XX" → XX를 DATA 타입으로 응답 (루프백 테스트)
 *               "ECHO:" 만 전송 시 "OK" 응답
 *   "PING"    → DATA "PONG" 응답 (엔드-투-엔드 연결 확인)
 */
void on_frame_received(const UartFrame *frame)
{
    switch (frame->type) {

        case UART_TYPE_DATA: {
            /*
             * TCP→UART 경로로 전달된 데이터 수신 처리
             *   - 다른 ATmega128(클라이언트)에서 온 센서 데이터 등
             *   - LCD 1행에 수신 내용 표시
             *   - ACK 응답 전송
             *
             * payload 는 null 종료 문자가 없으므로 별도 버퍼에 복사 후 표시
             */
            char rx_buf[UART_FRAME_MAX_PAYLOAD + 1];
            uint8_t len = frame->length < UART_FRAME_MAX_PAYLOAD
                          ? frame->length : UART_FRAME_MAX_PAYLOAD;
            memcpy(rx_buf, frame->payload, len);
            rx_buf[len] = '\0';

            lcd_printf(0, "RX:%-13s", rx_buf);
            send_ack();
            break;
        }

        case UART_TYPE_CMD:
            if (frame->length == 4
                && memcmp(frame->payload, "STOP", 4) == 0)
            {
                /* "STOP" 명령: 주기 전송 중지 */
                g_tx_running = 0;
                update_status_lcd();
                send_ack();
            }
            else if (frame->length == 5
                     && memcmp(frame->payload, "START", 5) == 0)
            {
                /* "START" 명령: 주기 전송 재개 */
                g_tx_running = 1;
                update_status_lcd();
                send_ack();
            }
            else if (frame->length == 4
                     && memcmp(frame->payload, "PING", 4) == 0)
            {
                /* "PING" 명령: 엔드-투-엔드 연결 확인
                 *   - DATA "PONG" 응답
                 *   - LCD 1행에 "PING RX" 표시
                 */
                UartFrame pong;
                pong.type      = UART_TYPE_DATA;
                pong.length    = 4;
                pong.payload[0] = 'P';
                pong.payload[1] = 'O';
                pong.payload[2] = 'N';
                pong.payload[3] = 'G';
                lcd_printf(0, "PING RX");
                uart_send_frame(&pong);
            }
            else if (frame->length >= 5
                     && memcmp(frame->payload, "ECHO:", 5) == 0)
            {
                /*
                 * "ECHO:XX" 명령: 루프백 테스트
                 *   - "ECHO:" 뒤 내용을 DATA 타입으로 그대로 응답
                 *   - 뒤 내용이 없으면 "OK" 응답
                 *   - LCD 1행에 에코 내용 표시 (시각 확인용)
                 */
                UartFrame echo;
                echo.type = UART_TYPE_DATA;
                if (frame->length > 5) {
                    echo.length = frame->length - 5;
                    memcpy(echo.payload, frame->payload + 5, echo.length);
                } else {
                    echo.length    = 2;
                    echo.payload[0] = 'O';
                    echo.payload[1] = 'K';
                }
                lcd_printf(0, "ECHO:%-11s", (char *)echo.payload);
                uart_send_frame(&echo);
            }
            else {
                /* 알 수 없는 명령 */
                send_err_cmd();
            }
            break;

        case UART_TYPE_ACK:
            break;

        case UART_TYPE_ERR:
            break;

        default:
            break;
    }
}

/*
 * 주기적 작업 (500ms마다 호출)
 * g_tx_running == 0 이면 데이터 전송을 건너뜁니다.
 */
void app_periodic_task(void)
{
    uint16_t adc0 = adc_read(0);

    /* LCD 1행: ADC0 값 표시 (전송 상태와 무관하게 항상 갱신) */
    lcd_printf(0, "ADC0: %4d", (int)adc0);

    /* 전송 중지 상태면 PC로 DATA를 보내지 않음 */
    if (!g_tx_running) {
        return;
    }

    /* PAYLOAD 포맷: "A0:XXXX" (7 bytes) */
    UartFrame f;
    f.type       = UART_TYPE_DATA;
    f.length     = 7;
    f.payload[0] = 'A';
    f.payload[1] = '0';
    f.payload[2] = ':';
    f.payload[3] = '0' + ((adc0 / 1000) % 10);
    f.payload[4] = '0' + ((adc0 /  100) % 10);
    f.payload[5] = '0' + ((adc0 /   10) % 10);
    f.payload[6] = '0' + ( adc0         % 10);
    uart_send_frame(&f);
}


/* ============================================================
 *  SECTION 16: main()
 * ============================================================ */

int main(void)
{
    system_init();

    while (1) {

        /* ① UART 프레임 수신 처리 */
        if (g_frame_ready) {
            g_frame_ready = 0;
            UartFrame frame_copy;
            cli();
            frame_copy = g_rx_frame;
            sei();
            on_frame_received(&frame_copy);
        }

        /* ② 주기적 작업 (500ms) */
        if (g_periodic_flag) {
            g_periodic_flag = 0;
            app_periodic_task();
        }

        /* ③ 키 입력 처리
         *   키 '1' → 전송 시작 (START)
         *   키 '2' → 전송 중지 (STOP)
         */
        uint8_t key = keymatrix_scan();
        if (key != KEY_NONE) {
            switch (key) {
                case 1:
                    g_tx_running = 1;
                    update_status_lcd();
                    break;
                case 2:
                    g_tx_running = 0;
                    update_status_lcd();
                    break;
                default:
                    break;
            }
        }

    }

    return 0;
}
