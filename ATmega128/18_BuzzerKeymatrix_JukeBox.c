/*
 * ATmega128 4x4 키패드 주크박스
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - 키패드로 곡을 선택하고 재생/일시정지를 제어하는 주크박스 (총 9곡)
 * - 악보의 박자부분은 조정할 필요 있음.
 * - Timer1 인터럽트를 사용하여 안정적인 연주 및 일시 정지/재개 기능 구현
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * 부저 (+): PE3 (OC3A)
 * 키 매트릭스 열 (C3-C0): PC0-PC3 (출력), 행 (L0-L3): PC4-PC7 (입력)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <avr/interrupt.h>

// --- 상수 정의 ---
#define KEY_START_STOP 15
#define KEY_CLEAR      10
#define KEY_NONE       255

#define LCD_DATA_PORT PORTB
#define LCD_DATA_DDR  DDRB
#define LCD_CTRL_PORT PORTA
#define LCD_CTRL_DDR  DDRA
#define RS_PIN 0 // PA0
#define RW_PIN 1 // PA1
#define E_PIN  2 // PA2

// --- 음계 주파수 (Hz) ---
#define REST    0
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523

// --- 박자 길이 (10ms 단위) ---
#define EIGHTH_NOTE  20 // 8분 음표 (200ms)
#define QUARTER_NOTE 40 // 4분 음표 (400ms)
#define HALF_NOTE    80 // 2분 음표 (800ms)

// --- 열거형 및 구조체 정의 ---
typedef enum { STATE_SELECT_SONG, STATE_PLAYING, STATE_PAUSED } JukeboxState;

typedef struct {
    const char* title;
    const uint16_t* melody;
    const uint8_t* rhythm;
    uint16_t note_count;
} Song;

// --- 4x4 키 매트릭스 맵 (전화기 배열) ---
const uint8_t keymap[4][4] = {
    {1, 2, 3, 12}, // 12: '+'
    {4, 5, 6, 13}, // 13: '-'
    {7, 8, 9, 14}, // 14: '*'
    {KEY_CLEAR, 0, KEY_START_STOP, 11}
};

// --- 곡 데이터 ---
// 1. 반짝반짝 작은 별
const uint16_t song1_melody[] = {NOTE_C4,NOTE_C4,NOTE_G4,NOTE_G4,NOTE_A4,NOTE_A4,NOTE_G4,NOTE_F4,NOTE_F4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_C4};
const uint8_t  song1_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
// 2. 학교 종이
const uint16_t song2_melody[] = {NOTE_G4,NOTE_G4,NOTE_A4,NOTE_A4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_E4,NOTE_D4,REST,NOTE_G4,NOTE_G4,NOTE_A4,NOTE_A4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_E4,NOTE_D4,NOTE_E4,NOTE_C4};
const uint8_t  song2_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
// 3. 비행기
const uint16_t song3_melody[] = {NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_C4};
const uint8_t  song3_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
// 4. 나비야
const uint16_t song4_melody[] = {NOTE_G4,NOTE_E4,NOTE_E4,NOTE_F4,NOTE_D4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_F4,NOTE_D4,NOTE_D4,NOTE_C4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_C4};
const uint8_t  song4_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
// 5. 산토끼
const uint16_t song5_melody[] = {NOTE_C5,NOTE_C5,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5,NOTE_C5,NOTE_C5,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5};
const uint8_t  song5_rhythm[] = {QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
// 6. Mary Had a Little Lamb
const uint16_t song6_melody[] = {NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_C4};
const uint8_t  song6_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
// 7. 환희의 송가
const uint16_t song7_melody[] = {NOTE_E4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_F4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_F4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4};
const uint8_t  song7_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,HALF_NOTE};
// 8. 징글벨
const uint16_t song8_melody[] = {NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_G4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_F4,NOTE_F4,NOTE_F4,NOTE_F4,NOTE_F4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_G4};
const uint8_t  song8_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
// 9. 생일 축하합니다
const uint16_t song9_melody[] = {NOTE_C4,NOTE_C4,NOTE_D4,NOTE_C4,NOTE_F4,NOTE_E4,NOTE_C4,NOTE_C4,NOTE_D4,NOTE_C4,NOTE_G4,NOTE_F4,NOTE_C4,NOTE_C4,NOTE_C5,NOTE_A4,NOTE_F4,NOTE_E4,NOTE_D4,NOTE_A4,NOTE_A4,NOTE_G4,NOTE_F4,NOTE_G4,NOTE_F4};
const uint8_t  song9_rhythm[] = {EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};

// --- 주크박스 곡 목록 ---
const Song jukebox[] = {
    {"Twinkle Star", song1_melody, song1_rhythm, sizeof(song1_melody)/sizeof(uint16_t)},
    {"School Bell",  song2_melody, song2_rhythm, sizeof(song2_melody)/sizeof(uint16_t)},
    {"Airplane",     song3_melody, song3_rhythm, sizeof(song3_melody)/sizeof(uint16_t)},
    {"Butterfly",    song4_melody, song4_rhythm, sizeof(song4_melody)/sizeof(uint16_t)},
    {"Mountain Rabbit", song5_melody, song5_rhythm, sizeof(song5_melody)/sizeof(uint16_t)},
    {"Mary's Lamb",    song6_melody, song6_rhythm, sizeof(song6_melody)/sizeof(uint16_t)},
    {"Ode to Joy",   song7_melody, song7_rhythm, sizeof(song7_melody)/sizeof(uint16_t)},
    {"Jingle Bells", song8_melody, song8_rhythm, sizeof(song8_melody)/sizeof(uint16_t)},
    {"Happy Birthday", song9_melody, song9_rhythm, sizeof(song9_melody)/sizeof(uint16_t)}
};
const uint8_t total_songs = sizeof(jukebox) / sizeof(Song);

// --- 전역 변수 ---
volatile JukeboxState g_state = STATE_SELECT_SONG;
volatile uint8_t g_selected_song_index = 0;
volatile uint8_t g_is_playing = 0;
volatile uint16_t g_note_index = 0;
volatile uint16_t g_note_timer = 0;
volatile uint8_t g_update_lcd_request = 1;

// --- 함수 선언 ---
void init_all(void);
void init_ports(void);
void init_timer1_interrupt(void);
void init_timer3_pwm(void);
void play_sound(uint16_t frequency);
void stop_sound(void);
uint8_t get_key(void);
void process_key_input(uint8_t key);
void update_lcd_display(void);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);

// --- Timer1 인터럽트 서비스 루틴 (10ms 마다 실행) ---
ISR(TIMER1_COMPA_vect) {
    if (!g_is_playing) return;

    g_note_timer++;

    const Song* current_song = &jukebox[g_selected_song_index];
    uint8_t note_duration = current_song->rhythm[g_note_index];
    uint8_t play_time = note_duration * 0.85;

    if (g_note_timer == play_time) {
        stop_sound();
    }
    
    if (g_note_timer >= note_duration) {
        g_note_timer = 0;
        g_note_index++;
        
        if (g_note_index >= current_song->note_count) {
            g_note_index = 0;
            g_is_playing = 0;
            g_state = STATE_SELECT_SONG;
            g_update_lcd_request = 1;
            return;
        }
        play_sound(current_song->melody[g_note_index]);
    }
}

int main(void) {
    init_all();
    while (1) {
        uint8_t key = get_key();
        if (key != KEY_NONE) {
            process_key_input(key);
        }
        if (g_update_lcd_request) {
            update_lcd_display();
            g_update_lcd_request = 0;
        }
        _delay_ms(10);
    }
    return 0;
}

void init_all(void) {
    init_ports();
    lcd_init();
    init_timer3_pwm();
    init_timer1_interrupt();
    sei();
}

void init_ports(void) {
    LCD_DATA_DDR = 0xFF;
    LCD_CTRL_DDR |= (1<<RS_PIN) | (1<<RW_PIN) | (1<<E_PIN);
    DDRE |= (1 << PE3);
    DDRC = 0x0F; PORTC = 0xF0;
}

void process_key_input(uint8_t key) {
    switch (g_state) {
        case STATE_SELECT_SONG:
            if (key >= 1 && key <= total_songs) {
                g_selected_song_index = key - 1;
            } else if (key == KEY_START_STOP) {
                g_note_index = 0;
                g_note_timer = 0;
                g_is_playing = 1;
                g_state = STATE_PLAYING;
                play_sound(jukebox[g_selected_song_index].melody[g_note_index]);
            }
            break;
        case STATE_PLAYING:
            if (key == KEY_START_STOP) {
                g_is_playing = 0;
                g_state = STATE_PAUSED;
                stop_sound();
            }
            break;
        case STATE_PAUSED:
            if (key == KEY_START_STOP) {
                g_is_playing = 1;
                g_state = STATE_PLAYING;
                play_sound(jukebox[g_selected_song_index].melody[g_note_index]);
            }
            break;
    }
    g_update_lcd_request = 1;
}

void update_lcd_display(void) {
    char line1[17] = "";
    char line2[17] = "";
    const Song* current_song = &jukebox[g_selected_song_index];

    lcd_command(0x01); // 화면 지우기

    switch (g_state) {
        case STATE_SELECT_SONG:
            sprintf(line1, "Select Song:");
            sprintf(line2, "> %d. %s", g_selected_song_index + 1, current_song->title);
            break;
        case STATE_PLAYING:
            sprintf(line1, "Now Playing...");
            sprintf(line2, "%s", current_song->title);
            break;
        case STATE_PAUSED:
            sprintf(line1, "Paused");
            sprintf(line2, "%s", current_song->title);
            break;
    }
    lcd_goto_xy(0, 0); lcd_string(line1);
    lcd_goto_xy(1, 0); lcd_string(line2);
}

// --- 타이머, PWM, LCD, 키패드 제어 함수들 (이전과 유사) ---
void init_timer1_interrupt(void) { TCCR1B|=(1<<WGM12)|(1<<CS11)|(1<<CS10); OCR1A=2499; TIMSK|=(1<<OCIE1A); }
void init_timer3_pwm(void) { TCCR3A|=(1<<WGM31)|(1<<COM3A1); TCCR3B|=(1<<WGM33)|(1<<WGM32)|(1<<CS31)|(1<<CS30); stop_sound(); }
void play_sound(uint16_t freq) { if(freq==0){stop_sound();return;} ICR3=(F_CPU/64/freq)-1; OCR3A=ICR3/2; }
void stop_sound(void) { OCR3A = 0; }
void lcd_command(unsigned char cmd) { PORTB=cmd; PORTA&=~((1<<PA0)|(1<<PA1)); PORTA|=(1<<PA2); _delay_us(1); PORTA&=~(1<<PA2); _delay_ms(2); }
void lcd_data(unsigned char data) { PORTB=data; PORTA|=(1<<PA0); PORTA&=~(1<<PA1); PORTA|=(1<<PA2); _delay_us(1); PORTA&=~(1<<PA2); _delay_us(50); }
void lcd_init(void) { _delay_ms(50); lcd_command(0x38); lcd_command(0x0C); lcd_command(0x01); lcd_command(0x06); }
void lcd_string(const char *str) { while (*str) lcd_data(*str++); }
void lcd_goto_xy(unsigned char r, unsigned char c) { lcd_command((r==0?0x80:0xC0)+c); }
uint8_t get_key() {
    static uint8_t last_key_state=KEY_NONE; uint8_t current_key=KEY_NONE;
    for (uint8_t c=0;c<4;c++) { PORTC=0xFF; PORTC&=~(1<<(3-c)); _delay_us(5);
        for(uint8_t r=0;r<4;r++) { if(!(PINC&(1<<(r+4)))){current_key=keymap[r][c];break;}}
        if(current_key!=KEY_NONE)break;
    }
    if(current_key==KEY_NONE&&last_key_state!=KEY_NONE){uint8_t p=last_key_state;last_key_state=KEY_NONE;return p;}
    last_key_state=current_key;return KEY_NONE;
}
