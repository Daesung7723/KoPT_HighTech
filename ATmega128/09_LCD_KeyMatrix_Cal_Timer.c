/*
 * ATmega128 4x4 키패드 계산기 & 타이머
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - 계산기 모드와 타이머 모드 간 전환 기능
 * - Timer1 인터럽트를 사용한 10ms 정밀 타이머
 * - 타이머 Start/Pause/Reset 기능
 * - 타이머 모드에서 0.1초 주기로 LCD를 안정적으로 새로고침
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * 키 매트릭스 열 (C3-C0): PC0-PC3 (출력), 행 (L0-L3): PC4-PC7 (입력)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>
#include <avr/interrupt.h> // 인터럽트 사용

// --- 모드 및 키패드 정의 ---
#define MODE_CALCULATOR 0
#define MODE_TIMER      1

#define KEY_PLUS  12
#define KEY_MINUS 13
#define KEY_MULTI 14
#define KEY_ENTER 15 // 계산기: '=', 타이머: 'Start/Pause'
#define KEY_CLEAR 10 // 계산기: 'C', 타이머: 'Reset'
#define KEY_MODE_SWITCH 11 // 모드 전환 키
#define KEY_NONE  255

// --- 계산기 상태 정의 ---
#define STATE_INPUT_NUM1 0
#define STATE_INPUT_NUM2 1
#define STATE_SHOW_RESULT 2

// --- 4x4 키 매트릭스 맵 ---
const uint8_t keymap[4][4] = {
    {KEY_MODE_SWITCH, 0, KEY_CLEAR, KEY_ENTER}, // L0
    {7, 8, 9, KEY_MULTI},                     // L1
    {4, 5, 6, KEY_MINUS},                     // L2
    {1, 2, 3, KEY_PLUS}                       // L3
};

// --- 전역 변수 ---
volatile uint8_t current_mode = MODE_CALCULATOR;
volatile uint8_t update_request = 1;

// 계산기용 변수
volatile uint8_t calc_state = STATE_INPUT_NUM1;
volatile int32_t num1 = 0, num2 = 0;
volatile uint8_t op = 0;
volatile int32_t display_num1 = 0, display_num2 = 0;
volatile uint8_t display_op = 0;
volatile uint8_t overflow_flag = 0;

// 타이머용 변수
volatile uint8_t timer_min = 0, timer_sec = 0, timer_msec = 0;
volatile uint8_t is_timer_running = 0;

// --- 함수 선언 ---
void init_ports(void);
void init_timer1(void);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);
uint8_t get_key(void);
void process_calculator_keys(uint8_t key);
void process_timer_keys(uint8_t key);
void update_lcd_display(void);

// --- Timer1 인터럽트 서비스 루틴 ---
ISR(TIMER1_COMPA_vect) {
    if (is_timer_running) {
        timer_msec++;
        if (timer_msec >= 100) {
            timer_msec = 0;
            timer_sec++;
            if (timer_sec >= 60) {
                timer_sec = 0;
                timer_min++;
                if (timer_min >= 60) {
                    timer_min = 0;
                }
            }
        }
    }
}

int main(void) {
    init_ports();
    init_timer1();
    lcd_init();
    sei(); // 전역 인터럽트 활성화

    uint16_t lcd_refresh_timer = 0;
    // --- 핵심 수정 사항: LCD 새로고침 주기를 0.1초로 변경 ---
    const uint16_t lcd_refresh_interval = 100; // 100ms (0.1초)

    while (1) {
        uint8_t key = get_key();
        if (key != KEY_NONE) {
            if (key == KEY_MODE_SWITCH) {
                current_mode = !current_mode; // 모드 토글
                is_timer_running = 0; 
            } else {
                if (current_mode == MODE_CALCULATOR) {
                    process_calculator_keys(key);
                } else {
                    process_timer_keys(key);
                }
            }
            update_request = 1; // 키 입력 시 즉시 화면 업데이트 요청
        }

        if (current_mode == MODE_TIMER) {
            _delay_ms(1); 
            lcd_refresh_timer++;
            if (lcd_refresh_timer >= lcd_refresh_interval) {
                lcd_refresh_timer = 0;
                update_request = 1; // 100ms 마다 업데이트 요청
            }
        }

        if (update_request) {
            update_lcd_display();
            update_request = 0;
        }
    }
    return 0;
}

void init_ports(void) {
    DDRA |= (1<<PA0) | (1<<PA1) | (1<<PA2); // LCD 제어
    DDRB = 0xFF; // LCD 데이터
    DDRC = 0x0F; PORTC = 0xF0; // 키패드
}

void init_timer1(void) {
    TCCR1B |= (1 << WGM12); // CTC 모드 설정
    OCR1A = 2499; // 10ms 인터럽트 주기 설정
    TIMSK |= (1 << OCIE1A); // Timer1 비교 매치 A 인터럽트 활성화
    TCCR1B |= (1 << CS11) | (1 << CS10); // Prescaler 64로 타이머 시작
}

void process_calculator_keys(uint8_t key) {
    // (이전 코드의 계산기 키 처리 로직과 동일)
    switch (calc_state) {
        case STATE_INPUT_NUM1:
            if (key <= 9) { if (num1 < 100000000) num1 = num1 * 10 + key; }
            else if (key >= KEY_PLUS && key <= KEY_MULTI) { op = key; calc_state = STATE_INPUT_NUM2; }
            else if (key == KEY_CLEAR) { num1 = 0; }
            break;
        case STATE_INPUT_NUM2:
            if (key <= 9) { if (num2 < 100000000) num2 = num2 * 10 + key; }
            else if (key == KEY_ENTER) {
                display_num1 = num1; display_num2 = num2; display_op = op;
                long long temp_result = 0;
                if (op == KEY_PLUS) temp_result = (long long)num1 + num2;
                else if (op == KEY_MINUS) temp_result = (long long)num1 - num2;
                else if (op == KEY_MULTI) temp_result = (long long)num1 * num2;
                if (temp_result > 2147483647LL || temp_result < -2147483648LL) {
                    overflow_flag = 1; num1 = 0;
                } else {
                    overflow_flag = 0; num1 = (int32_t)temp_result;
                }
                calc_state = STATE_SHOW_RESULT;
            }
            else if (key == KEY_CLEAR) { num1=0; num2=0; op=0; calc_state=STATE_INPUT_NUM1; }
            break;
        case STATE_SHOW_RESULT:
            if (key <= 9) { num1 = key; num2 = 0; op = 0; calc_state = STATE_INPUT_NUM1; }
            else if (key >= KEY_PLUS && key <= KEY_MULTI) { op = key; num2 = 0; calc_state = STATE_INPUT_NUM2; }
            else if (key == KEY_CLEAR) { num1=0; num2=0; op=0; calc_state=STATE_INPUT_NUM1; }
            break;
    }
}

void process_timer_keys(uint8_t key) {
    if (key == KEY_ENTER) { // Start/Pause
        is_timer_running = !is_timer_running;
    } else if (key == KEY_CLEAR) { // Reset
        is_timer_running = 0;
        timer_min = 0;
        timer_sec = 0;
        timer_msec = 0;
    }
}

void update_lcd_display(void) {
    char line1[17] = "";
    char line2[17] = "";
    char op_char = ' ';

    if (current_mode == MODE_CALCULATOR) {
        lcd_command(0x01); // 계산기 모드에서는 화면을 매번 지움
        switch (calc_state) {
            case STATE_INPUT_NUM1: sprintf(line1, "%ld", num1); break;
            case STATE_INPUT_NUM2:
                if (op == KEY_PLUS) op_char = '+'; else if (op == KEY_MINUS) op_char = '-'; else if (op == KEY_MULTI) op_char = '*';
                sprintf(line1, "%ld %c %ld", num1, op_char, num2);
                break;
            case STATE_SHOW_RESULT:
                if (display_op == KEY_PLUS) op_char = '+'; else if (display_op == KEY_MINUS) op_char = '-'; else if (display_op == KEY_MULTI) op_char = '*';
                sprintf(line1, "%ld %c %ld =", display_num1, op_char, display_num2);
                if (overflow_flag) { sprintf(line2, "OVERFLOW"); }
                else { sprintf(line2, "%ld", num1); }
                break;
        }
    } else { // MODE_TIMER
        // 타이머 모드에서는 화면을 지우지 않고 덮어씀 (깜빡임 최소화)
        sprintf(line1, "Timer Mode");
        sprintf(line2, "%02d:%02d:%02d", timer_min, timer_sec, timer_msec);
    }

    lcd_goto_xy(0, 0);
    lcd_string(line1);
    lcd_goto_xy(1, 0);
    lcd_string(line2);
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
