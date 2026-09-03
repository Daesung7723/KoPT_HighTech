/*
 * ============================================================
 *  ATmega128 Simon Says Game (No Timer, No Interrupt)
 * ============================================================
 *  시스템 개요
 *  - 4x4 키패드에서 숫자를 입력 받아 Simon Says 숫자 기억 게임을 수행
 *  - 4digit 7-Segment FND에 상태/숫자/점수 표시
 *  - 외부 LED 3개로 라이프(기회) 개수 표시
 *
 *  하드웨어 배선
 *  ------------------------------------------------------------
 *  FND (4digit, Common Anode 타입)
 *    - 세그먼트 (a,b,c,d,e,f,g,dp)  : PB0 ~ PB7
 *      → Common Anode: 세그먼트 ON = 0, OFF = 1
 *    - 자릿수 선택(COM0~COM3)      : PA0 ~ PA3
 *      → Active Low: 해당 자리 ON = 0, OFF = 1
 *      → PA0 = 가장 왼쪽 자리, PA3 = 가장 오른쪽 자리
 *
 *  4x4 Keypad
 *    - 컬럼(C0~C3, 열) 출력        : PC0 ~ PC3 (출력)
 *      → 기본 High, 선택할 열만 Low로 내려서 스캔
 *    - 로우(R0~R3, 행) 입력        : PC4 ~ PC7 (입력, 내부 풀업 사용)
 *      → 키가 눌리면 해당 행 핀은 Low로 읽힘
 *
 *  Life LED (3개, Active Low)
 *    - LED1 : PD0
 *    - LED2 : PD1
 *    - LED3 : PD2
 *    → 0(LOW) 출력 시 LED ON, 1(HIGH) 출력 시 LED OFF
 * ============================================================
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ------------------------------------------------------------
 *  포트 / 핀 매크로 정의
 *  - 코드에서 포트 이름을 직접 쓰지 않고 의미 있는 이름 사용
 * ----------------------------------------------------------*/
#define FND_SEG_DDR     DDRB      // FND 세그먼트 포트의 방향 레지스터
#define FND_SEG_PORT    PORTB     // FND 세그먼트 출력 레지스터

#define FND_COM_DDR     DDRA      // FND COM(자리 선택) 포트의 방향 레지스터
#define FND_COM_PORT    PORTA     // FND COM 출력 레지스터
#define FND_COM_MASK    0x0F      // PA0~PA3 사용 (0000 1111b)

#define KEY_DDR         DDRC      // 키패드 포트 방향 레지스터
#define KEY_PORT        PORTC     // 키패드 포트 출력 (컬럼 High, 로우 Pull-up)
#define KEY_PIN         PINC      // 키패드 입력 값 읽기
#define COL_MASK        0x0F      // PC0~PC3: 컬럼 출력 비트
#define ROW_MASK        0xF0      // PC4~PC7: 로우 입력 비트

#define LIFE_LED_DDR    DDRD      // 라이프 LED 포트 방향
#define LIFE_LED_PORT   PORTD     // 라이프 LED 출력
#define LIFE_LED1       PD0       // 라이프 1번 LED 핀
#define LIFE_LED2       PD1       // 라이프 2번 LED 핀
#define LIFE_LED3       PD2       // 라이프 3번 LED 핀

/* ------------------------------------------------------------
 *  게임 상태 정의 (State Machine)
 *  - 현재 게임이 어떤 단계인지 의미를 명확히 하기 위한 상수들
 * ----------------------------------------------------------*/
#define ST_LEVEL_SEL    0   // 레벨 선택 화면
#define ST_SHOW_SEQ     1   // 시퀀스(숫자 열) 보여주는 단계
#define ST_WAIT_INPUT   2   // 사용자 입력 대기 단계
#define ST_ROUND_CLEAR  3   // 라운드 성공 처리 단계
#define ST_LIFE_LOST    4   // 오답으로 라이프 감소 처리 단계
#define ST_GAME_OVER    5   // 게임 오버 처리 단계

/* ------------------------------------------------------------
 *  FND 폰트 데이터 (Common Anode)
 *  - 배열 인덱스: 0~9 숫자, 10:'-', 11:공백, 12~18: 알파벳
 *  - 비트 의미: (dp, g, f, e, d, c, b, a) 순이 아니라
 *               실제 보드에서 실험해 얻은 값 사용
 *  - CA 타입: 점등하려는 세그먼트 비트 = 0, 나머지 = 1
 * ----------------------------------------------------------*/
