/*
 * ATmega128 Multi-LED Blinking Example
 * Target: ATmega128
 * Crystal: 16MHz
 *
 * Connections:
 * - LED 1: PA1 Pin (1-second interval)
 * - LED 2: PA3 Pin (0.5-second interval)
 */

// CPU 클럭 속도를 16MHz로 정의합니다.
// _delay_ms() 함수의 정확한 시간 계산을 위해 필수입니다.
#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>

int main(void) {
    /* 초기 설정 부분 */

    // PA1과 PA3 핀을 출력으로 설정합니다.
    // (1 << PA1) | (1 << PA3)는 0b00001010 값을 만들어
    // DDRA의 1번, 3번 비트를 동시에 1(출력)로 설정합니다.
    DDRA |= (1 << PA1) | (1 << PA3);

    // 각 LED의 시간을 추적하기 위한 카운터 변수를 선언합니다.
    // unsigned int는 0 ~ 65535 범위의 값을 가집니다.
    unsigned int counter1 = 0; // 1초 주기 LED용 카운터
    unsigned int counter2 = 0; // 0.5초 주기 LED용 카운터

    // 기준 시간 단위를 1ms로 정합니다.
    const int base_delay = 1;

    /* 무한 루프 부분 */
    while (1) {
        // --- 1번 LED (1초 주기) 로직 ---
        if (counter1 >= 1000) {
            PORTA ^= (1 << PA1); // PA1 핀 상태를 반전(Toggle)
            counter1 = 0;        // 카운터 초기화
        }

        // --- 2번 LED (0.5초 주기) 로직 ---
        if (counter2 >= 500) {
            PORTA ^= (1 << PA3); // PA3 핀 상태를 반전(Toggle)
            counter2 = 0;        // 카운터 초기화
        }

        // 1ms 동안 대기합니다.
        _delay_ms(base_delay);

        // 대기한 시간만큼 각 카운터를 증가시킵니다.
        counter1 += base_delay;
        counter2 += base_delay;
    }

    return 0;
}
