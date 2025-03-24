#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int dice() {
	return rand() % 6 + 1;
}

void prt_sts(int P1, int P2, int c_dice, int turn) {
	(turn % 2 == 0) ? printf("P2차례 : %d\n", c_dice) : printf("P1차례 : %d\n", c_dice);
	printf("P1의 누적값: %d\n", P1);
	printf("P2의 누적값: %d\n", P2);
}

int winner(int P1, int P2) {
	if (P1 >= 100) {
		printf("P1이 승리하였습니다\n");
		return 1;
	}
	if (P2 >= 100) {
		printf("P2가 승리하였습니다\n");
		return 1;
	}
	return 0;
}

void main() {
	int d=0, t=0, key;
	int P1 = 0, P2 = 0;

	srand((unsigned int)time(NULL));

	while (1) {
		printf("주사위를 던지려면 0을 입력하세요.");
		scanf_s("%d", &key);
		if (key == 0) {
			t++;
			d = dice();
			if (t % 2 == 0) P2 += d;
			else P1 += d;
		}	
		
		prt_sts(P1, P2, d, t);
		if (winner(P1, P2)) break;
	}
}
