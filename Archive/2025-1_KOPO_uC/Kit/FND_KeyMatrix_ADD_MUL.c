#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>

#define Key_data_input PINC
#define Key_data_output PORTC
#define ADD 11
#define MUL 10
#define CAL 15

char fCal = 0;

void Init_avr(){
	DDRA = 0xff; // FND
	DDRB = 0xff; // Q0 ~ Q3
	DDRC = 0x0f; // Key Matrix
}

void Disp_Fnd(int num){
	unsigned char fnd[]={0xc0,0xf9,0xa4,0xb0,0x99,0x92,0x82,0xd8,0x80,0x98};
	unsigned char add[]={0xff,0x88,0xa1,0xa1};
	unsigned char mul[]={0xff,0xd4,0xe3,0xcf};
	int dnum[4]={0};
	int pos;
	
	dnum[0] = num%10;
	dnum[1] = (num/10)%10;
	dnum[2] = (num/100)%10;
	dnum[3] = num/1000;
	
	for(pos=0; pos<4; pos++){
		PORTB = ~(0x01<<pos);
		if(num==ADD) PORTA = add[3-pos];
		else if(num==MUL) PORTA = mul[3-pos];
		else PORTA = fnd[dnum[pos]];
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
			case 0x70: if((12+i)==CAL) return 12+i;
		}
		keyout = (keyout<<1) + 0x01;
	}
	return -1;

}

int main(void)
{
	int tmp=-1, disp=0;
	int num1=0, num2=0, rlt=0;
	char op = '\0';
	
	Init_avr();
	
	while(1){
		tmp = KeyMatrix();
		if((tmp!=disp)&&(tmp!=-1)) {
			if(tmp==ADD) {
				num1 = disp;
				op = '+';
				disp = tmp;
			}
			if(tmp==MUL) {
				num1 = disp;
				op = '*';
				disp = tmp;
			}
			else if(tmp==CAL) {
				num2 = disp;
				disp = tmp;
				if(op=='+') rlt = num1+num2;
				else if(op=='*') rlt = num1*num2;				
				fCal=1;
				num1 = num2 = 0;
				op = '\0';
			}
			else {
				disp = tmp;
				fCal = 0;
			}
		}		
		if(fCal) Disp_Fnd(rlt);
		else Disp_Fnd(disp);
	}
	
	return 0;
}