static const uint8_t fnd_font[] = {
    0xC0,   //  0: 숫자 0
    0xF9,   //  1: 숫자 1
    0xA4,   //  2: 숫자 2
    0xB0,   //  3: 숫자 3
    0x99,   //  4: 숫자 4
    0x92,   //  5: 숫자 5
    0x82,   //  6: 숫자 6
    0xF8,   //  7: 숫자 7
    0x80,   //  8: 숫자 8
    0x90,   //  9: 숫자 9
    0xBF,   // 10: '-' (가운데 막대만 ON)
    0xFF,   // 11: 공백 (모든 세그먼트 OFF)
    0xC7,   // 12: 'L' (보드에서 확인된 값)
    0x86,   // 13: 'E'
    0xA1,   // 14: 'd'
    0xAF,   // 15: 'r'
    0xAB,   // 16: 'n'
    0xC1,   // 17: 'V'
    0xC0    // 18: 'O' (0와 동일 패턴 사용)
};

// 폰트 인덱스를 의미 있는 이름으로 치환
#define FNT_DASH    10  // '-'
#define FNT_BLANK   11  // 공백
#define FNT_L       12
#define FNT_E       13
#define FNT_d       14
#define FNT_r       15
#define FNT_n       16
#define FNT_V       17
#define FNT_O       18

/* ------------------------------------------------------------
 *  전역 변수: 게임 상태 및 표시용 버퍼
 * ----------------------------------------------------------*/
static uint8_t  g_state;        // 현재 게임 상태
static uint8_t  g_level;        // 선택된 레벨 (1,2,3 중 하나)
static uint16_t g_show_ms;      // 시퀀스 숫자 표시 시간 (레벨에 따라 변경)

static uint8_t  g_seq[16];      // 미리 생성해둘 숫자 시퀀스 (최대 16개)
static uint8_t  g_round_len;    // 현재 라운드에서 사용할 시퀀스 길이
static uint8_t  g_input_idx;    // 현재 몇 번째 숫자를 입력 중인지

static uint8_t  g_life;         // 남은 라이프(기회) 개수 (3 → 0)
static uint16_t g_score;        // 최종 점수
static uint8_t  g_round;        // 클리어한 라운드 수

// FND 표시용 버퍼: g_disp[0]=왼쪽 자리, g_disp[3]=오른쪽 자리
static uint8_t  g_disp[4];

// 현재 FND에서 점등할 자리 인덱스 (0~3 순환)
static uint8_t  g_digit = 0;

// 의사 난수 생성용 시드 값
static uint16_t g_rand = 1;

/* ------------------------------------------------------------
 *  FND 한 자리만 켜서 표시하는 함수 (동적 구동)
 *  - 4자리 FND를 매우 빠르게 번갈아가며 켜서 전체가 켜져 보이게 함
 *  - 1번 호출 시, g_digit에 해당하는 자리만 짧게 켰다가 끔
 * ----------------------------------------------------------*/
static void fnd_refresh_once(void)
{
    // 1) 모든 자리 COM을 OFF (1)로 설정 → 아무 자리도 켜지지 않게 함
    FND_COM_PORT |= FND_COM_MASK;

    // 2) 현재 표시할 자리의 세그먼트 패턴을 세그먼트 포트로 출력
    //    g_disp[g_digit] 값은 fnd_font 배열의 인덱스
    FND_SEG_PORT = fnd_font[g_disp[g_digit]];

    // 3) 현재 자리의 COM 비트만 0으로 만들어 해당 자리 ON (Active Low)
    FND_COM_PORT &= ~(1 << g_digit);

    // 4) 짧은 시간 동안 켜두기 (약 200us)
    _delay_us(200);

    // 5) 다시 모든 COM을 OFF로 만들어 고스팅 방지
    FND_COM_PORT |= FND_COM_MASK;

    // 6) 다음 자리를 표시하도록 g_digit 인덱스 증가 (0→1→2→3→0)
    g_digit = (g_digit + 1) & 0x03;
}

/* ------------------------------------------------------------
 *  지정한 ms 동안 대기하면서 FND를 계속 refresh
 *  - 단순 _delay_ms() 를 쓰면 FND가 깜빡이므로
 *    1ms 동안 4자리 모두 refresh하는 방식으로 구현
 * ----------------------------------------------------------*/
