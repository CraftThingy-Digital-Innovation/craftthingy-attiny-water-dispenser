/**
 * =========================================================================
 * CraftThingy - ATtiny88 Water Dispenser Controller (Native AVR C++)
 * =========================================================================
 * Board: MH-ET LIVE Tiny88 / ATtiny88
 * Clock: 16.0 MHz (Micronucleus clock)
 *
 * Silkscreen Pin Mapping for MH-ET LIVE Tiny88:
 *   Left Side Pins:
 *     - "3" (PB3): Left Button (40 Seconds)
 *     - "4" (PB4): Middle Button (Toggle Continuous / Cancel)
 *     - "5" (PB5): Right Button (80 Seconds)
 *     - "6" (PC0): IRLZ44N Gate Output
 *     - "7" (PC1): Status LED Output
 *
 *   Right Side Pins (Standard PORTD):
 *     - "9"  (PD3): Left Button (Alternative)
 *     - "10" (PD4): Middle Button (Alternative)
 *     - "11" (PD5): Right Button (Alternative)
 *     - "12" (PD6): IRLZ44N Gate Output (Alternative)
 *     - "13" (PD7): Status LED Output (Alternative)
 *
 * Note: Both PORTB and PORTD are monitored and driven simultaneously!
 * =========================================================================
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

// State variables
static volatile uint32_t system_millis = 0;
static volatile uint8_t is_pump_on = 0;
static volatile uint8_t is_continuous = 0;
static uint32_t pump_start_time = 0;
static uint32_t pump_duration = 0;

// Timer1 Compare Match A: 1 ms tick
ISR(TIMER1_COMPA_vect) {
    system_millis++;
}

// PCINT0 Interrupt on PORTB (PB3, PB4, PB5 - Board Pins 3, 4, 5)
ISR(PCINT0_vect) {
    // Wake up from deep sleep
}

// PCINT2 Interrupt on PORTD (PD3, PD4, PD5 - Board Pins 9, 10, 11)
ISR(PCINT2_vect) {
    // Wake up from deep sleep
}

static inline void start_timer() {
    TCCR1A = 0;
    // CTC Mode with WGM12 = 1, Prescaler 64: CS11 = 1, CS10 = 1
    // 16MHz / 64 = 250kHz. OCR1A = 249 gives 250kHz / 250 = 1000 Hz (1ms tick)
    OCR1A = 249;
    TCNT1 = 0;
    TIFR1 = (1 << OCF1A);
    TIMSK1 |= (1 << OCIE1A); // Enable Compare Match A interrupt
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
}

static inline void stop_timer() {
    TCCR1B = 0;
    TIMSK1 &= ~(1 << OCIE1A);
}

static inline uint32_t get_millis() {
    uint32_t m;
    uint8_t sreg = SREG;
    cli();
    m = system_millis;
    SREG = sreg;
    return m;
}

static inline void drive_gate_high() {
    PORTC |= (1 << PC0); // Pin 6 (Left side)
    PORTD |= (1 << PD6); // Pin 12 (Right side)
}

static inline void drive_gate_low() {
    PORTC &= ~(1 << PC0);
    PORTD &= ~(1 << PD6);
}

static inline void drive_led_high() {
    PORTC |= (1 << PC1); // Pin 7 (Left side)
    PORTD |= (1 << PD7); // Pin 13 (Right side)
}

static inline void drive_led_low() {
    PORTC &= ~(1 << PC1);
    PORTD &= ~(1 << PD7);
}

static inline void turn_pump_on(uint32_t duration, uint8_t continuous) {
    is_pump_on = 1;
    is_continuous = continuous;
    pump_start_time = get_millis();
    pump_duration = duration;

    start_timer();
    drive_gate_high();
    drive_led_high();
}

static inline void turn_pump_off() {
    is_pump_on = 0;
    is_continuous = 0;

    drive_gate_low();
    drive_led_low();
    stop_timer();
}

static void enter_deep_sleep() {
    turn_pump_off();

    // Deep sleep mode (~0.5uA)
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();

    // CPU pauses here until a button press triggers PCINT0 or PCINT2
    sleep_cpu();

    // CPU wakes up here
    sleep_disable();
}

// Check button states (active LOW)
static inline uint8_t is_btn_left_pressed() {
    return (!(PINB & (1 << PB3)) || !(PIND & (1 << PD3)));
}

static inline uint8_t is_btn_mid_pressed() {
    return (!(PINB & (1 << PB4)) || !(PIND & (1 << PD4)));
}

static inline uint8_t is_btn_right_pressed() {
    return (!(PINB & (1 << PB5)) || !(PIND & (1 << PD5)));
}

int main(void) {
    // 1. Configure Button Inputs (with internal pull-ups):
    // PORTB: PB3 (Pin 3), PB4 (Pin 4), PB5 (Pin 5)
    DDRB &= ~((1 << PB3) | (1 << PB4) | (1 << PB5));
    PORTB |= (1 << PB3) | (1 << PB4) | (1 << PB5);

    // PORTD: PD3 (Pin 9), PD4 (Pin 10), PD5 (Pin 11)
    DDRD &= ~((1 << PD3) | (1 << PD4) | (1 << PD5));
    PORTD |= (1 << PD3) | (1 << PD4) | (1 << PD5);

    // 2. Configure Outputs:
    // PORTC: PC0 (Gate / Pin 6), PC1 (LED / Pin 7)
    DDRC |= (1 << PC0) | (1 << PC1);
    PORTC &= ~((1 << PC0) | (1 << PC1));

    // PORTD: PD6 (Gate / Pin 12), PD7 (LED / Pin 13)
    DDRD |= (1 << PD6) | (1 << PD7);
    PORTD &= ~((1 << PD6) | (1 << PD7));

    // 3. Configure Pin Change Interrupts:
    // PCIE0 enables PCINT0..7 (PORTB: PB3=PCINT3, PB4=PCINT4, PB5=PCINT5)
    // PCIE2 enables PCINT16..23 (PORTD: PD3=PCINT19, PD4=PCINT20, PD5=PCINT21)
    PCICR |= (1 << PCIE0) | (1 << PCIE2);
    PCMSK0 |= (1 << PCINT3) | (1 << PCINT4) | (1 << PCINT5);
    PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21);

    // 4. Startup Indicator: Blink LED 3 times quickly to signal boot ready
    for (uint8_t i = 0; i < 3; i++) {
        drive_led_high();
        _delay_ms(100);
        drive_led_low();
        _delay_ms(100);
    }

    // 5. Enable Global Interrupts
    sei();

    while (1) {
        if (!is_pump_on) {
            enter_deep_sleep();
            _delay_ms(30); // Wake debounce
        }

        uint32_t now = get_millis();

        // Left Button: 40 Seconds Dispense
        if (is_btn_left_pressed()) {
            turn_pump_on(40000UL, 0);
            _delay_ms(250); // Debounce
        }
        // Middle Button: Toggle Continuous ON / Cancel (Abort)
        else if (is_btn_mid_pressed()) {
            if (is_pump_on) {
                turn_pump_off();
            } else {
                turn_pump_on(0, 1); // Continuous ON
            }
            _delay_ms(250); // Debounce
        }
        // Right Button: 80 Seconds Dispense
        else if (is_btn_right_pressed()) {
            turn_pump_on(80000UL, 0);
            _delay_ms(250); // Debounce
        }

        // Automatic Timer Expiration Check
        if (is_pump_on && !is_continuous) {
            if ((now - pump_start_time) >= pump_duration) {
                turn_pump_off();
            }
        }
    }

    return 0;
}
