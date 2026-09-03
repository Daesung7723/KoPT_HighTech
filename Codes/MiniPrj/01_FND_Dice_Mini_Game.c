/*
 * ================================================================
 * Mini Project : 4-Digit Dice Poker with LED Feedback
 * Target MCU   : ATmega128
 * Clock        : 16MHz
 *
 * [하드웨어 구성]
 * - 4-digit FND  : Common Anode
 * - sw1          : PC0 (내부 풀업 사용, 버튼 누르면 0)
 * - sw2          : PC1 (내부 풀업 사용, 버튼 누르면 0)
 * - LED          : PC4 (Active Low, sink 전류 방식)
 *
 * [동작 설명]
 * 1. 초기값은 0000이다.
 * 2. sw1을 누르고 있는 동안 현재 자리 숫자가 1~6 범위에서 0.2초 간격으로 변한다.
 * 3. sw1을 떼면 현재 자리 숫자를 확정한다.
 *    단, 값이 1~6이 아니면 확정하지 않고 다시 롤링한다.
 * 4. 다음 sw1 입력 때 다음 자리로 이동한다.
 * 5. 4자리가 모두 확정되면 포커 족보를 판정한다.
 * 6. 족보에 따라 LED가 정해진 패턴으로 점멸한다.
 *    패턴 1사이클이 끝나면 LED는 3초 동안 꺼진 뒤 다시 같은 패턴을 반복한다.
 * 7. sw2를 누르면 언제든 전체 상태가 초기화된다.
 *
 * [족보별 LED 패턴]
 * HAND_FOUR     : ON-OFF × 4회, 150ms 간격 → 3초 OFF
 * HAND_TRIPLE   : ON-OFF × 3회, 200ms 간격 → 3초 OFF
 * HAND_TWO_PAIR : ON-OFF × 2회 → 500ms OFF → ON-OFF × 2회, 250ms 간격 → 3초 OFF
 * HAND_ONE_PAIR : ON-OFF × 2회, 250ms 간격 → 3초 OFF
 * HAND_NONE     : LED OFF
 *
 * [핀 배치]
 * - PA0~PA3 : FND 자리 선택 (digit select)
 * - PB0~PB7 : FND 세그먼트 데이터
 * - PC0     : sw1 (내부 풀업, Active Low)
 * - PC1     : sw2 (내부 풀업, Active Low)
 * - PC4     : LED (Active Low, sink 전류)
 * ================================================================
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ================================================================
 * 1. 상수 정의
 * ================================================================
 * 프로그램 전체에서 반복해서 사용하는 숫자, 핀 번호, 상태 코드를
 * 매크로로 정의해 두면 가독성이 좋아지고 수정이 쉬워진다.
 */

// 버튼이 연결된 PORTC의 비트 번호
#define BTN_SW1_PC          PC0
#define BTN_SW2_PC          PC1

// LED가 연결된 PORTC의 비트 번호
#define LED_PC              PC4

// 메인 루프 1회 실행마다 기다리는 시간
// 너무 크면 FND가 깜빡이고, 너무 작으면 처리 흐름이 복잡해진다.
#define LOOP_DELAY_MS       5

// 숫자가 한 번 바뀌는 간격 (200ms = 0.2초)
#define ROLL_INTERVAL_MS    200

// LED 패턴이 한 번 끝난 뒤 LED를 꺼두는 시간 (3초)
#define LED_OFF_HOLD_MS     3000

// 포커 족보를 숫자로 표현
#define HAND_NONE           0
#define HAND_ONE_PAIR       1
#define HAND_TWO_PAIR       2
#define HAND_TRIPLE         3
#define HAND_FOUR           4

// LED 동작 모드
// LED_MODE_PATTERN  : 실제 패턴 점멸 중
// LED_MODE_OFF_HOLD : 패턴이 끝난 뒤 3초 쉬는 구간
#define LED_MODE_PATTERN    0
#define LED_MODE_OFF_HOLD   1

