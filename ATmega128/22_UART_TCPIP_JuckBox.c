/*
 * ATmega128 통합 주크박스 (최종 수정본)
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 기능 ---
 * - ADC 값 표시와 주크박스 기능을 단일 모드로 통합
 * - LCD 상단: ADC0, ADC1 값 실시간 표시
 * - LCD 하단: 키 입력 및 UART 명령에 따른 곡 정보 및 재생 상태를 스croll하여 표시
 * - 모든 모드에서 ADC 값을 주기적으로 UART 통신
 * - UART 명령 "[Song-x]"를 수신하면 해당 곡을 즉시 연주
 *
 * --- 연결 정보 ---
 * LCD 데이터 (D0-D7): PORTB
 * LCD 제어 (RS, RW, E): PORTA (PA0, PA1, PA2)
 * 부저 (+): PE3 (OC3A)
 * 키 매트릭스: PC0-PC7
 * ADC0/ADC1: PF0/PF1
 * UART1 TXD1/RXD1: PD3/PD2
 */

#define F_CPU 16000000UL
#define BAUD_RATE 9600

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include <avr/interrupt.h>
#include <stdlib.h> // atoi 함수를 위해 추가

// --- 상수 정의 ---
#define KEY_START_STOP 15
#define KEY_CLEAR      10
#define KEY_NONE       255

#define LCD_DATA_PORT PORTB
#define LCD_DATA_DDR  DDRB
#define LCD_CTRL_PORT PORTA
#define LCD_CTRL_DDR  DDRA
#define RS_PIN 0
#define RW_PIN 1
#define E_PIN  2

// --- 음계 주파수 (Hz) ---
#define REST 0
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523

// --- 박자 길이 (10ms 단위) ---
#define EIGHTH_NOTE  20
#define QUARTER_NOTE 40
#define HALF_NOTE    80

// --- 열거형 및 구조체 정의 ---
typedef enum { STATE_SELECT_SONG, STATE_PLAYING, STATE_PAUSED } JukeboxState;

typedef struct {
    const char* title;
    const uint16_t* melody;
    const uint8_t* rhythm;
    uint16_t note_count;
} Song;

// --- 4x4 키 매트릭스 맵 (상하 및 좌우 반전) ---
const uint8_t keymap[4][4] = {
    {KEY_START_STOP, KEY_CLEAR, 0, 11},
    {14, 9, 8, 7},
    {13, 6, 5, 4},
    {12, 3, 2, 1}
};

// --- 곡 데이터 (9곡) ---
const uint16_t song1_melody[] = {NOTE_C4,NOTE_C4,NOTE_G4,NOTE_G4,NOTE_A4,NOTE_A4,NOTE_G4,NOTE_F4,NOTE_F4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_C4};
const uint8_t  song1_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
const uint16_t song2_melody[] = {NOTE_G4,NOTE_G4,NOTE_A4,NOTE_A4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_E4,NOTE_D4,REST,NOTE_G4,NOTE_G4,NOTE_A4,NOTE_A4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_E4,NOTE_D4,NOTE_E4,NOTE_C4};
const uint8_t  song2_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
const uint16_t song3_melody[] = {NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_C4};
const uint8_t  song3_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
const uint16_t song4_melody[] = {NOTE_G4,NOTE_E4,NOTE_E4,NOTE_F4,NOTE_D4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_F4,NOTE_D4,NOTE_D4,NOTE_C4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_C4};
const uint8_t  song4_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
const uint16_t song5_melody[] = {NOTE_C5,NOTE_C5,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5,NOTE_C5,NOTE_C5,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5,NOTE_G4,NOTE_A4,NOTE_G4,NOTE_E4,NOTE_G4,NOTE_C5};
const uint8_t  song5_rhythm[] = {QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
const uint16_t song6_melody[] = {NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_G4,NOTE_G4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_C4};
const uint8_t  song6_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
const uint16_t song7_melody[] = {NOTE_E4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_F4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_E4,NOTE_F4,NOTE_G4,NOTE_G4,NOTE_F4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_C4,NOTE_C4};
const uint8_t  song7_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,HALF_NOTE};
const uint16_t song8_melody[] = {NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_G4,NOTE_C4,NOTE_D4,NOTE_E4,NOTE_F4,NOTE_F4,NOTE_F4,NOTE_F4,NOTE_F4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_E4,NOTE_D4,NOTE_D4,NOTE_E4,NOTE_D4,NOTE_G4};
const uint8_t  song8_rhythm[] = {QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,EIGHTH_NOTE,EIGHTH_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,QUARTER_NOTE,HALF_NOTE};
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
volatile JukeboxState g_jukebox_state = STATE_SELECT_SONG;
volatile uint8_t g_selected_song_index = 0;
volatile uint8_t g_is_playing = 0;
volatile uint16_t g_note_index = 0;
volatile uint16_t g_note_timer = 0;