static void my_delay_ms(uint16_t ms)
{
    for (uint16_t i = 0; i < ms; i++)
    {
        // 1ms 동안 4자리 모두 한번씩 점등
        fnd_refresh_once();
        fnd_refresh_once();
        fnd_refresh_once();
        fnd_refresh_once();
    }
}

/* ------------------------------------------------------------
 *  FND 버퍼 관련 헬퍼 함수들
 * ----------------------------------------------------------*/

// 4자리를 모두 같은 값으로 채우는 함수
static void disp_all(uint8_t idx)
{
    g_disp[0] = g_disp[1] = g_disp[2] = g_disp[3] = idx;
}

// 각 자리별로 다른 값을 설정하는 함수
// d0: 왼쪽, d3: 오른쪽 자리
static void disp_set(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    g_disp[0] = d0;
    g_disp[1] = d1;
    g_disp[2] = d2;
    g_disp[3] = d3;
}

// 한 자리 숫자(0~9)를 오른쪽 정렬로 표시 (___N 형태)
static void disp_single(uint8_t n)
{
    disp_set(FNT_BLANK, FNT_BLANK, FNT_BLANK, n);
}

// 0~9999까지의 정수를 4자리로 표시 (앞쪽은 공백)
// 예: 5 → "___5", 123 → "_123", 2024 → "2024"
static void disp_number(uint16_t n)
{
    g_disp[3] =  n % 10;
    g_disp[2] = (n < 10)   ? FNT_BLANK : (n / 10)   % 10;
    g_disp[1] = (n < 100)  ? FNT_BLANK : (n / 100)  % 10;
    g_disp[0] = (n < 1000) ? FNT_BLANK : (n / 1000) % 10;
}

// 라운드 표시용: r--N 또는 r-NN 형태로 표시
//  예: round=3 → "r--3", round=12 → "r-12"
static void disp_round(uint8_t r)
{
    if (r < 10)
        disp_set(FNT_r, FNT_DASH, FNT_DASH, r);
    else
        disp_set(FNT_r, FNT_DASH, r / 10, r % 10);
}

/* ------------------------------------------------------------
 *  라이프 LED 제어 (Active Low)
 *  - life 값(0~3)에 따라 켜져야 하는 LED 개수 결정
 * ----------------------------------------------------------*/
static void life_leds_update(uint8_t lf)
{
    // 먼저 모든 LED를 OFF (1)로 설정
    LIFE_LED_PORT |=  (1 << LIFE_LED1) | (1 << LIFE_LED2) | (1 << LIFE_LED3);

    // life 값에 따라 앞에서부터 순서대로 켜기 (0=OFF, 1=ON)
    if (lf >= 1) LIFE_LED_PORT &= ~(1 << LIFE_LED1);
    if (lf >= 2) LIFE_LED_PORT &= ~(1 << LIFE_LED2);
    if (lf >= 3) LIFE_LED_PORT &= ~(1 << LIFE_LED3);
}

/* ------------------------------------------------------------
 *  키패드 스캔
 *  - 컬럼을 하나씩 LOW로 내리면서, 어떤 ROW가 LOW로 바뀌는지 확인
 *  - 상하 대칭 배선: 실제 ROW 인덱스를 뒤집어서 논리 ROW로 사용
 *
 *  논리 키 배치 (게임에서 사용할 좌표):
 *   ROW0: [1][2][3][X]
 *   ROW1: [4][5][6][X]
 *   ROW2: [7][8][9][X]
 *   ROW3: [X][0][X][X]
 * ----------------------------------------------------------*/
static const uint8_t KEY_MAP[4][4] = {
    {1,    2,    3,    0xFF},   // 논리 ROW0
    {4,    5,    6,    0xFF},   // 논리 ROW1
    {7,    8,    9,    0xFF},   // 논리 ROW2
    {0xFF, 0,    0xFF, 0xFF}    // 논리 ROW3
};

