/*
 * ATmega128 UART1 수신 데이터 LCD 표시 및 에코
 * Target: ATmega128
 * Crystal: 16MHz
 *
 * --- 기능 ---
 * 1. UART1 인터럽트(ISR) 방식으로 수신
 * 2. UART1 (RXD1: PD2, TXD1: PD3) 사용
 * 3. PC에서 UART1으로 문자열 전송
 * 4. 수신 데이터를 LCD에 표시 (새 메시지 수신 시 이전 메시지 삭제 후 표시)
 * 5. 수신 데이터에 "ATmega) " 프롬프트 붙여서 PC로 리턴
 * 6. PC 수신측에서 줄바꿈 처리 (\r\n)
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0~D7) : PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * UART1 RXD1         : PD2 (입력)
 * UART1 TXD1         : PD3 (출력)
 */

#define F_CPU    16000000UL
#define BAUD     9600
#define RX_BUF_SIZE  33        // LCD 2줄 × 16자 + null terminator

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

/* -------------------------------------------------------
 *  LCD 핀 정의
 * ----------------------------------------------------- */
#define LCD_DATA_PORT  PORTB
#define LCD_DATA_DDR   DDRB
#define LCD_CTRL_PORT  PORTA
#define LCD_CTRL_DDR   DDRA
#define RS_PIN  0   // PA0
#define RW_PIN  1   // PA1
#define E_PIN   2   // PA2

/* -------------------------------------------------------
 *  UART1 수신 전역 버퍼 (ISR 공유)
 * ----------------------------------------------------- */
volatile char    g_rx_buf[RX_BUF_SIZE];  // 수신 문자 저장 버퍼
volatile uint8_t g_rx_idx   = 0;         // 버퍼 인덱스
volatile uint8_t g_rx_ready = 0;         // 수신 완료 플래그

/* -------------------------------------------------------
 *  함수 선언
 * ----------------------------------------------------- */
void    init_ports(void);
void    init_uart1(void);
void    uart1_putc(char c);
void    uart1_puts(const char *str);
void    lcd_command(unsigned char cmd);
void    lcd_data(unsigned char data);
void    lcd_init(void);
void    lcd_string(const char *str);
void    lcd_goto_xy(unsigned char row, unsigned char col);
void    lcd_show(const char *str);

/* -------------------------------------------------------
 *  UART1 수신 완료 인터럽트 (ISR)
 *  - 문자를 버퍼에 누적
 *  - '\n' 또는 '\r' 수신 시 완료 플래그 세팅
 *  - 버퍼가 가득 차면 즉시 완료 처리
 * ----------------------------------------------------- */
ISR(USART1_RX_vect) {
    char c = UDR1;

    if (c == '\n' || c == '\r') {
        if (g_rx_idx > 0) {              // 내용 있을 때만 완료 처리 (\r\n 이중 트리거 방지)
            g_rx_buf[g_rx_idx] = '\0';
            g_rx_ready = 1;
        }
        g_rx_idx = 0;
    } else if (g_rx_idx < RX_BUF_SIZE - 1) {
        g_rx_buf[g_rx_idx++] = c;
        if (g_rx_idx >= RX_BUF_SIZE - 1) {  // 버퍼 가득 참
            g_rx_buf[g_rx_idx] = '\0';
            g_rx_ready = 1;
            g_rx_idx   = 0;
        }
    }
}

/* -------------------------------------------------------
 *  main
 * ----------------------------------------------------- */
int main(void) {
    init_ports();
    init_uart1();
    lcd_init();
    sei();                               // 전역 인터럽트 활성화

    /* 초기 LCD 안내 메시지 */
    lcd_goto_xy(0, 0);
    lcd_string("UART1 RX Ready  ");
    lcd_goto_xy(1, 0);
    lcd_string("9600bps  8-N-1  ");

    while (1) {

        /* 수신 완료 플래그 확인 */
        if (g_rx_ready) {

            /* 22번 방식: 플래그를 가장 먼저 클리어
             * → lcd_show() 실행 중 새 데이터가 와도 이번 처리와 분리됨 */
            g_rx_ready = 0;

            /* 버퍼 복사 (22번과 동일하게 단순 strcpy) */
            char local_buf[RX_BUF_SIZE];
            strcpy(local_buf, (const char *)g_rx_buf);

            /* LCD 이전 메시지 삭제 후 새 메시지 표시 */
            lcd_show(local_buf);

            /* 송신 전 RX 인터럽트 비활성화
             * → TX 신호가 RX로 피드백되어 ISR이 재트리거되는 현상 방지 */
            UCSR1B &= ~(1 << RXCIE1);

            /* "ATmega) " 프롬프트 + 메시지 + \r\n 리턴 */
            uart1_puts("\r\nATmega) ");
            uart1_puts(local_buf);
            uart1_puts("\r\n");

            /* 송신 완료 후 처리:
             * 1. 하드웨어 RX 버퍼 플러시
             *    → 송신 중 루프백으로 들어온 잔류 데이터 제거 (RXC1 클리어)
             * 2. g_rx_idx 초기화
             *    → 플러시 전 ISR이 부분적으로 쌓아둔 데이터도 무효화
             * 3. RX 인터럽트 재활성화 */
            while (UCSR1A & (1 << RXC1)) {
                (void)UDR1;   // 버퍼 읽기로 RXC1 클리어
            }
            g_rx_idx = 0;
            UCSR1B |= (1 << RXCIE1);
        }
    }
    return 0;
}

