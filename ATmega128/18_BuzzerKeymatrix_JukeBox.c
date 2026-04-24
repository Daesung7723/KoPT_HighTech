/*
 * ============================================================
 *  ATmega128 4x4 키패드 주크박스 (Jukebox)
 * ============================================================
 *  Target  : ATmega128
 *  Crystal : 16MHz
 *
 *  [기능]
 *  - 4x4 키패드로 9곡 중 하나를 선택하여 연주
 *  - 선택(1~9키) → 재생(C 키) → 일시정지(C 키) → 재개(C 키)
 *  - Timer1 CTC 인터럽트(10ms)로 음표 타이밍 제어
 *  - Timer3 Fast PWM으로 부저 주파수(음계) 출력
 *  - LCD 2줄로 현재 상태 및 곡 제목 표시
 *
 *  [핀 연결]
 * ┌────────────────┬──────────────────────────────────────┐
 * │  장치          │  연결 핀                             │
 * ├────────────────┼──────────────────────────────────────┤
 * │ LCD 데이터     │ PORTB (PB0~PB7 = D0~D7)              │
 * │ LCD RS         │ PA0                                  │
 * │ LCD RW         │ PA1                                  │
 * │ LCD E          │ PA2                                  │
 * │ 부저 (+)       │ PE3 (OC3A, Timer3 PWM 출력)          │
 * │ 키패드 열      │ PC0~PC3 (출력, Col3~Col0)            │
 * │ 키패드 행      │ PC4~PC7 (입력, Row0~Row3, 내부풀업)  │
 * └────────────────┴──────────────────────────────────────┘
 *
 *  [키패드 논리 배치]
 *  ┌─────┬─────┬─────┬─────┐
 *  │  1  │  2  │  3  │  +  │  Row 0  ← 1~3번 곡 선택
 *  │  4  │  5  │  6  │  -  │  Row 1  ← 4~6번 곡 선택
 *  │  7  │  8  │  9  │  *  │  Row 2  ← 7~9번 곡 선택
 *  │  C  │  0  │  #  │ 11  │  Row 3
 *  └─────┴─────┴─────┴─────┘
 *  C (KEY_START_STOP=10): 재생 / 일시정지 / 재개  ★
 *  # (KEY_CLEAR=15)     : 취소 (미사용, 확장 예비)
 * ============================================================
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

/* ============================================================
 *  상수 정의
 * ============================================================ */

/* 키 코드 ★ C키가 재생/정지 역할 */
#define KEY_START_STOP  10      // 'C' 키: 재생 / 일시정지 / 재개
#define KEY_CLEAR       15      // '#' 키: 취소 (미사용, 확장 예비)
#define KEY_NONE        255     // 아무 키도 눌리지 않음

/* LCD 포트 */
#define LCD_DATA_PORT   PORTB
#define LCD_DATA_DDR    DDRB
#define LCD_CTRL_PORT   PORTA
#define LCD_CTRL_DDR    DDRA
#define RS_PIN          0       // PA0: Register Select (0=명령, 1=데이터)
#define RW_PIN          1       // PA1: Read/Write     (0=쓰기, 1=읽기)
#define E_PIN           2       // PA2: Enable         (High→Low 엣지에서 래치)

/* ============================================================
 *  음계 주파수 정의 (Hz)
 * ============================================================
 *  부저는 Timer3 PWM으로 구동하며, ICR3 값으로 주파수를 설정.
 *  공식: ICR3 = (F_CPU / 64 / freq) - 1
 *        Prescaler = 64 사용
 * ============================================================ */
#define REST        0       // 쉼표 (소리 없음)
#define NOTE_C4   262       // 도 (4옥타브)
#define NOTE_D4   294       // 레
#define NOTE_E4   330       // 미
#define NOTE_F4   349       // 파
#define NOTE_G4   392       // 솔
#define NOTE_A4   440       // 라
#define NOTE_B4   494       // 시
#define NOTE_C5   523       // 도 (5옥타브)

