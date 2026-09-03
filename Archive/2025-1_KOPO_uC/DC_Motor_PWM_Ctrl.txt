#define F_CPU 16000000L
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// LCD 핀 정의
#define PORT_DATA		PORTA		// Data pin connection port
#define PORT_CONTROL	PORTB		// Control pin connection port
#define DDR_DATA		DDRA		// Data pin data direction
#define DDR_CONTROL		DDRB		// Control pin data direction

#define RS_PIN			0		// RS control pin bit number
#define RW_PIN			1		// R/W control pin bit number
#define E_PIN			2		// E control pin bit number

// LCD 명령어 정의
#define COMMAND_CLEAR_DISPLAY	0x01
#define COMMAND_8_BIT_MODE		0x38	// 8-bit, 2-line, 5x8 font
#define COMMAND_4_BIT_MODE		0x28	// 4-bit, 2-line, 5x8 font (not used in this 8-bit example)

#define COMMAND_DISPLAY_ON_OFF_BIT		2
#define COMMAND_CURSOR_ON_OFF_BIT		1
#define COMMAND_BLINK_ON_OFF_BIT		0

// DC Motor 및 스위치 핀 정의
#define DCM_CTRL		PORTC
#define DCM_CTRL_SET	DDRC
#define IN1_PIN			0		// [Out] DC Motor Direction Ctrl (PORTC.0)
#define IN2_PIN			1		// [Out] (IN1,IN2) -> (1,0)Fwd / (0,1)Rvs / (1,1)or(0,0)Fast Stop (PORTC.1)

// PWM 관련 정의 (ENA_PIN은 PWM 핀으로 대체)
#define PWM_PORT        PORTB       // PWM output port (OC1A is typically on PORTB)
#define PWM_DDR         DDRB        // PWM output data direction register
#define PWM_PIN         5           // OC1A pin for ATmega128 is PB5
#define PWM_TOP         255         // Max duty cycle value for 8-bit resolution (0-255)
#define PWM_RATE		27			// PWM RATE(%) (27~100%)

#define CW_PIN			4		// [In] Clock Wise SW (PORTC.4)
#define CCW_PIN			5		// [In] Counter Clock Wise SW (PORTC.5)

// 핀 제어 매크로
#define SET_PIN(port,pin)	(port|=(1<<pin))
#define CLR_PIN(port,pin)	(port&=~(1<<pin))

// 스위치 상태 확인 매크로 (내부 풀업 저항 사용 시)
// 스위치가 눌리면 해당 핀은 LOW가 됩니다.
#define IS_CW_PRESSED   (!(PINC & (1<<CW_PIN)))
#define IS_CCW_PRESSED  (!(PINC & (1<<CCW_PIN)))

// AVR 초기화 함수
void init_avr(void){
	// DC Motor 방향 제어 핀 (IN1, IN2)을 PORTC의 출력으로 설정
	SET_PIN(DCM_CTRL_SET,IN1_PIN);
	SET_PIN(DCM_CTRL_SET,IN2_PIN);

	// PWM 출력 핀 (ENA_PIN에 해당)을 PORTB의 출력으로 설정
	SET_PIN(PWM_DDR, PWM_PIN);

	// 스위치 핀을 입력으로 설정 (DDRC 해당 비트를 0으로)
	CLR_PIN(DCM_CTRL_SET,CW_PIN);
	CLR_PIN(DCM_CTRL_SET,CCW_PIN);

	// 스위치 핀의 내부 풀업 저항 활성화 (PORTC 해당 비트를 1로)
	SET_PIN(DCM_CTRL,CW_PIN);
	SET_PIN(DCM_CTRL,CCW_PIN);

	// Timer/Counter1 Fast PWM Mode 14 (TOP = ICR1) 설정
	// COM1A1: OC1A 핀을 비반전 모드로 설정 (비교 일치 시 클리어, BOTTOM 시 세트)
	TCCR1A |= (1 << COM1A1);
	// WGM11, WGM12, WGM13: Fast PWM 모드 14를 위한 설정
	TCCR1A |= (1 << WGM11);
	TCCR1B |= (1 << WGM12) | (1 << WGM13);

	// 프리 스케일러 설정 (예: 64)
	// F_PWM = F_CPU / (N * (1 + TOP))
	// F_PWM = 16MHz / (64 * (1 + 255)) = 16M / (64 * 256) = 16M / 16384 = 약 976.56 Hz
	TCCR1B |= (1 << CS11) | (1 << CS10); // Prescaler 64

	// PWM 주기(TOP 값) 설정
	ICR1 = PWM_TOP; // 8비트 해상도를 위해 255로 설정
	OCR1A = 0; // 초기 듀티 사이클 0 (모터 정지)
}

