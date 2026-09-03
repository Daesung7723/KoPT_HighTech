#define F_CPU 16000000L
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define PORT_DATA		PORTA		// 데이터 핀 연결 포트
#define PORT_CONTROL	PORTB		// 제어 핀 연결 포트
#define DDR_DATA		DDRA		// 데이터 핀의 데이터 방향
#define DDR_CONTROL		DDRB		// 제어 핀의 데이터 방향

#define RS_PIN			0		// RS 제어 핀의 비트 번호
#define RW_PIN			1		// R/W 제어 핀의 비트 번호
#define E_PIN			2		// E 제어 핀의 비트 번호

#define COMMAND_CLEAR_DISPLAY	0x01
#define COMMAND_8_BIT_MODE		0x38	// 8비트, 2라인, 5x8 폰트
#define COMMAND_4_BIT_MODE		0x28	// 4비트, 2라인, 5x8 폰트

#define COMMAND_DISPLAY_ON_OFF_BIT		2
#define COMMAND_CURSOR_ON_OFF_BIT		1
#define COMMAND_BLINK_ON_OFF_BIT		0

#define DCM_CTRL		PORTC
#define DCM_CTRL_SET	DDRC
#define IN1_PIN			0		// [Out] DC Motor Direction Ctrl
#define IN2_PIN			1		// [Out] (IN1,IN2) -> (1,0)Fwd / (0,1)Rvs / (1,1)or(0,0)Fast Stop
#define ENA_PIN			2		// [Out] DC Motor run, speed ctrl
#define CW_PIN			4		// [In] Clock Wise SW
#define CCW_PIN			5		// [In] Counter Clock Wise SW

#define SW_STS	PINC&0x30
#define SW_CW	0x20
#define SW_CCW	0x10
#define SET_PIN(port,pin)	(port|=(1<<pin))
#define CLR_PIN(port,pin)	(port&=~(1<<pin))

void init_avr(void){	
	SET_PIN(DCM_CTRL_SET,IN1_PIN);
	SET_PIN(DCM_CTRL_SET,IN2_PIN);
	SET_PIN(DCM_CTRL_SET,ENA_PIN);
	CLR_PIN(DCM_CTRL_SET,CW_PIN);
	CLR_PIN(DCM_CTRL_SET,CCW_PIN);
}

void Clock_Wise(void)
{
	SET_PIN(DCM_CTRL,ENA_PIN);	
	SET_PIN(DCM_CTRL,IN1_PIN);
	CLR_PIN(DCM_CTRL,IN2_PIN);

}

void Counter_Clock_Wise(void)
{
	SET_PIN(DCM_CTRL,ENA_PIN);
	CLR_PIN(DCM_CTRL,IN1_PIN);
	SET_PIN(DCM_CTRL,IN2_PIN);
}

void Stop(void)
{
	CLR_PIN(DCM_CTRL,ENA_PIN);
	CLR_PIN(DCM_CTRL,IN1_PIN);
	CLR_PIN(DCM_CTRL,IN2_PIN);
}

void LCD_pulse_enable(void) 		// 하강 에지에서 동작
{
	SET_PIN(PORT_CONTROL, E_PIN);
	_delay_us(1);
	CLR_PIN(PORT_CONTROL, E_PIN);
	_delay_ms(1);
}

void LCD_write_data(uint8_t data)
{	
	SET_PIN(PORT_CONTROL, RS_PIN);
	PORT_DATA = data;			// 출력할 문자 데이터
	LCD_pulse_enable();			// 문자 출력
	_delay_ms(2);
}

void LCD_write_command(uint8_t command)
{
	CLR_PIN(PORT_CONTROL, RS_PIN);
	PORT_DATA = command;		// 데이터 핀에 명령어 전달
	LCD_pulse_enable();			// 명령어 실행
	_delay_ms(2);
}

void LCD_clear(void)
{
	LCD_write_command(COMMAND_CLEAR_DISPLAY);
	_delay_ms(2);
}

void LCD_init(void)
{
	_delay_ms(50);				// 초기 구동 시간
	
	// 연결 핀을 출력으로 설정
	DDR_DATA = 0xFF;
	PORT_DATA = 0x00;
	DDR_CONTROL |= (1 << RS_PIN) | (1 << RW_PIN) | (1 << E_PIN);

	// RW 핀으로 LOW를 출력하여 쓰기 전용으로 사용
	CLR_PIN(PORT_CONTROL, RW_PIN);
	_delay_ms(2);
	
	LCD_write_command(COMMAND_8_BIT_MODE);		// 8비트 모드
	
	// display on/off control
	// 화면 on, 커서 off, 커서 깜빡임 off
	uint8_t command = 0x08 | (1 << COMMAND_DISPLAY_ON_OFF_BIT);
	LCD_write_command(command);

	LCD_clear();				// 화면 지움

	// Entry Mode Set
	// 출력 후 커서를 오른쪽으로 옮김, 즉, DDRAM의 주소가 증가하며 화면 이동은 없음
	LCD_write_command(0x06);
}

void LCD_write_string(char *string)
{
	uint8_t i;
	for(i = 0; string[i]; i++)			// 종료 문자를 만날 때까지
	LCD_write_data(string[i]);		// 문자 단위 출력
}

void LCD_goto_XY(uint8_t row, uint8_t col)
{
	col %= 16;		// [0 15]
	row %= 2;		// [0 1]

	// 첫째 라인 시작 주소는 0x00, 둘째 라인 시작 주소는 0x40
	uint8_t address = (0x40 * row) + col;
	uint8_t command = 0x80 + address;
	
	LCD_write_command(command);	// 커서 이동
}

int main(void)
{
	unsigned char SW_Val, last_val=0;
	init_avr();
	LCD_init();

    while (1) 
    {
		SW_Val = SW_STS;
		_delay_ms(100);
		
		if(last_val != SW_Val)
		{
			last_val = SW_Val;
			switch(SW_Val){
				case SW_CW:		
					Clock_Wise();
					LCD_goto_XY(0,0);
					LCD_clear();
					LCD_write_string("Dir : CW");
					break;
				case SW_CCW:	
					Counter_Clock_Wise();
					LCD_goto_XY(0,0);
					LCD_clear();
					LCD_write_string("Dir : CCW");
					break;
				default:	
					Stop();
					LCD_goto_XY(0,0);
					LCD_clear();
					LCD_write_string("Stop");
					break;
			}
		}
    }
	return 0;
}