/* ================================================================
 * 2. FND 관련
 * ================================================================
 * 4-digit FND를 동적 구동(multiplexing) 방식으로 제어한다.
 * 한 번에 4자리를 동시에 켜는 것이 아니라,
 * 매우 빠르게 1자리씩 번갈아 켜서 사람 눈에는 4자리가 모두 켜진 것처럼 보이게 한다.
 */

// Common Anode용 숫자 폰트 데이터
// 각 값은 PB0~PB7로 출력되어 7세그먼트의 각 segment를 제어한다.
const uint8_t fnd_font[10] = {
    0xC0, // 0
    0xF9, // 1
    0xA4, // 2
    0xB0, // 3
    0x99, // 4
    0x92, // 5
    0x82, // 6
    0xF8, // 7
    0x80, // 8
    0x90  // 9
};

// FND에 실제로 표시할 4자리 값
// digits[0]: 천의 자리, digits[1]: 백의 자리
// digits[2]: 십의 자리, digits[3]: 일의 자리
volatile uint8_t digits[4] = {0, 0, 0, 0};

// 현재 FND에서 몇 번째 자리를 표시할지 저장 (0~3 순환)
volatile uint8_t mux_digit_idx = 0;

/*
 * FND 관련 포트 초기화
 * - PA : 자리 선택 신호 출력
 * - PB : 세그먼트 데이터 출력
 */
void fnd_init(void) {
    DDRA = 0xFF;
    DDRB = 0xFF;
}

/*
 * FND 한 자리만 표시하는 함수
 * 이 함수는 메인 루프에서 계속 반복 호출된다.
 * 호출될 때마다 표시 자리를 0→1→2→3→0... 순서로 바꾼다.
 */
void fnd_display_step(void) {
    uint8_t value = digits[mux_digit_idx];

    // 혹시라도 잘못된 값이 들어오면 0으로 안전 처리
    if (value > 9) value = 0;

    // 먼저 모든 자리를 끈다 (블랭킹)
    PORTA = 0xFF;

    // 현재 표시할 숫자의 segment 데이터 출력
    PORTB = fnd_font[value];

    // 해당 자리만 선택해서 켠다
    // Common Anode이므로 자리 선택이 active low 형태
    PORTA = ~(1 << mux_digit_idx);

    // 다음 호출에서는 다음 자리를 표시하도록 인덱스 증가
    mux_digit_idx++;
    if (mux_digit_idx >= 4) mux_digit_idx = 0;
}

/* ================================================================
 * 3. 버튼 & LED 관련
 * ================================================================
 * 버튼은 내부 풀업 저항을 사용한다.
 * 따라서 버튼을 누르지 않았을 때는 1, 누르면 GND로 연결되어 0이 된다.
 *
 * LED는 Active Low 방식이다.
 * 즉, PORTC의 해당 비트를 0으로 만들면 LED가 켜지고 (sink 전류),
 * 1로 만들면 LED가 꺼진다.
 */

// 이전 버튼 상태를 저장해서 edge(눌림/떼짐)를 판별하기 위해 사용
volatile uint8_t last_button_state = 0xFF;

/*
 * 버튼과 LED 관련 포트 초기화
 */
void io_init(void) {
    // PC0, PC1은 버튼 입력
    DDRC &= ~((1 << BTN_SW1_PC) | (1 << BTN_SW2_PC));

    // 내부 풀업 활성화
    PORTC |= (1 << BTN_SW1_PC) | (1 << BTN_SW2_PC);

    // PC4는 LED 출력
    DDRC |= (1 << LED_PC);

    // Active Low LED이므로 1을 출력하면 OFF
    PORTC |= (1 << LED_PC);
}

/*
 * 현재 버튼 입력 상태를 읽는 함수
 */
uint8_t read_buttons(void) {
    return PINC;
}

/*
 * Active Low LED 제어 함수
 * - led_on()  : PC4 = 0 → sink 전류 → LED 켜짐
 * - led_off() : PC4 = 1 → LED 꺼짐
 */
void led_on(void) {
    PORTC &= ~(1 << LED_PC);
}

void led_off(void) {
    PORTC |= (1 << LED_PC);
}

