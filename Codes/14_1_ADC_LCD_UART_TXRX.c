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
 * 7. 수신 완료 조건: 마지막 문자 수신 후 RX_TIMEOUT_MS 동안 추가 수신 없으면 완료
 *    (엔터키 불필요 - 타임아웃 방식)
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0~D7) : PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * UART1 RXD1         : PD2 (입력)
 * UART1 TXD1         : PD3 (출력)
 */

#define F_CPU          16000000UL
#define BAUD           9600
#define RX_BUF_SIZE    33     // LCD 2줄 × 16자 + null terminator
#define RX_TIMEOUT_MS  100    // 마지막 수신 후 100ms 내 추가 수신 없으면 완료

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
 *  전역 변수
 * ----------------------------------------------------- */
volatile char     g_rx_buf[RX_BUF_SIZE]; // UART1 수신 버퍼
volatile uint8_t  g_rx_idx    = 0;       // 버퍼 인덱스
volatile uint8_t  g_rx_ready  = 0;       // 수신 완료 플래그
volatile uint16_t g_rx_timeout = 0;      // 마지막 수신 후 경과 ms (Timer0 틱)
volatile uint8_t  g_rx_active  = 0;      // 수신 진행 중 플래그

/* -------------------------------------------------------
 *  함수 선언
 * ----------------------------------------------------- */
void init_ports(void);
void init_uart1(void);
void init_timer0(void);
void uart1_putc(char c);
void uart1_puts(const char *str);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);
void lcd_show(const char *str);

/* -------------------------------------------------------
 *  Timer0 비교일치 ISR - 1ms 틱
 *  수신 진행 중일 때 타임아웃 카운터를 증가시킴
 *  RX_TIMEOUT_MS 초과 시 수신 완료 처리
 * ----------------------------------------------------- */
ISR(TIMER0_COMP_vect) {
    if (g_rx_active) {
        g_rx_timeout++;
        if (g_rx_timeout >= RX_TIMEOUT_MS) {
            // 타임아웃 완료 처리
            g_rx_buf[g_rx_idx] = '\0';
            g_rx_ready  = 1;
            g_rx_active = 0;
            g_rx_timeout = 0;
            g_rx_idx    = 0;
        }
    }
}

/* -------------------------------------------------------
 *  UART1 수신 완료 인터럽트 (ISR)
 *  - \r, \n 무시 (타임아웃 방식이므로 엔터키 불필요)
 *  - 문자 수신 시 타임아웃 카운터 리셋
 *  - 버퍼 가득 참 시 즉시 완료 처리
 * ----------------------------------------------------- */
ISR(USART1_RX_vect) {
    char c = UDR1;

    // \r, \n은 버퍼에 저장하지 않고 무시
    if (c == '\r' || c == '\n') return;

    if (g_rx_idx < RX_BUF_SIZE - 1) {
        g_rx_buf[g_rx_idx++] = c;
        g_rx_active  = 1;    // 수신 진행 중 표시
        g_rx_timeout = 0;    // 타임아웃 카운터 리셋

        // 버퍼 가득 참 → 즉시 완료
        if (g_rx_idx >= RX_BUF_SIZE - 1) {
            g_rx_buf[g_rx_idx] = '\0';
            g_rx_ready  = 1;
            g_rx_active = 0;
            g_rx_timeout = 0;
            g_rx_idx    = 0;
        }
    }
}

/* -------------------------------------------------------
 *  main
 * ----------------------------------------------------- */
