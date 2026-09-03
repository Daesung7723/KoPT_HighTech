/*
 * ============================================================
 *  ATmega128 DC 모터 PWM 속도 제어 + LCD 상태 표시 실습 코드
 * ============================================================
 *
 * [동작 개요]
 *  - ADC(가변저항)로 읽은 값을 PWM 듀티 사이클로 변환하여 DC 모터 속도를 제어한다.
 *  - CW(시계방향) / CCW(반시계방향) 스위치로 모터 방향을 전환한다.
 *  - 현재 방향(CW / CCW / Stop)과 듀티 비율(%)을 LCD 2줄에 실시간으로 표시한다.
 *
 * [하드웨어 연결 요약]
 *  - PORTA(8비트)     : LCD 데이터 버스 (D0~D7)
 *  - PORTB.0(RS)      : LCD Register Select 핀
 *  - PORTB.1(RW)      : LCD Read/Write 핀 (항상 Write 고정)
 *  - PORTB.2(E)       : LCD Enable 핀
 *  - PORTB.5(OC1A)    : Timer1 PWM 출력 → 모터 드라이버 ENA 핀
 *  - PORTC.0(IN1)     : 모터 드라이버 방향 제어 핀 A
 *  - PORTC.1(IN2)     : 모터 드라이버 방향 제어 핀 B
 *  - PORTC.4(CW SW)   : 시계방향 스위치 입력 (내부 풀업)
 *  - PORTC.5(CCW SW)  : 반시계방향 스위치 입력 (내부 풀업)
 *  - PORTF.0(ADC0)    : 가변저항 입력 (0~5V → ADC 0~1023)
 *                       ※ ATmega128의 ADC 전용 포트는 PORTF이다.
 *                         PORTA를 LCD 데이터 버스(출력)로 사용하므로
 *                         ADC 입력 핀을 PORTA와 분리해야 한다.
 *
 * [PWM 주파수 계산]
 *  F_PWM = F_CPU / (N × (1 + TOP))
 *        = 16,000,000 / (64 × 256)
 *        ≒ 976 Hz
 *
 * [모터 방향 제어 진리표 (L298N 기준)]
 *  IN1=1, IN2=0 → 시계 방향(CW)
 *  IN1=0, IN2=1 → 반시계 방향(CCW)
 *  IN1=0, IN2=0 → 정지(Stop / Free-wheeling)
 *  IN1=1, IN2=1 → 급정지(Fast Brake)
 * ============================================================
 */

/* F_CPU는 avr/io.h 및 util/delay.h 에서 사용하는 클록 주파수 정의.
   반드시 #include 이전에 선언해야 _delay_ms()가 올바르게 동작한다. */
#define F_CPU 16000000L

#include <avr/io.h>          // AVR 레지스터/비트 이름 정의 (DDRA, PORTA, TCCR1A 등)
#include <avr/interrupt.h>   // 인터럽트 관련 매크로 (현재 코드에서 직접 사용하지 않으나 확장 대비 포함)
#include <util/delay.h>      // _delay_ms(), _delay_us() 함수 제공
#include <stdio.h>           // sprintf() 함수 제공 (숫자 → 문자열 변환)

/* ============================================================
 *  [1] LCD 핀 및 명령어 정의
 * ============================================================ */

/* LCD 데이터/제어 핀이 연결된 포트를 매크로로 분리해 두면
   하드웨어를 변경할 때 이 부분만 수정하면 된다. */
#define PORT_DATA       PORTA   // LCD 데이터 핀(D0~D7)이 연결된 출력 포트
#define PORT_CONTROL    PORTB   // LCD 제어 핀(RS, RW, E)이 연결된 출력 포트
#define DDR_DATA        DDRA    // PORTA 데이터 방향 레지스터 (1=출력, 0=입력)
#define DDR_CONTROL     DDRB    // PORTB 데이터 방향 레지스터

/* LCD 제어 핀의 비트 번호 (PORTB 기준) */
#define RS_PIN  0   // Register Select: 0=명령(Command), 1=데이터(Data)
#define RW_PIN  1   // Read/Write: 0=쓰기(Write), 1=읽기(Read). 항상 Write만 사용하므로 0으로 고정
#define E_PIN   2   // Enable: 상승→하강 엣지에서 LCD가 데이터를 래치(latch)함