/* ================================================================
 * 4. LED 패턴 상태 변수
 * ================================================================
 * LED는 블로킹 delay로 길게 점멸시키지 않고,
 * 메인 루프 안에서 조금씩 상태를 바꾸는 논블로킹 방식으로 처리한다.
 * 이렇게 하면 LED 점멸 중에도 FND 표시가 계속 유지된다.
 */

// 현재 판정된 족보
volatile uint8_t current_hand = HAND_NONE;

// LED가 지금 패턴 실행 중인지, 3초 OFF 대기 중인지 저장
volatile uint8_t led_mode = LED_MODE_PATTERN;

// 패턴 내부에서 몇 번째 단계인지를 저장
volatile uint8_t led_step = 0;

// LED 패턴용 시간 누적 변수
volatile uint16_t led_timer_ms = 0;

/* ================================================================
 * 5. 게임 상태 변수
 * ================================================================
 */

// 현재 사용자가 입력 중인 자리 번호
// 0: 천의 자리, 1: 백의 자리, 2: 십의 자리, 3: 일의 자리
volatile uint8_t current_digit_index = 0;

// 현재 자리 숫자가 롤링 중인지 여부
volatile uint8_t is_rolling = 0;

// 4자리 모두 확정되었는지 여부
volatile uint8_t is_done = 0;

// 숫자 롤링용 시간 누적 변수
uint16_t rolling_timer_ms = 0;

/* ================================================================
 * 6. 포커 족보 판정 함수
 * ================================================================
 * digits[4]의 값(1~6)이 어떤 조합인지 판정한다.
 *
 * 예:
 * 1 1 1 1 → HAND_FOUR
 * 2 2 2 5 → HAND_TRIPLE
 * 3 3 5 5 → HAND_TWO_PAIR
 * 4 4 1 6 → HAND_ONE_PAIR
 * 1 2 3 4 → HAND_NONE
 *
 * 판정 과정:
 * 1) cnt[1]~cnt[6]에 각 숫자가 몇 번 나왔는지 센다.
 * 2) 4개 일치 여부, 3개 일치 여부, pair 개수를 조사한다.
 * 3) 우선순위에 따라 족보를 결정한다.
 *
 * [설계 이유]
 * has_four / has_three / pair_count 를 분리해 두면
 * 나중에 족보 규칙을 추가할 때 마지막 if/else 부분만 수정하면 된다.
 * (카운팅 단계와 판정 단계를 분리하는 구조)
 */
uint8_t evaluate_poker_hand(void) {
    // 숫자 1~6의 등장 횟수를 저장하는 배열
    // cnt[0]은 사용하지 않고, cnt[1]~cnt[6]만 사용
    uint8_t cnt[7] = {0,};

    uint8_t i;
    for (i = 0; i < 4; i++) {
        uint8_t v = digits[i];

        // 혹시 잘못된 값이 있어도 1~6 범위만 카운트
        if (v >= 1 && v <= 6) {
            cnt[v]++;
        }
    }

    // pair가 몇 종류 있는지 저장 (예: 3 3 5 5 → pair_count = 2)
    uint8_t pair_count = 0;

    // 3개 일치 존재 여부
    uint8_t has_three = 0;

    // 4개 일치 존재 여부
    uint8_t has_four = 0;

    uint8_t v;
    for (v = 1; v <= 6; v++) {
        if (cnt[v] == 4) {
            has_four = 1;
        }
        else if (cnt[v] == 3) {
            has_three = 1;
        }
        else if (cnt[v] == 2) {
            pair_count++;
        }
    }

    // 족보 우선순위에 따라 반환
    // 포카드 > 트리플 > 투페어 > 원페어 > 없음
    if (has_four) {
        return HAND_FOUR;
    }
    else if (has_three) {
        return HAND_TRIPLE;
    }
    else if (pair_count == 2) {
        return HAND_TWO_PAIR;
    }
    else if (pair_count == 1) {
        return HAND_ONE_PAIR;
    }
    else {
        return HAND_NONE;
    }
}

