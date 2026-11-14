#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

/**
 * =========================================================================
 * CraftThingy - Drinking Water Gallon Pump Controller
 * =========================================================================
 * Microcontroller: ATtiny88 (MH-ET LIVE Tiny88)
 * Architecture: AVR 8-bit
 * Power Source: 3.7V - 4.2V LiPo Cell (connected to 5V pin, bypassing onboard LDO)
 * Motor: 12V DC Pump driven via Boost Converter & IRLZ44N Logic-Level MOSFET
 *
 * Pin Layout:
 *   - Pin 3 (PD3 / PCINT19): Left Button (40 Seconds Timer) -> Active-LOW
 *   - Pin 4 (PD4 / PCINT20): Middle Button (Toggle Continuous ON / OFF) -> Active-LOW
 *   - Pin 5 (PD5 / PCINT21): Right Button (80 Seconds Timer) -> Active-LOW
 *   - Pin 6 (PD6):           MOSFET Gate Output (via 220R resistor + 10k pulldown)
 *   - Pin 7 (PD7):           LED Status Indicator (via 330R resistor)
 * =========================================================================
 */

// Pin Definitions
const int btn40s     = 3; // Left Button
const int btnToggle  = 4; // Middle Button (Toggle ON/OFF)
const int btn80s     = 5; // Right Button
const int pumpPin    = 6; // Connected to IRLZ44N Gate
const int ledPin     = 7; // Status LED Indicator

// State tracking variables
unsigned long startTime    = 0;
unsigned long runDuration  = 0;
bool isPumpOn              = false;
bool isContinuous          = false;

// Forward declarations
void goToSleep();

void setup() {
  // Configure button inputs with internal pull-ups
  pinMode(btn40s, INPUT_PULLUP);
  pinMode(btnToggle, INPUT_PULLUP);
  pinMode(btn80s, INPUT_PULLUP);

  // Configure output pins
  pinMode(pumpPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(pumpPin, LOW); // Motor strictly OFF at boot
  digitalWrite(ledPin, LOW);  // LED OFF at boot

  // Configure Pin Change Interrupts on PORTD (PCINT16-PCINT23)
  // Listen specifically to Pins 3, 4, 5 (PCINT19, PCINT20, PCINT21)
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21);

  sei(); // Enable global interrupts
}

// ISR for Pin Change Interrupt on PORTD
ISR(PCINT2_vect) {
  // Wakes the CPU from PWR_DOWN sleep mode
}

void goToSleep() {
  digitalWrite(ledPin, LOW);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  
  // MCU execution halts here and draws ~0.5 uA until a button is pressed
  sleep_cpu();

  // Execution resumes here after interrupt
  sleep_disable();
}

void loop() {
  // If the pump is not actively running, immediately enter deep sleep
  if (!isPumpOn) {
    goToSleep();
  }

  unsigned long currentMillis = millis();

  // --- BUTTON 1: Left Button (40 Seconds Dispense) ---
  if (digitalRead(btn40s) == LOW) {
    startTime = currentMillis;
    runDuration = 40000UL;
    isPumpOn = true;
    isContinuous = false;
    digitalWrite(pumpPin, HIGH); // Turn motor ON
    digitalWrite(ledPin, HIGH);  // Turn LED ON
    delay(250); // Debounce
  }
  // --- BUTTON 2: Middle Button (Toggle Continuous ON / Cancel / Abort) ---
  else if (digitalRead(btnToggle) == LOW) {
    if (isPumpOn) {
      // If already running (either timed or continuous), immediately turn OFF / Abort
      isPumpOn = false;
      isContinuous = false;
      digitalWrite(pumpPin, LOW);
      digitalWrite(ledPin, LOW);
    } else {
      // If OFF, toggle ON continuously until pressed again
      isPumpOn = true;
      isContinuous = true;
      digitalWrite(pumpPin, HIGH);
      digitalWrite(ledPin, HIGH);
    }
    delay(250); // Debounce
  }
  // --- BUTTON 3: Right Button (80 Seconds Dispense) ---
  else if (digitalRead(btn80s) == LOW) {
    startTime = currentMillis;
    runDuration = 80000UL;
    isPumpOn = true;
    isContinuous = false;
    digitalWrite(pumpPin, HIGH); // Turn motor ON
    digitalWrite(ledPin, HIGH);  // Turn LED ON
    delay(250); // Debounce
  }

  // --- Non-Blocking Timer Logic ---
  if (isPumpOn && !isContinuous) {
    if (currentMillis - startTime >= runDuration) {
      isPumpOn = false;
      digitalWrite(pumpPin, LOW);
      digitalWrite(ledPin, LOW);
    }
  }
}
