#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#define STU 5

typedef struct stu {
	char name[20];
	int kor;
	int eng;
	int math;
	int rank;
	double avg;
}StuData;

void cal_rank(StuData* dp) {
	for (int i = 0; i < STU; i++) {
		for (int j = 0; j < STU; j++) {
			if (dp[i].avg < dp[j].avg) dp[i].rank++;
		}
	}
}

void input_all(StuData* dp) {
	for (int i = 0; i < STU; i++) {
		printf("[ %d 번째 학생 정보 입력 ]\n", i + 1);
		printf("이름 : ");
		scanf("%s", dp[i].name);
		printf("국어 영어 수학 성적 입력 :");
		scanf("%d%d%d", &dp[i].kor, &dp[i].eng, &dp[i].math);

		dp[i].avg = (dp[i].kor + dp[i].eng + dp[i].math) / 3.0;
		dp[i].rank = 1;
	}
}

void prt_data(StuData* dp) {
	for (int i = 0; i < STU; i++) {
		printf("%s\t%d\t%d\t%d\t%lf\t%d\n", dp[i].name, dp[i].kor, dp[i].eng, dp[i].math, dp[i].avg, dp[i].rank);
	}
}

void save_data(StuData* dp) {
	FILE* fp = NULL;

	fp = fopen("StuData.bin", "wb");
	if (fp == NULL)
	{
		fprintf(stderr, "파일을열수없습니다.");
		return 1;
	}

	fwrite(dp, sizeof(StuData), STU, fp);

	fclose(fp);
}

void load_data(StuData* dp) {
	FILE* fp = NULL;

	fp = fopen("StuData.bin", "rb");
	if (fp == NULL)
	{
		fprintf(stderr, "binary.bin 파일을 열 수 없습니다.");
		return 1;
	}
	fread(dp, sizeof(StuData), STU, fp);

	fclose(fp);

}

int menu() {
	int m;
	do {
		printf("1. 입력하기\n");
		printf("2. 수정하기\n");
		printf("3. 출력하기\n");
		printf("4. 저장하기\n");
		printf("5. 불러오기\n");
		printf("6. 종료하기\n");
		printf("번호를 선택하세요 :");
		scanf("%d", &m);
	} while (m<1||m>6);

	return m;
}

void main() {
	StuData arr[STU];

	while (1) {
		switch (menu()) {
		case 1:
			input_all(arr);
			cal_rank(arr);
			break;
		case 2:
			break;
		case 3:
			prt_data(arr);
			break;
		case 4:
			save_data(arr);
			break;
		case 5:
			load_data(arr);
			break;
		case 6:
			return 0;
			break;
		}
	}	
}
