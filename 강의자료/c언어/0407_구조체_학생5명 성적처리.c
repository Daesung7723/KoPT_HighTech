#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#define STU 5

struct stu {
	char name[20];
	int kor;
	int eng;
	int math;
	int rank;
	double avg;
};

void main() {
	struct stu arr[STU];

	for (int i = 0; i < STU; i++) {
		printf("[ %d 번째 학생 정보 입력 ]\n", i+1);
		printf("이름 : ");
		scanf("%s", arr[i].name);
		printf("국어 영어 수학 성적 입력 :");
		scanf("%d%d%d", &arr[i].kor, &arr[i].eng, &arr[i].math);

		arr[i].avg = (arr[i].kor + arr[i].eng + arr[i].math) / 3.0;
		arr[i].rank = 1;
	}

	for (int i = 0; i < STU; i++) {
		for (int j = 0; j < STU; j++) {
			if (arr[i].avg < arr[j].avg) arr[i].rank++;
		}
	}

	for (int i = 0; i < STU; i++) {
		printf("%s\t%d\t%d\t%d\t%lf\t%d\n", arr[i].name, arr[i].kor, arr[i].eng, arr[i].math, arr[i].avg, arr[i].rank);
	}
}
