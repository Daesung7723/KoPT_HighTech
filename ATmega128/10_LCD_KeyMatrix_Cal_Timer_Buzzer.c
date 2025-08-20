/*
 * ATmega128 4x4 키패드 계산기 & 타이머 (리팩토링 v2)
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 개선 사항 ---
 * - 계산기 및 타이머 상태를 구조체(struct)로 캡슐화
 * - 모드 및 상태를 열거형(enum)으로 정의하여 가독성 향상
 * - LCD 업데이트 로직을 모드별 함수로 분리하여 역할 명확화
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>
#include <avr/interrupt.h>

// --- 상수 정의 ---
// 키패드 값
#define KEY_PLUS  12
#define KEY_MINUS 13
#define KEY_MULTI 14
#define KEY_ENTER 15
#define KEY_CLEAR 10
#define KEY_MODE_SWITCH 11
#define KEY_NONE  255

// 알림음 주파수
#define NOTE_HIGH 880
#define NOTE_LOW  440

// --- 열거형 정의 ---
// 시스템 모드
typedef enum {
    MODE_CALCULATOR,
    MODE_TIMER
} SystemMode;

// 계산기 상태
typedef enum {
    CALC_STATE_INPUT_NUM1,
    CALC_STATE_INPUT_NUM2,
    CALC_STATE_SHOW_RESULT
} CalculatorState;

// --- 구조체 정의 ---
// 계산기 상태 데이터
typedef struct {
    CalculatorState state;
    int32_t num1;
    int32_t num2;
    uint8_t op;
    int32_t display_num1;
    int32_t display_num2;
    uint8_t display_op;
    uint8_t overflow_flag;
} Calculator;

// 타이머 상태 데이터
typedef struct {
    uint8_t min;
    uint8_t sec;
    uint8_t msec;
    uint8_t is_running;
} Timer;

// --- 4x4 키 매트릭스 맵 ---
const uint8_t keymap[4][4] = {
    {KEY_MODE_SWITCH, 0, KEY_CLEAR, KEY_ENTER}, {7, 8, 9, KEY_MULTI},
    {4, 5, 6, KEY_MINUS}, {1, 2, 3, KEY_PLUS}
};

// --- 전역 변수 ---
volatile SystemMode g_current_mode = MODE_CALCULATOR;
volatile Calculator g_calc = {CALC_STATE_INPUT_NUM1, 0, 0, 0, 0, 0, 0, 0};
volatile Timer g_timer = {0, 0, 0, 0};
volatile uint8_t g_update_request = 1;
volatile uint8_t g_play_notification_request = 0;

// --- 함수 선언 ---
void init_all(void);
void init_ports(void);
void init_timer1_interrupt(void);
void init_timer3_pwm(void);
void play_sound(uint16_t frequency);
void stop_sound(void);
void play_notification_sound(void);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);
uint8_t get_key(void);
void process_key_input(uint8_t key);
void update_lcd_display(void);
void update_calculator_display(void);
void update_timer_display(void);

// --- Timer1 인터럽트 서비스 루틴 (10ms 마다 실행) ---
ISR(TIMER1_COMPA_vect) {
    if (!g_timer.is_running) return;

    g_timer.msec++;
    if (g_timer.msec < 100) return;

    g_timer.msec = 0;
    g_timer.sec++;

    if (g_timer.sec >= 60) {
        g_timer.sec = 0;
        g_timer.min++;
        if (g_timer.min >= 60) g_timer.min = 0;
    }

    if (g_timer.sec > 0 && g_timer.sec % 10 == 0) {
        if (g_current_mode == MODE_TIMER) {
            g_play_notification_request = 1;
        }
    }
}

int main(void) {
    init_all();

    uint16_t lcd_refresh_timer = 0;
    const uint16_t lcd_refresh_interval = 100;

    while (1) {
        uint8_t key = get_key();
        if (key != KEY_NONE) {
            process_key_input(key);
        }

        if (g_current_mode == MODE_TIMER) {
            _delay_ms(1);
            lcd_refresh_timer++;
            if (lcd_refresh_timer >= lcd_refresh_interval) {
                lcd_refresh_timer = 0;
                g_update_request = 1;
            }
        }

        if (g_update_request) {
            update_lcd_display();
            g_update_request = 0;
        }

        if (g_play_notification_request) {
            play_notification_sound();
            g_play_notification_request = 0;
        }
    }
    return 0;
}

void init_all(void) {
    init_ports();
    init_timer1_interrupt();
    init_timer3_pwm();
    lcd_init();
    play_notification_sound(); // 부팅 완료 알림음
    sei(); // 전역 인터럽트 활성화
}

void init_ports(void) {
    DDRA |= (1<<PA0) | (1<<PA1) | (1<<PA2);
    DDRB = 0xFF;
    DDRC = 0x0F; PORTC = 0xF0;
    DDRE |= (1 << PE3);
}

void init_timer1_interrupt(void) {
    TCCR1B |= (1 << WGM12);
    OCR1A = 2499;
    TIMSK |= (1 << OCIE1A);
    TCCR1B |= (1 << CS11) | (1 << CS10);
}

void init_timer3_pwm(void) {
    TCCR3A |= (1 << WGM31) | (1 << COM3A1);
    TCCR3B |= (1 << WGM33) | (1 << WGM32) | (1 << CS31) | (1 << CS30);
    stop_sound();
}

void play_sound(uint16_t frequency) {
    if (frequency == 0) { stop_sound(); return; }
    uint16_t top_value = (F_CPU / 64 / frequency) - 1;
    ICR3 = top_value;
    OCR3A = top_value / 2;
}

void stop_sound(void) { OCR3A = 0; }

void play_notification_sound(void) {
    play_sound(NOTE_HIGH); _delay_ms(100);
    play_sound(NOTE_LOW);  _delay_ms(150);
    stop_sound();
}

void process_key_input(uint8_t key) {
    if (key == KEY_MODE_SWITCH) {
        g_current_mode = (g_current_mode == MODE_CALCULATOR) ? MODE_TIMER : MODE_CALCULATOR;
        g_timer.is_running = 0;
    } else {
        if (g_current_mode == MODE_CALCULATOR) {
            switch (g_calc.state) {
                case CALC_STATE_INPUT_NUM1:
                    if (key <= 9) { if (g_calc.num1 < 100000000) g_calc.num1 = g_calc.num1 * 10 + key; }
                    else if (key >= KEY_PLUS && key <= KEY_MULTI) { g_calc.op = key; g_calc.state = CALC_STATE_INPUT_NUM2; }
                    else if (key == KEY_CLEAR) { g_calc.num1 = 0; }
                    break;
                case CALC_STATE_INPUT_NUM2:
                    if (key <= 9) { if (g_calc.num2 < 100000000) g_calc.num2 = g_calc.num2 * 10 + key; }
                    else if (key == KEY_ENTER) {
                        g_calc.display_num1 = g_calc.num1; g_calc.display_num2 = g_calc.num2; g_calc.display_op = g_calc.op;
                        long long temp_result = 0;
                        if (g_calc.op == KEY_PLUS) temp_result = (long long)g_calc.num1 + g_calc.num2;
                        else if (g_calc.op == KEY_MINUS) temp_result = (long long)g_calc.num1 - g_calc.num2;
                        else if (g_calc.op == KEY_MULTI) temp_result = (long long)g_calc.num1 * g_calc.num2;
                        if (temp_result > 2147483647LL || temp_result < -2147483648LL) {
                            g_calc.overflow_flag = 1; g_calc.num1 = 0;
                        } else {
                            g_calc.overflow_flag = 0; g_calc.num1 = (int32_t)temp_result;
                        }
                        g_calc.state = CALC_STATE_SHOW_RESULT;
                    }
                    else if (key == KEY_CLEAR) { g_calc.num1=0; g_calc.num2=0; g_calc.op=0; g_calc.state=CALC_STATE_INPUT_NUM1; }
                    break;
                case CALC_STATE_SHOW_RESULT:
                    if (key <= 9) { g_calc.num1 = key; g_calc.num2 = 0; g_calc.op = 0; g_calc.state = CALC_STATE_INPUT_NUM1; }
                    else if (key >= KEY_PLUS && key <= KEY_MULTI) { g_calc.op = key; g_calc.num2 = 0; g_calc.state = CALC_STATE_INPUT_NUM2; }
                    else if (key == KEY_CLEAR) { g_calc.num1=0; g_calc.num2=0; g_calc.op=0; g_calc.state=CALC_STATE_INPUT_NUM1; }
                    break;
            }
        } else { // MODE_TIMER
            if (key == KEY_ENTER) {
                if (!g_timer.is_running) play_notification_sound();
                g_timer.is_running = !g_timer.is_running;
            } else if (key == KEY_CLEAR) {
                g_timer.is_running = 0; g_timer.min = 0; g_timer.sec = 0; g_timer.msec = 0;
            }
        }
    }
    g_update_request = 1;
}

void update_lcd_display(void) {
    if (g_current_mode == MODE_CALCULATOR) {
        update_calculator_display();
    } else {
        update_timer_display();
    }
}

void update_calculator_display(void) {
    char line1[17] = ""; char line2[17] = ""; char op_char = ' ';
    lcd_command(0x01);
    switch (g_calc.state) {
        case CALC_STATE_INPUT_NUM1: sprintf(line1, "%ld", g_calc.num1); break;
        case CALC_STATE_INPUT_NUM2:
            if (g_calc.op == KEY_PLUS) op_char = '+'; else if (g_calc.op == KEY_MINUS) op_char = '-'; else if (g_calc.op == KEY_MULTI) op_char = '*';
            sprintf(line1, "%ld %c %ld", g_calc.num1, op_char, g_calc.num2);
            break;
        case CALC_STATE_SHOW_RESULT:
            if (g_calc.display_op == KEY_PLUS) op_char = '+'; else if (g_calc.display_op == KEY_MINUS) op_char = '-'; else if (g_calc.display_op == KEY_MULTI) op_char = '*';
            sprintf(line1, "%ld %c %ld =", g_calc.display_num1, op_char, g_calc.display_num2);
            if (g_calc.overflow_flag) { sprintf(line2, "OVERFLOW"); }
            else { sprintf(line2, "%ld", g_calc.num1); }
            break;
    }
    lcd_goto_xy(0, 0); lcd_string(line1);
    lcd_goto_xy(1, 0); lcd_string(line2);
}

void update_timer_display(void) {
    char line1[17] = ""; char line2[17] = "";
    sprintf(line1, "Timer Mode");
    sprintf(line2, "%02d:%02d:%02d", g_timer.min, g_timer.sec, g_timer.msec);
    lcd_goto_xy(0, 0); lcd_string(line1);
    lcd_goto_xy(1, 0); lcd_string(line2);
}

// --- LCD 및 키패드 제어 함수들 (변경 없음) ---
void lcd_command(unsigned char cmd) { PORTB = cmd; PORTA &= ~((1<<PA0)|(1<<PA1)); PORTA |= (1<<PA2); _delay_us(1); PORTA &= ~(1<<PA2); _delay_ms(2); }
void lcd_data(unsigned char data) { PORTB = data; PORTA |= (1<<PA0); PORTA &= ~(1<<PA1); PORTA |= (1<<PA2); _delay_us(1); PORTA &= ~(1<<PA2); _delay_us(50); }
void lcd_init(void) { _delay_ms(50); lcd_command(0x38); lcd_command(0x0C); lcd_command(0x01); lcd_command(0x06); }
void lcd_string(const char *str) { while (*str) lcd_data(*str++); }
void lcd_goto_xy(unsigned char row, unsigned char col) { lcd_command((row == 0 ? 0x80 : 0xC0) + col); }
uint8_t get_key() {
    static uint8_t last_key_state = KEY_NONE; uint8_t current_key = KEY_NONE;
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
