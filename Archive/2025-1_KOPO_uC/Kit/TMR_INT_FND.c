#define F_CPU 16000000L
#include <avr/io.h>
#include <avr/interrupt.h>
#define MS 0
#define SEC1 1
#define SEC10 2
#define MIN 3
#define BTNDLY 100

volatile int DigitSel = 0, run=0;
volatile int msCnt=0, runBtnDly=0, rstBtnDly=0;
unsigned char fnd[]={0xc0,0xf9,0xa4,0xb0,0x99,0x92,0x82,0xd8,0x80,0x98};
int Digits[4]={0};

void Init_INT0(void)
{
	EIMSK |= (1 << INT0);					// INT0 인터럽트 활성화
	EICRA |= (1 << ISC01);					// 하강 에지에서 인터럽트 발생
}

void Init_INT1(void)
{
	EIMSK |= (1 << INT1);					// INT1 인터럽트 활성화
	EICRA |= (1 << ISC11);					// 하강 에지에서 인터럽트 발생
}

void Init_TMR1(){							// Timer1 사용
	TCCR1B |= ((1 << CS11)|(1 << CS10));	// 분주비를 64으로 설정 - 4us(62.5ns*64)
	OCR1A = 250;							// 비교일치 값 설정(250*4us=1ms)
	TIMSK |= (1 << OCIE1A);					// 비교일치 A 인터럽트 허용
}

void Init_avr(){
	DDRA = 0xff; // FND
	DDRB = 0xff; // Q0 ~ Q3
	
	Init_INT0();
	Init_INT1();
	Init_TMR1();
}

ISR(INT0_vect)
{
	if(!runBtnDly) {
		run = (run + 1) % 2;			// run-stop toggle
		runBtnDly=BTNDLY;				// run Button Delay = 100ms
	}
}

ISR(INT1_vect)
{
	if(!rstBtnDly) {
		rstBtnDly=BTNDLY;				// reset Button Delay = 100ms
		run = 0;						// stop
		for(int i=0; i<4; i++)			// Initialize Digits 
			Digits[i]=0;
	}
}

ISR(TIMER1_COMPA_vect)						// 1ms마다 인터럽트 발생
{
	if(run) msCnt++;
	if(runBtnDly) runBtnDly--;
	if(rstBtnDly) rstBtnDly--;
	
	PORTB = ~(0x01<<DigitSel);				// FND Select
	PORTA = fnd[Digits[DigitSel]];			// FND Data
	if(DigitSel%2==1) PORTA &= 0x7f;		// 분, 초 구분 점
	DigitSel = (DigitSel+1)%4;				// FND Select Change(0~3)
	
	if(msCnt==100){							// 0.1sec
		Digits[MS]++;
		msCnt=0;
		
		if(Digits[MS]==10){					// 1sec
			Digits[MS]=0;
			Digits[SEC1]++;
			
			if(Digits[SEC1]==10){			// 10sec
				Digits[SEC1]=0;
				Digits[SEC10]++;
				
				if(Digits[SEC10]==6){		// 1Min
					Digits[SEC10]=0;
					Digits[MIN]++;
					
					if(Digits[MIN]==10)		// Max
						Digits[MIN]=0;
				}
			}
		}
	}
	
	TCNT1 = 0;
}

int main(void)
{
	Init_avr();
	sei();
	
	while(1){ }
	return 0;
}