/* ── LCD 명령어 코드 (HD44780 호환 컨트롤러 기준) ─────────────────── */
#define LCD_CMD_CLEAR           0x01    // 화면 전체 지우기 (커서 홈 복귀 포함)
#define LCD_CMD_FUNCTION_SET    0x38    // Function Set: 8비트 모드, 2줄, 5×8 폰트
#define LCD_CMD_DISPLAY_BASE    0x08    // Display On/Off Control 명령의 기본값
#define LCD_CMD_ENTRY_MODE      0x06    // Entry Mode Set: 커서 우측 이동, 화면 이동 없음
#define LCD_CMD_DDRAM_BASE      0x80    // Set DDRAM Address 명령 (MSB=1 고정)

/* ── LCD DDRAM 행 시작 주소 ────────────────────────────────────────── */
#define LCD_LINE1_ADDR  0x00    // 1번째 줄 DDRAM 시작 주소
#define LCD_LINE2_ADDR  0x40    // 2번째 줄 DDRAM 시작 주소 (HD44780 규격)

/* ── LCD 표시 크기 ──────────────────────────────────────────────────── */
#define LCD_COLS    16      // LCD 1줄당 최대 표시 문자 수
#define LCD_ROWS     2      // LCD 총 줄 수

/* Display On/Off Control 명령의 개별 비트 번호 */
#define LCD_BIT_DISPLAY_ON  2   // D비트: 1이면 Display ON
#define LCD_BIT_CURSOR_ON   1   // C비트: 1이면 커서 표시
#define LCD_BIT_BLINK_ON    0   // B비트: 1이면 커서 깜빡임

/* ============================================================
 *  [2] DC 모터 및 스위치 핀 정의
 * ============================================================ */

/* 모터 드라이버(L298N 등) 방향 제어 핀이 연결된 포트 */
#define DCM_CTRL        PORTC   // 방향 제어 출력 포트
#define DCM_CTRL_SET    DDRC    // PORTC 데이터 방향 레지스터

#define IN1_PIN 0   // PORTC.0 → 모터 드라이버 IN1 핀 (방향 제어 A)
#define IN2_PIN 1   // PORTC.1 → 모터 드라이버 IN2 핀 (방향 제어 B)

/* ============================================================
 *  [3] PWM 관련 정의
 * ============================================================ */

/* OC1A 핀(PORTB.5)에서 Timer1의 PWM 신호가 출력된다.
   이 신호가 모터 드라이버의 ENA(Enable/Speed) 핀에 연결된다. */
#define PWM_DDR     DDRB    // PWM 출력 방향 레지스터
#define PWM_PIN     5       // OC1A = PORTB.5

/* ICR1(TOP) = 255로 설정 → 0~255의 8비트 해상도 PWM */
#define PWM_TOP     255

/* 듀티 사이클 최소/최대 비율(%).
   최솟값을 27%로 설정한 이유: 듀티 사이클이 너무 낮으면 모터가 기동 토크 부족으로 회전하지 않는다. */
#define PWM_MIN_RATE_PERCENTAGE  27   // 모터가 실제로 회전하기 시작하는 최소 듀티 비율
#define PWM_MAX_RATE_PERCENTAGE 100   // 최대 듀티 비율 (전압 100%)

/* 방향 전환 스위치 핀 번호 (PORTC 기준) */
#define CW_PIN  4   // PORTC.4 → 시계방향(CW) 스위치 입력
#define CCW_PIN 5   // PORTC.5 → 반시계방향(CCW) 스위치 입력

/* ============================================================
 *  [4] ADC 관련 정의
 * ============================================================
 *
 *  ※ 포트 충돌 방지 설계
 *  ATmega128의 ADC 입력 전용 포트는 PORTF(PF0~PF7)이다.
 *  PORTA는 LCD 데이터 버스로 전체 출력(DDR=0xFF)하므로
 *  ADC 채널을 반드시 PORTF로 배치해야 한다.
 *
 *  만약 PA0를 ADC 입력으로 사용하면:
 *   DDRA = 0xFF (LCD 초기화 시 설정) → PA0도 출력 모드가 됨
 *   → ADC가 자신이 출력하는 전압을 측정하는 오동작 발생
 *
 *  PORTF의 ADC 핀은 DDR 설정 없이 자동으로 아날로그 입력 모드가 된다.
 *  ADC0 채널 = PF0 핀 (ADMUX MUX[4:0] = 00000)
 */
#define ADC_CHANNEL     0       // 사용할 ADC 채널 번호 (0 = PF0)
#define ADC_MAX_VALUE   1023    // 10비트 ADC 최대값 (2^10 - 1)

/* ============================================================
 *  [5] 핀 제어 매크로 (비트 단위 SET / CLR)
 * ============================================================ */

/* 특정 포트의 특정 비트를 1로 설정 (비트 OR 연산) */
#define SET_PIN(port,pin)   (port |=  (1 << pin))