int main(void) {
    init_ports();
    init_uart1();
    init_timer0();
    lcd_init();
    sei();  // 전역 인터럽트 활성화

    // 초기 LCD 안내 메시지
    lcd_goto_xy(0, 0);
    lcd_string("UART1 RX Ready  ");
    lcd_goto_xy(1, 0);
    lcd_string("9600bps  8-N-1  ");

    while (1) {

        // 수신 완료 플래그 확인 (타임아웃 또는 버퍼 풀)
        if (g_rx_ready) {

            // 플래그 먼저 클리어 (22번 방식)
            g_rx_ready = 0;

            // 버퍼 복사
            char local_buf[RX_BUF_SIZE];
            strcpy(local_buf, (const char *)g_rx_buf);

            // LCD 표시 (이전 메시지 덮어씀)
            lcd_show(local_buf);

            // 송신 중 RX 인터럽트 비활성화 (루프백 방지)
            UCSR1B &= ~(1 << RXCIE1);

            // "ATmega) " 프롬프트 + 수신 문자열 + 줄바꿈 리턴
            uart1_puts("\r\nATmega) ");
            uart1_puts(local_buf);
            uart1_puts("\r\n");

            // 하드웨어 RX 버퍼 플러시 + 상태 초기화 후 RX 인터럽트 재활성화
            while (UCSR1A & (1 << RXC1)) {
                (void)UDR1;
            }
            g_rx_idx     = 0;
            g_rx_active  = 0;
            g_rx_timeout = 0;
            UCSR1B |= (1 << RXCIE1);
        }
    }
    return 0;
}

/* -------------------------------------------------------
 *  포트 초기화
 * ----------------------------------------------------- */
void init_ports(void) {
    LCD_DATA_DDR  = 0xFF;
    LCD_CTRL_DDR |= (1<<RS_PIN) | (1<<RW_PIN) | (1<<E_PIN);
    DDRD |=  (1 << PD3);   // PD3: TXD1 (출력)
    DDRD &= ~(1 << PD2);   // PD2: RXD1 (입력)
}

/* -------------------------------------------------------
 *  UART1 초기화: 9600bps, 8-N-1, TX+RX+RX인터럽트
 * ----------------------------------------------------- */
void init_uart1(void) {
    uint16_t ubrr = (F_CPU / (16UL * BAUD)) - 1;
    UBRR1H = (unsigned char)(ubrr >> 8);
    UBRR1L = (unsigned char)(ubrr);
    UCSR1B = (1<<TXEN1) | (1<<RXEN1) | (1<<RXCIE1);
    UCSR1C = (1<<UCSZ11) | (1<<UCSZ10);  // 8비트, 1 스톱비트, 패리티 없음
}

/* -------------------------------------------------------
 *  Timer0 초기화: CTC 모드, 1ms 틱
 *  16MHz / 64 = 250kHz → OCR0 = 249 → 1ms 주기
 * ----------------------------------------------------- */
void init_timer0(void) {
    TCCR0  = (1<<WGM01) | (1<<CS01) | (1<<CS00); // CTC, prescaler 64
    OCR0   = 249;                                  // 1ms
    TIMSK |= (1<<OCIE0);                           // 비교일치 인터럽트 활성화
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
 *  LCD 표시: 1행(1~16자), 2행(17~32자), 공백으로 이전 메시지 덮어씀
 * ----------------------------------------------------- */
void lcd_show(const char *str) {
    char line[17];
    uint8_t len = (uint8_t)strlen(str);

    // 1행
    memset(line, ' ', 16);
    line[16] = '\0';
    if (len > 0) {
        uint8_t n = (len < 16) ? len : 16;
        memcpy(line, str, n);
    }
    lcd_goto_xy(0, 0);
    lcd_string(line);

    // 2행
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
    LCD_CTRL_PORT &= ~((1<<RS_PIN) | (1<<RW_PIN));
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
    lcd_command(0x38);  // Function Set: 8비트, 2행
    lcd_command(0x0C);  // Display ON, 커서 OFF
    lcd_command(0x01);  // Display Clear
    lcd_command(0x06);  // Entry Mode: 커서 오른쪽
}

void lcd_string(const char *str) {
    while (*str) lcd_data(*str++);
}

void lcd_goto_xy(unsigned char row, unsigned char col) {
    lcd_command((row == 0 ? 0x80 : 0xC0) + col);
}