/* ================================================================
 * 7. LED 패턴 함수 (논블로킹)
 * ================================================================
 * 이 함수는 메인 루프에서 계속 호출된다.
 * 한 번에 긴 delay를 주지 않고, elapsed_ms씩 시간을 누적하면서
 * 단계별로 LED 상태를 바꾼다.
 *
 * 패턴 한 사이클 종료 → LED_MODE_OFF_HOLD(3초 OFF) → 다시 패턴 반복
 *
 * [step % 2 규칙]
 * step이 짝수이면 led_on(), 홀수이면 led_off()
 *
 * [HAND_TWO_PAIR 주의사항]
 * 쌍 사이 OFF 구간(step 4) 이후 step을 10(짝수)으로 점프한다.
 * 이렇게 하면 두 번째 쌍도 반드시 led_on()부터 시작하는 것이 보장된다.
 * (step 5로 넘어가면 홀수이므로 led_off()가 실행되어 한 번 깜빡임이 누락됨)
 *
 * [타이밍 여유값 설정 이유]
 * FND 다이내믹 구동 시 블랭킹 구간이 반복되면서 LED ON 시간이
 * 시각적으로 짧아 보이는 현상이 발생한다.
 * 이를 보완하기 위해 기존 100ms/150ms 간격을 150ms/200ms/250ms로 늘렸다.
 */
void led_pattern_step(uint16_t elapsed_ms) {
    // 족보가 없으면 LED는 항상 OFF
    if (current_hand == HAND_NONE) {
        led_off();
        return;
    }

    // 시간 누적
    led_timer_ms += elapsed_ms;

    // OFF 대기 모드: 3초 후 패턴 모드로 복귀
    if (led_mode == LED_MODE_OFF_HOLD) {
        if (led_timer_ms >= LED_OFF_HOLD_MS) {
            led_timer_ms = 0;
            led_mode = LED_MODE_PATTERN;
            led_step = 0;
        }

        // 대기 중에는 LED OFF 유지
        led_off();
        return;
    }

    // 패턴 모드: 족보별 한 사이클 실행 후 OFF_HOLD 진입
    switch (current_hand) {

    case HAND_FOUR:
        /*
         * 4개 일치 패턴: ON-OFF × 4회, 150ms 간격
         * step 0: ON  step 1: OFF
         * step 2: ON  step 3: OFF
         * step 4: ON  step 5: OFF
         * step 6: ON  step 7: OFF → OFF_HOLD
         */
        if (led_timer_ms >= 150) {
            led_timer_ms = 0;

            if (led_step % 2 == 0) {
                led_on();
            } else {
                led_off();
            }

            led_step++;

            // step 0~7 총 8단계가 끝나면 한 사이클 완료
            if (led_step > 7) {
                led_off();
                led_step = 0;
                led_mode = LED_MODE_OFF_HOLD;
                led_timer_ms = 0;
            }
        }
        break;

    case HAND_TRIPLE:
        /*
         * 3개 일치 패턴: ON-OFF × 3회, 200ms 간격
         * step 0: ON  step 1: OFF
         * step 2: ON  step 3: OFF
         * step 4: ON  step 5: OFF → OFF_HOLD
         */
        if (led_timer_ms >= 200) {
            led_timer_ms = 0;

            if (led_step % 2 == 0) {
                led_on();
            } else {
                led_off();
            }

            led_step++;

            if (led_step > 5) {
                led_off();
                led_step = 0;
                led_mode = LED_MODE_OFF_HOLD;
                led_timer_ms = 0;
            }
        }
        break;

    case HAND_TWO_PAIR:
        /*
         * 2개 2개 일치 패턴
         * ON-OFF × 2회 → 500ms OFF → ON-OFF × 2회 → 3초 OFF
         *
         * [1단계] step 0~3: 첫 번째 쌍 (250ms 간격)
         *   step 0: ON  step 1: OFF
         *   step 2: ON  step 3: OFF
         *
         * [2단계] step 4: 쌍 사이 짧은 OFF 대기 (500ms)
         *   500ms 후 step을 10(짝수)으로 점프
         *   → 두 번째 쌍이 반드시 led_on()부터 시작하도록 보장
         *   → step 5로 넘어가면 홀수 → led_off()로 깜빡임 1회 누락됨
         *
         * [3단계] step 10~13: 두 번째 쌍 (250ms 간격)
         *   step 10: ON  step 11: OFF
         *   step 12: ON  step 13: OFF → OFF_HOLD
         */
        if (led_step == 4) {
            // 두 쌍 사이의 짧은 OFF 구간
            led_off();

            if (led_timer_ms >= 500) {
                led_timer_ms = 0;
                led_step = 10; // 짝수로 점프 → 다음 실행에서 led_on() 보장
            }
        }
        else if (led_step >= 14) {
            // 패턴 1사이클 종료
            led_off();
            led_step = 0;
            led_mode = LED_MODE_OFF_HOLD;
            led_timer_ms = 0;
        }
        else {
            // 첫 번째 쌍 (step 0~3), 두 번째 쌍 (step 10~13)
            if (led_timer_ms >= 250) {
                led_timer_ms = 0;

                if (led_step % 2 == 0) {
                    led_on();
                } else {
                    led_off();
                }

                led_step++;
            }
        }
        break;

    case HAND_ONE_PAIR:
        /*
         * 2개 일치 패턴: ON-OFF × 2회, 250ms 간격
         * step 0: ON  step 1: OFF
         * step 2: ON  step 3: OFF → OFF_HOLD
         */
        if (led_timer_ms >= 250) {
            led_timer_ms = 0;

            if (led_step % 2 == 0) {
                led_on();
            } else {
                led_off();
            }

            led_step++;

            if (led_step > 3) {
                led_off();
                led_step = 0;
                led_mode = LED_MODE_OFF_HOLD;
                led_timer_ms = 0;
            }
        }
        break;

    default:
        led_off();
        break;
    }
}

