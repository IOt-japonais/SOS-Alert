#include <NRFLite.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/power.h>

// --- Pin Definitions ---
const static uint8_t RADIO_ID             = 2;
const static uint8_t DESTINATION_RADIO_ID = 0;
const static uint8_t PIN_RADIO_MOMI       = 0;  // PB0
const static uint8_t PIN_RADIO_SCK        = 1;  // PB1
const static uint8_t PIN_BUTTON           = 3;  // PB3 — PCINT3 wake source

// --- Radio Packet ---
struct RadioPacket {
  uint8_t FromRadioId;
  uint8_t data_sent;
};

NRFLite     _radio;
RadioPacket _radioData;

// --- SOS triple-press tracking ---
uint8_t        pressCount      = 0;
const uint8_t  SOS_PRESS_COUNT = 3;
const uint16_t SOS_WINDOW_MS   = 10000; // 10 seconds

// --- ISR: Pin Change Interrupt (just wakes MCU) ---
ISR(PCINT0_vect) {
  // Wake only — no action here
}

// --- Put nRF24L01 into power-down ---
void radioSleep() {
  _radio.powerDown();
}

// --- Wake nRF24L01 and re-initialise ---
void radioWake() {
  _radio.initTwoPin(RADIO_ID, PIN_RADIO_MOMI, PIN_RADIO_SCK);
  _radioData.FromRadioId = RADIO_ID;
  delay(5); // allow radio to stabilise
}

// --- Send SOS with up to 3 retries ---
void sendSOS() {
  _radioData.data_sent = 0xFF; // 255 = SOS marker
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    if (_radio.send(DESTINATION_RADIO_ID,
                    &_radioData,
                    sizeof(_radioData),
                    NRFLite::REQUIRE_ACK)) {
      break;
    }
    delay(50);
  }
}

void waitForRelease() {
  while (digitalRead(PIN_BUTTON) == HIGH) {
    delay(10);
  }
  delay(50); // debounce on release
}

// --- Put ATtiny85 into Power-Down sleep ---
void goToSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  GIMSK |= (1 << PCIE);    // enable pin-change interrupt group
  PCMSK |= (1 << PCINT3);  // unmask PCINT3 (PB3)

  sei();
  sleep_cpu();              // <<< MCU halts here

  // --- Resumes here after PCINT3 fires ---
  sleep_disable();
  PCMSK &= ~(1 << PCINT3); // mask until next sleep cycle
}

// =============================================================
void setup() {
  // External 5K pull-down: PB3 is LOW at rest, goes HIGH when button pressed
  // Button wiring: VCC → button → PB3, with 5K resistor from PB3 → GND
  pinMode(PIN_BUTTON, INPUT); // no internal pull-up

  if (!_radio.initTwoPin(RADIO_ID, PIN_RADIO_MOMI, PIN_RADIO_SCK)) {
    while (1); // Radio init failed
  }
  _radioData.FromRadioId = RADIO_ID;

  radioSleep(); // radio sleeps immediately until needed
}

void loop() {
  // ── 1. Sleep until PCINT3 fires ────────────────────────────
  goToSleep();

  // ── 2. Debounce — confirm button is genuinely HIGH ─────────
  delay(50);
  if (digitalRead(PIN_BUTTON) == LOW) {
    // Spurious wake (noise pulled line momentarily) — back to sleep
    radioSleep();
    return;
  }

// after
  // ── 3. Valid press #1 — start the SOS window ───────────────
  waitForRelease();
  pressCount = 1;

  // ── 4. Stay awake, polling for 2 more presses within 10s ───
  uint32_t windowStart = millis();
  while (pressCount < SOS_PRESS_COUNT &&
         (millis() - windowStart < SOS_WINDOW_MS)) {
    if (digitalRead(PIN_BUTTON) == HIGH) {
      delay(50); // debounce
      if (digitalRead(PIN_BUTTON) == HIGH) {
        pressCount++;
        waitForRelease();
      }
    }
  }

  // ── 5. 3 presses reached in time — wake radio and send SOS ──
  if (pressCount >= SOS_PRESS_COUNT) {
    radioWake();
    sendSOS();
  }
  pressCount = 0;

  // ── 6. Radio to sleep — MCU sleeps at top of next loop ──────
  radioSleep();
  
}
