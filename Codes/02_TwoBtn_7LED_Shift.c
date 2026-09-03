/*
 * ATmega128 LED Shifting with Button Controls
 * Target: ATmega128
 * Crystal: 16MHz
 *
 * --- Functionality ---
 * - PORTA (PA0-PA7): 8 LEDs, shifting back and forth (0 -> 7 -> 0).
 * - PORTB0 (Button 1): Start / Pause the animation.
 * - PORTB1 (Button 2): Reset the animation to the initial state.
 *
 * --- Connections ---
 * - LEDs (Active Low): VCC(+5V) -> Resistor -> LED Anode(+) -> MCU Pin (PA0-PA7)
 * - Buttons: MCU Pin (PB0, PB1) -> Button -> GND
 */

// CPU 클럭 속도를 16MHz로 정의합니다.
#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>

// --- 상태 변수 선언 ---
volatile uint8_t is_running = 0; // 0: Pause, 1: Run (프로그램 전체에서 사용)
volatile uint8_t led_position = 0; // 현재 LED 위치 (0-7)
volatile int8_t direction = 1;   // 이동 방향 (1: 0->7, -1: 7->0)

int main(void) {
    /* ========= 초기 설정 ========= */

    // 포트 방향 설정
    DDRA = 0xFF; // PORTA의 모든 핀(PA0-PA7)을 출력으로 설정
    DDRB = 0x00; // PORTB의 모든 핀(PB0-PB7)을 입력으로 설정

    // 내부 풀업 저항 활성화
    // 버튼이 눌리지 않았을 때 핀이 HIGH 상태를 유지하도록 하여 노이즈를 방지합니다.
    PORTB = (1 << PB0) | (1 << PB1);

    // 버튼의 이전 상태를 저장하기 위한 변수 (엣지 감지를 위함)
    uint8_t last_button_state = PINB;

    // 타이머 변수
    uint16_t led_timer = 0;
    const uint16_t led_update_time = 500; // 0.5초 (500ms)
    const uint8_t loop_delay = 10;        // 메인 루프의 기본 지연 시간(10ms)

    /* ========= 무한 루프 ========= */
    while (1) {
        // 현재 버튼 상태 읽기
        uint8_t current_button_state = PINB;

        // --- 버튼 0 (Start/Pause) 로직 ---
        // 버튼이 이전에 떼져 있었고(HIGH), 지금 눌렸다면(LOW)
        if ((last_button_state & (1 << PB0)) && !(current_button_state & (1 << PB0))) {
            _delay_ms(20); // 디바운싱: 채터링(떨림) 현상 무시
            if (!(PINB & (1 << PB0))) { // 20ms 후에도 여전히 눌려있는지 다시 확인
                is_running = !is_running; // is_running 상태를 반전 (0 -> 1, 1 -> 0)
            }
        }

        // --- 버튼 1 (Reset) 로직 ---
        // 버튼이 이전에 떼져 있었고(HIGH), 지금 눌렸다면(LOW)
        if ((last_button_state & (1 << PB1)) && !(current_button_state & (1 << PB1))) {
            _delay_ms(20); // 디바운싱
            if (!(PINB & (1 << PB1))) { // 20ms 후에도 여전히 눌려있는지 다시 확인
                is_running = 0;      // 정지 상태로 변경
                led_position = 0;    // LED 위치를 처음으로
                direction = 1;       // 방향을 초기 상태(정방향)로
            }
        }
        
        // 다음 루프를 위해 현재 버튼 상태를 저장
        last_button_state = current_button_state;


        // --- LED 애니메이션 로직 ---
        if (is_running) {
            led_timer += loop_delay; // 루프가 한 번 돌 때마다 10ms씩 시간 누적

            // 누적된 시간이 500ms를 넘으면 LED 위치 업데이트
            if (led_timer >= led_update_time) {
                led_timer = 0; // 타이머 초기화

                // 현재 방향에 따라 위치 이동
                led_position += direction;

                // 경계 확인 및 방향 전환
                if (led_position == 7) {
                    direction = -1; // 7번에 도달하면 역방향으로
                } else if (led_position == 0) {
                    direction = 1;  // 0번에 도달하면 정방향으로
                }
            }
        }

        // --- LED 출력 ---
        // is_running 상태와 관계없이 항상 현재 위치의 LED를 켭니다.
        // (정지 상태일 때 현재 위치에서 멈춰 있도록 하기 위함)
        PORTA = ~(1 << led_position);

        // 메인 루프의 기본 지연
        _delay_ms(loop_delay);
    }

    return 0;
}

/*
[ AI Prompt ]
  Atmega128 코딩.
- PORTA에 LED 8개를연결
- LED는 0번 ~7번, 다시 0번으로 왕복하도록 코딩( 딜레이는 0.5초)
- PORTB에 버튼 2개 연결(PORTB0, PORTB1에 연결)
- 0번 : Start/Pause, 1번 : Reset

[ Code 설명 ]
## 코드 핵심 원리
1. 비동기식 제어 (Non-Blocking): _delay_ms(500)처럼 긴 딜레이를 사용하지 않고, 짧은 _delay_ms(10)를 반복하면서 시간을 직접 셉니다 (led_timer). 이 덕분에 LED가 움직이는 도중에도 버튼 입력을 즉시 감지하여 반응할 수 있습니다.
2. 디바운싱 (Debouncing): 버튼을 누르는 순간 물리적인 접점은 매우 짧은 시간 동안 여러 번 붙었다 떨어지기를 반복합니다. _delay_ms(20)은 이 불안정한 시간을 기다려, 안정된 신호 하나만 입력으로 처리하기 위한 필수적인 기법입니다.
3. 엣지 감지 (Edge Detection): last_button_state 변수를 사용하여 버튼이 눌려있는 상태가 아닌, 눌리는 그 순간(HIGH -> LOW로 바뀌는 시점)을 단 한 번만 감지합니다. 이를 통해 버튼을 길게 누르고 있어도 Start/Pause가 계속 반복되는 현상을 막을 수 있습니다.
4. 상태 변수 관리:
  - is_running: 애니메이션의 진행 여부를 결정하는 '깃발' 역할을 합니다. 이 값이 1일 때만 LED 위치가 업데이트됩니다.
  - led_position, direction: 애니메이션의 현재 상태를 저장하는 변수들입니다. Reset 버튼은 이 변수들을 초기값으로 되돌리는 역할을 합니다.
*/