// 작업 요청 플래그
volatile uint8_t g_update_lcd_request = 1;
volatile uint8_t g_periodic_task_request = 0;
volatile uint8_t g_scroll_request = 0;
volatile uint8_t g_uart_command_ready = 0;

// 데이터 저장용 변수
volatile uint16_t g_adc0_val = 0, g_adc1_val = 0;
char g_scroll_text_buffer[40] = "Select Song...";
volatile uint8_t g_scroll_index = 0;

// UART 수신 버퍼 및 인덱스
#define UART_RX_BUFFER_SIZE 32
volatile char g_uart_rx_buffer[UART_RX_BUFFER_SIZE];
volatile uint8_t g_uart_rx_index = 0;

// --- 함수 선언 ---
void init_all(void);
void init_ports(void);
void init_adc(void);
void init_uart1(void);
void init_timer1_interrupt(void);
void init_timer3_pwm(void);
void play_sound(uint16_t frequency);
void stop_sound(void);
uint8_t get_key(void);
void process_key_input(uint8_t key);
void process_uart_command(void);
void update_lcd_display(void);
uint16_t read_adc(uint8_t channel);
void uart1_puts(const char *str);
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);

// --- Timer1 인터럽트 서비스 루틴 (10ms 시스템 틱) ---
ISR(TIMER1_COMPA_vect) {
    static uint16_t periodic_counter = 0;
    static uint16_t scroll_counter = 0;

    // 음악 재생 로직
    if (g_is_playing) {
        g_note_timer++;
        const Song* current_song = &jukebox[g_selected_song_index];
        uint8_t note_duration = current_song->rhythm[g_note_index];

        if (g_note_timer >= note_duration) {
            g_note_timer = 0;
            g_note_index++;
            if (g_note_index >= current_song->note_count) {
                g_note_index = 0;
                g_is_playing = 0;
                g_jukebox_state = STATE_SELECT_SONG;
                g_update_lcd_request = 1;
            } else {
                play_sound(current_song->melody[g_note_index]);
            }
        } else if (g_note_timer == (note_duration * 17 / 20)) {
            stop_sound();
        }
    }

    // 500ms 주기 작업 플래그
    periodic_counter++;
    if (periodic_counter >= 50) {
        periodic_counter = 0;
        g_periodic_task_request = 1;
    }

    // 400ms 스크롤 작업 플래그
    scroll_counter++;
    if (scroll_counter >= 40) {
        scroll_counter = 0;
        g_scroll_request = 1;
    }
}

// --- UART 수신 인터럽트 서비스 루틴 ---
ISR(USART1_RX_vect) {
    char received_char = UDR1;
    if (received_char == '\n' || received_char == '\r') {
        g_uart_rx_buffer[g_uart_rx_index] = '\0';
        if (g_uart_rx_index > 0) {
            g_uart_command_ready = 1;
        }
        g_uart_rx_index = 0;
    } else if (g_uart_rx_index < UART_RX_BUFFER_SIZE - 1) {
        g_uart_rx_buffer[g_uart_rx_index++] = received_char;
    }
}

