/*
학생 5명의 성적을 처리하는 프로그램
과목은 3과목
출력 형태는

학생  영어  수학  과학  총점  평균  석차
A
B
C
D
E     -------------------------------------

조건 1) 영어 수학 과학 점수는 랜덤(1~100)
조건 2) 자동으로 성적이 채워진 후에 총점, 평균, 석차 계산하여 채울 것
조건 3) 위의 학생 이름 순으로 출력 된 결과를 석차 순으로 다시 정렬하여 값을 출력 할 것(선택정렬 알고리즘 사용)

학생  영어  수학  과학  총점  평균  석차
C		                      				1
B						                      2
E				                      		3
A						                      4
D    						                  5
*/

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define ROW 5
#define COL 7
#define SUM 4
#define AVG 5
#define RNK 6

int main(void)
{
	int arr[ROW][COL] = { 0 };
	int least;
	int tmp[7] = { 0 };

	srand((unsigned int)time(NULL));

	for (int i = 0; i < ROW; i++) {
		arr[i][0] = 'A' + i;
		arr[i][RNK] = 1;
		for (int j = 1; j < 4; j++) {
			arr[i][j] = rand() % 100 + 1;
			arr[i][SUM] += arr[i][j];
		}
		arr[i][AVG] = arr[i][SUM] / 3;
	}

	for (int i = 0; i < ROW; i++) {
		for (int j = 0; j < ROW; j++) {
			if (arr[i][SUM] < arr[j][SUM]) arr[i][RNK]++;
		}
	}

	printf("이름\t영어\t수학\t과학\t총점\t평균\t석차\n");
	for (int i = 0; i < ROW; i++) {
		printf("%c\t", arr[i][0]);
		for (int j = 1; j < COL; j++) {
			printf("%d\t", arr[i][j]);
		}
		printf("\n");
	}

	return 0;
}
