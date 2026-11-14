#include <avr/sleep.h>
#include <avr/interrupt.h>

/**
 * =========================================================================
 * CraftThingy - Drinking Water Gallon Pump Controller
 * =========================================================================
 * Board: MH-ET LIVE Tiny88 (16.0MHz) or ATtiny88
 * Core: ATTinyCore / Digistump AVR
 * Programmer: Micronucleus
 * =========================================================================
 */

const int btn40s     = 3; // Left Button (40s)
const int btnToggle  = 4; // Middle Button (Toggle ON/OFF)
const int btn80s     = 5; // Right Button (80s)
const int pumpPin    = 6; // IRLZ44N Gate
const int ledPin     = 7; // Status LED

unsigned long startTime    = 0;
unsigned long runDuration  = 0;
bool isPumpOn              = false;
bool isContinuous          = false;

void goToSleep();

void setup() {
  pinMode(btn40s, INPUT_PULLUP);
  pinMode(btnToggle, INPUT_PULLUP);
  pinMode(btn80s, INPUT_PULLUP);

  pinMode(pumpPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(pumpPin, LOW);
  digitalWrite(ledPin, LOW);

  // PCINT on PORTD (Pins 3, 4, 5)
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT19) | (1 << PCINT20) | (1 << PCINT21);
  sei();
}

ISR(PCINT2_vect) {
  // Wake-up interrupt
}

void goToSleep() {
  digitalWrite(ledPin, LOW);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
  sleep_disable();
}

void loop() {
  if (!isPumpOn) {
    goToSleep();
  }

  unsigned long currentMillis = millis();

  // Left Button: 40s
  if (digitalRead(btn40s) == LOW) {
    startTime = currentMillis;
    runDuration = 40000UL;
    isPumpOn = true;
    isContinuous = false;
    digitalWrite(pumpPin, HIGH);
    digitalWrite(ledPin, HIGH);
    delay(250);
  }
  // Middle Button: Toggle ON / OFF (Abort)
  else if (digitalRead(btnToggle) == LOW) {
    if (isPumpOn) {
      isPumpOn = false;
      isContinuous = false;
      digitalWrite(pumpPin, LOW);
      digitalWrite(ledPin, LOW);
    } else {
      isPumpOn = true;
      isContinuous = true;
      digitalWrite(pumpPin, HIGH);
      digitalWrite(ledPin, HIGH);
    }
    delay(250);
  }
  // Right Button: 80s
  else if (digitalRead(btn80s) == LOW) {
    startTime = currentMillis;
    runDuration = 80000UL;
    isPumpOn = true;
    isContinuous = false;
    digitalWrite(pumpPin, HIGH);
    digitalWrite(ledPin, HIGH);
    delay(250);
  }

  // Timer check
  if (isPumpOn && !isContinuous) {
    if (currentMillis - startTime >= runDuration) {
      isPumpOn = false;
      digitalWrite(pumpPin, LOW);
      digitalWrite(ledPin, LOW);
    }
  }
}
