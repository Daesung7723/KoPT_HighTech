#define F_CPU 16000000L
#include <avr/io.h>
#include <util/delay.h>

#define Key_data_input PINC
#define Key_data_output PORTC

#define ADD 11
#define CAL 15

char fCal = 0;

void Init_avr(){
	DDRA = 0xff; // FND
	DDRB = 0xff; // Q0 ~ Q3
	DDRC = 0x0f; // Key Matrix
}

void Disp_Fnd(int num){
	unsigned char fnd[]={0xc0,0xf9,0xa4,0xb0,0x99,0x92,0x82,0xd8,0x80,0x98};
	int dnum[4]={0};
	int pos;
	
	dnum[0] = num%10;
	dnum[1] = (num/10)%10;
	dnum[2] = (num/100)%10;
	dnum[3] = num/1000;
	
	for(pos=0; pos<4; pos++){
		PORTB = ~(0x01<<pos);
		PORTA = fnd[dnum[pos]];
		_delay_us(50);
	}
}

int KeyMatrix()
{
	int keyout = 0xfe;
	int i;
	for( i = 0; i <= 3; i++ )
	{
		Key_data_output = keyout;
		_delay_us(1);
		switch(Key_data_input & 0xf0)
		{
			case 0xe0: return 0+i;
			case 0xd0: return 4+i ;
			case 0xb0: return 8+i;
			case 0x70: return 12+i;
		}
		keyout = (keyout<<1) + 0x01;
	}
	return -1;

}

void UART1_init(void)
{
	UBRR1H = 0x00;			// 9600 보율로 설정
	UBRR1L = 207;
	
	UCSR1A |= _BV(U2X1);			// 2배속 모드
	// 비동기, 8비트 데이터, 패리티 없음, 1비트 정지 비트 모드
	UCSR1C |= 0x06;
	
	UCSR1B |= _BV(RXEN1);		// 송수신 가능
	UCSR1B |= _BV(TXEN1);
}

void UART1_transmit(char data)
{
	while( !(UCSR1A & (1 << UDRE1)) );	// 송신 가능 대기
	UDR1 = data;				// 데이터 전송
}

unsigned char UART1_receive(void)
{
	while( !(UCSR1A & (1<<RXC1)) );	// 데이터 수신 대기
	return UDR1;
}

void UART1_print_string(char *str)	// 문자열 송신
{
	for(int i = 0; str[i]; i++)			// ‘\0’ 문자를 만날 때까지 반복
	UART1_transmit(str[i]);			// 바이트 단위 출력
}

void UART1_print_1_byte_number(uint8_t n)
{
	char numString[4] = "0";
	int i, index = 0;
	
	if(n > 0){					// 문자열 변환
		for(i = 0; n != 0 ; i++)
		{
			numString[i] = n % 10 + '0';
			n = n / 10;
		}
		numString[i] = '\0';
		index = i - 1;
	}
	
	for(i = index; i >= 0; i--)		// 변환된 문자열을 역순으로 출력
	UART1_transmit(numString[i]);
}

int main(void)
{
	int cnt=0, dly=0;
	int tmp=-1, key=-1;
	Init_avr();
	UART1_init();				// UART1 초기화
	
	while(1)
	{
		tmp = KeyMatrix();
		if((tmp!=key)&&(tmp!=-1)) {
			if((tmp>=0)&&(tmp<3)) key = tmp;
		}
		
		if(dly%2000==0) {
			if(key==0) {
				cnt++;
				UART1_print_string("[Up]");
			}
			else if(key==1){
				UART1_print_string("[Stop]");
			}
			else if(key==2){
				cnt--;
				UART1_print_string("[Down]");
			}
			UART1_print_1_byte_number(cnt);
			UART1_print_string("\n");
		}
		Disp_Fnd(cnt);
		dly++;
		_delay_us(1);
	}
	
	return 0;
}