/* 특정 포트의 특정 비트를 0으로 클리어 (비트 AND + NOT 연산) */
#define CLR_PIN(port,pin)   (port &= ~(1 << pin))

/* ============================================================
 *  [6] 스위치 상태 확인 매크로
 * ============================================================ */

/* 내부 풀업 저항(PORTC에 1 출력 후 DDR을 입력으로 설정)을 사용하므로
   스위치가 눌리면 해당 핀이 GND와 연결되어 LOW(0)가 된다.
   → 비트를 NOT 처리하여 "눌림 = 1"이 되도록 논리를 반전시킨다. */
#define IS_CW_PRESSED   (!(PINC & (1 << CW_PIN)))   // CW 스위치 눌림 여부 (1=눌림)
#define IS_CCW_PRESSED  (!(PINC & (1 << CCW_PIN)))  // CCW 스위치 눌림 여부 (1=눌림)

/* ============================================================
 *  [7] 모터 상태 열거형 (enum)
 * ============================================================
 *
 *  uint8_t에 숫자(0/1/2)를 쓰는 대신 의미 있는 이름을 사용하면
 *  코드 가독성이 높아지고, 잘못된 값 대입을 컴파일러가 경고할 수 있다.
 *
 *  사용 예:
 *    MotorState state = MOTOR_STOP;
 *    if (state == MOTOR_CW) { ... }      // 숫자 1 대신 MOTOR_CW 사용
 */
typedef enum {
    MOTOR_STOP = 0,   // 정지 상태
    MOTOR_CW   = 1,   // 시계방향(Clock Wise) 회전
    MOTOR_CCW  = 2    // 반시계방향(Counter Clock Wise) 회전
} MotorState;

/* ============================================================
 *  [8] 함수 프로토타입 (선언)
 * ============================================================ */
void init_avr(void);
void set_motor_speed(uint8_t speed);
void Clock_Wise_Dir(void);
void Counter_Clock_Wise_Dir(void);
void Stop_Dir(void);
void LCD_pulse_enable(void);
void LCD_write_data(uint8_t data);
void LCD_write_command(uint8_t command);
void LCD_clear(void);
void LCD_init(void);
void LCD_write_string(char *string);
void LCD_goto_XY(uint8_t row, uint8_t col);

/* ============================================================
 *  [9] AVR 초기화 함수
 * ============================================================
 *
 *  이 함수에서 수행하는 작업:
 *   (a) DC 모터 방향 제어 핀 출력 설정
 *   (b) PWM 출력 핀 출력 설정
 *   (c) 스위치 핀 입력 + 내부 풀업 설정
 *   (d) Timer1 Fast PWM 모드 14 설정
 *   (e) ADC 초기화 (채널: PF0)
 */