int main(void) {
    init_all();

    while (1) {
        // 이벤트 처리: 키패드
        uint8_t key = get_key();
        if (key != KEY_NONE) {
            process_key_input(key);
        }

        // 이벤트 처리: UART 명령
        if (g_uart_command_ready) {
            g_uart_command_ready = 0;
            process_uart_command();
        }

        // 주기적 작업 처리 (500ms)
        if (g_periodic_task_request) {
            g_periodic_task_request = 0;
            g_adc0_val = read_adc(0);
            g_adc1_val = read_adc(1);
            g_update_lcd_request = 1;
        }
        
        // 스크롤 처리 (400ms)
        if (g_scroll_request) {
            g_scroll_request = 0;
            g_scroll_index++;
            g_update_lcd_request = 1;
        }

        // 화면 업데이트 요청 처리
        if (g_update_lcd_request) {
            g_update_lcd_request = 0;
            update_lcd_display();
        }
    }
    return 0;
}

void init_all(void) {
    init_ports();
    init_adc();
    init_uart1();
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
    DDRF &= ~((1 << PF0) | (1 << PF1));
    DDRD |= (1 << PD3);
}

void init_uart1(void) {
    uint16_t ubrr = (F_CPU / (16UL * BAUD_RATE)) - 1;
    UBRR1H = (uint8_t)(ubrr >> 8);
    UBRR1L = (uint8_t)ubrr;
    UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

void process_key_input(uint8_t key) {
    g_scroll_index = 0;
    
    if (key >= 1 && key <= total_songs) {
        g_selected_song_index = key - 1;
        g_jukebox_state = STATE_SELECT_SONG;
        g_is_playing = 0;
        stop_sound();
    } else if (key == KEY_START_STOP) {
        if (g_jukebox_state == STATE_PLAYING) {
            g_is_playing = 0;
            g_jukebox_state = STATE_PAUSED;
            stop_sound();
        } else {
            g_note_index = (g_jukebox_state == STATE_SELECT_SONG) ? 0 : g_note_index;
            g_note_timer = 0;
            g_is_playing = 1;
            g_jukebox_state = STATE_PLAYING;
            play_sound(jukebox[g_selected_song_index].melody[g_note_index]);
        }
    }
    g_update_lcd_request = 1;
}

void process_uart_command(void) {
    char command_buffer[UART_RX_BUFFER_SIZE];
    strcpy(command_buffer, (const char*)g_uart_rx_buffer);

    // 1. 메시지가 "[Song-" 으로 시작하는지 검사
    if (strncmp(command_buffer, "[Song-", 6) == 0) {
        
        // 2. 닫는 대괄호 ']'의 위치를 찾음
        char* end_bracket = strchr(command_buffer, ']');

        if (end_bracket != NULL) {
            const char* number_start = command_buffer + 6;
            
            // 3. 숫자 부분을 정수로 변환
            int song_num = atoi(number_start);

            // 4. 변환된 곡 번호가 유효한 범위에 있는지 확인
            if (song_num >= 1 && song_num <= total_songs) {
                // 모든 검사를 통과 -> 곡 재생
                g_selected_song_index = song_num - 1;
                g_jukebox_state = STATE_PLAYING;
                g_is_playing = 1;
                g_note_index = 0;
                g_note_timer = 0;
                g_scroll_index = 0;
                play_sound(jukebox[g_selected_song_index].melody[g_note_index]);
                g_update_lcd_request = 1;

                char msg[30];
                sprintf(msg, "OK: Play Song %d\r\n", song_num);
                uart1_puts(msg);
            } else {
                uart1_puts("Error: Invalid song number.\r\n");
            }
        } else {
            uart1_puts("Error: Malformed command.\r\n");
        }
    }
}

void update_lcd_display(void) {
    char line1_buf[17];
    char temp_scroll_buf[17];
    char uart_buf[40];
    
    // Line 1: ADC 값 표시
    sprintf(line1_buf, "C0:%04d C1:%04d", (int)g_adc0_val, (int)g_adc1_val);
    lcd_goto_xy(0, 0);
    lcd_string(line1_buf);

    // Line 2: 상태 및 곡 정보 스크롤
    const Song* current_song = &jukebox[g_selected_song_index];
    switch (g_jukebox_state) {
        case STATE_SELECT_SONG:
        sprintf(g_scroll_text_buffer, "Select: %d. %s  ", g_selected_song_index + 1, current_song->title);
        break;
        case STATE_PLAYING:
        sprintf(g_scroll_text_buffer, "Playing: %s  ", current_song->title);
        break;
        case STATE_PAUSED:
        sprintf(g_scroll_text_buffer, "Paused: %s  ", current_song->title);
        break;
    }

    int len = strlen(g_scroll_text_buffer);
    if (g_scroll_index >= len) g_scroll_index = 0;

    strncpy(temp_scroll_buf, g_scroll_text_buffer + g_scroll_index, 16);
    temp_scroll_buf[16] = '\0';
    
    if (strlen(temp_scroll_buf) < 16 && len > 16) {
        strncat(temp_scroll_buf, g_scroll_text_buffer, 16 - strlen(temp_scroll_buf));
    }
    
    lcd_goto_xy(1, 0);
    lcd_string(temp_scroll_buf);
    for(int i = strlen(temp_scroll_buf); i < 16; i++) {
        lcd_data(' ');
    }

    // UART 전송
    sprintf(uart_buf, "[ADC0]%d\r\n", (int)g_adc0_val); uart1_puts(uart_buf);
    sprintf(uart_buf, "[ADC1]%d\r\n", (int)g_adc1_val); uart1_puts(uart_buf);
    uart1_puts("[LCD-0]"); uart1_puts(line1_buf); uart1_puts("\r\n");
    uart1_puts("[LCD-1]"); uart1_puts(temp_scroll_buf); uart1_puts("\r\n");
}

// --- 기타 유틸리티 및 하드웨어 제어 함수들 (변경 없음) ---
void init_adc(void) { ADMUX = (1 << REFS0); ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); }
void init_timer1_interrupt(void) { TCCR1B|=(1<<WGM12)|(1<<CS11)|(1<<CS10); OCR1A=2499; TIMSK|=(1<<OCIE1A); }
void init_timer3_pwm(void) { TCCR3A|=(1<<WGM31)|(1<<COM3A1); TCCR3B|=(1<<WGM33)|(1<<WGM32)|(1<<CS31)|(1<<CS30); ICR3=4999; stop_sound(); }
void play_sound(uint16_t freq) { if(freq==0){stop_sound();return;} ICR3=(F_CPU/64/freq)-1; OCR3A=ICR3/2; }
void stop_sound(void) { OCR3A = 0; }
uint16_t read_adc(uint8_t channel) { ADMUX=(ADMUX&0xE0)|(channel&0x1F); ADCSRA|=(1<<ADSC); while(ADCSRA&(1<<ADSC)); return ADC; }
void uart1_puts(const char *str) { while (*str) { while(!(UCSR1A&(1<<UDRE1))); UDR1=*str++; } }
void lcd_command(unsigned char cmd) { PORTB=cmd; PORTA&=~((1<<RS_PIN)|(1<<RW_PIN)); PORTA|=(1<<E_PIN); _delay_us(1); PORTA&=~(1<<E_PIN); _delay_ms(2); }
void lcd_data(unsigned char data) { PORTB=data; PORTA|=(1<<RS_PIN); PORTA&=~(1<<RW_PIN); PORTA|=(1<<E_PIN); _delay_us(1); PORTA&=~(1<<E_PIN); _delay_us(50); }
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