/* ================================================================
 * 8. 버튼 처리 함수
 * ================================================================
 * 버튼은 edge detection 방식으로 처리한다.
 *
 * - falling edge : 1 → 0, 버튼이 눌린 순간
 * - rising edge  : 0 → 1, 버튼을 뗀 순간
 *
 * 버튼을 누르고 있는 동안 롤링,
 * 버튼을 떼는 순간 값을 확정하는 구조를 위해 edge 검출이 필요하다.
 * 디바운싱: edge 감지 후 20ms 대기 후 재확인
 */

/*
 * sw1 처리
 * - 눌림 시작 (falling edge): 현재 자리 롤링 시작
 * - 떼는 순간 (rising edge) : 현재 값이 1~6이면 확정, 아니면 다시 롤링
 */
void handle_sw1_edge(uint8_t prev_sw1, uint8_t curr_sw1) {
    // falling edge : 버튼이 눌린 순간 (1 → 0)
    if (prev_sw1 && !curr_sw1) {
        // 디바운싱
        _delay_ms(20);

        // 다시 읽어서 진짜 눌렸는지 확인
        curr_sw1 = (read_buttons() & (1 << BTN_SW1_PC)) ? 1 : 0;

        if (!curr_sw1 && !is_done) {
            // 아직 게임이 끝나지 않았다면 롤링 시작
            is_rolling = 1;
            rolling_timer_ms = 0;
        }
    }

    // rising edge : 버튼을 떼는 순간 (0 → 1)
    if (!prev_sw1 && curr_sw1) {
        _delay_ms(20);

        // 다시 읽어서 진짜 떼어졌는지 확인
        curr_sw1 = (read_buttons() & (1 << BTN_SW1_PC)) ? 1 : 0;

        if (curr_sw1 && !is_done) {
            // 일단 롤링 정지
            is_rolling = 0;

            // 현재 자리 값 확인
            uint8_t v = digits[current_digit_index];

            // 1~6이 아니면 확정하지 않고 다시 롤링
            if (v < 1 || v > 6) {
                is_rolling = 1;
                rolling_timer_ms = 0;
            }
            else {
                // 정상 값이면 자리 확정
                if (current_digit_index < 3) {
                    // 아직 마지막 자리가 아니면 다음 자리로 이동
                    current_digit_index++;
                }
                else {
                    // 마지막 자리까지 모두 확정됨
                    is_done = 1;

                    // 족보 판정
                    current_hand = evaluate_poker_hand();

                    // LED 패턴 상태 초기화 후 패턴 시작
                    led_mode = LED_MODE_PATTERN;
                    led_step = 0;
                    led_timer_ms = 0;
                }
            }
        }
    }
}

