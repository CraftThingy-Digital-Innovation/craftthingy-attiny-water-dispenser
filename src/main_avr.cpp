/**
 * =========================================================================
 * CraftThingy - ATtiny88 Water Dispenser Controller (Native AVR C++)
 * =========================================================================
 * Board: MH-ET LIVE Tiny88 / ATtiny88
 * F_CPU: 16.0 MHz (Micronucleus clock)
 * Flash Footprint: < 500 bytes (Clean, rock-solid, zero bloated libraries)
 *
 * Pinout:
 *   - PD3 (Pin 3): Left Button (40 Seconds) -> Active LOW
 *   - PD4 (Pin 4): Middle Button (Toggle Continuous / Cancel) -> Active LOW
 *   - PD5 (Pin 5): Right Button (80 Seconds) -> Active LOW
 *   - PD6 (Pin 6): IRLZ44N Gate Output -> Active HIGH
 *   - PD7 (Pin 7): Status LED Indicator -> Active HIGH
 * =========================================================================
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

// Pin Bit Masks on PORTD
#define BTN_40S_BIT    PD3
#define BTN_TOGGLE_BIT PD4
#define BTN_80S_BIT    PD5
#define PUMP_GATE_BIT  PD6
#define STATUS_LED_BIT PD7

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

// PCINT2 Interrupt on PORTD: Wakes up from deep sleep
ISR(PCINT2_vect) {
    // Empty ISR purely to resume execution after sleep_cpu()
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

static inline void turn_pump_on(uint32_t duration, uint8_t continuous) {
    is_pump_on = 1;
    is_continuous = continuous;
    pump_start_time = get_millis();
    pump_duration = duration;

    // Enable Timer1 tick
    start_timer();

    // Set Gate and LED HIGH
    PORTD |= (1 << PUMP_GATE_BIT) | (1 << STATUS_LED_BIT);
}

static inline void turn_pump_off() {
    is_pump_on = 0;
    is_continuous = 0;

    // Set Gate and LED LOW
    PORTD &= ~((1 << PUMP_GATE_BIT) | (1 << STATUS_LED_BIT));

    // Turn off timer to save power
    stop_timer();
}

static void enter_deep_sleep() {
    turn_pump_off();

    // Set sleep mode to lowest possible power consumption (PWR_DOWN: ~0.5uA)
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();

    // Enable interrupts before sleeping
    sei();

    // CPU execution halts here until a button press triggers PCINT
    sleep_cpu();

    // Resumes here upon wake
    sleep_disable();
}

int main(void) {
    // 1. Configure I/O Pins:
    // Inputs: PD3, PD4, PD5
    DDRD &= ~((1 << BTN_40S_BIT) | (1 << BTN_TOGGLE_BIT) | (1 << BTN_80S_BIT));
    // Enable internal pull-ups for buttons
    PORTD |= (1 << BTN_40S_BIT) | (1 << BTN_TOGGLE_BIT) | (1 << BTN_80S_BIT);

    // Outputs: PD6 (MOSFET Gate), PD7 (Status LED)
    DDRD |= (1 << PUMP_GATE_BIT) | (1 << STATUS_LED_BIT);
    // Ensure outputs are strictly LOW initially
    PORTD &= ~((1 << PUMP_GATE_BIT) | (1 << STATUS_LED_BIT));

    // 2. Configure Pin Change Interrupts for PORTD (PCINT19, PCINT20, PCINT21)
    PCICR |= (1 << PCIE2);
    PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21);

    // 3. Enable Global Interrupts
    sei();

    while (1) {
        if (!is_pump_on) {
            enter_deep_sleep();
            _delay_ms(50); // Wake debounce
        }

        uint32_t now = get_millis();

        // Check Left Button: 40s
        if (!(PIND & (1 << BTN_40S_BIT))) {
            turn_pump_on(40000UL, 0);
            _delay_ms(250); // Button debounce
        }
        // Check Middle Button: Toggle ON / Cancel (Abort)
        else if (!(PIND & (1 << BTN_TOGGLE_BIT))) {
            if (is_pump_on) {
                turn_pump_off();
            } else {
                turn_pump_on(0, 1); // Continuous ON
            }
            _delay_ms(250); // Button debounce
        }
        // Check Right Button: 80s
        else if (!(PIND & (1 << BTN_80S_BIT))) {
            turn_pump_on(80000UL, 0);
            _delay_ms(250); // Button debounce
        }

        // Timer expiration check
        if (is_pump_on && !is_continuous) {
            if ((now - pump_start_time) >= pump_duration) {
                turn_pump_off();
            }
        }
    }

    return 0;
}
