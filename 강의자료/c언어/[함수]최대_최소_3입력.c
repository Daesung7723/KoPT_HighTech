#include <stdio.h>

int Max(int x, int y, int z)
{
	if (x > y) {
		if (x > z) return x;
		else return z;
	}
	else {
		if (y > z) return y;
		else return z;
	}
}

int Min(int x, int y, int z)
{
	if (x < y) {
		if (x < z) return x;
		else return z;
	}
	else {
		if (y < z) return y;
		else return z;
	}
}

int Menu()
{
	int menu;

	do {
		printf("메뉴를 선택하세요\n");
		printf("1. 최대값\n");
		printf("2. 최소값\n");
		printf("3. 종료\n");
		scanf_s("%d", &menu);
	} while ((menu < 1) || (menu > 3));

	return menu;
}

void main()
{
	int m;
	int in1, in2, in3;

	while (1) {
		m = Menu();
		if (m == 3) break;

		printf("정수값 3개를 입력하세요 :");
		scanf_s("%d%d%d", &in1, &in2, &in3);

		if (m == 1) printf("최대값은 %d 입니다.\n", Max(in1, in2, in3));
		else if(m==2) printf("최소값은 %d 입니다.\n", Min(in1, in2, in3));
	}
}
