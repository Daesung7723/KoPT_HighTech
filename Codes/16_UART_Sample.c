/*
 * ATmega128 ADC 서보 모터 제어 및 UART 통신
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - ADC0 채널의 아날로그 전압으로 서보 모터의 각도를 제어
 * - Timer3 Fast PWM을 사용하여 20ms(50Hz) 주기의 제어 신호 생성
 * - ADC 값과 각도를 LCD에 표시
 * - ADC/각도의 순수 데이터 값과 LCD 표시 내용을 UART1을 통해 GUI 형식으로 송신
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * ADC 입력 (ADC0): PF0 (가변저항 연결)
 * 서보 모터 신호선: PE3 (OC3A)
 * UART1 TXD1: PD3
 */

#define F_CPU 16000000UL
#define BAUD_RATE 9600

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>  // sprintf() 함수 사용

// --- LCD 핀 및 포트 정의 ---
#define LCD_DATA_PORT PORTB
#define LCD_DATA_DDR  DDRB
#define LCD_CTRL_PORT PORTA
#define LCD_CTRL_DDR  DDRA
#define RS_PIN 0 // PA0
#define RW_PIN 1 // PA1
#define E_PIN  2 // PA2

// --- 함수 선언 ---
void init_ports(void);
void init_adc(void);
void init_timer3_pwm_servo(void);
void init_uart1(void);
void uart1_putc(char data);
void uart1_puts(const char *str);
uint16_t read_adc(uint8_t channel);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);

int main(void) {
    init_ports();
    init_adc();
    init_uart1();
    lcd_init();
    init_timer3_pwm_servo();

    char line1_buf[17];
    char line2_buf[17];
    char uart_buf[20]; // 순수 데이터 전송용 버퍼

    while (1) {
        // ADC0 채널에서 값을 읽어옴
        uint16_t adc_value = read_adc(0);

        // ADC 값(0-1023)을 서보 모터 펄스 폭(OCR3A 값)으로 변환
        uint16_t ocr_value = 1400 + ((long)adc_value * 3200 / 1023);
        OCR3A = ocr_value;

        // 표시를 위해 ADC 값을 각도(-90 ~ +90)로 변환
        int16_t angle = -90 + ((long)adc_value * 180 / 1023);

        // LCD용 문자열 생성
        sprintf(line1_buf, "ADC: %04d", adc_value);
        sprintf(line2_buf, "Angle: %4d deg", angle);

        // LCD에 문자열 출력
        lcd_goto_xy(0, 0);
        lcd_string(line1_buf);
        lcd_goto_xy(1, 0);
        lcd_string(line2_buf);
        
        // --- UART 송신 ---
        // 1. 순수 데이터 송신 (GUI 파싱용)
        sprintf(uart_buf, "[ADC]%d\n", adc_value);
        uart1_puts(uart_buf);
        sprintf(uart_buf, "[ANGLE]%d\n", angle);
        uart1_puts(uart_buf);

        // 2. LCD 표시 내용 송신
        uart1_puts("[LCD Data-0]");
        uart1_puts(line1_buf);
        uart1_puts("\n");

        uart1_puts("[LCD Data-1]");
        uart1_puts(line2_buf);
        uart1_puts("\n");

        // 50ms 마다 새로고침
        _delay_ms(50);
    }
    return 0;
}

/**
 * @brief 모든 포트의 방향 및 초기 상태를 설정합니다.
 */
void init_ports(void) {
    LCD_DATA_DDR = 0xFF; // PORTB: LCD 데이터 (출력)
    LCD_CTRL_DDR |= (1<<RS_PIN) | (1<<RW_PIN) | (1<<E_PIN); // PORTA: LCD 제어 (출력)
    DDRE |= (1 << PE3);  // PORTE: 서보 모터 핀 (출력)
    DDRF &= ~(1 << PF0); // PORTF: ADC0 핀 (입력)
    DDRD |= (1 << PD3);  // PORTD: UART1 TXD1 핀 (출력)
}

/**
 * @brief ADC를 초기화합니다.
 */
void init_adc(void) {
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

/**
 * @brief UART1을 9600, 8-N-1 조건으로 초기화합니다.
 */
void init_uart1(void) {
    // Baud Rate 설정 (9600bps @ 16MHz)
    uint16_t ubrr_value = (F_CPU / (16UL * BAUD_RATE)) - 1;
    UBRR1H = (unsigned char)(ubrr_value >> 8);
    UBRR1L = (unsigned char)ubrr_value;

    // 송신(TX) 활성화
    UCSR1B = (1 << TXEN1);
    
    // 프레임 포맷 설정: 8 데이터 비트, 1 스톱 비트, 패리티 없음
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

/**
 * @brief UART1으로 한 문자를 송신합니다.
 */
void uart1_putc(char data) {
    // 송신 버퍼가 빌 때까지 대기
    while (!(UCSR1A & (1 << UDRE1)));
    UDR1 = data;
}

/**
 * @brief UART1으로 문자열을 송신합니다.
 */
void uart1_puts(const char *str) {
    while (*str) {
        uart1_putc(*str++);
    }
}

/**
 * @brief 지정된 채널의 ADC 값을 읽어 반환합니다.
 */
uint16_t read_adc(uint8_t channel) {
    ADMUX = (ADMUX & 0xE0) | (channel & 0x1F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}

/**
 * @brief 서보 모터 제어를 위한 Timer3 PWM을 초기화합니다. (50Hz 주기)
 */
void init_timer3_pwm_servo(void) {
    TCCR3A |= (1 << WGM31) | (1 << COM3A1);
    TCCR3B |= (1 << WGM33) | (1 << WGM32) | (1 << CS31);
    ICR3 = 39999;
    OCR3A = 3000;
}

// --- LCD 제어 함수들 (변경 없음) ---
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