/* -------------------------------------------------------
 *  포트 초기화
 * ----------------------------------------------------- */
void init_ports(void) {
    LCD_DATA_DDR  = 0xFF;                               // PORTB: LCD 데이터 (출력)
    LCD_CTRL_DDR |= (1<<RS_PIN)|(1<<RW_PIN)|(1<<E_PIN);// PORTA: LCD 제어 (출력)
    DDRD |=  (1 << PD3);                                // PD3: TXD1 (출력)
    DDRD &= ~(1 << PD2);                                // PD2: RXD1 (입력)
}

/* -------------------------------------------------------
 *  UART1 초기화 : 9600bps, 8-N-1, TX+RX, RX 인터럽트
 * ----------------------------------------------------- */
void init_uart1(void) {
    uint16_t ubrr = (F_CPU / (16UL * BAUD)) - 1;
    UBRR1H = (unsigned char)(ubrr >> 8);
    UBRR1L = (unsigned char)(ubrr);
    UCSR1B = (1<<TXEN1) | (1<<RXEN1) | (1<<RXCIE1);   // TX, RX, RX인터럽트 활성화
    UCSR1C = (1<<UCSZ11) | (1<<UCSZ10);                // 8 데이터비트, 1 스톱비트, 패리티 없음
}

/* -------------------------------------------------------
 *  UART1 송신 함수
 * ----------------------------------------------------- */
void uart1_putc(char c) {
    while (!(UCSR1A & (1 << UDRE1)));
    UDR1 = c;
}

void uart1_puts(const char *str) {
    while (*str) uart1_putc(*str++);
}

/* -------------------------------------------------------
 *  LCD 표시 함수
 *  - 수신 문자열을 1행(1~16자), 2행(17~32자)에 나누어 표시
 *  - 빈 자리는 공백으로 채워 이전 메시지를 완전히 덮어씀
 * ----------------------------------------------------- */
void lcd_show(const char *str) {
    char line[17];
    uint8_t len = (uint8_t)strlen(str);

    /* 1행 */
    memset(line, ' ', 16);
    line[16] = '\0';
    if (len > 0) {
        uint8_t n = (len < 16) ? len : 16;
        memcpy(line, str, n);
    }
    lcd_goto_xy(0, 0);
    lcd_string(line);

    /* 2행 */
    memset(line, ' ', 16);
    line[16] = '\0';
    if (len > 16) {
        uint8_t n = ((len - 16) < 16) ? (len - 16) : 16;
        memcpy(line, str + 16, n);
    }
    lcd_goto_xy(1, 0);
    lcd_string(line);
}

/* -------------------------------------------------------
 *  LCD 제어 함수
 * ----------------------------------------------------- */
void lcd_command(unsigned char cmd) {
    LCD_DATA_PORT  = cmd;
    LCD_CTRL_PORT &= ~((1<<RS_PIN)|(1<<RW_PIN));
    LCD_CTRL_PORT |=  (1<<E_PIN);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1<<E_PIN);
    _delay_ms(2);
}

void lcd_data(unsigned char data) {
    LCD_DATA_PORT  = data;
    LCD_CTRL_PORT |=  (1<<RS_PIN);
    LCD_CTRL_PORT &= ~(1<<RW_PIN);
    LCD_CTRL_PORT |=  (1<<E_PIN);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1<<E_PIN);
    _delay_us(50);
}

void lcd_init(void) {
    _delay_ms(50);
    lcd_command(0x38);  // Function Set: 8비트, 2행, 5×8 폰트
    lcd_command(0x0C);  // Display ON, 커서 OFF
    lcd_command(0x01);  // Display Clear
    lcd_command(0x06);  // Entry Mode: 커서 오른쪽 이동
}

void lcd_string(const char *str) {
    while (*str) lcd_data(*str++);
}

void lcd_goto_xy(unsigned char row, unsigned char col) {
    lcd_command((row == 0 ? 0x80 : 0xC0) + col);
}
