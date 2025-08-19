/*
 * ATmega128 FND & 4x4 키 매트릭스 컨트롤러
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 연결 정보 ---
 * FND 타입: Common Anode (공통 애노드)
 * FND 자리 선택 (Q0-Q3): PA0-PA3
 * FND 세그먼트 (DA-DP): PB0-PB7
 *
 * 키 매트릭스 열 (C0-C3): PC0-PC3 (출력)
 * 키 매트릭스 행 (L0-L3): PC4-PC7 (입력)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>

// --- 7세그먼트 폰트 데이터 (Common Anode 타입) ---
// 비트가 '0'일 때 해당 세그먼트가 켜짐. 인덱스 10='-', 11=공백.
const uint8_t fnd_font[] = {
    0xC0, // 0
    0xF9, // 1
    0xA4, // 2
    0xB0, // 3
    0x99, // 4
    0x92, // 5
    0x82, // 6
    0xF8, // 7
    0x80, // 8
    0x90, // 9
    0xBF, // 10: 하이픈 (-)
    0xFF  // 11: 공백 (Blank)
};

// --- 4x4 키 매트릭스 맵 ---
// 일반적인 계산기 배열. 값 10은 '-' 키를 의미.
const uint8_t keymap[4][4] = {
    {10, 0, 10, 10},
    {7, 8, 9, 10},
    {4, 5, 6, 10},
    {1, 2, 3, 10}
};

/**
 * @brief 열(Column)을 구동하는 방식으로 키 매트릭스를 스캔합니다.
 * @return 눌린 키의 값(0-10)을 반환하거나, 아무것도 눌리지 않았으면 255를 반환합니다.
 */
uint8_t get_key() {
    static uint8_t last_key_state = 255;
    uint8_t current_key = 255;

    // 한 번에 하나의 열(Column)만 LOW로 만들면서 스캔합니다.
    for (uint8_t col = 0; col < 4; col++) {
        // 모든 열을 HIGH로 설정 후, 현재 스캔할 열만 LOW로 설정합니다.
        // 열 맵핑이 반대임 (C3~C0 -> PC0~PC3).
        PORTC = 0xFF;
        PORTC &= ~(1 << (3 - col)); // col 0 -> PC3, col 1 -> PC2, ...

        _delay_us(5); // 신호가 안정화될 때까지 잠시 대기

        // 행(Row) 핀들을 읽습니다 (L0~L3 -> PC4~PC7).
        for (uint8_t row = 0; row < 4; row++) {
            if (!(PINC & (1 << (row + 4)))) {
                current_key = keymap[row][col];
                break;
            }
        }
        if (current_key != 255) break;
    }

    // 키를 눌렀다가 뗄 때만 키 값을 반환합니다.
    if (current_key == 255 && last_key_state != 255) {
        uint8_t pressed_key = last_key_state;
        last_key_state = 255;
        return pressed_key;
    }
    
    last_key_state = current_key;
    return 255; // 새로운 키 입력이 감지되지 않음.
}

int main(void) {
    /* ========= 초기 설정 ========= */
    DDRA = 0xFF; // FND 자리 선택 핀 (출력)
    DDRB = 0xFF; // FND 세그먼트 핀 (출력)
    
    // 키 매트릭스: 열 C3-C0 (PC0-3)는 출력, 행 L0-L3 (PC4-7)는 입력
    DDRC = 0x0F;
    // 행 입력 핀들의 내부 풀업 저항 활성화
    PORTC = 0xF0;

    uint8_t display_data[4] = {11, 11, 11, 11}; // 공백으로 화면 초기화
    uint8_t current_digit = 0;
    const uint8_t loop_delay = 2;

    /* ========= 메인 루프 ========= */
    while (1) {
        // --- 1. 키 입력 스캔 ---
        uint8_t pressed_key = get_key();

        if (pressed_key != 255) {
            // 새로운 키가 눌렸으면, 표시 버퍼를 업데이트합니다.
            display_data[0] = pressed_key; // 첫 번째 자리에 표시
            display_data[1] = 11; // 나머지 자리는 공백
            display_data[2] = 11; // 공백
            display_data[3] = 11; // 공백
        }

        // --- 2. FND 구동 (멀티플렉싱) ---
        // 모든 자리를 꺼서 고스팅(잔상) 현상을 방지합니다.
        PORTA = 0x00;
        
        // 현재 자리에 맞는 세그먼트 데이터를 출력합니다.
        PORTB = fnd_font[display_data[current_digit]];
        
        // 현재 자리를 켭니다.
        PORTA = ~(1 << current_digit);
        
        // 다음 루프에서 표시할 자리를 가리키도록 인덱스를 이동합니다.
        current_digit = (current_digit + 1) % 4;
        
        _delay_ms(loop_delay);
    }
    return 0;
}
