#include <stdio.h>
#include <windows.h>

int main() {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    int price, change;
    int w5000, w1000, w500, w100;

    printf("물건값을 입력하세요: ");
    scanf("%d", &price);

    if (price > 10000) {
        printf("물건값이 10000원을 초과합니다.\n");
        return 1;
    }

    change = 10000 - price;
    printf("거스름돈: %d원\n", change);

    w5000 = change / 5000; change %= 5000;
    w1000 = change / 1000; change %= 1000;
    w500  = change / 500;  change %= 500;
    w100  = change / 100;

    printf("5000원: %d개\n", w5000);
    printf("1000원: %d개\n", w1000);
    printf(" 500원: %d개\n", w500);
    printf(" 100원: %d개\n", w100);

    return 0;
}