/*
 * sw2 처리
 * - 눌리는 순간 (falling edge): 전체 상태 초기화
 */
void handle_sw2_edge(uint8_t prev_sw2, uint8_t curr_sw2) {
    // falling edge : 버튼이 눌린 순간 (1 → 0)
    if (prev_sw2 && !curr_sw2) {
        _delay_ms(20);

        // 디바운싱 후 재확인
        curr_sw2 = (read_buttons() & (1 << BTN_SW2_PC)) ? 1 : 0;

        if (!curr_sw2) {
            uint8_t i;

            // 표시 숫자 0000으로 초기화
            for (i = 0; i < 4; i++) {
                digits[i] = 0;
            }

            // 입력 상태 초기화
            current_digit_index = 0;
            is_rolling = 0;
            is_done = 0;

            // LED 관련 상태 초기화
            current_hand = HAND_NONE;
            led_mode = LED_MODE_PATTERN;
            led_step = 0;
            led_timer_ms = 0;

            // 롤링 타이머 초기화
            rolling_timer_ms = 0;

            // LED 끄기
            led_off();
        }
    }
}

/* ================================================================
 * 9. 메인 함수
 * ================================================================
 * 메인 루프에서 계속 반복 수행하는 작업:
 * 1) 버튼 상태 읽기
 * 2) sw1, sw2 edge 처리
 * 3) 숫자 롤링 처리
 * 4) LED 패턴 처리
 * 5) FND 한 자리 표시
 * 6) 루프 주기 delay
 */
int main(void) {
    // 포트 초기화
    fnd_init();
    io_init();

    // 시작 시 버튼 상태 저장
    last_button_state = read_buttons();

    while (1) {
        // 현재 버튼 상태 읽기
        uint8_t curr_button_state = read_buttons();

        // 이전/현재 sw1 상태 분리
        uint8_t prev_sw1 = (last_button_state & (1 << BTN_SW1_PC)) ? 1 : 0;
        uint8_t curr_sw1 = (curr_button_state & (1 << BTN_SW1_PC)) ? 1 : 0;

        // 이전/현재 sw2 상태 분리
        uint8_t prev_sw2 = (last_button_state & (1 << BTN_SW2_PC)) ? 1 : 0;
        uint8_t curr_sw2 = (curr_button_state & (1 << BTN_SW2_PC)) ? 1 : 0;

        // 버튼 edge 처리
        handle_sw1_edge(prev_sw1, curr_sw1);
        handle_sw2_edge(prev_sw2, curr_sw2);

        // 이번 상태를 다음 루프의 이전 상태로 저장
        last_button_state = curr_button_state;

        // --------------------------------------------------------
        // 숫자 롤링 처리
        // --------------------------------------------------------
        // 롤링 중이면 0.2초마다 현재 자리 값이 1~6으로 바뀜
        if (is_rolling && !is_done) {
            rolling_timer_ms += LOOP_DELAY_MS;

            if (rolling_timer_ms >= ROLL_INTERVAL_MS) {
                rolling_timer_ms = 0;

                uint8_t v = digits[current_digit_index];

                // 현재 값이 1~6 범위 밖이면 1부터 시작
                if (v < 1 || v > 6) {
                    v = 1;
                }
                else {
                    v++;
                    if (v > 6) {
                        v = 1;
                    }
                }

                digits[current_digit_index] = v;
            }
        }

        // --------------------------------------------------------
        // LED 패턴 처리
        // --------------------------------------------------------
        // 4자리 입력 완료 후에만 패턴 수행
        if (is_done) {
            led_pattern_step(LOOP_DELAY_MS);
        }

        // --------------------------------------------------------
        // FND 표시
        // --------------------------------------------------------
        fnd_display_step();

        // 메인 루프 주기 조절
        _delay_ms(LOOP_DELAY_MS);
    }

    return 0;
}