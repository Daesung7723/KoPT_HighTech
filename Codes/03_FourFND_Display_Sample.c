/*
 * ATmega128 4-Digit 7-Segment (FND) Control Example
 * Target: ATmega128
 * Crystal: 16MHz
 * FND Type: Common Anode (with inverted select signal)
 *
 * --- Desired Output ---
 * Show "4321" on the display.
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>

// 7-세그먼트 폰트 데이터 (Common Anode 타입)
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
    0xFF  // 10: Blank
};

// --- 핵심 수정 사항 ---
// 표시할 데이터를 4, 3, 2, 1로 변경합니다.
const uint8_t display_data[4] = {4, 3, 2, 1};

int main(void) {
    // 포트 방향 설정
    DDRA = 0xFF;
    DDRB = 0xFF;

    uint8_t current_digit = 0;

    while (1) {
        // 현재 자리에 해당하는 숫자 데이터를 PORTB로 출력
        PORTB = fnd_font[display_data[current_digit]];

        // 자리 선택 신호를 반전하여 출력 (해당 핀만 LOW)
        PORTA = ~(1 << current_digit);

        // 짧은 시간 대기
        _delay_ms(2);

        // 다음 자리로 이동
        current_digit++;
        if (current_digit >= 4) {
            current_digit = 0;
        }
    }
    return 0;
}

/*
[ AI Prompt ]
Atmega128 코딩.
- 4개의 7-Segment가 연결되어 있는 FND를 사용
- Q0~Q3는 PORTA의 0~3에 연결
- DA~DP는 순서대로 PORTB의 0~7로 연결
- 프로그램은 Q0부터 1, 2, 3, 4 가 나타나도록 구현
- Anode Type

[ Code 설명 ]
## 코드 핵심 원리
1. Common Anode (공통 애노드) 방식
FND 타입: Common Anode는 여러 개의 LED 양극(+)이 하나로 묶여있는 구조입니다.

동작 원리:
  - 자리를 선택하는 Q핀(PORTA)에 HIGH(5V) 신호를 주어 해당 자리에 전원을 공급합니다.
  - 켜고 싶은 세그먼트에 연결된 DA~DP핀(PORTB)에 LOW(0V) 신호를 주어 전류를 흐르게 합니다.
폰트 데이터: 위와 같은 이유로 fnd_font 배열의 값은 켜고 싶은 세그먼트의 비트가 0이 됩니다. 예를 들어 숫자 '1'(B, C 세그먼트만 켬)의 패턴은 0b11111001입니다.

2. 동적 구동 방식 (Persistence of Vision)
사람의 눈은 짧은 시간 동안 본 이미지를 기억하는 잔상 효과가 있습니다. 이 원리를 이용하여 FND를 제어합니다.

while(1) 루프의 동작:
  - (1번째 자리): PORTA로 1번째 자리를 켜고, PORTB로 숫자 '4'의 패턴을 출력합니다. → 아주 잠깐 동안 4 이 표시됩니다.
  - (2번째 자리): PORTA로 2번째 자리를 켜고, PORTB로 숫자 '3'의 패턴을 출력합니다. → 아주 잠깐 동안 3 이 표시됩니다.
  - (3번째 자리): PORTA로 3번째 자리를 켜고, PORTB로 숫자 '2'의 패턴을 출력합니다. → 아주 잠깐 동안 2 이 표시됩니다.
  - (4번째 자리): PORTA로 4번째 자리를 켜고, PORTB로 숫자 '1'의 패턴을 출력합니다. → 아주 잠깐 동안 1 이 표시됩니다.

결과: 이 과정이 _delay_ms(2)라는 매우 짧은 간격으로 빠르게 반복되면, 우리 눈에는 모든 자리가 동시에 켜져 "1234" 가 표시되는 것처럼 보입니다. 이것이 동적 구동의 핵심입니다.