void init_avr(void)
{
    /* (a) 모터 드라이버 방향 제어 핀(IN1, IN2)을 출력으로 설정 */
    SET_PIN(DCM_CTRL_SET, IN1_PIN);  // DDRC.0 = 1 → PORTC.0 출력
    SET_PIN(DCM_CTRL_SET, IN2_PIN);  // DDRC.1 = 1 → PORTC.1 출력

    /* (b) PWM 신호 출력 핀(OC1A = PORTB.5)을 출력으로 설정
           Timer1이 PWM을 출력하려면 반드시 해당 핀이 출력 모드여야 한다.
           LCD 제어 핀(RS/RW/E)도 PORTB를 공유하므로
           |= 연산으로 해당 비트만 설정한다 (다른 비트 보존). */
    SET_PIN(PWM_DDR, PWM_PIN);       // DDRB.5 = 1 → PORTB.5 출력

    /* (c-1) 스위치 핀(CW, CCW)을 입력으로 설정
             DDR 비트를 0으로 설정 → 입력 모드 */
    CLR_PIN(DCM_CTRL_SET, CW_PIN);   // DDRC.4 = 0 → PORTC.4 입력
    CLR_PIN(DCM_CTRL_SET, CCW_PIN);  // DDRC.5 = 0 → PORTC.5 입력

    /* (c-2) 입력 핀에 내부 풀업 저항 활성화
             입력 모드(DDR=0)인 상태에서 PORT 비트를 1로 쓰면 내부 풀업이 연결된다.
             → 스위치가 열릴 때 핀이 High로 유지되어 노이즈 오동작을 방지한다. */
    SET_PIN(DCM_CTRL, CW_PIN);       // PORTC.4 = 1 → 내부 풀업 ON
    SET_PIN(DCM_CTRL, CCW_PIN);      // PORTC.5 = 1 → 내부 풀업 ON

    /* --------------------------------------------------------
     *  (d) Timer/Counter1 Fast PWM 설정 (Mode 14: TOP = ICR1)
     * --------------------------------------------------------
     *
     *  Fast PWM 모드에서 카운터는 BOTTOM(0)부터 TOP(ICR1)까지 증가하고
     *  다시 BOTTOM으로 돌아온다(단방향 카운팅).
     *
     *  OC1A 핀 동작 (Non-Inverting):
     *   - 카운터 == BOTTOM  : OC1A = High (SET)
     *   - 카운터 == OCR1A   : OC1A = Low  (CLEAR)
     *  → OCR1A 값이 클수록 High 구간이 길어져 듀티 사이클이 높아진다.
     *
     *  TCCR1A 레지스터:
     *   COM1A1=1, COM1A0=0 → Non-inverting Fast PWM
     *   WGM11=1             → Fast PWM Mode 14의 일부
     *
     *  TCCR1B 레지스터:
     *   WGM12=1, WGM13=1   → Fast PWM Mode 14 완성
     *   CS11=1, CS10=1      → Prescaler 64 선택
     */
    TCCR1A |= (1 << COM1A1);                     // Non-inverting 모드: OC1A 비반전 PWM
    TCCR1A |= (1 << WGM11);                      // Fast PWM Mode 14 (WGM11 비트)
    TCCR1B |= (1 << WGM12) | (1 << WGM13);       // Fast PWM Mode 14 (WGM12, WGM13 비트)

    /* 프리스케일러 64 선택 (CS11=1, CS10=1)
     *  PWM 주파수 = 16MHz / (64 × (255+1)) = 976.56 Hz ≒ 1kHz */
    TCCR1B |= (1 << CS11) | (1 << CS10);

    /* TOP 값 설정: ICR1 = PWM_TOP(255) → 8비트 해상도 (0~255 단계로 속도 조절) */
    ICR1  = PWM_TOP;
    /* 초기 듀티 사이클 0 → 모터 정지 상태에서 시작 */
    OCR1A = 0;

    /* --------------------------------------------------------
     *  (e) ADC 초기화 (입력 채널: PORTF.0 = PF0 = ADC0)
     * --------------------------------------------------------
     *
     *  ADMUX 레지스터:
     *   REFS1=0, REFS0=1 → AVCC(5V)를 기준 전압으로 사용
     *   MUX4~MUX0 = ADC_CHANNEL(0b00000) → ADC0 채널(PF0) 선택
     *
     *  ADCSRA 레지스터:
     *   ADEN  = 1 → ADC 활성화 (이 비트를 1로 설정해야 변환이 가능)
     *   ADPS2, ADPS1, ADPS0 = 111 → 프리스케일러 128
     *   ADC 클록 = 16MHz / 128 = 125kHz (권장 범위: 50kHz~200kHz)
     */
    ADMUX  = (1 << REFS0) | ADC_CHANNEL;                               // AVCC 기준, PF0 채널
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); // ADC ON, Prescaler 128
}

/* ============================================================
 *  [10] 모터 속도 설정 함수
 * ============================================================
 *
 *  매개변수 speed: 0 ~ PWM_TOP(255) 범위의 듀티 사이클 값
 *   - 0   → 듀티 0%  → 모터 정지
 *   - 255 → 듀티 100% → 최대 속도
 *
 *  OCR1A에 값을 쓰면 다음 PWM 주기부터 즉시 반영된다.
 */
void set_motor_speed(uint8_t speed)
{
    OCR1A = speed;  // 비교 매치 레지스터에 듀티 사이클 값 저장
}

/* ============================================================
 *  [11] 모터 방향 제어 함수
 * ============================================================
 *
 *  L298N 등의 모터 드라이버는 IN1/IN2 핀의 조합으로 방향을 결정한다.
 *  속도는 별도로 PWM(set_motor_speed)으로 제어한다.
 */

/* 시계 방향(CW): IN1=HIGH, IN2=LOW */
void Clock_Wise_Dir(void)
{
    SET_PIN(DCM_CTRL, IN1_PIN);  // PORTC.0 = 1
    CLR_PIN(DCM_CTRL, IN2_PIN);  // PORTC.1 = 0
}

/* 반시계 방향(CCW): IN1=LOW, IN2=HIGH */
void Counter_Clock_Wise_Dir(void)
{
    CLR_PIN(DCM_CTRL, IN1_PIN);  // PORTC.0 = 0
    SET_PIN(DCM_CTRL, IN2_PIN);  // PORTC.1 = 1
}

