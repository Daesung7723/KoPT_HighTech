#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define SIZE 7
#define SUM 5
#define AVG 6

void prt(int* p) {
	for (int i = 0; i < SIZE; i++) 
		printf("%5d", *(p + i));
	printf("\n");
}

void rnd_data(int* p) {
	for (int i = 0; i < SIZE - 2; i++)
		*(p + i) = rand() % 100 + 1;
}

void cal_sum(int* p) {
	for (int i = 0; i < 5; i++)
		*(p + SUM) += *(p + i);
}

void cal_avg(int* p) {
	*(p + AVG) = *(p + SUM) / 5;
}

int main(void)
{
	int arr[SIZE] = { 0 };

	srand((unsigned int)time(NULL));

	rnd_data(arr);
	prt(arr);

	cal_sum(arr);
	prt(arr);

	cal_avg(arr);
	prt(arr);

	return 0;
}