/* ============================================================
 *  박자 길이 정의 (Timer1 10ms 틱 단위)
 * ============================================================
 *  Timer1 ISR은 10ms마다 실행되므로:
 *  EIGHTH_NOTE  = 20틱 = 200ms (8분음표)
 *  QUARTER_NOTE = 40틱 = 400ms (4분음표)
 *  HALF_NOTE    = 80틱 = 800ms (2분음표)
 *
 *  ※ 실제 템포에 맞게 값을 조정할 수 있음
 * ============================================================ */
#define EIGHTH_NOTE   20
#define QUARTER_NOTE  40
#define HALF_NOTE     80

/* ============================================================
 *  상태 열거형 (State Machine)
 * ============================================================
 *  주크박스는 아래 3가지 상태 중 하나로 동작:
 *
 *  STATE_SELECT_SONG : 곡 선택 대기 중
 *  STATE_PLAYING     : 곡 재생 중
 *  STATE_PAUSED      : 일시 정지 중
 * ============================================================ */
typedef enum {
    STATE_SELECT_SONG,
    STATE_PLAYING,
    STATE_PAUSED
} JukeboxState;

/* ============================================================
 *  곡 데이터 구조체
 * ============================================================
 *  title      : 곡 제목 문자열 (LCD 표시용)
 *  melody     : 음계 주파수 배열 (Hz)
 *  rhythm     : 박자 길이 배열 (10ms 틱 단위)
 *  note_count : 음표 개수
 * ============================================================ */
typedef struct {
    const char*     title;
    const uint16_t* melody;
    const uint8_t*  rhythm;
    uint16_t        note_count;
} Song;

/* ============================================================
 *  4x4 키매트릭스 맵 ★ C키↔#키 역할 교체
 * ============================================================
 *  물리 배치:
 *  ┌─────┬─────┬─────┬─────┐
 *  │  1  │  2  │  3  │ 12  │  Row 0
 *  │  4  │  5  │  6  │ 13  │  Row 1
 *  │  7  │  8  │  9  │ 14  │  Row 2
 *  │ 10  │  0  │ 15  │ 11  │  Row 3
 *  └─────┴─────┴─────┴─────┘
 *  keymap[행][열]
 *  Row3 Col0: KEY_START_STOP(10) = C키 → 재생/정지
 *  Row3 Col2: KEY_CLEAR(15)      = #키 → 취소(미사용)
 * ============================================================ */
const uint8_t keymap[4][4] = {
    {1,              2,  3,         12},   // Row 0
    {4,              5,  6,         13},   // Row 1
    {7,              8,  9,         14},   // Row 2
    {KEY_START_STOP, 0,  KEY_CLEAR, 11}    // Row 3: C=재생/정지, #=취소
};

/* ============================================================
 *  곡 데이터 (악보)
 * ============================================================
 *  각 곡은 melody[] (음계)와 rhythm[] (박자) 배열로 구성.
 *  배열 길이는 반드시 동일해야 함 (인덱스 1:1 대응).
 * ============================================================ */

