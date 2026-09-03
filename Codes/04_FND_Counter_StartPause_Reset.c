/*
 * ATmega128 FND Counter with Button Controls
 * Target: ATmega128
 * Crystal: 16MHz
 * FND Type: Common Anode (with inverted select signal)
 *
 * --- Functionality ---
 * - A 4-digit FND displays a number from 0 to 9999.
 * - Button 0 (PC0): Starts or pauses the counter.
 * - Button 1 (PC1): Resets the counter to 0 and pauses.
 * - Counter increments by 1 every 0.1 seconds when running.
 *
 * --- Connections ---
 * - Digit Select (Q0-Q3): PA0-PA3
 * - Segment Data (DA-DP): PB0-PB7
 * - Buttons: PC0, PC1 -> Button -> GND
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>

// 7-세그먼트 폰트 데이터 (Common Anode 타입)
const uint8_t fnd_font[10] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90
};

// 전역 상태 변수
volatile uint16_t count = 0;
volatile uint8_t is_running = 0;

int main(void) {
    /* ========= 초기 설정 ========= */
    DDRA = 0xFF; // PORTA (자리 선택)는 출력
    DDRB = 0xFF; // PORTB (세그먼트 데이터)는 출력
    DDRC = 0x00; // PORTC (버튼 입력)는 입력
    PORTC = (1 << PC0) | (1 << PC1); // PORTC 내부 풀업 저항 활성화

    uint8_t display_data[4] = {0, 0, 0, 0};
    uint8_t current_digit = 0;
    
    uint16_t timer_ms = 0;
    uint8_t last_button_state = PINC;
    const uint8_t loop_delay = 2;

    /* ========= 무한 루프 ========= */
    while (1) {
        /* --- 1. 버튼 입력 처리 --- */
        uint8_t current_button_state = PINC;

        if ((last_button_state & (1 << PC0)) && !(current_button_state & (1 << PC0))) {
            _delay_ms(20);
            if (!(PINC & (1 << PC0))) is_running = !is_running;
        }

        if ((last_button_state & (1 << PC1)) && !(current_button_state & (1 << PC1))) {
            _delay_ms(20);
            if (!(PINC & (1 << PC1))) {
                is_running = 0;
                count = 0;
            }
        }
        last_button_state = current_button_state;

        /* --- 2. 카운터 로직 처리 --- */
        if (is_running) {
            timer_ms += loop_delay;
            if (timer_ms >= 100) {
                timer_ms = 0;
                count++;
                if (count > 9999) count = 0;
            }
        }

        /* --- 3. FND 표시 데이터 업데이트 (핵심 수정 사항) --- */
        // 물리적인 첫 번째 자리(PA0)에 천의 자리를, 마지막 자리(PA3)에 일의 자리를 할당합니다.
        display_data[0] = (count / 1000) % 10; // 첫 번째 자리에 천의 자리
        display_data[1] = (count / 100) % 10;  // 두 번째 자리에 백의 자리
        display_data[2] = (count / 10) % 10;   // 세 번째 자리에 십의 자리
        display_data[3] = count % 10;          // 네 번째 자리에 일의 자리

        /* --- 4. FND 동적 구동 --- */
        PORTA = 0xFF; // 블랭킹
        PORTB = fnd_font[display_data[current_digit]];
        PORTA = ~(1 << current_digit); // 자리 선택
        
        current_digit++;
        if (current_digit >= 4) {
            current_digit = 0;
        }

        _delay_ms(loop_delay);
    }
    return 0;
}

/*
## 코드 핵심 원리
1. 상태 관리: is_running이라는 변수를 사용하여 현재 카운터가 동작 중인지, 멈춤 상태인지를 관리합니다. Start/Pause 버튼은 이 변수의 값을 0과 1 사이에서 전환(Toggle)하는 역할을 합니다.
2. 시간 계산: _delay_ms(100)과 같은 긴 딜레이를 사용하면 그 시간 동안 버튼 입력이나 FND 디스플레이가 멈추게 됩니다. 이를 해결하기 위해 루프마다 _delay_ms(2)라는 매우 짧은 딜레이를 주고, timer_ms 변수에 이 시간을 계속 더해 100ms가 되었는지 확인하는 방식으로 0.1초를 셉니다.
3. 숫자 분리: FND에 1234라는 숫자를 표시하려면 1, 2, 3, 4라는 각 자리의 숫자를 알아내야 합니다. 아래의 나머지(%)와 나누기(/) 연산을 사용하여 count 변수 값으로부터 각 자리의 숫자를 추출합니다.
  - count % 10 : 일의 자리
  - (count / 10) % 10 : 십의 자리
  - (count / 100) % 10 : 백의 자리
4. 버튼 처리: 이전 코드들과 마찬가지로 디바운싱과 엣지 감지 로직을 적용하여 버튼이 한 번만 정확하게 인식되도록 했습니다.
  */
