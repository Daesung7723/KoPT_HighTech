/*
 * ATmega128 4x4 키패드 LCD 계산기 (리팩토링 버전)
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - 직전 연산 결과를 다음 연산의 첫 값으로 사용하는 기능 추가
 * - 계산 로직과 디스플레이 로직을 분리하여 코드 구조 개선
 * - 32비트 정수 연산, 8자리 수 입력, 오버플로우 메시지 표시
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * 키 매트릭스 열 (C3-C0): PC0-PC3 (출력), 행 (L0-L3): PC4-PC7 (입력)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h> // itoa() 함수 사용
#include <stdio.h>  // sprintf() 함수 사용

// --- LCD 핀 및 포트 정의 ---
#define LCD_DATA_PORT PORTB
#define LCD_DATA_DDR  DDRB
#define LCD_CTRL_PORT PORTA
#define LCD_CTRL_DDR  DDRA
#define RS_PIN 0 // PA0
#define RW_PIN 1 // PA1
#define E_PIN  2 // PA2

// --- 키패드 및 연산자 정의 ---
#define KEY_PLUS  12
#define KEY_MINUS 13
#define KEY_MULTI 14
#define KEY_ENTER 15
#define KEY_CLEAR 10
#define KEY_NONE  255 // 키가 눌리지 않은 상태

// --- 계산기 상태 정의 ---
#define STATE_INPUT_NUM1 0
#define STATE_INPUT_NUM2 1
#define STATE_SHOW_RESULT 2

// --- 4x4 키 매트릭스 맵 ---
const uint8_t keymap[4][4] = {
    {KEY_CLEAR, 0, KEY_CLEAR, KEY_ENTER}, 
	{7, 8, 9, KEY_MULTI},
    {4, 5, 6, KEY_MINUS}, 
	{1, 2, 3, KEY_PLUS}
};

// --- 전역 변수 ---
volatile uint8_t state = STATE_INPUT_NUM1;
volatile int32_t num1 = 0, num2 = 0;
volatile uint8_t op = 0;
volatile uint8_t update_request = 1;

// 화면 표시용 변수
volatile int32_t display_num1 = 0, display_num2 = 0;
volatile uint8_t display_op = 0;
volatile uint8_t overflow_flag = 0;

// --- 함수 선언 ---
void init_ports(void);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);
uint8_t get_key(void);
void process_key_input(uint8_t key);
void update_lcd_display(void);

int main(void) {
    init_ports();
    lcd_init();

    while (1) {
        uint8_t key = get_key();
        if (key != KEY_NONE) {
            process_key_input(key);
            update_request = 1;
        }

        if (update_request) {
            update_lcd_display();
            update_request = 0;
        }
    }
    return 0;
}

void init_ports(void) {
    DDRC = 0x0F; PORTC = 0xF0;
    LCD_DATA_DDR = 0xFF;
    LCD_CTRL_DDR |= (1<<RS_PIN) | (1<<RW_PIN) | (1<<E_PIN);
}

void process_key_input(uint8_t key) {
    switch (state) {
        case STATE_INPUT_NUM1:
            if (key <= 9) { if (num1 < 100000000) num1 = num1 * 10 + key; }
            else if (key >= KEY_PLUS && key <= KEY_MULTI) { op = key; state = STATE_INPUT_NUM2; }
            else if (key == KEY_CLEAR) { num1 = 0; }
            break;
        case STATE_INPUT_NUM2:
            if (key <= 9) { if (num2 < 100000000) num2 = num2 * 10 + key; }
            else if (key == KEY_ENTER) {
                // 계산 전, 화면에 표시할 연산식을 별도 변수에 저장
                display_num1 = num1;
                display_num2 = num2;
                display_op = op;

                long long temp_result = 0;
                if (op == KEY_PLUS) temp_result = (long long)num1 + num2;
                else if (op == KEY_MINUS) temp_result = (long long)num1 - num2;
                else if (op == KEY_MULTI) temp_result = (long long)num1 * num2;
                
                if (temp_result > 2147483647LL || temp_result < -2147483648LL) {
                    overflow_flag = 1;
                    num1 = 0; // 오버플로우 시 다음 연산은 0부터 시작
                } else {
                    overflow_flag = 0;
                    num1 = (int32_t)temp_result; // 다음 연산을 위해 결과값을 num1에 저장
                }
                state = STATE_SHOW_RESULT;
            }
            else if (key == KEY_CLEAR) { num1=0; num2=0; op=0; state=STATE_INPUT_NUM1; }
            break;
        case STATE_SHOW_RESULT:
            if (key <= 9) { // 새 계산 시작
                num1 = key; num2 = 0; op = 0; state = STATE_INPUT_NUM1;
            }
            else if (key >= KEY_PLUS && key <= KEY_MULTI) { // 직전 결과(num1)를 이용해 연산 계속
                op = key;
                num2 = 0;
                state = STATE_INPUT_NUM2;
            }
            else if (key == KEY_CLEAR) { num1=0; num2=0; op=0; state=STATE_INPUT_NUM1; }
            break;
    }
}

void update_lcd_display(void) {
    char line1[17] = "";
    char line2[17] = "";
    char op_char = ' ';

    lcd_command(0x01);

    switch (state) {
        case STATE_INPUT_NUM1:
            sprintf(line1, "%ld", num1);
            break;
        case STATE_INPUT_NUM2:
            if (op == KEY_PLUS) op_char = '+';
            else if (op == KEY_MINUS) op_char = '-';
            else if (op == KEY_MULTI) op_char = '*';
            sprintf(line1, "%ld %c %ld", num1, op_char, num2);
            break;
        case STATE_SHOW_RESULT:
            if (display_op == KEY_PLUS) op_char = '+';
            else if (display_op == KEY_MINUS) op_char = '-';
            else if (display_op == KEY_MULTI) op_char = '*';
            
            sprintf(line1, "%ld %c %ld =", display_num1, op_char, display_num2);
            
            if (overflow_flag) {
                sprintf(line2, "OVERFLOW");
            } else {
                sprintf(line2, "%ld", num1); // 결과값(현재 num1)을 출력
            }
            break;
    }

    lcd_goto_xy(0, 0);
    lcd_string(line1);
    lcd_goto_xy(1, 0);
    lcd_string(line2);
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
    _delay_ms(50); lcd_command(0x38); lcd_command(0x0C);
    lcd_command(0x01); lcd_command(0x06);
}
void lcd_string(const char *str) { while (*str) lcd_data(*str++); }
void lcd_goto_xy(unsigned char row, unsigned char col) {
    lcd_command((row == 0 ? 0x80 : 0xC0) + col);
}
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