// 모터 속도 설정 함수 (0-PWM_TOP)
void set_motor_speed(uint8_t speed) {
	OCR1A = speed; // 듀티 사이클 설정
}

// 시계 방향 회전 함수
void Clock_Wise(void)
{
	set_motor_speed((uint8_t)(PWM_TOP*(PWM_RATE/100.0)));
	SET_PIN(DCM_CTRL,IN1_PIN);
	CLR_PIN(DCM_CTRL,IN2_PIN);
}

// 반시계 방향 회전 함수
void Counter_Clock_Wise(void)
{
	set_motor_speed((uint8_t)(PWM_TOP*(PWM_RATE/100.0)));
	CLR_PIN(DCM_CTRL,IN1_PIN);
	SET_PIN(DCM_CTRL,IN2_PIN);
}

// 모터 정지 함수
void Stop(void)
{
	set_motor_speed(0); // 속도를 0으로 설정하여 정지
	CLR_PIN(DCM_CTRL,IN1_PIN);
	CLR_PIN(DCM_CTRL,IN2_PIN);
}

// LCD E 핀 펄스 생성 함수
void LCD_pulse_enable(void)
{
	SET_PIN(PORT_CONTROL, E_PIN);
	_delay_us(1); // E 펄스 폭 유지
	CLR_PIN(PORT_CONTROL, E_PIN);
	_delay_ms(1); // 명령/데이터 처리 대기 시간 (LCD 데이터시트 참조)
}

// LCD에 데이터 쓰기 함수
void LCD_write_data(uint8_t data)
{
	SET_PIN(PORT_CONTROL, RS_PIN); // RS = 1 (데이터 모드)
	PORT_DATA = data;			// Output character data
	LCD_pulse_enable();			// Execute pulse
	_delay_ms(2);				// Data processing delay
}

// LCD에 명령어 쓰기 함수
void LCD_write_command(uint8_t command)
{
	CLR_PIN(PORT_CONTROL, RS_PIN); // RS = 0 (명령어 모드)
	PORT_DATA = command;		// Send command to data pins
	LCD_pulse_enable();			// Execute command
	_delay_ms(2);				// Command processing delay
}

void LCD_clear(void)
{
	LCD_write_command(COMMAND_CLEAR_DISPLAY);
	_delay_ms(2); 
}

void LCD_init(void)
{
	_delay_ms(50);				// Initial power-on delay for LCD
	
	DDR_DATA = 0xFF;
	PORT_DATA = 0x00;
	DDR_CONTROL |= (1 << RS_PIN) | (1 << RW_PIN) | (1 << E_PIN);

	// Set RW pin to LOW for write-only mode
	CLR_PIN(PORT_CONTROL, RW_PIN);
	_delay_ms(2);

	LCD_write_command(COMMAND_8_BIT_MODE);		// 8-bit mode, 2 lines, 5x8 font
	
	uint8_t command = 0x08 | (1 << COMMAND_DISPLAY_ON_OFF_BIT);
	LCD_write_command(command);

	LCD_clear();	
	LCD_write_command(0x06);
}

void LCD_write_string(char *string)
{
	uint8_t i;
	for(i = 0; string[i]; i++)	
		LCD_write_data(string[i]);
}

void LCD_goto_XY(uint8_t row, uint8_t col)
{
	col %= 16;
	row %= 2;

	uint8_t address = (0x40 * row) + col;
	uint8_t command = 0x80 + address;
	
	LCD_write_command(command);
}

int main(void)
{
	// 스위치 상태를 추적하기 위한 변수
	uint8_t cw_pressed_prev = 0;
	uint8_t ccw_pressed_prev = 0;

	init_avr(); // AVR 및 DC 모터 핀 초기화
	LCD_init(); // LCD 초기화

	LCD_goto_XY(0,0);
	LCD_write_string("Motor Control");
	_delay_ms(1000); 
	LCD_clear();

	while (1)
	{
		_delay_ms(50);

		uint8_t cw_current = IS_CW_PRESSED;
		uint8_t ccw_current = IS_CCW_PRESSED;

		// 상승 에지 감지
		if (cw_current && !cw_pressed_prev) {
			Clock_Wise();
			LCD_goto_XY(0,0);
			LCD_clear();
			LCD_write_string("Dir : CW");
		}
		else if (ccw_current && !ccw_pressed_prev) {
			Counter_Clock_Wise();
			LCD_goto_XY(0,0);
			LCD_clear();
			LCD_write_string("Dir : CCW");
		}
		else if (!cw_current && !ccw_current && (cw_pressed_prev || ccw_pressed_prev)) {
			Stop();
			LCD_goto_XY(0,0);
			LCD_clear();
			LCD_write_string("Stop");
		}
		// 스위치 상태 업데이트
		cw_pressed_prev = cw_current;
		ccw_pressed_prev = ccw_current;
	}
	return 0;
}