/* 정지(Free-wheeling Stop): IN1=LOW, IN2=LOW
   모터에 걸리는 전압이 없어 관성으로 서서히 멈춘다.
   (IN1=IN2=HIGH이면 급제동(Fast Brake)이 된다.) */
void Stop_Dir(void)
{
    CLR_PIN(DCM_CTRL, IN1_PIN);  // PORTC.0 = 0
    CLR_PIN(DCM_CTRL, IN2_PIN);  // PORTC.1 = 0
}

/* ============================================================
 *  [12] LCD 관련 함수
 * ============================================================ */

/*
 * LCD E(Enable) 핀 펄스 생성 함수
 * --------------------------------
 *  HD44780 컨트롤러는 E 핀의 하강 엣지(High→Low)에서
 *  데이터 버스의 값을 내부로 래치(latch)한다.
 *  따라서 E를 High로 올린 뒤 최소 1μs를 유지하고
 *  다시 Low로 내려야 LCD가 데이터를 인식한다.
 */
void LCD_pulse_enable(void)
{
    SET_PIN(PORT_CONTROL, E_PIN);  // E = High (데이터 유효 구간 시작)
    _delay_us(1);                  // E High 최소 유지 시간 (HD44780: 최소 230ns)
    CLR_PIN(PORT_CONTROL, E_PIN);  // E = Low  (하강 엣지 → 데이터 래치)
    _delay_ms(1);                  // LCD 내부 처리 대기 (명령 실행 시간)
}

/*
 * LCD 데이터(문자) 쓰기 함수
 * ---------------------------
 *  RS = 1로 설정하여 LCD가 수신 데이터를 문자 코드(ASCII)로 해석하게 한다.
 *  data: 출력할 문자의 ASCII 코드 (예: 'A' = 0x41)
 */
void LCD_write_data(uint8_t data)
{
    SET_PIN(PORT_CONTROL, RS_PIN);  // RS = 1 → 데이터 모드
    PORT_DATA = data;               // 데이터 버스(PORTA)에 ASCII 값 출력
    LCD_pulse_enable();             // E 펄스로 데이터 래치
    _delay_ms(2);                   // 문자 처리 대기 시간
}

/*
 * LCD 명령어 쓰기 함수
 * ---------------------
 *  RS = 0으로 설정하여 LCD가 수신 데이터를 제어 명령으로 해석하게 한다.
 *  command: 실행할 LCD 명령 코드
 */
void LCD_write_command(uint8_t command)
{
    CLR_PIN(PORT_CONTROL, RS_PIN);  // RS = 0 → 명령 모드
    PORT_DATA = command;            // 데이터 버스에 명령 코드 출력
    LCD_pulse_enable();             // E 펄스로 명령 래치
    _delay_ms(2);                   // 명령 처리 대기 시간
}

/*
 * LCD 화면 지우기 함수
 * ---------------------
 *  LCD_CMD_CLEAR(0x01) 명령은 DDRAM 전체를 공백(0x20)으로 초기화하고
 *  커서를 (0,0) 위치로 자동 복귀시킨다.
 *  이 명령은 수행 시간이 최대 1.52ms로 다른 명령보다 길다.
 */
void LCD_clear(void)
{
    LCD_write_command(LCD_CMD_CLEAR);
    _delay_ms(2);  // Clear Display 명령의 추가 실행 시간 대기
}

/*
 * LCD 초기화 함수 (전원 투입 후 1회 호출)
 * -----------------------------------------
 *  HD44780 데이터시트의 초기화 시퀀스를 따른다:
 *   1. 전원 안정 대기 (50ms 이상)
 *   2. Function Set: 8비트 모드, 2줄, 5×8 폰트
 *   3. Display ON/OFF Control: Display ON, Cursor OFF, Blink OFF
 *   4. Display Clear
 *   5. Entry Mode Set: 커서 오른쪽 이동, 화면 이동 없음
 */
