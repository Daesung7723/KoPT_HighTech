/*
 * ATmega128 ADC 값 LCD 표시 및 부저 출력
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - ADC0 채널의 아날로그 전압(0~5V)을 10비트 디지털 값으로 변환
 * - 변환된 디지털 값에 비례하여 부저의 주파수를 변경하여 출력
 * - ADC 값과 현재 주파수를 16x2 LCD 화면에 표시
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * ADC 입력 (ADC0): PF0
 * 부저 (+): PE3 (OC3A)
 */

#define F_CPU 16000000UL

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
void init_timer3_pwm(void);
void play_sound(uint16_t frequency);
void stop_sound(void);
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
    init_timer3_pwm(); // PWM 기능 활성화

    char lcd_buffer[17]; // LCD 표시용 문자열 버퍼

    while (1) {
        // ADC0 채널에서 값을 읽어옴
        uint16_t adc_value = read_adc(0);

        // ADC 값(0~1023)을 주파수(100Hz~2000Hz)로 변환
        // adc_value가 0에 가까우면 낮은 음, 1023에 가까우면 높은 음이 남
        uint16_t frequency = 100 + ((long)adc_value * 1900 / 1023);
        play_sound(frequency);

        // LCD에 표시할 문자열 생성
        sprintf(lcd_buffer, "ADC:%04d %4dHz", adc_value, frequency);

        // LCD에 문자열 출력
        lcd_command(0x01); // 화면을 지우고 새로 표시
        lcd_goto_xy(0, 0);
        lcd_string(lcd_buffer);
        
        // 0.1초마다 새로고침하여 반응성 향상
        _delay_ms(100);
    }
    return 0;
}

/**
 * @brief 모든 포트의 방향 및 초기 상태를 설정합니다.
 */
void init_ports(void) {
    LCD_DATA_DDR = 0xFF; // PORTB: LCD 데이터 (출력)
    LCD_CTRL_DDR |= (1<<RS_PIN) | (1<<RW_PIN) | (1<<E_PIN); // PORTA: LCD 제어 (출력)
    DDRE |= (1 << PE3);  // PORTE: 부저 핀 (출력)
    DDRF &= ~(1 << PF0); // PORTF: ADC0 핀 (입력)
}

/**
 * @brief ADC를 초기화합니다.
 */
void init_adc(void) {
    // 기준 전압: AVCC (5V)
    ADMUX = (1 << REFS0);
    // ADC 활성화, Prescaler 128로 설정 (16MHz / 128 = 125kHz)
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

/**
 * @brief 지정된 채널의 ADC 값을 읽어 반환합니다.
 * @param channel 읽을 ADC 채널 번호 (0-7)
 * @return 10비트 디지털 변환 값 (0-1023)
 */
uint16_t read_adc(uint8_t channel) {
    // 채널 선택 (하위 5비트만 변경)
    ADMUX = (ADMUX & 0xE0) | (channel & 0x1F);
    
    // ADC 변환 시작
    ADCSRA |= (1 << ADSC);
    
    // 변환이 완료될 때까지 대기
    while (ADCSRA & (1 << ADSC));
    
    // 10비트 결과값 반환
    return ADC;
}

/**
 * @brief Timer3 PWM을 초기화합니다.
 */
void init_timer3_pwm(void) {
    // 모드 14: Fast PWM, TOP=ICR3
    TCCR3A |= (1 << WGM31);
    TCCR3B |= (1 << WGM33) | (1 << WGM32);
    // OC3A 핀 비반전 모드 (COM3A1=1, COM3A0=0)
    TCCR3A |= (1 << COM3A1);
    // Prescaler 64로 타이머 시작
    TCCR3B |= (1 << CS31) | (1 << CS30);
    stop_sound(); // 초기화 직후 소리 끄기
}

/**
 * @brief 지정된 주파수로 부저를 울립니다.
 * @param frequency 재생할 소리의 주파수(Hz)
 */
void play_sound(uint16_t frequency) {
    if (frequency < 20) { // 너무 낮은 주파수는 소리 끄기
        stop_sound();
        return;
    }
    // 주파수에 맞는 TOP 값(ICR3) 계산: (16,000,000 / 64) / frequency - 1
    uint16_t top_value = (F_CPU / 64 / frequency) - 1;
    ICR3 = top_value;
    // 50% 듀티 사이클 설정
    OCR3A = top_value / 2;
}

/**
 * @brief 부저 소리를 끕니다.
 */
void stop_sound(void) {
    OCR3A = 0; // PWM 출력을 0으로 만들어 소리 중단
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
    lcd_command(0x38); // 8비트, 2줄, 5x8 폰트
    lcd_command(0x0C); // 화면 ON, 커서 OFF
    lcd_command(0x01); // 화면 지우기
    lcd_command(0x06); // 주소 증가, 화면 이동 없음
}

void lcd_string(const char *str) {
    while (*str) lcd_data(*str++);
}

void lcd_goto_xy(unsigned char row, unsigned char col) {
    lcd_command((row == 0 ? 0x80 : 0xC0) + col);
}