// 키패드 상태를 한 번 스캔하여 눌린 키의 숫자(0~9) 또는 0xFF(없음)를 반환
static uint8_t keypad_scan(void)
{
    // col: 0~3 에 대해서 해당 컬럼만 LOW로 만들고, 행을 읽는다.
    for (uint8_t col = 0; col < 4; col++)
    {
        // 1) 모든 컬럼을 HIGH로 만들고, 모든 로우 입력에 Pull-up 적용
        KEY_PORT = 0xFF;

        // 2) 현재 선택한 컬럼만 LOW로 설정
        //    (3-col)을 사용하는 이유는 하드웨어 배선이 뒤집혀 있기 때문
        KEY_PORT &= ~(1 << (3 - col));

        _delay_us(5);  // 신호 안정화 대기

        // 3) 4개의 ROW(PC4~PC7)를 읽어 어떤 행이 LOW인지 확인
        for (uint8_t prow = 0; prow < 4; prow++)
        {
            // prow = 물리적 row (PC4~PC7)
            if (!(KEY_PIN & (1 << (prow + 4))))
            {
                // 물리 ROW(prow)를 상하 대칭 변환: 논리 ROW = 3 - prow
                uint8_t lrow = 3 - prow;

                // 포트 상태 복구 (다시 모두 HIGH, Pull-up)
                KEY_PORT = COL_MASK | ROW_MASK;

                // 키 매핑 테이블에서 해당 키 값 반환
                return KEY_MAP[lrow][col];
            }
        }
    }

    // 어떤 키도 눌리지 않았으면 0xFF 반환
    KEY_PORT = COL_MASK | ROW_MASK;
    return 0xFF;
}

/*
 * 키 입력이 발생할 때까지 기다리는 함수 (블로킹)
 * - 기다리는 동안에도 FND는 계속 refresh
 * - 키가 눌렸다가 완전히 떨어질 때까지 처리 (디바운스 포함)
 */
static uint8_t keypad_wait(void)
{
    uint8_t k;

    // 1) 이전에 눌려있던 키가 모두 떨어질 때까지 대기
    do {
        fnd_refresh_once();
        k = keypad_scan();
    } while (k != 0xFF);

    // 2) 새로운 키가 눌릴 때까지 대기
    do {
        fnd_refresh_once();
        k = keypad_scan();
    } while (k == 0xFF);

    // 3) 키가 안정적으로 눌렸는지 확인하기 위해 20ms 정도 대기
    my_delay_ms(20);

    // 4) 다시 스캔했을 때 눌린 키가 없다면 노이즈로 판단하고 취소
    if (keypad_scan() == 0xFF) return 0xFF;

    // 5) 키가 완전히 떨어질 때까지 대기 (오토 리피트 방지)
    do {
        fnd_refresh_once();
    } while (keypad_scan() != 0xFF);

    // 6) 해제 후 짧은 디바운스
    my_delay_ms(10);

    return k;
}

/* ------------------------------------------------------------
 *  의사 난수 (Pseudo Random) 생성: 1~9 범위의 숫자
 *  - rand() 함수 대신, 간단한 Linear Congruential Generator 사용
 * ----------------------------------------------------------*/
static uint8_t pseudo_rand(void)
{
    // LCG 알고리즘: seed = seed * A + C 형태
    g_rand = g_rand * 1103515245UL + 12345;
    // 상위 비트를 섞은 뒤 1~9 범위로 맞추기
    return (uint8_t)((g_rand >> 5) % 9) + 1;
}

/* ------------------------------------------------------------
 *  시퀀스 생성 / 표시
 * ----------------------------------------------------------*/

// 최대 16개까지 사용할 전체 시퀀스를 미리 생성
static void seq_generate(void)
{
    for (uint8_t i = 0; i < 16; i++)
    {
        g_seq[i] = pseudo_rand();   // 1~9 사이의 숫자
    }
}

// 현재 라운드에서 필요한 길이(g_round_len)만큼 시퀀스를 차례로 보여줌
static void seq_show(void)
{
    for (uint8_t i = 0; i < g_round_len; i++)
    {
        // 1) 이번에 보여줄 숫자 한 개를 우측 정렬로 표시 (___N)
        disp_single(g_seq[i]);
        my_delay_ms(g_show_ms);     // 레벨에 따른 표시 시간 유지

        // 2) 잠시 공백 화면으로 간격 두기
        disp_all(FNT_BLANK);
        my_delay_ms(200);
    }
}

/* ------------------------------------------------------------
 *  게임 초기화: 레벨 설정, 라이프/라운드/점수 초기화, 시퀀스 생성
 * ----------------------------------------------------------*/
