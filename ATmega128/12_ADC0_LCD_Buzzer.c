/*
 * ATmega128 ADC 값 LCD 표시 및 부저 출력 (ON/OFF 기능 추가)
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - ADC0 채널의 아날로그 전압(0~5V)을 10비트 디지털 값으로 변환
 * - 키패드의 Start/Pause 버튼으로 소리 출력 기능을 활성화/비활성화
 * - 기능 활성화 시, 변환된 ADC 값에 비례하여 부저 주파수 변경
 * - ADC 값, 현재 주파수, 기능 상태를 16x2 LCD 화면에 표시
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * ADC 입력 (ADC0): PF0
 * 부저 (+): PE3 (OC3A)
 * 키 매트릭스 열 (C3-C0): PC0-PC3 (출력), 행 (L0-L3): PC4-PC7 (입력)
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

// --- 키패드 정의 ---
#define KEY_ENTER 15 // Start/Pause 기능으로 사용
#define KEY_NONE  255

// --- 4x4 키 매트릭스 맵 ---
const uint8_t keymap[4][4] = {
    {11, 0, 10, KEY_ENTER}, {7, 8, 9, 14},
    {4, 5, 6, 13}, {1, 2, 3, 12}
};

// --- 함수 선언 ---
void init_ports(void);
void init_adc(void);
void init_timer3_pwm(void);
void play_sound(uint16_t frequency);
void stop_sound(void);
uint16_t read_adc(uint8_t channel);
uint8_t get_key(void);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);

int main(void) {
    init_ports();
    init_adc();
    lcd_init();
    init_timer3_pwm();

    char line1_buf[17];
    char line2_buf[17];
    uint8_t is_sound_enabled = 0; // 0: OFF, 1: ON

    while (1) {
        // 키패드 입력 확인
        uint8_t key = get_key();
        if (key == KEY_ENTER) {
            is_sound_enabled = !is_sound_enabled; // 상태 토글
        }

        // ADC0 채널에서 값을 읽어옴
        uint16_t adc_value = read_adc(0);
        
        if (is_sound_enabled) {
            // ADC 값(0~1023)을 주파수(100Hz~2000Hz)로 변환
            uint16_t frequency = 100 + ((long)adc_value * 1900 / 1023);
            play_sound(frequency);
            sprintf(line1_buf, "ADC:%04d %4dHz", adc_value, frequency);
        } else {
            stop_sound(); // 기능이 꺼져있으면 소리 중단
            sprintf(line1_buf, "ADC:%04d ----Hz", adc_value);
        }

        // LCD 두 번째 줄에 상태 표시
        sprintf(line2_buf, "Sound: %s", is_sound_enabled ? "ON " : "OFF");

        // --- 핵심 수정 사항: 화면을 업데이트하기 전에 지웁니다 ---
        lcd_command(0x01); // 화면 전체 지우기

        // LCD에 문자열 출력
        lcd_goto_xy(0, 0);
        lcd_string(line1_buf);
        lcd_goto_xy(1, 0);
        lcd_string(line2_buf);
        
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
    DDRC = 0x0F; PORTC = 0xF0; // PORTC: 키패드
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
 * @brief Timer3 PWM을 초기화합니다.
 */
void init_timer3_pwm(void) {
    TCCR3A |= (1 << WGM31) | (1 << COM3A1);
    TCCR3B |= (1 << WGM33) | (1 << WGM32) | (1 << CS31) | (1 << CS30);
    stop_sound();
}

/**
 * @brief 지정된 주파수로 부저를 울립니다.
 */
void play_sound(uint16_t frequency) {
    if (frequency < 20) {
        stop_sound();
        return;
    }
    uint16_t top_value = (F_CPU / 64 / frequency) - 1;
    ICR3 = top_value;
    OCR3A = top_value / 2;
}

/**
 * @brief 부저 소리를 끕니다.
 */
void stop_sound(void) {
    OCR3A = 0;
}

/**
 * @brief 키패드를 스캔하여 눌린 키 값을 반환합니다. (Non-blocking)
 */
uint8_t get_key() {
    static uint8_t last_key_state = KEY_NONE;
    uint8_t current_key = KEY_NONE;
    for (uint8_t col = 0; col < 4; col++) {
        PORTC = 0xFF; PORTC &= ~(1 << (3 - col)); _delay_us(5);
        for (uint8_t row = 0; row < 4; row++) {
            if (!(PINC & (1 << (row + 4)))) { current_key = keymap[row][col]; break; }
        }
        if (current_key != KEY_NONE) break;
    }
    if (current_key == KEY_NONE && last_key_state != KEY_NONE) {
        uint8_t pressed_key = last_key_state; last_key_state = KEY_NONE; return pressed_key;
    }
    last_key_state = current_key; return KEY_NONE;
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
