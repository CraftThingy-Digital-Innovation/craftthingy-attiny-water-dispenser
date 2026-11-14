/**
 * =========================================================================
 * CraftThingy - ATtiny88 Water Dispenser Controller (Native AVR C++)
 * =========================================================================
 * Board: MH-ET LIVE Tiny88 / ATtiny88
 * Clock: 16.0 MHz (Micronucleus clock)
 *
 * Updates:
 *   - Soft-Start PWM Ramp: Smooth 500ms ramp prevents high inrush stall
 *     current from tripping the battery protection circuit (BMS).
 *   - Dual Port Support (PORTB & PORTD)
 *   - Low-power deep sleep (PWR_DOWN: ~0.5uA)
 * =========================================================================
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>

// State variables
static volatile uint32_t system_millis = 0;
static volatile uint8_t is_pump_on = 0;
static volatile uint8_t is_continuous = 0;
static uint32_t pump_start_time = 0;
static uint32_t pump_duration = 0;

// Catch-all ISR: Any unhandled interrupt returns safely without resetting to 0x0000!
ISR(BADISR_vect) {
    // Safely ignore spurious interrupts
}

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
    OCR1A = 249; // 16MHz / 64 = 250kHz. 250 counts = 1ms tick
    TCNT1 = 0;
    TIFR1 = (1 << OCF1A);
    TIMSK1 |= (1 << OCIE1A);
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

static void delay_micros(uint16_t us) {
    while (us >= 20) {
        _delay_us(20);
        us -= 20;
    }
}

/**
 * Soft-Start Motor Ramp:
 * Gradually increases the MOSFET duty cycle over ~500ms using a 1kHz PWM.
 * This prevents the stationary motor coils from creating an instantaneous
 * stall inrush spike that collapses the battery rail or trips the BMS.
 */
static void soft_start_motor() {
    drive_led_high(); // Turn LED on immediately for feedback

    // 50 steps x 10ms per step = 500ms smooth ramp
    for (uint8_t step = 1; step <= 50; step++) {
        uint16_t on_us = (uint16_t)step * 20; // 20us up to 1000us
        uint16_t off_us = 1000 - on_us;

        // Run 10 PWM cycles (10 x 1ms = 10ms) at this duty cycle
        for (uint8_t cycle = 0; cycle < 10; cycle++) {
            if (on_us > 0) {
                drive_gate_high();
                delay_micros(on_us);
            }
            if (off_us > 0) {
                drive_gate_low();
                delay_micros(off_us);
            }
        }
    }

    // Fully lock Gate ON once up to speed
    drive_gate_high();
}

static void blink_led(uint8_t count, uint16_t on_ms, uint16_t off_ms) {
    for (uint8_t i = 0; i < count; i++) {
        drive_led_high();
        for (uint16_t t = 0; t < on_ms; t += 10) _delay_ms(10);
        drive_led_low();
        for (uint16_t t = 0; t < off_ms; t += 10) _delay_ms(10);
    }
}

static inline void turn_pump_on(uint32_t duration, uint8_t continuous) {
    is_pump_on = 1;
    is_continuous = continuous;
    pump_start_time = get_millis();
    pump_duration = duration;

    start_timer();
    soft_start_motor(); // Smooth PWM ramp up
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

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();

    sleep_cpu();

    sleep_disable();
}

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
    // 0. Read Reset Reason & Disable Watchdog immediately
    uint8_t reset_flags = MCUSR;
    MCUSR = 0;
    wdt_disable();
    EIMSK = 0;

    // 1. Configure Button Inputs (with internal pull-ups):
    DDRB &= ~((1 << PB3) | (1 << PB4) | (1 << PB5));
    PORTB |= (1 << PB3) | (1 << PB4) | (1 << PB5);

    DDRD &= ~((1 << PD3) | (1 << PD4) | (1 << PD5));
    PORTD |= (1 << PD3) | (1 << PD4) | (1 << PD5);

    // 2. Configure Outputs (Gate & LED initially LOW):
    DDRC |= (1 << PC0) | (1 << PC1);
    PORTC &= ~((1 << PC0) | (1 << PC1));

    DDRD |= (1 << PD6) | (1 << PD7);
    PORTD &= ~((1 << PD6) | (1 << PD7));

    // 3. Configure Pin Change Interrupts:
    PCICR |= (1 << PCIE0) | (1 << PCIE2);
    PCMSK0 |= (1 << PCINT3) | (1 << PCINT4) | (1 << PCINT5);
    PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21);

    // 4. Diagnostic Reset Indicator:
    if (reset_flags & (1 << BORF)) {
        blink_led(3, 400, 200); // 3 long pulses = Brownout
    } else if (reset_flags & (1 << EXTRF)) {
        blink_led(2, 400, 200); // 2 long pulses = Reset Pin
    } else if (reset_flags & (1 << WDRF)) {
        blink_led(4, 150, 150); // 4 pulses = Watchdog
    } else {
        blink_led(1, 300, 200); // 1 clean pulse = Ready
    }

    // 5. Enable Global Interrupts
    sei();

    while (1) {
        if (!is_pump_on) {
            enter_deep_sleep();
            _delay_ms(50); // Wake debounce
        }

        uint32_t now = get_millis();

        // Left Button: 40 Seconds Dispense
        if (is_btn_left_pressed()) {
            turn_pump_on(40000UL, 0);
            _delay_ms(250);
        }
        // Middle Button: Toggle Continuous ON / Cancel (Abort)
        else if (is_btn_mid_pressed()) {
            if (is_pump_on) {
                turn_pump_off();
            } else {
                turn_pump_on(0, 1);
            }
            _delay_ms(250);
        }
        // Right Button: 80 Seconds Dispense
        else if (is_btn_right_pressed()) {
            turn_pump_on(80000UL, 0);
            _delay_ms(250);
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
