/*
 * ============================================================
 *  Simon Says Game - ATmega128 @ 16MHz
 *  (타이머/인터럽트 미사용, 키패드 상하 대칭 보정)
 * ============================================================
 *  하드웨어:
 *   PB      : FND 세그먼트 (Common Anode, 0=ON)
 *   PA[0..3]: FND COM (Active Low, 0=ON), PA0=좌측 ~ PA3=우측
 *   PC0~PC3 : Keypad COL 출력 (3-col 역순 구동)
 *   PC4~PC7 : Keypad ROW 입력 (Pull-up, 상하 대칭 배선)
 *   PD0~PD2 : 라이프 LED (Active Low)
 * ============================================================
 */

#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* ================================================================
   포트 / 핀 정의
   ================================================================ */
#define FND_SEG_DDR     DDRB
#define FND_SEG_PORT    PORTB

#define FND_COM_DDR     DDRA
#define FND_COM_PORT    PORTA
#define FND_COM_MASK    0x0F        /* PA0~PA3 */

#define KEY_DDR         DDRC
#define KEY_PORT        PORTC
#define KEY_PIN         PINC
#define COL_MASK        0x0F        /* PC0~PC3: COL 출력 */
#define ROW_MASK        0xF0        /* PC4~PC7: ROW 입력 */

#define LIFE_LED_DDR    DDRD
#define LIFE_LED_PORT   PORTD
#define LIFE_LED1       PD0
#define LIFE_LED2       PD1
#define LIFE_LED3       PD2

/* ================================================================
   상태 정의
   ================================================================ */
#define ST_LEVEL_SEL    0
#define ST_SHOW_SEQ     1
#define ST_WAIT_INPUT   2
#define ST_ROUND_CLEAR  3
#define ST_LIFE_LOST    4
#define ST_GAME_OVER    5

/* ================================================================
   FND 폰트 (Common Anode, 0=ON)
   ================================================================ */
static const uint8_t fnd_font[] = {
    0xC0,   /*  0: 0 */
    0xF9,   /*  1: 1 */
    0xA4,   /*  2: 2 */
    0xB0,   /*  3: 3 */
    0x99,   /*  4: 4 */
    0x92,   /*  5: 5 */
    0x82,   /*  6: 6 */
    0xF8,   /*  7: 7 */
    0x80,   /*  8: 8 */
    0x90,   /*  9: 9 */
    0xBF,   /* 10: - */
    0xFF,   /* 11:   (공백) */
    0xC7,   /* 12: L */
    0x86,   /* 13: E */
    0xA1,   /* 14: d */
    0xAF,   /* 15: r */
    0xAB,   /* 16: n */
    0xC1,   /* 17: V */
    0xC0,   /* 18: O */
};

#define FNT_DASH    10
#define FNT_BLANK   11
#define FNT_L       12
#define FNT_E       13
#define FNT_d       14
#define FNT_r       15
#define FNT_n       16
#define FNT_V       17
#define FNT_O       18

/* ================================================================
   전역 변수
   ================================================================ */
static uint8_t  g_state;
static uint8_t  g_level;
static uint16_t g_show_ms;

static uint8_t  g_seq[16];
static uint8_t  g_round_len;
static uint8_t  g_input_idx;

static uint8_t  g_life;
static uint16_t g_score;
static uint8_t  g_round;

static uint8_t  g_disp[4];     /* [0]=좌측, [3]=우측 */
static uint8_t  g_digit = 0;   /* 0→1→2→3→0 순환 */

static uint16_t g_rand = 1;

/* ================================================================
   FND 멀티플렉싱
   ================================================================ */
static void fnd_refresh_once(void) {
    FND_COM_PORT |=  FND_COM_MASK;                  /* 전체 COM OFF */
    FND_SEG_PORT  =  fnd_font[g_disp[g_digit]];     /* 세그먼트 출력 */
    FND_COM_PORT &= ~(1 << g_digit);                /* 해당 COM ON  */
    _delay_us(200);
    FND_COM_PORT |=  FND_COM_MASK;                  /* 전체 COM OFF */
    g_digit = (g_digit + 1) & 0x03;                /* 0→1→2→3→0   */
}

/* N ms 대기 (FND refresh 유지) */
static void my_delay_ms(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        fnd_refresh_once();
        fnd_refresh_once();
        fnd_refresh_once();
        fnd_refresh_once();
    }
}

/* ================================================================
   FND 버퍼 설정
   disp_set(좌측, 2번, 3번, 우측)
   ================================================================ */
static void disp_all(uint8_t idx) {
    g_disp[0] = g_disp[1] = g_disp[2] = g_disp[3] = idx;
}

static void disp_set(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3) {
    g_disp[0] = d0;
    g_disp[1] = d1;
    g_disp[2] = d2;
    g_disp[3] = d3;
}

/* 숫자 우측 정렬: [공백][공백][공백][N] */
static void disp_single(uint8_t n) {
    disp_set(FNT_BLANK, FNT_BLANK, FNT_BLANK, n);
}