/* 1. 반짝반짝 작은 별 */
const uint16_t song1_melody[] = {
    NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4,
    NOTE_F4, NOTE_F4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4, NOTE_C4
};
const uint8_t song1_rhythm[] = {
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 2. 학교 종이 */
const uint16_t song2_melody[] = {
    NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4, NOTE_G4, NOTE_E4,
    NOTE_G4, NOTE_G4, NOTE_E4, NOTE_E4, NOTE_D4,
    REST,
    NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4, NOTE_G4, NOTE_E4,
    NOTE_G4, NOTE_E4, NOTE_D4, NOTE_E4, NOTE_C4
};
const uint8_t song2_rhythm[] = {
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 3. 비행기 */
const uint16_t song3_melody[] = {
    NOTE_E4, NOTE_D4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_E4,
    NOTE_D4, NOTE_D4, NOTE_D4,
    NOTE_E4, NOTE_G4, NOTE_G4,
    NOTE_E4, NOTE_D4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_E4,
    NOTE_D4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_C4
};
const uint8_t song3_rhythm[] = {
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 4. 나비야 */
const uint16_t song4_melody[] = {
    NOTE_G4, NOTE_E4, NOTE_E4, NOTE_F4, NOTE_D4, NOTE_D4,
    NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_G4, NOTE_G4,
    NOTE_G4, NOTE_E4, NOTE_E4, NOTE_E4, NOTE_F4, NOTE_D4, NOTE_D4,
    NOTE_C4, NOTE_E4, NOTE_G4, NOTE_G4, NOTE_C4
};
const uint8_t song4_rhythm[] = {
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 5. 산토끼 */
const uint16_t song5_melody[] = {
    NOTE_C5, NOTE_C5, NOTE_C5, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, NOTE_G4,
    NOTE_C5, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, NOTE_G4,
    NOTE_C5, NOTE_C5, NOTE_C5, NOTE_C5, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, NOTE_G4,
    NOTE_C5, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, NOTE_G4, NOTE_C5
};
const uint8_t song5_rhythm[] = {
    QUARTER_NOTE, EIGHTH_NOTE, EIGHTH_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, EIGHTH_NOTE, EIGHTH_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 6. Mary Had a Little Lamb */
const uint16_t song6_melody[] = {
    NOTE_E4, NOTE_D4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_E4,
    NOTE_D4, NOTE_D4, NOTE_D4,
    NOTE_E4, NOTE_G4, NOTE_G4,
    NOTE_E4, NOTE_D4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_E4, NOTE_E4,
    NOTE_D4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_C4
};
const uint8_t song6_rhythm[] = {
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 7. 환희의 송가 */
const uint16_t song7_melody[] = {
    NOTE_E4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4,
    NOTE_C4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4,
    NOTE_E4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4,
    NOTE_C4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_C4, NOTE_C4
};
const uint8_t song7_rhythm[] = {
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    HALF_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    HALF_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 8. 징글벨 */
const uint16_t song8_melody[] = {
    NOTE_E4, NOTE_E4, NOTE_E4,
    NOTE_E4, NOTE_E4, NOTE_E4,
    NOTE_E4, NOTE_G4, NOTE_C4, NOTE_D4, NOTE_E4,
    NOTE_F4, NOTE_F4, NOTE_F4, NOTE_F4, NOTE_F4,
    NOTE_E4, NOTE_E4, NOTE_E4, NOTE_E4,
    NOTE_D4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_G4
};
const uint8_t song8_rhythm[] = {
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, EIGHTH_NOTE, EIGHTH_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, EIGHTH_NOTE, EIGHTH_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* 9. 생일 축하합니다 */
const uint16_t song9_melody[] = {
    NOTE_C4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_F4, NOTE_E4,
    NOTE_C4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_G4, NOTE_F4,
    NOTE_C4, NOTE_C4, NOTE_C5, NOTE_A4, NOTE_F4, NOTE_E4, NOTE_D4,
    NOTE_A4, NOTE_A4, NOTE_G4, NOTE_F4, NOTE_G4, NOTE_F4
};
const uint8_t song9_rhythm[] = {
    EIGHTH_NOTE, EIGHTH_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    EIGHTH_NOTE, EIGHTH_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    EIGHTH_NOTE, EIGHTH_NOTE, QUARTER_NOTE, QUARTER_NOTE,
    QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE,
    EIGHTH_NOTE, EIGHTH_NOTE, QUARTER_NOTE, QUARTER_NOTE, QUARTER_NOTE, HALF_NOTE
};

/* ============================================================
 *  주크박스 곡 목록
 * ============================================================
 *  sizeof(melody) / sizeof(uint16_t) 로 음표 개수 자동 계산
 * ============================================================ */
const Song jukebox[] = {
    {"Twinkle Star",    song1_melody, song1_rhythm, sizeof(song1_melody)/sizeof(uint16_t)},
    {"School Bell",     song2_melody, song2_rhythm, sizeof(song2_melody)/sizeof(uint16_t)},
    {"Airplane",        song3_melody, song3_rhythm, sizeof(song3_melody)/sizeof(uint16_t)},
    {"Butterfly",       song4_melody, song4_rhythm, sizeof(song4_melody)/sizeof(uint16_t)},
    {"Mountain Rabbit", song5_melody, song5_rhythm, sizeof(song5_melody)/sizeof(uint16_t)},
    {"Mary's Lamb",     song6_melody, song6_rhythm, sizeof(song6_melody)/sizeof(uint16_t)},
    {"Ode to Joy",      song7_melody, song7_rhythm, sizeof(song7_melody)/sizeof(uint16_t)},
    {"Jingle Bells",    song8_melody, song8_rhythm, sizeof(song8_melody)/sizeof(uint16_t)},
    {"Happy Birthday",  song9_melody, song9_rhythm, sizeof(song9_melody)/sizeof(uint16_t)}
};
const uint8_t total_songs = sizeof(jukebox) / sizeof(Song);

/* ============================================================
 *  전역 변수
 * ============================================================ */
volatile JukeboxState g_state               = STATE_SELECT_SONG;
volatile uint8_t      g_selected_song_index = 0;    // 현재 선택된 곡 인덱스 (0~8)
volatile uint8_t      g_is_playing          = 0;    // 1=재생 중, 0=정지
volatile uint16_t     g_note_index          = 0;    // 현재 연주 중인 음표 인덱스
volatile uint16_t     g_note_timer          = 0;    // 현재 음표 경과 시간 (10ms 틱)
volatile uint8_t      g_update_lcd_request  = 1;    // 1이면 메인루프에서 LCD 갱신

/* ============================================================
 *  함수 선언 (프로토타입)
 * ============================================================ */
void    init_all(void);
void    init_ports(void);
void    init_timer1_interrupt(void);
void    init_timer3_pwm(void);
void    play_sound(uint16_t frequency);
void    stop_sound(void);
uint8_t get_key(void);
void    process_key_input(uint8_t key);
void    update_lcd_display(void);
void    lcd_command(unsigned char cmd);
void    lcd_data(unsigned char data);
void    lcd_init(void);
void    lcd_string(const char *str);
void    lcd_goto_xy(unsigned char row, unsigned char col);

/* ============================================================
 *  Timer1 Compare Match A ISR (10ms 주기)
 * ============================================================
 *  동작 원리:
 *  1) g_is_playing == 0 이면 즉시 리턴 (재생 중이 아님)
 *  2) g_note_timer를 1씩 증가 (1틱 = 10ms)
 *  3) 음표 재생 시간의 85% 시점에서 소리를 끔
 *     → 음표 간 끊김(아티큘레이션) 효과
 *  4) 음표 전체 시간이 경과하면 다음 음표로 이동
 *     - 마지막 음표이면 재생 종료 → STATE_SELECT_SONG으로 복귀
 *     - 아니면 다음 음표 재생
 * ============================================================ */
ISR(TIMER1_COMPA_vect)
{
    if (!g_is_playing) return;

    g_note_timer++;

    const Song* s         = &jukebox[g_selected_song_index];
    uint8_t     duration  = s->rhythm[g_note_index];
    uint8_t     play_time = (uint8_t)(duration * 0.85); // 85% 시점에서 묵음

    /* ── 아티큘레이션: 음표 끝 부분에서 소리 OFF ── */
    if (g_note_timer == play_time) {
        stop_sound();
    }

    /* ── 음표 한 개 길이 경과 → 다음 음표로 이동 ── */
    if (g_note_timer >= duration) {
        g_note_timer = 0;
        g_note_index++;

        /* 마지막 음표 이후: 연주 종료 처리 */
        if (g_note_index >= s->note_count) {
            g_note_index         = 0;
            g_is_playing         = 0;
            g_state              = STATE_SELECT_SONG;
            g_update_lcd_request = 1;
            return;
        }

        /* 다음 음표 재생 */
        play_sound(s->melody[g_note_index]);
    }
}

/* ============================================================
 *  메인 함수
 * ============================================================
 *  초기화 후 무한루프에서:
 *  1) 키 입력 감지 및 처리
 *  2) LCD 갱신 요청이 있으면 화면 업데이트
 *  3) 10ms 딜레이 (키 스캔 주기)
 * ============================================================ */
int main(void)
{
    init_all();

    while (1) {
        /* 1. 키 입력 처리 */
        uint8_t key = get_key();
        if (key != KEY_NONE) {
            process_key_input(key);
        }

        /* 2. LCD 갱신 (상태 변화 시에만) */
        if (g_update_lcd_request) {
            update_lcd_display();
            g_update_lcd_request = 0;
        }

        /* 3. 키 스캔 주기 (10ms) */
        _delay_ms(10);
    }
    return 0;
}

/* ============================================================
 *  초기화 함수
 * ============================================================ */
void init_all(void)
{
    init_ports();            // 포트 방향 및 초기값 설정
    lcd_init();              // LCD 초기화
    init_timer3_pwm();       // 부저 PWM 타이머 초기화
    init_timer1_interrupt(); // 음표 타이밍 인터럽트 초기화
    sei();                   // 전역 인터럽트 활성화
}

void init_ports(void)
{
    LCD_DATA_DDR  = 0xFF;                                           // PORTB: LCD 데이터 출력
    LCD_CTRL_DDR |= (1 << RS_PIN) | (1 << RW_PIN) | (1 << E_PIN); // PORTA: LCD 제어 출력
    DDRE         |= (1 << PE3);                                     // PE3: 부저 PWM 출력 (OC3A)
    DDRC          = 0x0F;                                           // PC0~PC3: 키패드 열 출력
    PORTC         = 0xF0;                                           // PC4~PC7: 키패드 행 입력 + 내부 풀업
}

/* ============================================================
 *  키 입력 처리 (상태 머신)
 * ============================================================
 *  STATE_SELECT_SONG:
 *    1~9 키 → 곡 선택 (g_selected_song_index 변경)
 *    C 키   → 선택된 곡 재생 시작
 *
 *  STATE_PLAYING:
 *    C 키   → 일시 정지 (소리 OFF, STATE_PAUSED)
 *
 *  STATE_PAUSED:
 *    C 키   → 재개 (현재 음표부터 재생, STATE_PLAYING)
 * ============================================================ */
void process_key_input(uint8_t key)
{
    switch (g_state) {

        case STATE_SELECT_SONG:
            if (key >= 1 && key <= total_songs) {
                /* 1~9: 해당 번호의 곡 선택 */
                g_selected_song_index = key - 1;
            } else if (key == KEY_START_STOP) {
                /* C키: 선택된 곡 처음부터 재생 */
                g_note_index = 0;
                g_note_timer = 0;
                g_is_playing = 1;
                g_state      = STATE_PLAYING;
                play_sound(jukebox[g_selected_song_index].melody[0]);
            }
            break;

        case STATE_PLAYING:
            if (key == KEY_START_STOP) {
                /* C키: 일시 정지 */
                g_is_playing = 0;
                g_state      = STATE_PAUSED;
                stop_sound();
            }
            break;

        case STATE_PAUSED:
            if (key == KEY_START_STOP) {
                /* C키: 재개 (멈춘 음표부터 이어서 재생) */
                g_is_playing = 1;
                g_state      = STATE_PLAYING;
                play_sound(jukebox[g_selected_song_index].melody[g_note_index]);
            }
            break;
    }

    g_update_lcd_request = 1;  // 상태 변화 → LCD 갱신 요청
}

/* ============================================================
 *  LCD 화면 갱신
 * ============================================================
 *  상태에 따라 2줄로 표시:
 *
 *  SELECT_SONG : "Select Song:"    / "> 1. Twinkle Star"
 *  PLAYING     : "Now Playing..."  / "Twinkle Star"
 *  PAUSED      : "Paused"          / "Twinkle Star"
 * ============================================================ */
void update_lcd_display(void)
{
    char line1[17] = "";
    char line2[17] = "";
    const Song* s = &jukebox[g_selected_song_index];

    lcd_command(0x01);   // LCD 전체 지우기

    switch (g_state) {
        case STATE_SELECT_SONG:
            sprintf(line1, "Select Song:");
            sprintf(line2, "> %d. %s", g_selected_song_index + 1, s->title);
            break;
        case STATE_PLAYING:
            sprintf(line1, "Now Playing...");
            sprintf(line2, "%s", s->title);
            break;
        case STATE_PAUSED:
            sprintf(line1, "Paused");
            sprintf(line2, "%s", s->title);
            break;
    }

    lcd_goto_xy(0, 0); lcd_string(line1);  // 1행 출력
    lcd_goto_xy(1, 0); lcd_string(line2);  // 2행 출력
}

/* ============================================================
 *  Timer1 초기화: CTC 모드, 10ms 주기
 * ============================================================
 *  설정 근거:
 *    클럭: 16MHz, 프리스케일러: 64
 *    타이머 클럭 = 16,000,000 / 64 = 250,000 Hz (4us/tick)
 *    OCR1A = 2499 → 2500 tick × 4us = 10,000us = 10ms
 * ============================================================ */
void init_timer1_interrupt(void)
{
    TCCR1B |= (1 << WGM12) | (1 << CS11) | (1 << CS10); // CTC, 프리스케일러 64
    OCR1A   = 2499;                                       // 10ms 주기
    TIMSK  |= (1 << OCIE1A);                              // Compare Match A 인터럽트 활성화
}

/* ============================================================
 *  Timer3 초기화: Fast PWM 모드 (부저 주파수 제어)
 * ============================================================
 *  WGM31, WGM33, WGM32 : Fast PWM (TOP = ICR3)
 *  COM3A1               : OC3A(PE3)에 PWM 출력
 *  CS31, CS30           : 프리스케일러 64
 *
 *  주파수 공식:
 *    ICR3  = (F_CPU / 64 / freq) - 1
 *    OCR3A = ICR3 / 2  (듀티비 50%)
 * ============================================================ */
void init_timer3_pwm(void)
{
    TCCR3A |= (1 << WGM31) | (1 << COM3A1);            // Fast PWM, OC3A 출력
    TCCR3B |= (1 << WGM33) | (1 << WGM32)              // Fast PWM (TOP=ICR3)
            | (1 << CS31)  | (1 << CS30);               // 프리스케일러 64
    stop_sound();                                        // 초기 소리 OFF
}

/* ============================================================
 *  부저 제어 함수
 * ============================================================
 *  play_sound(freq): ICR3으로 TOP 설정 → 해당 주파수 PWM 출력
 *  stop_sound()    : OCR3A = 0 → 듀티비 0% → 소리 없음
 * ============================================================ */
void play_sound(uint16_t freq)
{
    if (freq == 0) { stop_sound(); return; }   // 쉼표(REST) 처리
    ICR3  = (F_CPU / 64 / freq) - 1;           // PWM TOP 값 = 주파수 결정
    OCR3A = ICR3 / 2;                           // 듀티비 50% = 최대 음량
}

void stop_sound(void)
{
    OCR3A = 0;   // 듀티비 0% → 소리 없음
}

/* ============================================================
 *  키패드 스캔 함수 (Non-blocking, 엣지 감지)
 * ============================================================
 *  동작 원리:
 *  - 열(Col) 하나씩 Low로 구동 → 행(Row) 상태 읽기
 *  - 배선 보정: (3 - c)로 열 인덱스 반전
 *  - 행 반전: 물리 행(r)을 논리 행(3-r)으로 변환
 *
 *  엣지 감지:
 *  - 이전 상태에서 키가 눌렸다가 → 현재 아무 키도 없으면
 *    이전 키 값을 반환 (키를 뗀 순간에 1회 처리)
 *  - 이렇게 하면 키를 계속 누르고 있어도 1회만 인식
 * ============================================================ */
uint8_t get_key(void)
{
    static uint8_t last_key = KEY_NONE;   // 직전 키 상태 저장
    uint8_t        curr_key = KEY_NONE;

    /* 열 스캔 */
    for (uint8_t c = 0; c < 4; c++) {
        PORTC  = 0xFF;                    // 모든 열 High
        PORTC &= ~(1 << (3 - c));         // c번 열만 Low (배선 보정)
        _delay_us(5);                     // 신호 안정화

        /* 행 읽기 (PC4~PC7) */
        for (uint8_t r = 0; r < 4; r++) {
            if (!(PINC & (1 << (r + 4)))) {
                curr_key = keymap[3 - r][c];   // 논리 행 변환 후 맵핑
                break;
            }
        }
        if (curr_key != KEY_NONE) break;
    }

    /* 엣지 감지: 키를 뗀 순간에만 반환 */
    if (curr_key == KEY_NONE && last_key != KEY_NONE) {
        uint8_t released = last_key;
        last_key = KEY_NONE;
        return released;
    }

    last_key = curr_key;
    return KEY_NONE;
}

/* ============================================================
 *  LCD 제어 함수
 * ============================================================
 *  lcd_command(cmd) : RS=0으로 명령 전송
 *  lcd_data(data)   : RS=1으로 데이터(문자) 전송
 *  lcd_init()       : 8비트 모드, 2행, 커서 OFF, 자동 증가
 *  lcd_string(str)  : 문자열 출력 (널 종료까지)
 *  lcd_goto_xy(r,c) : 커서 이동 (r=행 0~1, c=열 0~15)
 *
 *  Enable 펄스(E 핀):
 *    High → _delay_us(1) → Low 로 LCD가 데이터를 래치함
 * ============================================================ */
void lcd_command(unsigned char cmd)
{
    PORTB  = cmd;
    PORTA &= ~((1 << PA0) | (1 << PA1));   // RS=0, RW=0 (명령 쓰기)
    PORTA |=  (1 << PA2);                  // E = High
    _delay_us(1);
    PORTA &= ~(1 << PA2);                  // E = Low (래치)
    _delay_ms(2);                          // 명령 실행 대기
}

void lcd_data(unsigned char data)
{
    PORTB  = data;
    PORTA |=  (1 << PA0);                  // RS=1 (데이터)
    PORTA &= ~(1 << PA1);                  // RW=0 (쓰기)
    PORTA |=  (1 << PA2);                  // E = High
    _delay_us(1);
    PORTA &= ~(1 << PA2);                  // E = Low (래치)
    _delay_us(50);                         // 데이터 실행 대기
}

void lcd_init(void)
{
    _delay_ms(50);          // 전원 안정화 대기
    lcd_command(0x38);      // Function Set: 8비트, 2행, 5×8 폰트
    lcd_command(0x0C);      // Display ON, 커서 OFF, 깜빡임 OFF
    lcd_command(0x01);      // Display Clear
    lcd_command(0x06);      // Entry Mode: 커서 오른쪽 이동, 화면 이동 없음
}

void lcd_string(const char *str)
{
    while (*str) lcd_data(*str++);   // 문자열 끝('\0')까지 한 글자씩 출력
}

void lcd_goto_xy(unsigned char r, unsigned char c)
{
    /* DDRAM 주소: 1행 = 0x80+c, 2행 = 0xC0+c */
    lcd_command((r == 0 ? 0x80 : 0xC0) + c);
}