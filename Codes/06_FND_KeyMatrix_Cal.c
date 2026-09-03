/*
 * ATmega128 4x4 키패드 계산기 (v2)
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - 세 자리 수(0-999)까지 입력 가능
 * - 결과값이 4자리 표시 범위를(-999 ~ 9999) 벗어나면 흐르듯이 스크롤하여 표시
 *
 * --- 연결 정보 ---
 * FND 타입: Common Anode (공통 애노드), Select 신호 반전
 * FND 자리 선택 (Q0-Q3): PA0-PA3 (PA0이 최상위 자릿수)
 * FND 세그먼트 (DA-DP): PB0-PB7
 * 키 매트릭스 열 (C3-C0): PC0-PC3 (출력), 행 (L0-L3): PC4-PC7 (입력)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h> // abs(), itoa() 함수 사용
#include <string.h> // strlen() 함수 사용

// --- 키패드 및 연산자 정의 ---
#define KEY_PLUS  12
#define KEY_MINUS 13
#define KEY_MULTI 14
#define KEY_ENTER 15
#define KEY_CLEAR 10

// --- 계산기 상태 정의 ---
#define STATE_INPUT_NUM1 0      // 첫 번째 숫자 입력
#define STATE_INPUT_NUM2 1      // 두 번째 숫자 입력
#define STATE_SHOW_RESULT 2     // 결과 표시
#define STATE_SHOW_OVERFLOW 3   // 오버플로우 결과 스크롤 표시

// --- 7세그먼트 폰트 데이터 (Common Anode 타입) ---
const uint8_t fnd_font[] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90, // 0-9
    0xBF, 0xFF, 0xE3, 0x9C, 0xC6 // 10='-', 11=' ', 12='+', 13='*', 14='E'
};

// --- 4x4 키 매트릭스 맵 ---
const uint8_t keymap[4][4] = {
    {KEY_CLEAR, 0, KEY_CLEAR, KEY_ENTER}, {7, 8, 9, KEY_MULTI},
    {4, 5, 6, KEY_MINUS}, {1, 2, 3, KEY_PLUS}
};

// --- 전역 변수 ---
volatile uint8_t state = STATE_INPUT_NUM1;
volatile int16_t num1 = 0, num2 = 0;
volatile uint8_t op = 0;
char overflow_buffer[8]; // 오버플로우된 결과값을 문자열로 저장

// Non-blocking 방식의 키 스캔 함수
uint8_t get_key() {
    static uint8_t last_key_state = 255;
    uint8_t current_key = 255;
    for (uint8_t col = 0; col < 4; col++) {
        PORTC = 0xFF; PORTC &= ~(1 << (3 - col)); _delay_us(5);
        for (uint8_t row = 0; row < 4; row++) {
            if (!(PINC & (1 << (row + 4)))) { current_key = keymap[row][col]; break; }
        }
        if (current_key != 255) break;
    }
    if (current_key == 255 && last_key_state != 255) {
        uint8_t pressed_key = last_key_state; last_key_state = 255; return pressed_key;
    }
    last_key_state = current_key; return 255;
}

// 숫자를 FND 버퍼에 변환하는 함수
void number_to_display(int16_t n, uint8_t* buf) {
    if (n < 0) { buf[0] = 10; n = abs(n); }
    else { buf[0] = (n / 1000) % 10; }
    buf[1] = (n / 100) % 10; buf[2] = (n / 10) % 10; buf[3] = n % 10;
    if (n < 1000 && buf[0] != 10) buf[0] = 11;
    if (n < 100) buf[1] = 11; if (n < 10) buf[2] = 11;
}

int main(void) {
    DDRA = 0xFF; DDRB = 0xFF; DDRC = 0x0F; PORTC = 0xF0;

    uint8_t display_data[4] = {11, 11, 11, 0};
    uint8_t current_digit = 0;
    const uint8_t loop_delay = 2;
    uint16_t scroll_timer = 0;
    uint8_t scroll_index = 0;

    while (1) {
        uint8_t key = get_key();
        if (key != 255) {
            switch (state) {
                case STATE_INPUT_NUM1:
                    if (key <= 9) { if (num1 < 100) num1 = num1 * 10 + key; } // 3자리까지 입력
                    else if (key >= KEY_PLUS && key <= KEY_MULTI) { op = key; state = STATE_INPUT_NUM2; }
                    else if (key == KEY_CLEAR) { num1 = 0; }
                    break;
                case STATE_INPUT_NUM2:
                    if (key <= 9) { if (num2 < 100) num2 = num2 * 10 + key; } // 3자리까지 입력
                    else if (key == KEY_ENTER) {
                        int16_t result = 0;
                        if (op == KEY_PLUS) result = num1 + num2;
                        else if (op == KEY_MINUS) result = num1 - num2;
                        else if (op == KEY_MULTI) result = num1 * num2;
                        
                        if (result > 9999 || result < -999) { // 오버플로우 확인
                            itoa(result, overflow_buffer, 10); // 결과를 문자열로 변환
                            scroll_index = 0; scroll_timer = 0;
                            state = STATE_SHOW_OVERFLOW;
                        } else {
                            num1 = result;
                            state = STATE_SHOW_RESULT;
                        }
                    } else if (key == KEY_CLEAR) { num1=0; num2=0; op=0; state=STATE_INPUT_NUM1; }
                    break;
                case STATE_SHOW_RESULT:
                case STATE_SHOW_OVERFLOW: // 결과 표시 또는 스크롤 중에 아무 키나 누르면 초기화
                    num1 = (key <= 9) ? key : 0; num2 = 0; op = 0;
                    state = STATE_INPUT_NUM1;
                    break;
            }
        }

        // --- 현재 상태에 맞게 FND 버퍼 업데이트 ---
        switch (state) {
            case STATE_INPUT_NUM1: number_to_display(num1, display_data); break;
            case STATE_INPUT_NUM2:
                if (num2 == 0 && op != 0) { // 연산자 입력 직후
                    display_data[0]=11; display_data[1]=11; display_data[2]=11;
                    if (op == KEY_PLUS) display_data[3] = 12;
                    else if (op == KEY_MINUS) display_data[3] = 10;
                    else if (op == KEY_MULTI) display_data[3] = 13;
                } else { number_to_display(num2, display_data); }
                break;
            case STATE_SHOW_RESULT: number_to_display(num1, display_data); break;
            case STATE_SHOW_OVERFLOW:
                scroll_timer += loop_delay;
                if (scroll_timer >= 400) { // 0.4초마다 스크롤
                    scroll_timer = 0;
                    scroll_index++;
                    if (scroll_index >= strlen(overflow_buffer)) scroll_index = 0;
                }
                // 스크롤 버퍼에서 4글자를 가져와 display_data에 채움
                for (int i=0; i<4; i++) {
                    char c = overflow_buffer[scroll_index + i];
                    if (scroll_index + i >= strlen(overflow_buffer)) { display_data[i] = 11; } // 문자열 끝이면 공백
                    else if (c == '-') { display_data[i] = 10; } // 마이너스 기호
                    else { display_data[i] = c - '0'; } // 숫자
                }
                break;
        }

        // --- FND 동적 구동 ---
        PORTA = 0xFF; // 블랭킹
        PORTB = fnd_font[display_data[current_digit]];
        PORTA = ~(1 << current_digit); // 자리 선택
        current_digit = (current_digit + 1) % 4;
        _delay_ms(loop_delay);
    }
    return 0;
}
