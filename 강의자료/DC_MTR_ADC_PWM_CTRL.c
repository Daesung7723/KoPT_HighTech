#define F_CPU 16000000L
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

// LCD pin definitions
#define PORT_DATA		PORTA		// Data pin connection port
#define PORT_CONTROL	PORTB		// Control pin connection port
#define DDR_DATA		DDRA		// Data pin data direction
#define DDR_CONTROL		DDRB		// Control pin data direction

#define RS_PIN			0		// RS control pin bit number
#define RW_PIN			1		// R/W control pin bit number
#define E_PIN			2		// E control pin bit number

// LCD command definitions
#define COMMAND_CLEAR_DISPLAY	0x01
#define COMMAND_8_BIT_MODE		0x38	// 8-bit, 2-line, 5x8 font

#define COMMAND_DISPLAY_ON_OFF_BIT		2
#define COMMAND_CURSOR_ON_OFF_BIT		1
#define COMMAND_BLINK_ON_OFF_BIT		0

// DC Motor and switch pin definitions
#define DCM_CTRL		PORTC
#define DCM_CTRL_SET	DDRC
#define IN1_PIN			0			// [Out] DC Motor Direction Ctrl (PORTC.0)
#define IN2_PIN			1			// [Out] (IN1,IN2) -> (1,0)Fwd / (0,1)Rvs / (1,1)or(0,0)Fast Stop (PORTC.1)

// PWM related definitions
#define PWM_PORT        PORTB       // PWM output port (OC1A is typically on PORTB)
#define PWM_DDR         DDRB        // PWM output data direction register
#define PWM_PIN         5           // OC1A pin for ATmega128 is PB5
#define PWM_TOP         255         // Max duty cycle value for 8-bit resolution (0-255)
#define PWM_MIN_RATE_PERCENTAGE 27  // PWM minimum speed ratio (%)
#define PWM_MAX_RATE_PERCENTAGE 100 // PWM maximum speed ratio (%)

#define CW_PIN			4		// [In] Clock Wise SW (PORTC.4)
#define CCW_PIN			5		// [In] Counter Clock Wise SW (PORTC.5)

// Pin control macros
#define SET_PIN(port,pin)	(port|=(1<<pin))
#define CLR_PIN(port,pin)	(port&=~(1<<pin))

// Switch status check macros (assuming internal pull-up resistors are used)
// If the switch is pressed, the pin will be LOW.
#define IS_CW_PRESSED   (!(PINC & (1<<CW_PIN)))
#define IS_CCW_PRESSED  (!(PINC & (1<<CCW_PIN)))

