#include <stdio.h>

void main()
{
	int menu;
	int in1, in2, res=0;
	char op=0;

	while (1)
	{
		printf("계산기 프로그램입니다. 연산을 선택하세요.");
		printf("\n1. +\n2. -\n3. *\n4. /\n5. %\n6. Exit\n");
		scanf_s("%d", &menu);
		if (menu == 6) break;

		printf("연산하고자 하는 값을 순서대로 2개 입력하세요.");
		scanf_s("%d%d", &in1, &in2);

		switch (menu) {
		case 1:
			res = in1 + in2;
			op = '+';
			break;
		case 2:
			res = in1 - in2;
			op = '-';
			break;
		case 3:
			res = in1 * in2;
			op = '*';
			break;
		case 4:
			if (in2 == 0) {
				printf("나누는 값이 0이 될수 없습니다.");
				continue;
			}
			res = in1 / in2;
			op = '/';
			break;
		case 5:
			if (in2 == 0) {
				printf("나누는 값이 0이 될수 없습니다.");
				continue;
			}
			res = in1 % in2;
			op = '%';
			break;
		}
		printf("연산결과는 : \n %d %c %d = %d \n", in1, op, in2, res);
	}

}
