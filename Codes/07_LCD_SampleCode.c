/*
 * ATmega128 16x2 캐릭터 LCD 제어 예제
 * Target: ATmega128
 * Crystal: 16MHz (16MHz 크리스탈 사용)
 *
 * --- 연결 정보 ---
 * LCD 데이터 핀 (D0-D7): PORTB (PB0-PB7)
 * LCD RS 핀: PA0
 * LCD RW 핀: PA1
 * LCD E(Enable) 핀: PA2
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>

// --- LCD 핀 및 포트 정의 ---
#define LCD_DATA_PORT PORTB
#define LCD_DATA_DDR  DDRB
#define LCD_CTRL_PORT PORTA
#define LCD_CTRL_DDR  DDRA
#define RS_PIN 0 // PA0
#define RW_PIN 1 // PA1
#define E_PIN  2 // PA2

// --- 함수 선언 ---
void lcd_command(unsigned char cmd);
void lcd_data(unsigned char data);
void lcd_init(void);
void lcd_string(const char *str);
void lcd_goto_xy(unsigned char row, unsigned char col);

/**
 * @brief LCD에 명령어를 전송하는 함수
 * @param cmd 전송할 명령어 바이트
 */
void lcd_command(unsigned char cmd) {
    LCD_DATA_PORT = cmd; // 데이터 포트에 명령어 전송
    LCD_CTRL_PORT &= ~(1 << RS_PIN); // RS = 0 (명령어 모드)
    LCD_CTRL_PORT &= ~(1 << RW_PIN); // RW = 0 (쓰기 모드)
    
    // Enable 핀을 HIGH->LOW로 토글하여 명령어 실행
    LCD_CTRL_PORT |= (1 << E_PIN);
    _delay_us(1); // Enable 펄스 폭 유지 시간
    LCD_CTRL_PORT &= ~(1 << E_PIN);
    
    _delay_ms(2); // 대부분의 명령어가 실행되기에 충분한 시간 대기
}

/**
 * @brief LCD에 한 글자의 데이터를 전송하는 함수
 * @param data 표시할 문자의 아스키 코드
 */
void lcd_data(unsigned char data) {
    LCD_DATA_PORT = data; // 데이터 포트에 문자 데이터 전송
    LCD_CTRL_PORT |= (1 << RS_PIN);  // RS = 1 (데이터 모드)
    LCD_CTRL_PORT &= ~(1 << RW_PIN); // RW = 0 (쓰기 모드)
    
    // Enable 핀을 HIGH->LOW로 토글하여 데이터 쓰기
    LCD_CTRL_PORT |= (1 << E_PIN);
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1 << E_PIN);
    
    _delay_us(50); // 데이터 쓰기 후 잠시 대기
}

/**
 * @brief LCD를 초기화하는 함수
 */
void lcd_init(void) {
    // 제어 핀과 데이터 핀을 모두 출력으로 설정
    LCD_CTRL_DDR |= (1 << RS_PIN) | (1 << RW_PIN) | (1 << E_PIN);
    LCD_DATA_DDR = 0xFF; // PORTB 전체를 출력으로
    
    _delay_ms(50); // 전원 인가 후 LCD가 안정화될 때까지 대기

    // --- LCD 초기화 시퀀스 ---
    lcd_command(0x38); // 기능 설정: 8비트 데이터, 2줄, 5x8 폰트
    _delay_ms(5);
    lcd_command(0x0C); // 화면 표시 설정: 화면 ON, 커서 OFF, 커서 깜빡임 OFF
    _delay_ms(5);
    lcd_command(0x01); // 화면 전체 지우기
    _delay_ms(5);
    lcd_command(0x06); // 입력 모드 설정: 주소 1씩 증가, 화면 이동 없음
    _delay_ms(5);
}

/**
 * @brief LCD에 문자열을 출력하는 함수
 * @param str 출력할 문자열
 */
void lcd_string(const char *str) {
    // 문자열의 끝(NULL)을 만날 때까지 한 글자씩 출력
    while (*str) {
        lcd_data(*str++);
    }
}

/**
 * @brief LCD 커서 위치를 이동시키는 함수
 * @param row 이동할 줄 (0 또는 1)
 * @param col 이동할 칸 (0-15)
 */
void lcd_goto_xy(unsigned char row, unsigned char col) {
    unsigned char address;
    
    // 줄(row)에 따라 시작 주소 설정
    if (row == 0) {
        address = 0x80; // 첫 번째 줄 시작 주소
    } else {
        address = 0xC0; // 두 번째 줄 시작 주소
    }
    
    // 시작 주소에 칸(col) 위치를 더하여 최종 주소 계산
    address += col;
    
    lcd_command(address); // DDRAM 주소 설정 명령어 전송
}

int main(void) {
    // LCD 초기화
    lcd_init();

    // 첫 번째 줄에 "Hello" 출력
    lcd_goto_xy(0, 0); // 0번째 줄, 0번째 칸으로 커서 이동
    lcd_string("Hello");

    // 두 번째 줄에 "ATmega128" 출력
    lcd_goto_xy(1, 0); // 1번째 줄, 0번째 칸으로 커서 이동
    lcd_string("ATmega128");

    // 프로그램이 종료되지 않도록 무한 루프 실행
    while (1) {
        // 특별한 동작 없이 화면 유지
    }
    
    return 0;
}