static void game_start(uint8_t lv)
{
    g_level = lv;

    // 난이도별 숫자 표시 시간 설정
    switch (lv)
    {
        case 1:  g_show_ms = 1000; break;  // 1초 간격
        case 2:  g_show_ms = 600;  break;  // 0.6초 간격
        default: g_show_ms = 300;  break;  // 0.3초 간격
    }

    // 게임 변수 초기화
    g_life      = 3;   // 라이프 3개에서 시작
    g_score     = 0;   // 초기 점수 0
    g_round     = 0;   // 아직 클리어한 라운드 없음
    g_round_len = 1;   // 첫 라운드는 시퀀스 길이 1부터 시작

    // 라이프 LED 갱신 및 시퀀스 생성
    life_leds_update(g_life);
    seq_generate();
}

/* ------------------------------------------------------------
 *  하드웨어 초기화
 *  - 각 포트의 입출력 방향 및 초기 상태 설정
 * ----------------------------------------------------------*/
static void hw_init(void)
{
    // FND 세그먼트 포트: PB0~PB7 모두 출력으로 설정, 모두 OFF로 초기화
    FND_SEG_DDR  = 0xFF;
    FND_SEG_PORT = 0xFF;

    // FND COM 포트: PA0~PA3를 출력으로 설정, 모두 OFF(1)로 초기화
    FND_COM_DDR  |= FND_COM_MASK;
    FND_COM_PORT |= FND_COM_MASK;

    // 키패드: PC0~PC3을 출력(COL), PC4~PC7을 입력(ROW, 풀업)
    KEY_DDR  = COL_MASK;             // 하위 4비트(컬럼) 출력
    KEY_PORT = COL_MASK | ROW_MASK;  // 컬럼 High, 로우 Pull-up

    // 라이프 LED: PD0~PD2를 출력으로 설정, 모두 OFF(1)로 초기화
    LIFE_LED_DDR  |= (1 << LIFE_LED1) | (1 << LIFE_LED2) | (1 << LIFE_LED3);
    LIFE_LED_PORT |= (1 << LIFE_LED1) | (1 << LIFE_LED2) | (1 << LIFE_LED3);

    g_digit = 0;   // FND 표시 시작 자리는 0번(왼쪽)으로
}

/* ------------------------------------------------------------
 *  메인 함수: 상태 머신 기반 게임 루프
 * ----------------------------------------------------------*/
