/*
 * ATmega128 ADC 서보 모터 제어
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - ADC0 채널의 아날로그 전압으로 서보 모터의 각도를 제어
 * - Timer3 Fast PWM을 사용하여 20ms(50Hz) 주기의 제어 신호 생성
 * - ADC 값(0-1023)을 각도(-90° ~ +90°)로 변환하여 LCD에 표시
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * ADC 입력 (ADC0): PF0 (가변저항 연결)
 * 서보 모터 신호선: PE3 (OC3A)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>  // sprintf() 함수 사용

// --- LCD 핀 및 포트 정의 ---
#define LCD_DATA_PORT PORTB
#define LCD_DATA_DDR  DDRBs
#define LCD_CTRL_PORT PORTA
#define LCD_CTRL_DDR  DDRA
#define RS_PIN 0 // PA0
#define RW_PIN 1 // PA1
#define E_PIN  2 // PA2

// --- 함수 선언 ---
void init_ports(void);
void init_adc(void);
void init_timer3_pwm_servo(void);
uint16_t read_adc(uint8_t channel);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);

int main(void) {
    init_ports();
    init_adc();
    lcd_init();
    init_timer3_pwm_servo();

    char line1_buf[17];
    char line2_buf[17];

    while (1) {
        // ADC0 채널에서 값을 읽어옴
        uint16_t adc_value = read_adc(0);

        // ADC 값(0-1023)을 서보 모터 펄스 폭(OCR3A 값)으로 변환
        // 0.7ms (-90도) -> OCR 값 1400
        // 2.3ms (+90도) -> OCR 값 4600
        uint16_t ocr_value = 1400 + ((long)adc_value * 3200 / 1023);
        OCR3A = ocr_value;

        // 표시를 위해 ADC 값을 각도(-90 ~ +90)로 변환
        int16_t angle = -90 + ((long)adc_value * 180 / 1023);

        // LCD에 표시할 문자열 생성
        sprintf(line1_buf, "ADC: %04d", adc_value);
        sprintf(line2_buf, "Angle: %4d deg", angle);

        // LCD에 문자열 출력
        lcd_goto_xy(0, 0);
        lcd_string(line1_buf);
        lcd_goto_xy(1, 0);
        lcd_string(line2_buf);
        
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
}

/**
 * @brief ADC를 초기화합니다.
 */
void init_adc(void) {
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
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
    // 모드 14: Fast PWM, TOP=ICR3
    TCCR3A |= (1 << WGM31);
    TCCR3B |= (1 << WGM33) | (1 << WGM32);
    // OC3A 핀 비반전 모드 (COM3A1=1, COM3A0=0)
    TCCR3A |= (1 << COM3A1);
    
    // Prescaler 8로 설정
    TCCR3B |= (1 << CS31);
    
    // TOP 값(ICR3) 설정으로 20ms(50Hz) 주기 생성
    // (16,000,000 / 8) / 40000 = 50Hz
    ICR3 = 39999;
    
    // 초기 각도를 0도(1.5ms)로 설정
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