void LCD_init(void)
{
    _delay_ms(50);   // LCD 전원 안정화 대기 (데이터시트 요구: 40ms 이상)

    /* 데이터 핀(PORTA) 전체를 출력으로 설정하고 초기값 0 출력 */
    DDR_DATA  = 0xFF;   // DDRA = 0xFF → PORTA 전체 출력 모드
    PORT_DATA = 0x00;   // PORTA = 0x00 → 데이터 버스 초기화

    /* 제어 핀(RS, RW, E)을 출력으로 설정
       |= 연산을 사용하여 PORTB의 PWM 핀(PB5) 설정을 보존한다. */
    DDR_CONTROL |= (1 << RS_PIN) | (1 << RW_PIN) | (1 << E_PIN);

    /* RW = 0으로 고정 → 항상 LCD에 쓰기(Write) 전용으로 사용
       LCD 읽기(Busy Flag 확인)는 이 코드에서 사용하지 않는다. */
    CLR_PIN(PORT_CONTROL, RW_PIN);
    _delay_ms(2);

    /* Function Set: 8비트 데이터 버스, 2줄 표시, 5×8 도트 폰트 */
    LCD_write_command(LCD_CMD_FUNCTION_SET);    // 0x38

    /* Display ON/OFF Control
     *  LCD_CMD_DISPLAY_BASE(0x08) | (1 << LCD_BIT_DISPLAY_ON) = 0x0C
     *  → Display ON, Cursor OFF, Blink OFF */
    LCD_write_command(LCD_CMD_DISPLAY_BASE | (1 << LCD_BIT_DISPLAY_ON)); // 0x0C

    LCD_clear();  // 화면 초기화 (잔여 데이터 제거)

    /* Entry Mode Set: LCD_CMD_ENTRY_MODE(0x06)
     *  I/D = 1 → 문자 출력 후 커서 오른쪽으로 자동 이동
     *  S   = 0 → 화면 이동 없음 */
    LCD_write_command(LCD_CMD_ENTRY_MODE);      // 0x06
}

/*
 * LCD 문자열 출력 함수
 * ----------------------
 *  string: 출력할 문자열 포인터 (null 종단 문자열)
 *  현재 커서 위치부터 문자열 끝('\0')까지 한 글자씩 출력한다.
 */
void LCD_write_string(char *string)
{
    uint8_t i;
    for (i = 0; string[i]; i++)     // '\0'을 만날 때까지 반복
        LCD_write_data(string[i]);  // 한 글자씩 LCD에 출력
}

/*
 * LCD 커서 위치 이동 함수
 * ------------------------
 *  row: 줄 번호 (0 = 1번째 줄, 1 = 2번째 줄)
 *  col: 열 번호 (0 ~ LCD_COLS-1)
 *
 *  HD44780 DDRAM 주소 구조:
 *   1번째 줄: LCD_LINE1_ADDR(0x00) ~ 0x0F
 *   2번째 줄: LCD_LINE2_ADDR(0x40) ~ 0x4F
 *
 *  Set DDRAM Address 명령: LCD_CMD_DDRAM_BASE(0x80) | address
 */
void LCD_goto_XY(uint8_t row, uint8_t col)
{
    col %= LCD_COLS;    // 열 범위 제한: 0 ~ LCD_COLS-1
    row %= LCD_ROWS;    // 행 범위 제한: 0 ~ LCD_ROWS-1

    /* 행에 따라 DDRAM 시작 주소 결정
     *  row=0: LCD_LINE1_ADDR(0x00) + col
     *  row=1: LCD_LINE2_ADDR(0x40) + col */
    uint8_t address = (row == 0 ? LCD_LINE1_ADDR : LCD_LINE2_ADDR) + col;

    LCD_write_command(LCD_CMD_DDRAM_BASE | address);  // Set DDRAM Address 명령 전송
}

/* ============================================================
 *  [13] main 함수 - 메인 제어 루프
 * ============================================================ */
