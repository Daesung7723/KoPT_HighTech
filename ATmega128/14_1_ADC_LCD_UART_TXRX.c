/*
 * ATmega128 UART 수신 데이터 LCD 표시
 * Target: ATmega128
 * Crystal: 16MHz
 *
 * --- 기능 ---
 * - UART1(9600bps)으로 수신한 문자열을 LCD에 표시
 * - '\n' 또는 '\r' 수신 시 LCD 갱신 및 에코 송신 (1회)
 * - LCD 1행: 수신 문자열  1~16번째 문자
 * - LCD 2행: 수신 문자열 17~32번째 문자
 * - UART RX는 인터럽트(ISR) 방식으로 처리 (22번 코드 방식 적용)
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * UART1 RXD1: PD2 (입력)
 * UART1 TXD1: PD3 (출력)
 */

#define F_CPU 16000000UL
#define BAUD_RATE 9600
#define RX_BUF_SIZE 33  // LCD 2줄 × 16자 + null terminator

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <string.h>

// --- LCD 핀 및 포트 정의 ---
#define LCD_DATA_PORT PORTB
#define LCD_DATA_DDR  DDRB
#define LCD_CTRL_PORT PORTA
#define LCD_CTRL_DDR  DDRA
#define RS_PIN 0  // PA0
#define RW_PIN 1  // PA1
#define E_PIN  2  // PA2

// --- UART RX 전역 버퍼 (ISR에서 사용) ---
volatile char     g_rx_buf[RX_BUF_SIZE];
volatile uint8_t  g_rx_idx = 0;
volatile uint8_t  g_rx_ready = 0;  // 한 줄 수신 완료 플래그

// --- 함수 선언 ---
void init_ports(void);
void init_uart1(void);
void uart1_putc(char data);
void uart1_puts(const char *str);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);
void lcd_display(const char *str);

// --- UART1 수신 완료 인터럽트 서비스 루틴 ---
// 22번 코드 방식: 문자를 버퍼에 저장하고 개행 수신 시 플래그만 세움
ISR(USART1_RX_vect) {
    char c = UDR1;

    if (c == '\n' || c == '\r') {
        g_rx_buf[g_rx_idx] = '\0';
        if (g_rx_idx > 0) {
            g_rx_ready = 1;  // 메인 루프에 처리 요청
        }
        g_rx_idx = 0;
    } else if (g_rx_idx < RX_BUF_SIZE - 1) {
        g_rx_buf[g_rx_idx++] = c;

        // 32자 꽉 찬 경우 즉시 완료 처리
        if (g_rx_idx >= RX_BUF_SIZE - 1) {
            g_rx_buf[g_rx_idx] = '\0';
            g_rx_ready = 1;
            g_rx_idx = 0;
        }
    }
}

int main(void) {
    init_ports();
    init_uart1();
    lcd_init();
    sei();  // 전역 인터럽트 활성화

    // 시작 안내 메시지 표시
    lcd_goto_xy(0, 0);
    lcd_string("UART RX Ready   ");
    lcd_goto_xy(1, 0);
    lcd_string("9600bps 8-N-1   ");

    while (1) {
        // 수신 완료 플래그 확인 (ISR에서 세팅)
        if (g_rx_ready) {
            g_rx_ready = 0;

            // 수신 버퍼를 로컬에 복사 (ISR과의 충돌 방지)
            char local_buf[RX_BUF_SIZE];
            strcpy(local_buf, (const char *)g_rx_buf);

            // LCD에 수신 문자열 표시
            lcd_display(local_buf);

            // 에코: 수신된 전체 문자열을 1회 송신
            uart1_puts(local_buf);
            uart1_puts("\r\n");
        }
    }
    return 0;
}

/**
 * @brief 포트 방향 및 초기 상태 설정
 */
void init_ports(void) {
    LCD_DATA_DDR  = 0xFF;  // PORTB: LCD 데이터 (출력)
    LCD_CTRL_DDR |= (1 << RS_PIN) | (1 << RW_PIN) | (1 << E_PIN);  // PORTA: LCD 제어 (출력)
    DDRD |= (1 << PD3);   // PD3: UART1 TXD1 (출력)
    DDRD &= ~(1 << PD2);  // PD2: UART1 RXD1 (입력)
}

/**
 * @brief UART1을 9600bps, 8-N-1, 송수신 + RX 인터럽트 활성화로 초기화
 */
void init_uart1(void) {
    uint16_t ubrr_value = (F_CPU / (16UL * BAUD_RATE)) - 1;
    UBRR1H = (unsigned char)(ubrr_value >> 8);
    UBRR1L = (unsigned char)(ubrr_value);

    // TX + RX 활성화, RX 완료 인터럽트(RXCIE1) 활성화
    UCSR1B = (1 << TXEN1) | (1 << RXEN1) | (1 << RXCIE1);

    // 프레임 포맷: 8 데이터 비트, 1 스톱 비트, 패리티 없음
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

/**
 * @brief UART1으로 한 문자를 송신
 */
void uart1_putc(char data) {
    while (!(UCSR1A & (1 << UDRE1)));
    UDR1 = data;
}

/**
 * @brief UART1으로 문자열을 송신
 */
void uart1_puts(const char *str) {
    while (*str) {
        uart1_putc(*str++);
    }
}

/**
 * @brief 수신 문자열을 LCD 2줄에 나누어 표시
 *        1행: 1~16번째 문자, 2행: 17~32번째 문자
 */
void lcd_display(const char *str) {
    char line[17];
    uint8_t len = strlen(str);

    // 1행 출력 (최대 16자)
    memset(line, ' ', 16);
    line[16] = '\0';
    if (len > 0) {
        uint8_t n = (len < 16) ? len : 16;
        memcpy(line, str, n);
    }
    lcd_goto_xy(0, 0);
    lcd_string(line);

    // 2행 출력 (17~32번째 문자)
    memset(line, ' ', 16);
    line[16] = '\0';
    if (len > 16) {
        uint8_t n = (len - 16 < 16) ? len - 16 : 16;
        memcpy(line, str + 16, n);
    }
    lcd_goto_xy(1, 0);
    lcd_string(line);
}

// --- LCD 제어 함수 ---
void lcd_command(unsigned char cmd) {
    LCD_DATA_PORT = cmd;
    LCD_CTRL_PORT &= ~((1 << RS_PIN) | (1 << RW_PIN));
    LCD_CTRL_PORT |= (1 << E_PIN);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << E_PIN);
    _delay_ms(2);
}

void lcd_data(unsigned char data) {
    LCD_DATA_PORT = data;
    LCD_CTRL_PORT |= (1 << RS_PIN);
    LCD_CTRL_PORT &= ~(1 << RW_PIN);
    LCD_CTRL_PORT |= (1 << E_PIN);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << E_PIN);
    _delay_us(50);
}

void lcd_init(void) {
    _delay_ms(50);
    lcd_command(0x38);
    lcd_command(0x0C);
    lcd_command(0x01);
    lcd_command(0x06);
}

void lcd_string(const char *str) {
    while (*str) lcd_data(*str++);
}

void lcd_goto_xy(unsigned char row, unsigned char col) {
    lcd_command((row == 0 ? 0x80 : 0xC0) + col);
}
