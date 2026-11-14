/**
 * =========================================================================
 * CraftThingy - ATtiny88 Water Dispenser Controller (Arduino IDE Sketch)
 * =========================================================================
 * Board: MH-ET LIVE Tiny88 (16.0MHz)
 * Programmer: Micronucleus
 *
 * Smart Dispense Features:
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
 *   - 100% Full Power with 500ms Soft-Start: Protects battery BMS.
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

#define MODE_IDLE       0
#define MODE_40S        1
#define MODE_80S        2
#define MODE_CONTINUOUS 3

static volatile uint32_t system_millis = 0;
static volatile uint8_t current_mode = MODE_IDLE;
static uint32_t pump_start_time = 0;
static uint32_t pump_duration = 0;

static inline void drive_gate_high() {
    PORTC |= (1 << PC0);
    PORTD |= (1 << PD6);
}

static inline void drive_gate_low() {
    PORTC &= ~(1 << PC0);
    PORTD &= ~(1 << PD6);
}

static inline void drive_led_high() {
    PORTC |= (1 << PC1);
    PORTD |= (1 << PD7);
}

static inline void drive_led_low() {
    PORTC &= ~(1 << PC1);
    PORTD &= ~(1 << PD7);
}

ISR(BADISR_vect) {}

ISR(TIMER1_COMPA_vect) {
    system_millis++;
}

ISR(PCINT0_vect) {}
ISR(PCINT2_vect) {}

static inline void start_timer() {
    TCCR1A = 0;
    OCR1A = 249;
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

static void delay_micros(uint16_t us) {
    while (us >= 20) {
        _delay_us(20);
        us -= 20;
    }
}

static inline uint8_t is_led_active_during_soft_start(uint8_t step, uint8_t blinks) {
    if (blinks == 1) {
        return (step <= 25);
    } else if (blinks == 2) {
        return (step <= 12) || (step >= 26 && step <= 37);
    } else if (blinks == 3) {
        return (step <= 8) || (step >= 17 && step <= 24) || (step >= 33 && step <= 41);
    }
    return 1;
}

static void soft_start_motor(uint8_t blinks) {
    for (uint8_t step = 1; step <= 50; step++) {
        if (is_led_active_during_soft_start(step, blinks)) {
            drive_led_high();
        } else {
            drive_led_low();
        }

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

    // 100% Full Power Continuous Output & Solid LED
    drive_gate_high();
    drive_led_high();
}

static void blink_led(uint8_t count, uint16_t on_ms, uint16_t off_ms) {
    for (uint8_t i = 0; i < count; i++) {
        drive_led_high();
        for (uint16_t t = 0; t < on_ms; t += 10) _delay_ms(10);
        drive_led_low();
        for (uint16_t t = 0; t < off_ms; t += 10) _delay_ms(10);
    }
}

static inline uint8_t is_btn_left_raw() {
    return (!(PINB & (1 << PB3)) || !(PIND & (1 << PD3)));
}

static inline uint8_t is_btn_mid_raw() {
    return (!(PINB & (1 << PB4)) || !(PIND & (1 << PD4)));
}

static inline uint8_t is_btn_right_raw() {
    return (!(PINB & (1 << PB5)) || !(PIND & (1 << PD5)));
}

// 50ms Anti-Spark Noise Filter
static uint8_t is_btn_left_debounced() {
    if (is_btn_left_raw()) {
        _delay_ms(50);
        return is_btn_left_raw();
    }
    return 0;
}

static uint8_t is_btn_mid_debounced() {
    if (is_btn_mid_raw()) {
        _delay_ms(50);
        return is_btn_mid_raw();
    }
    return 0;
}

static uint8_t is_btn_right_debounced() {
    if (is_btn_right_raw()) {
        _delay_ms(50);
        return is_btn_right_raw();
    }
    return 0;
}

static void turn_pump_on_fresh(uint8_t mode, uint32_t duration, uint8_t blinks) {
    current_mode = mode;
    pump_duration = duration;

    start_timer();
    pump_start_time = get_millis();

    // Motor starts INSTANTLY with 0ms delay! Blinks concurrently during 500ms ramp
    soft_start_motor(blinks);
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

void setup() {
    uint8_t reset_flags = MCUSR;
    MCUSR = 0;
    wdt_disable();
    EIMSK = 0;

    DDRB &= ~((1 << PB3) | (1 << PB4) | (1 << PB5));
    PORTB |= (1 << PB3) | (1 << PB4) | (1 << PB5);

    DDRD &= ~((1 << PD3) | (1 << PD4) | (1 << PD5));
    PORTD |= (1 << PD3) | (1 << PD4) | (1 << PD5);

    DDRC |= (1 << PC0) | (1 << PC1);
    PORTC &= ~((1 << PC0) | (1 << PC1));

    DDRD |= (1 << PD6) | (1 << PD7);
    PORTD &= ~((1 << PD6) | (1 << PD7));

    PCICR |= (1 << PCIE0) | (1 << PCIE2);
    PCMSK0 |= (1 << PCINT3) | (1 << PCINT4) | (1 << PCINT5);
    PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21);

    // Diagnostic Reset Indicator:
    // If a hardware reset fault occurred (BORF, WDRF, EXTRF):
    //   Step 1: 2 rapid blinks (80ms on/80ms off) as the Alert Prefix
    //   Step 2: 800ms pause
    //   Step 3: Specific Error Code blinks:
    //           - 1 blink : Brown-Out Reset (BORF - Battery sag / voltage drop < 2.7V)
    //           - 2 blinks: Watchdog Reset (WDRF - CPU hung / lockup)
    //           - 3 blinks: External Reset (EXTRF - Noise on reset pin)
    // If clean boot (PORF or 0), zero error blinks occur.
    if (reset_flags & (1 << BORF)) {
        blink_led(2, 80, 80);    // 2 rapid alert blinks
        _delay_ms(800);
        blink_led(1, 400, 250);  // Code 1: Brown-Out
    } else if (reset_flags & (1 << WDRF)) {
        blink_led(2, 80, 80);    // 2 rapid alert blinks
        _delay_ms(800);
        blink_led(2, 400, 250);  // Code 2: Watchdog
    } else if (reset_flags & (1 << EXTRF)) {
        blink_led(2, 80, 80);    // 2 rapid alert blinks
        _delay_ms(800);
        blink_led(3, 400, 250);  // Code 3: External Reset
    }

    sei();
}

void loop() {
    if (current_mode == MODE_IDLE) {
        enter_deep_sleep();
        _delay_ms(50);
    }

    uint32_t now = get_millis();

    if (is_btn_left_debounced()) {
        if (current_mode == MODE_40S) {
            turn_pump_off(); // Same button pressed -> STOP
        } else if (current_mode == MODE_80S) {
            turn_pump_off(); // 80s to 40s -> STOP
        } else if (current_mode == MODE_CONTINUOUS) {
            turn_pump_off();
        } else {
            turn_pump_on_fresh(MODE_40S, 40000UL, 1); // 1 blink for 40s
        }
        _delay_ms(300);
    }
    else if (is_btn_mid_debounced()) {
        if (current_mode != MODE_IDLE) {
            turn_pump_off(); // Running -> STOP immediately
        } else {
            turn_pump_on_fresh(MODE_CONTINUOUS, 0, 3); // 3 blinks for Continuous
        }
        _delay_ms(300);
    }
    else if (is_btn_right_debounced()) {
        if (current_mode == MODE_80S) {
            turn_pump_off(); // Same button pressed -> STOP
        } else if (current_mode == MODE_40S) {
            current_mode = MODE_80S;
            pump_duration = 80000UL; // Extend total target to 80s!
            blink_led(2, 120, 100);  // 2 blinks to confirm 80s mode
            drive_led_high();        // Keep LED on while running
        } else if (current_mode == MODE_CONTINUOUS) {
            turn_pump_off();
        } else {
            turn_pump_on_fresh(MODE_80S, 80000UL, 2); // 2 blinks for 80s
        }
        _delay_ms(300);
    }

    if (current_mode == MODE_40S || current_mode == MODE_80S) {
        if ((now - pump_start_time) >= pump_duration) {
            turn_pump_off();
        }
    }
}
