/**
 * =========================================================================
 * CraftThingy - ATtiny88 Water Dispenser Controller (Native AVR C++)
 * =========================================================================
 * Board: MH-ET LIVE Tiny88 / ATtiny88
 * Clock: 16.0 MHz (Micronucleus clock)
 *
 * Smart Dispense Features:
 *   - Diagnostics on Reset:
 *       * If a fault occurs (Brown-out / Watchdog): blinks 2 times for diagnostics.
 *       * If clean boot (no failure): skips diagnostic blinks.
 *   - Mode Visual Feedback:
 *       * 40s mode: blinks 1 time.
 *       * 80s mode: blinks 2 times.
 *       * Continuous mode: blinks 3 times.
 *   - Same-Button Toggle:
 *       * Press 40s while 40s is running -> STOPS immediately.
 *       * Press 80s while 80s is running -> STOPS immediately.
 *   - Smart Duration Extension (40s -> 80s):
 *       * If 40s is running and user presses 80s, total pour target becomes
 *         80s from the initial start (remaining time = 80s - elapsed).
 *   - Impossible Time Switch (80s -> 40s):
 *       * If 80s is running and user presses 40s, immediately STOPS the pump.
 *   - Middle Button:
 *       * If pump is OFF -> Starts Continuous ON.
 *       * If pump is ON (any mode) -> Cancels and STOPS immediately.
 *   - 100% Full Power with 500ms PWM Soft-Start: Protects battery BMS.
 *   - Ultra-low power Deep Sleep: ~0.5uA when idle.
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

// Dispenser Operating Modes
#define MODE_IDLE       0
#define MODE_40S        1
#define MODE_80S        2
#define MODE_CONTINUOUS 3

// State variables
static volatile uint32_t system_millis = 0;
static volatile uint8_t current_mode = MODE_IDLE;
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
 * Eliminates startup inrush stall spikes that trip battery BMS circuits.
 */
static void soft_start_motor() {
    drive_led_high();

    for (uint8_t step = 1; step <= 50; step++) {
        uint16_t on_us = (uint16_t)step * 20;
        uint16_t off_us = 1000 - on_us;

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

static void turn_pump_on_fresh(uint8_t mode, uint32_t duration, uint8_t blinks) {
    current_mode = mode;
    pump_duration = duration;

    // Visual mode confirmation feedback: 1 for 40s, 2 for 80s, 3 for Continuous
    blink_led(blinks, 150, 150);

    start_timer();
    pump_start_time = get_millis();
    soft_start_motor();
}

static void turn_pump_off() {
    current_mode = MODE_IDLE;

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
    // 0. Reset Reason & Watchdog disable
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
    // If a fault/failure occurred (Brown-Out BORF or Watchdog WDRF), blink 2 times first for diagnostics.
    // If clean startup (nothing happened / no failure), do not blink error.
    if (reset_flags & ((1 << BORF) | (1 << WDRF))) {
        blink_led(2, 400, 250);
    }

    // 5. Enable Global Interrupts
    sei();

    while (1) {
        // Sleep only when idle
        if (current_mode == MODE_IDLE) {
            enter_deep_sleep();
            _delay_ms(50); // Wake debounce
        }

        uint32_t now = get_millis();

        // =================================================================
        // BUTTON 1: LEFT BUTTON (40s)
        // =================================================================
        if (is_btn_left_pressed()) {
            if (current_mode == MODE_40S) {
                // Pressed 40s again while 40s running -> STOP!
                turn_pump_off();
            } else if (current_mode == MODE_80S) {
                // Switched from 80s to 40s -> Stop/cancel immediately
                turn_pump_off();
            } else if (current_mode == MODE_CONTINUOUS) {
                // Cancel continuous -> STOP
                turn_pump_off();
            } else {
                // Fresh start: 40s (1 blink)
                turn_pump_on_fresh(MODE_40S, 40000UL, 1);
            }
            _delay_ms(300); // Button debounce
        }

        // =================================================================
        // BUTTON 2: MIDDLE BUTTON (Toggle Continuous ON / Cancel / Abort)
        // =================================================================
        else if (is_btn_mid_pressed()) {
            if (current_mode != MODE_IDLE) {
                // If running in ANY mode, cancel and stop immediately!
                turn_pump_off();
            } else {
                // If OFF, start continuous dispensing (3 blinks)
                turn_pump_on_fresh(MODE_CONTINUOUS, 0, 3);
            }
            _delay_ms(300); // Button debounce
        }

        // =================================================================
        // BUTTON 3: RIGHT BUTTON (80s)
        // =================================================================
        else if (is_btn_right_pressed()) {
            if (current_mode == MODE_80S) {
                // Pressed 80s again while 80s running -> STOP!
                turn_pump_off();
            } else if (current_mode == MODE_40S) {
                // Switched from 40s to 80s! Total target becomes 80s from initial start
                current_mode = MODE_80S;
                pump_duration = 80000UL;
                blink_led(2, 120, 100); // 2 blinks to confirm 80s mode
                drive_led_high();       // Keep LED on while running
            } else if (current_mode == MODE_CONTINUOUS) {
                // Cancel continuous -> STOP
                turn_pump_off();
            } else {
                // Fresh start: 80s (2 blinks)
                turn_pump_on_fresh(MODE_80S, 80000UL, 2);
            }
            _delay_ms(300); // Button debounce
        }

        // =================================================================
        // AUTOMATIC TIMER EXPIRATION
        // =================================================================
        if (current_mode == MODE_40S || current_mode == MODE_80S) {
            if ((now - pump_start_time) >= pump_duration) {
                turn_pump_off();
            }
        }
    }

    return 0;
}