// AVR initialization function
void init_avr(void){
	// Set DC Motor direction control pins (IN1, IN2) as outputs on PORTC
	SET_PIN(DCM_CTRL_SET,IN1_PIN);
	SET_PIN(DCM_CTRL_SET,IN2_PIN);

	// Set PWM output pin (corresponding to ENA_PIN) as output on PORTB
	SET_PIN(PWM_DDR, PWM_PIN);

	// Set switch pins as inputs (by setting corresponding DDRC bits to 0)
	CLR_PIN(DCM_CTRL_SET,CW_PIN);
	CLR_PIN(DCM_CTRL_SET,CCW_PIN);

	// Enable internal pull-up resistors for switch pins (by setting corresponding PORTC bits to 1)
	SET_PIN(DCM_CTRL,CW_PIN);
	SET_PIN(DCM_CTRL,CCW_PIN);

	// Timer/Counter1 Fast PWM Mode 14 (TOP = ICR1) configuration
	// COM1A1: Set OC1A pin to non-inverting mode (clear on compare match, set at BOTTOM)
	TCCR1A |= (1 << COM1A1);
	// WGM11, WGM12, WGM13: Settings for Fast PWM Mode 14
	TCCR1A |= (1 << WGM11);
	TCCR1B |= (1 << WGM12) | (1 << WGM13);

	// Prescaler setting
	// F_PWM = F_CPU / (N * (1 + TOP))
	// F_PWM = 16MHz / (64 * (1 + 255)) = 16M / (64 * 256) = 16M / 16384 = 976.56 Hz
	TCCR1B |= (1 << CS11) | (1 << CS10); // Prescaler 64

	// Set PWM period (TOP value)
	ICR1 = PWM_TOP; // Set to 255 for 8-bit resolution
	OCR1A = 0; // Initial duty cycle 0 (motor stopped)

	// ADC initialization settings
	// REFS0: Use AVCC as reference voltage (external capacitor connected to AREF pin)
	// MUX0-MUX4: Select ADC0 channel (0b00000)
	ADMUX = (1 << REFS0); // Select ADC0 (PA0)

	// ADEN: Enable ADC
	// ADPS2, ADPS1, ADPS0: ADC clock prescaler setting (128 in this case)
	// F_ADC = F_CPU / Prescaler = 16MHz / 128 = 125kHz (within recommended range)
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

// Function to set motor speed (0-PWM_TOP)
void set_motor_speed(uint8_t speed) {
	OCR1A = speed; // Set duty cycle
}

// Function to set clockwise direction (speed control is done by ADC value in main loop)
void Clock_Wise_Dir(void)
{
	SET_PIN(DCM_CTRL,IN1_PIN);
	CLR_PIN(DCM_CTRL,IN2_PIN);
}

// Function to set counter-clockwise direction (speed control is done by ADC value in main loop)
void Counter_Clock_Wise_Dir(void)
{
	CLR_PIN(DCM_CTRL,IN1_PIN);
	SET_PIN(DCM_CTRL,IN2_PIN);
}

// Function to set motor stop direction (speed control is done by setting speed to 0 in main loop)
void Stop_Dir(void)
{
	CLR_PIN(DCM_CTRL,IN1_PIN);
	CLR_PIN(DCM_CTRL,IN2_PIN);
}

// Function to generate LCD E pin pulse
void LCD_pulse_enable(void)
{
	SET_PIN(PORT_CONTROL, E_PIN);
	_delay_us(1); // Maintain E pulse width
	CLR_PIN(PORT_CONTROL, E_PIN);
	_delay_ms(1); // Command/data processing wait time (refer to LCD datasheet)
}

// Function to write data to LCD
void LCD_write_data(uint8_t data)
{
	SET_PIN(PORT_CONTROL, RS_PIN); // RS = 1 (data mode)
	PORT_DATA = data;			// Output character data
	LCD_pulse_enable();			// Execute pulse
	_delay_ms(2);				// Data processing delay
}

// Function to write command to LCD
void LCD_write_command(uint8_t command)
{
	CLR_PIN(PORT_CONTROL, RS_PIN); // RS = 0 (command mode)
	PORT_DATA = command;		// Send command to data pins
	LCD_pulse_enable();			// Execute command
	_delay_ms(2);				// Command processing delay
}

// Function to clear LCD screen
void LCD_clear(void)
{
	LCD_write_command(COMMAND_CLEAR_DISPLAY);
	_delay_ms(2); // Clear display command requires longer delay
}

// LCD initialization function
void LCD_init(void)
{
	_delay_ms(50);				// Initial power-on delay for LCD
	
	// Set data pins as output
	DDR_DATA = 0xFF;
	PORT_DATA = 0x00; // Initialize data pins to LOW
	// Set control pins (RS, RW, E) as output
	DDR_CONTROL |= (1 << RS_PIN) | (1 << RW_PIN) | (1 << E_PIN);

	// Set RW pin to LOW for write-only mode
	CLR_PIN(PORT_CONTROL, RW_PIN);
	_delay_ms(2); // Wait after setting RW

	LCD_write_command(COMMAND_8_BIT_MODE);		// 8-bit mode, 2 lines, 5x8 font
	
	// Display On/Off Control: Display ON, Cursor OFF, Blink OFF
	uint8_t command = 0x08 | (1 << COMMAND_DISPLAY_ON_OFF_BIT); // 0x0C
	LCD_write_command(command);

	LCD_clear();				// Clear screen

	// Entry Mode Set: Increment cursor, no display shift
	LCD_write_command(0x06);
}

// Function to write string to LCD
void LCD_write_string(char *string)
{
	uint8_t i;
	for(i = 0; string[i]; i++)			// Until null terminator is met
	LCD_write_data(string[i]);		// Output character by character
}

// Function to move LCD cursor
void LCD_goto_XY(uint8_t row, uint8_t col)
{
	col %= 16;		// Column range [0-15]
	row %= 2;		// Row range [0-1]

	// First line start address is 0x00, second line start address is 0x40
	uint8_t address = (0x40 * row) + col;
	uint8_t command = 0x80 + address; // Set DDRAM address command (MSB is 1)
	
	LCD_write_command(command);	// Move cursor
}

int main(void)
{
	uint8_t cw_pressed_prev = 0;
	uint8_t ccw_pressed_prev = 0;
	uint8_t current_motor_state = 0; // 0: Stop, 1: CW, 2: CCW

	// Calculate min and max PWM duty cycle values
	const uint8_t min_duty_cycle = (uint8_t)((PWM_TOP * PWM_MIN_RATE_PERCENTAGE) / 100.0);
	const uint8_t max_duty_cycle = (uint8_t)((PWM_TOP * PWM_MAX_RATE_PERCENTAGE) / 100.0);
	const uint8_t duty_cycle_range = max_duty_cycle - min_duty_cycle;

	// Buffer for converting values to string
	char lcd_str_buffer[16]; // Max 16 characters for a line

	// Store previous ADC percentage value to optimize LCD updates
	uint8_t prev_adc_percentage = 0;

	init_avr(); // Initialize AVR and DC motor pins
	LCD_init(); // Initialize LCD

	LCD_goto_XY(0,0);
	LCD_write_string("Motor Control");
	_delay_ms(1000); // Display initial message for 1 second
	LCD_clear();

	while (1)
	{
		// Short delay for switch debouncing
		_delay_ms(50);

		// Read current switch states
		uint8_t cw_current = IS_CW_PRESSED;
		uint8_t ccw_current = IS_CCW_PRESSED;

		// Manually start ADC conversion
		ADCSRA |= (1 << ADSC);
		// Wait for ADC conversion to complete
		while (ADCSRA & (1 << ADSC)); // Loop while ADSC is 1 (conversion in progress)

		// Read ADC value
		uint16_t adc_raw_value = ADC; // ADC holds value from 0 to 1023

		// Convert ADC raw value (0-1023) to percentage (0-100%)
		// Adding 0.5 for proper rounding
		uint8_t adc_percentage = (uint8_t)(((float)adc_raw_value / 1023.0) * 100.0 + 0.5);
		// Clamp percentage to 100 in case of floating point inaccuracies
		if (adc_percentage > 100) adc_percentage = 100;


		// Map 10-bit ADC value to duty cycle range (min_duty_cycle ~ max_duty_cycle)
		// Ensure the mapping is from 0-1023 to the desired PWM range
		uint8_t motor_speed = (uint8_t)(((float)adc_raw_value / 1023.0) * duty_cycle_range + min_duty_cycle);

		// Motor direction control (based on switch input)
		if (cw_current && !cw_pressed_prev) { // If CW switch is pressed and was not pressed before (rising edge detection)
			if (current_motor_state != 1) { // Only update if state has changed
				Clock_Wise_Dir(); // Set clockwise direction
				current_motor_state = 1; // Change to CW state
				LCD_goto_XY(0,0);
				LCD_clear();
				LCD_write_string("Direction : CW");
			}
			} else if (ccw_current && !ccw_pressed_prev) { // If CCW switch is pressed and was not pressed before (rising edge detection)
			if (current_motor_state != 2) { // Only update if state has changed
				Counter_Clock_Wise_Dir(); // Set counter-clockwise direction
				current_motor_state = 2; // Change to CCW state
				LCD_goto_XY(0,0);
				LCD_clear();
				LCD_write_string("Direction : CCW");
			}
			} else if (!cw_current && !ccw_current && (cw_pressed_prev || ccw_pressed_prev)) { // If both switches are released and at least one was pressed before (falling edge detection or both released)
			if (current_motor_state != 0) { // Only update if state has changed
				Stop_Dir(); // Set motor direction to stop
				current_motor_state = 0; // Change to stop state
				LCD_goto_XY(0,0);
				LCD_clear();
				LCD_write_string("Stop");
			}
		}

		if (current_motor_state == 0) {
			set_motor_speed(0); // If in stop state, set speed to 0
			} else {
			set_motor_speed(motor_speed); // If rotating, control speed with ADC value
		}

		// Update LCD second line only if ADC percentage value has changed
		if (adc_percentage != prev_adc_percentage) {
			LCD_goto_XY(1,0); // Move cursor to second line, first column
			sprintf(lcd_str_buffer, "Duty Rate :%3d%%", adc_percentage); // Convert percentage to string (3 digits for value, then '%%')
			LCD_write_string(lcd_str_buffer); // Output converted string
			for (int i = 0; i < 5; i++) {
				LCD_write_data(' ');
			}
			prev_adc_percentage = adc_percentage; // Update previous ADC percentage value
		}

		// Update switch states for the next loop iteration
		cw_pressed_prev = cw_current;
		ccw_pressed_prev = ccw_current;
	}
	return 0;
}