int main(void)
{
    /* --------------------------------------------------------
     *  변수 선언 및 초기화
     * -------------------------------------------------------- */

    /* 스위치의 이전 상태 저장 변수 (엣지(edge) 검출에 사용)
       0 = 이전에 눌리지 않음, 1 = 이전에 눌림 */
    uint8_t cw_pressed_prev  = 0;
    uint8_t ccw_pressed_prev = 0;

    /* 현재 모터 동작 상태 (MotorState 열거형 사용)
     *  숫자 대신 MOTOR_STOP / MOTOR_CW / MOTOR_CCW 로 표현하여
     *  코드 의미를 명확하게 한다. */
    MotorState current_motor_state = MOTOR_STOP;

    /* --------------------------------------------------------
     *  PWM 듀티 사이클 범위 계산 (컴파일 타임에 확정)
     *
     *  min_duty_cycle  = 255 × 27%  =  68  (모터 기동 최소 전압)
     *  max_duty_cycle  = 255 × 100% = 255  (최대 전압)
     *  duty_cycle_range = 255 - 68  = 187  (ADC 값이 매핑되는 범위)
     *
     *  ADC    0 → motor_speed =  68 (최저속)
     *  ADC 1023 → motor_speed = 255 (최고속)
     * -------------------------------------------------------- */
    const uint8_t min_duty_cycle   = (uint8_t)((PWM_TOP * PWM_MIN_RATE_PERCENTAGE) / 100.0);
    const uint8_t max_duty_cycle   = (uint8_t)((PWM_TOP * PWM_MAX_RATE_PERCENTAGE) / 100.0);
    const uint8_t duty_cycle_range = max_duty_cycle - min_duty_cycle;

    char    lcd_str_buffer[LCD_COLS + 1]; // sprintf() 결과 버퍼 (+1은 null 종단 문자용)
    uint8_t prev_adc_percentage = 0;      // LCD 갱신 최적화: 이전에 표시한 ADC 퍼센트 값

    /* --------------------------------------------------------
     *  시스템 초기화
     * -------------------------------------------------------- */
    init_avr();   // AVR 주변장치 초기화 (포트, Timer1, ADC)
    LCD_init();   // LCD 초기화 (HD44780 초기화 시퀀스 실행)

    /* 시작 메시지 표시 후 1초 대기
     *  LCD_clear()가 커서를 (0,0)으로 자동 복귀시키므로
     *  LCD_goto_XY(0,0)은 생략 가능하나, 의도를 명확히 하기 위해 유지한다. */
    LCD_goto_XY(0, 0);
    LCD_write_string("Motor Control");
    _delay_ms(1000);
    LCD_clear();

    /* ============================================================
     *  메인 루프 (무한 반복)
     * ============================================================
     *
     *  매 루프마다 수행하는 작업:
     *   [Step 1] 스위치 채터링(debouncing) 지연
     *   [Step 2] 스위치 현재 상태 읽기
     *   [Step 3] ADC 변환 실행 및 결과 읽기
     *   [Step 4] ADC 값 → 퍼센트 및 PWM 듀티 사이클 변환
     *   [Step 5] 스위치 엣지 검출 → 모터 방향 변경 + LCD 1행 갱신
     *   [Step 6] 모터 속도 설정 (PWM OCR1A 갱신)
     *   [Step 7] ADC 퍼센트 변경 시 LCD 2행 갱신
     *   [Step 8] 이전 스위치 상태 업데이트
     */
    while (1)
    {
        /* [Step 1] 채터링 방지 지연
         *  기계식 스위치는 눌리는 순간 수ms 동안 접점이 불안정하게
         *  ON/OFF를 반복한다(바운싱). 50ms 대기로 신호가 안정될 때까지 기다린다. */
        _delay_ms(50);

        /* [Step 2] 스위치 현재 상태 읽기
         *  IS_CW_PRESSED 매크로: PINC의 CW_PIN 비트를 읽어 반전 → 눌리면 1 반환 */
        uint8_t cw_current  = IS_CW_PRESSED;
        uint8_t ccw_current = IS_CCW_PRESSED;

        /* [Step 3] ADC 변환 실행
         *  ADSC(ADC Start Conversion) 비트를 1로 쓰면 변환 시작.
         *  변환이 완료되면 하드웨어가 ADSC를 자동으로 0으로 클리어한다.
         *  폴링(polling) 방식으로 완료를 대기한다. */
        ADCSRA |= (1 << ADSC);           // ADC 변환 시작
        while (ADCSRA & (1 << ADSC));    // ADSC = 0이 될 때까지 대기 (변환 완료 폴링)

        /* ADC 결과 읽기
         *  ADC 레지스터(ADCL + ADCH, 10비트)를 16비트 변수로 읽는다.
         *  값 범위: 0 (0V) ~ ADC_MAX_VALUE(1023, AVCC=5V) */
        uint16_t adc_raw_value = ADC;

        /* [Step 4-A] ADC 원시값 → 퍼센트 변환
         *  (adc_raw_value / ADC_MAX_VALUE) × 100 + 0.5 (반올림)
         *  결과: 0% ~ 100%  → LCD 2행에 현재 속도 비율로 표시 */
        uint8_t adc_percentage = (uint8_t)(((float)adc_raw_value / ADC_MAX_VALUE) * 100.0 + 0.5);
        if (adc_percentage > 100) adc_percentage = 100;  // 부동소수점 오차 보정

        /* [Step 4-B] ADC 원시값 → PWM 듀티 사이클 값 변환
         *  선형 매핑: adc_raw_value(0~ADC_MAX_VALUE) → motor_speed(min_duty~max_duty)
         *  공식: motor_speed = (adc / ADC_MAX_VALUE) × range + min
         *  → 가변저항을 0으로 돌려도 최소 27% 듀티를 유지하여 모터가 회전 가능 */
        uint8_t motor_speed = (uint8_t)(((float)adc_raw_value / ADC_MAX_VALUE) * duty_cycle_range + min_duty_cycle);

        /* --------------------------------------------------------
         *  [Step 5] 스위치 엣지(edge) 검출 및 모터 방향 제어
         *
         *  상승 엣지(Rising Edge) 검출:
         *   현재 = 눌림(1), 이전 = 안 눌림(0) → 이 순간에만 방향 전환 처리
         *   → 버튼을 계속 누르고 있어도 방향 전환이 한 번만 발생
         *
         *  하강 엣지(Falling Edge) 검출:
         *   두 버튼 모두 현재 = 안 눌림, 이전 = 적어도 하나 눌림 → 정지 처리
         * -------------------------------------------------------- */

        if (cw_current && !cw_pressed_prev) {
            /* CW 버튼 상승 엣지: MOTOR_CW 상태로 전환 */
            if (current_motor_state != MOTOR_CW) {
                Clock_Wise_Dir();                    // IN1=1, IN2=0 → CW 방향 설정
                current_motor_state = MOTOR_CW;      // 상태 변수 업데이트
                LCD_clear();
                LCD_write_string("Direction : CW");  // LCD 1행에 방향 표시
            }

        } else if (ccw_current && !ccw_pressed_prev) {
            /* CCW 버튼 상승 엣지: MOTOR_CCW 상태로 전환 */
            if (current_motor_state != MOTOR_CCW) {
                Counter_Clock_Wise_Dir();             // IN1=0, IN2=1 → CCW 방향 설정
                current_motor_state = MOTOR_CCW;
                LCD_clear();
                LCD_write_string("Direction : CCW"); // LCD 1행에 방향 표시
            }

        } else if (!cw_current && !ccw_current && (cw_pressed_prev || ccw_pressed_prev)) {
            /* 두 버튼 모두 하강 엣지: MOTOR_STOP 상태로 전환 */
            if (current_motor_state != MOTOR_STOP) {
                Stop_Dir();                           // IN1=0, IN2=0 → Free-wheeling 정지
                current_motor_state = MOTOR_STOP;
                LCD_clear();
                LCD_write_string("Stop");             // LCD 1행에 정지 표시
            }
        }

        /* [Step 6] 모터 속도 설정
         *  MOTOR_STOP: OCR1A = 0 → PWM 듀티 0% (전압 인가 없음)
         *  MOTOR_CW / MOTOR_CCW: OCR1A = motor_speed → ADC 값에 따른 속도 */
        if (current_motor_state == MOTOR_STOP) {
            set_motor_speed(0);           // 정지 중: 듀티 0%로 강제 설정
        } else {
            set_motor_speed(motor_speed); // 회전 중: 가변저항 ADC 값으로 속도 제어
        }

        /* [Step 7] LCD 2행 ADC 퍼센트 표시 (값이 변경된 경우에만 갱신)
         *  매 루프마다 LCD를 쓰면 LCD 처리 시간으로 인한 딜레이가 누적되어
         *  스위치 반응이 느려질 수 있다. 이전 값과 다를 때만 LCD를 갱신한다. */
        if (adc_percentage != prev_adc_percentage) {
            LCD_goto_XY(1, 0);  // 커서를 2번째 줄(row=1), 첫 번째 열(col=0)로 이동

            /* sprintf로 "Duty Rate : XX%" 형식의 문자열 생성
             *  %3d: 최대 3자리 정수 (오른쪽 정렬, 예: " 27", "100")
             *  %%: '%' 문자 리터럴 출력
             *  전체 포맷 "Duty Rate :%3d%%" → 최대 15자 → LCD_COLS(16) 이내 */
            sprintf(lcd_str_buffer, "Duty Rate :%3d%%", adc_percentage);
            LCD_write_string(lcd_str_buffer);

            /* 나머지 1열(16번째 칸)을 공백으로 덮어 잔상 제거
             *  "Duty Rate : 27%" = 15자이므로 마지막 1칸만 공백 처리하면 충분하다. */
            LCD_write_data(' ');

            prev_adc_percentage = adc_percentage;  // 이전 퍼센트 값 업데이트
        }

        /* [Step 8] 다음 루프를 위해 현재 스위치 상태를 이전 상태로 저장
         *  엣지 검출에 필요한 "이전 루프의 스위치 상태" 갱신 */
        cw_pressed_prev  = cw_current;
        ccw_pressed_prev = ccw_current;

    } // while(1) 끝

    return 0;  // ATmega128에서는 실제로 도달하지 않지만 컴파일러 경고 방지용
}