/* 0~9999 숫자 표시 (앞자리 공백) */
static void disp_number(uint16_t n) {
    g_disp[3] =  n % 10;
    g_disp[2] = (n < 10)   ? FNT_BLANK : (n / 10)   % 10;
    g_disp[1] = (n < 100)  ? FNT_BLANK : (n / 100)  % 10;
    g_disp[0] = (n < 1000) ? FNT_BLANK : (n / 1000) % 10;
}

/* 라운드 표시: r--N (1~9) / r-NN (10~16) */
static void disp_round(uint8_t r) {
    if (r < 10)
        disp_set(FNT_r, FNT_DASH, FNT_DASH, r);
    else
        disp_set(FNT_r, FNT_DASH, r / 10, r % 10);
}

/* ================================================================
   라이프 LED (Active Low)
   ================================================================ */
static void life_leds_update(uint8_t lf) {
    LIFE_LED_PORT |=  (1 << LIFE_LED1) | (1 << LIFE_LED2) | (1 << LIFE_LED3);
    if (lf >= 1) LIFE_LED_PORT &= ~(1 << LIFE_LED1);
    if (lf >= 2) LIFE_LED_PORT &= ~(1 << LIFE_LED2);
    if (lf >= 3) LIFE_LED_PORT &= ~(1 << LIFE_LED3);
}

/* ================================================================
   키패드 스캔 (상하 대칭 보정)
   COL: PC0~PC3 출력 (3-col 역순)
   ROW: PC4~PC7 입력 (풀업)
   상하 대칭: logical_row = 3 - physical_row
   ================================================================ */

/*
   논리 키 배치 (게임에서 사용할 좌표):

   논리 ROW0: [1][2][3][X]
   논리 ROW1: [4][5][6][X]
   논리 ROW2: [7][8][9][X]
   논리 ROW3: [X][0][X][X]

   실제 하드웨어는 ROW가 상하 반대로 배선 → 3-row로 뒤집어서 매핑
*/
static const uint8_t KEY_MAP[4][4] = {
    {1,    2,    3,    0xFF},   /* logical ROW0 */
    {4,    5,    6,    0xFF},   /* logical ROW1 */
    {7,    8,    9,    0xFF},   /* logical ROW2 */
    {0xFF, 0,    0xFF, 0xFF}    /* logical ROW3 */
};

static uint8_t keypad_scan(void) {
    for (uint8_t col = 0; col < 4; col++) {
        /* 계산기 코드 방식: 전체 HIGH, 해당 COL만 LOW */
        KEY_PORT = 0xFF;
        KEY_PORT &= ~(1 << (3 - col));          /* PC3→PC2→PC1→PC0 */
        _delay_us(5);

        for (uint8_t prow = 0; prow < 4; prow++) {
            if (!(KEY_PIN & (1 << (prow + 4)))) {   /* physical ROW: PC4~PC7 */

                uint8_t lrow = 3 - prow;            /* 상하 대칭 보정 */

                KEY_PORT = COL_MASK | ROW_MASK;     /* 복원 */
                return KEY_MAP[lrow][col];
            }
        }
    }
    KEY_PORT = COL_MASK | ROW_MASK;
    return 0xFF;
}

/*
 * 키 입력 대기 (블로킹, FND refresh 유지)
 */
static uint8_t keypad_wait(void) {
    uint8_t k;

    /* 1. 이전 키 해제 */
    do {
        fnd_refresh_once();
        k = keypad_scan();
    } while (k != 0xFF);

    /* 2. 새 키 눌림 대기 */
    do {
        fnd_refresh_once();
        k = keypad_scan();
    } while (k == 0xFF);

    /* 3. 20ms 디바운스 */
    my_delay_ms(20);

    /* 4. 잡음 확인 */
    if (keypad_scan() == 0xFF) return 0xFF;

    /* 5. 키 해제 대기 */
    do {
        fnd_refresh_once();
    } while (keypad_scan() != 0xFF);

    my_delay_ms(10);

    return k;
}

/* ================================================================
   의사 난수 (1~9)
   ================================================================ */
static uint8_t pseudo_rand(void) {
    g_rand = g_rand * 1103515245UL + 12345;
    return (uint8_t)((g_rand >> 5) % 9) + 1;
}

/* ================================================================
   시퀀스 생성 / 표시
   ================================================================ */
static void seq_generate(void) {
    for (uint8_t i = 0; i < 16; i++) {
        g_seq[i] = pseudo_rand();
    }
}

static void seq_show(void) {
    for (uint8_t i = 0; i < g_round_len; i++) {
        disp_single(g_seq[i]);
        my_delay_ms(g_show_ms);
        disp_all(FNT_BLANK);
        my_delay_ms(200);
    }
}

/* ================================================================
   게임 초기화
   ================================================================ */
static void game_start(uint8_t lv) {
    g_level = lv;
    switch (lv) {
        case 1:  g_show_ms = 1000; break;
        case 2:  g_show_ms = 600;  break;
        default: g_show_ms = 300;  break;
    }
    g_life      = 3;
    g_score     = 0;
    g_round     = 0;
    g_round_len = 1;

    life_leds_update(g_life);
    seq_generate();
}