int main(void)
{
    hw_init();  // 하드웨어 설정

    // 처음에는 레벨 선택 상태로 시작, "L--1" 표시
    g_state = ST_LEVEL_SEL;
    disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);

    while (1)
    {
        switch (g_state)
        {
            /* ---------------------------------------
             * 레벨 선택 상태:
             *  - FND에 L--1 또는 L--2, L--3 표시
             *  - 키패드 1,2,3 중 하나가 눌리면 해당 레벨로 시작
             * -------------------------------------*/
            case ST_LEVEL_SEL:
            {
                // 레벨 선택 대기 중에도 FND는 계속 refresh
                g_rand++;              // 대기 중 난수 시드 계속 변경
                fnd_refresh_once();

                uint8_t k = keypad_scan();

                // 1~3 중 하나가 눌렸는지 확인
                if (k == 1 || k == 2 || k == 3)
                {
                    // 간단한 디바운스
                    my_delay_ms(20);
                    if (keypad_scan() == k)
                    {
                        // 키가 떨어질 때까지 대기
                        while (keypad_scan() != 0xFF)
                            fnd_refresh_once();
                        my_delay_ms(10);

                        // 선택된 레벨을 잠깐 표시
                        disp_set(FNT_L, FNT_DASH, FNT_DASH, k);
                        my_delay_ms(500);

                        // 게임 시작
                        game_start(k);
                        g_state = ST_SHOW_SEQ;
                    }
                }
                break;
            }

            /* ---------------------------------------
             * 시퀀스 표시 상태:
             *  - 현재 라운드 길이만큼 숫자 시퀀스를 차례대로 보여줌
             * -------------------------------------*/
            case ST_SHOW_SEQ:
            {
                seq_show();   // 시퀀스 전체를 표시

                // 사용자 입력 대기 화면 '----'로 전환
                disp_set(FNT_DASH, FNT_DASH, FNT_DASH, FNT_DASH);
                g_input_idx = 0;      // 첫 번째 숫자부터 입력받기
                g_state = ST_WAIT_INPUT;
                break;
            }

            /* ---------------------------------------
             * 입력 대기 상태:
             *  - 사용자가 하나씩 입력하는 숫자를 받음
             *  - 정답이면 다음 인덱스로 넘어감, 오답이면 라이프 감소
             * -------------------------------------*/
            case ST_WAIT_INPUT:
            {
                uint8_t k = keypad_wait();  // 키가 눌릴 때까지 대기
                if (k == 0xFF) break;       // 노이즈면 무시

                // 입력한 숫자를 0.3초 동안 에코 표시
                disp_single(k);
                my_delay_ms(300);

                // 다시 '----' 화면으로 복귀
                disp_set(FNT_DASH, FNT_DASH, FNT_DASH, FNT_DASH);

                // 정답/오답 판정
                if (k == g_seq[g_input_idx])
                {
                    // 정답이면 다음 인덱스로
                    g_input_idx++;
                    if (g_input_idx >= g_round_len)
                    {
                        // 이번 라운드 모든 숫자 입력 성공
                        g_round++;            // 클리어 라운드 수 증가
                        g_state = ST_ROUND_CLEAR;
                    }
                }
                else
                {
                    // 오답: 라이프 감소
                    g_life--;
                    life_leds_update(g_life);
                    g_state = ST_LIFE_LOST;
                }
                break;
            }

            /* ---------------------------------------
             * 라운드 클리어 상태:
             *  - "r--N" 형태로 라운드 번호를 1초 동안 표시
             *  - 16 라운드 이상 클리어 시 게임 클리어 처리
             * -------------------------------------*/
            case ST_ROUND_CLEAR:
            {
                // r--N 또는 r-NN 표시
                disp_round(g_round);
                my_delay_ms(1000);

                if (g_round >= 16)
                {
                    // 모든 라운드 클리어: "donE" 표시
                    disp_set(FNT_d, FNT_O, FNT_n, FNT_E);
                    my_delay_ms(2000);

                    // 점수 계산: (클리어한 라운드 수 × 10) + (잔여 라이프 × 5)
                    g_score = (uint16_t)(g_round * 10)
                            + (uint16_t)(g_life  *  5);

                    // 점수 표시
                    disp_number(g_score);
                    my_delay_ms(2000);

                    // 다시 레벨 선택 화면으로 복귀
                    disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);
                    g_state = ST_LEVEL_SEL;
                }
                else
                {
                    // 다음 라운드로: 시퀀스 길이를 1 증가
                    g_round_len++;
                    g_state = ST_SHOW_SEQ;
                }
                break;
            }

            /* ---------------------------------------
             * 라이프 감소 상태:
             *  - "L--x" 형태로 남은 라이프 개수를 1초 동안 표시
             *  - 라이프가 0이면 게임오버, 그렇지 않으면 같은 라운드 재도전
             * -------------------------------------*/
            case ST_LIFE_LOST:
            {
                disp_set(FNT_L, FNT_DASH, FNT_DASH, g_life);
                my_delay_ms(1000);

                if (g_life == 0)
                {
                    g_state = ST_GAME_OVER;
                }
                else
                {
                    // 라이프가 남아 있으면 같은 라운드 시퀀스를 다시 보여줌
                    g_state = ST_SHOW_SEQ;
                }
                break;
            }

            /* ---------------------------------------
             * 게임 오버 상태:
             *  - "OVEr" 표시 → 최종 라운드 → 최종 점수 → 레벨 화면 복귀
             * -------------------------------------*/
            case ST_GAME_OVER:
            {
                // 게임 오버 표시
                disp_set(FNT_O, FNT_V, FNT_E, FNT_r);
                my_delay_ms(1500);

                // 마지막으로 도전했던 라운드 표시
                disp_round(g_round);
                my_delay_ms(2000);

                // 점수 계산 및 표시
                g_score = (uint16_t)(g_round * 10)
                        + (uint16_t)(g_life  *  5);
                disp_number(g_score);
                my_delay_ms(2000);

                // 레벨 선택 화면으로 돌아가기
                disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);
                g_state = ST_LEVEL_SEL;
                break;
            }

            // 예외적으로 이상한 값이면 레벨 선택 상태로 되돌리기
            default:
            {
                g_state = ST_LEVEL_SEL;
                disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);
                break;
            }
        } // switch 끝
    }     // while(1) 끝

    return 0;
}