/* ================================================================
   하드웨어 초기화
   ================================================================ */
static void hw_init(void) {
    /* FND 세그먼트 */
    FND_SEG_DDR  = 0xFF;
    FND_SEG_PORT = 0xFF;

    /* FND COM */
    FND_COM_DDR  |= FND_COM_MASK;
    FND_COM_PORT |= FND_COM_MASK;

    /* 키패드: PC0~PC3 출력(COL), PC4~PC7 입력(ROW, 풀업) */
    KEY_DDR  = COL_MASK;
    KEY_PORT = COL_MASK | ROW_MASK;

    /* 라이프 LED: Active Low */
    LIFE_LED_DDR  |= (1 << LIFE_LED1) | (1 << LIFE_LED2) | (1 << LIFE_LED3);
    LIFE_LED_PORT |= (1 << LIFE_LED1) | (1 << LIFE_LED2) | (1 << LIFE_LED3);

    g_digit = 0;
}

/* ================================================================
   메인
   ================================================================ */
int main(void) {
    hw_init();

    g_state = ST_LEVEL_SEL;
    disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);    /* L--1 초기 표시 */

    while (1) {
        switch (g_state) {

        /* ──────────────────────────────────────
           레벨 선택
           ────────────────────────────────────── */
        case ST_LEVEL_SEL: {
            g_rand++;
            fnd_refresh_once();

            uint8_t k = keypad_scan();
            if (k == 1 || k == 2 || k == 3) {
                my_delay_ms(20);
                if (keypad_scan() == k) {
                    while (keypad_scan() != 0xFF)
                        fnd_refresh_once();
                    my_delay_ms(10);

                    /* 선택 레벨 표시 후 게임 시작 */
                    disp_set(FNT_L, FNT_DASH, FNT_DASH, k);
                    my_delay_ms(500);

                    game_start(k);
                    g_state = ST_SHOW_SEQ;
                }
            }
            break;
        }

        /* ──────────────────────────────────────
           시퀀스 표시
           ────────────────────────────────────── */
        case ST_SHOW_SEQ: {
            seq_show();
            disp_set(FNT_DASH, FNT_DASH, FNT_DASH, FNT_DASH);  /* ---- */
            g_input_idx = 0;
            g_state = ST_WAIT_INPUT;
            break;
        }

        /* ──────────────────────────────────────
           숫자 입력 대기
           ────────────────────────────────────── */
        case ST_WAIT_INPUT: {
            uint8_t k = keypad_wait();
            if (k == 0xFF) break;

            /* 입력 에코 0.3초 */
            disp_single(k);
            my_delay_ms(300);
            disp_set(FNT_DASH, FNT_DASH, FNT_DASH, FNT_DASH);

            if (k == g_seq[g_input_idx]) {
                g_input_idx++;
                if (g_input_idx >= g_round_len) {
                    g_round++;
                    g_state = ST_ROUND_CLEAR;
                }
            } else {
                g_life--;
                life_leds_update(g_life);
                g_state = ST_LIFE_LOST;
            }
            break;
        }

        /* ──────────────────────────────────────
           라운드 클리어
           ────────────────────────────────────── */
        case ST_ROUND_CLEAR: {
            disp_round(g_round);
            my_delay_ms(1000);

            if (g_round >= 16) {
                /* donE */
                disp_set(FNT_d, FNT_O, FNT_n, FNT_E);
                my_delay_ms(2000);
                g_score = (uint16_t)(g_round * 10)
                        + (uint16_t)(g_life  *  5);
                disp_number(g_score);
                my_delay_ms(2000);
                disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);
                g_state = ST_LEVEL_SEL;
            } else {
                g_round_len++;
                g_state = ST_SHOW_SEQ;
            }
            break;
        }

        /* ──────────────────────────────────────
           라이프 감소
           ────────────────────────────────────── */
        case ST_LIFE_LOST: {
            disp_set(FNT_L, FNT_DASH, FNT_DASH, g_life);
            my_delay_ms(1000);

            if (g_life == 0) {
                g_state = ST_GAME_OVER;
            } else {
                g_state = ST_SHOW_SEQ;
            }
            break;
        }

        /* ──────────────────────────────────────
           게임 오버
           ────────────────────────────────────── */
        case ST_GAME_OVER: {
            disp_set(FNT_O, FNT_V, FNT_E, FNT_r);  /* OVEr */
            my_delay_ms(1500);

            disp_round(g_round);
            my_delay_ms(2000);

            g_score = (uint16_t)(g_round * 10)
                    + (uint16_t)(g_life  *  5);
            disp_number(g_score);
            my_delay_ms(2000);

            disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);
            g_state = ST_LEVEL_SEL;
            break;
        }

        default:
            g_state = ST_LEVEL_SEL;
            disp_set(FNT_L, FNT_DASH, FNT_DASH, 1);
            break;
        }
    }
    return 0;
}