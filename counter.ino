/*
 * Counter - Arduino conversion of ESPHome config
 *
 * This conversion was done by claude.ai and is untested so it may or may not work.
 * If it doesn't.... At least it might be a starting point to get it working for you :)
 *
 * Hardware:
 *   Board  : Wemos D1 Mini (ESP8266)
 *   Display: TM1637 4-digit 7-segment
 *     CLK  -> GPIO4 (D2)
 *     DIO  -> GPIO5 (D1)
 *   Button Up   -> GPIO14 (D5), active-low with INPUT_PULLUP
 *   Button Down -> GPIO12 (D6), active-low with INPUT_PULLUP
 *
 * Behaviour:
 *   - Up button   : increment counter (max  9999)
 *   - Down button : decrement counter (min -999)
 *   - Both held   : reset to 0 after 1 s
 *   - Counter is persisted to EEPROM
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <TM1637Display.h>

// ── Pin definitions ────────────────────────────────────────────────────────
#define CLK_PIN   4   // GPIO4  (D2 on D1 Mini)
#define DIO_PIN   5   // GPIO5  (D1 on D1 Mini)
#define BTN_UP    14  // GPIO14 (D5 on D1 Mini)
#define BTN_DOWN  12  // GPIO12 (D6 on D1 Mini)

// ── Constants ──────────────────────────────────────────────────────────────
#define DEBOUNCE_MS       10
#define BOTH_HOLD_MS    1000
#define DISPLAY_INTERVAL 100   // ms between display refreshes
#define EEPROM_ADDR        0   // address where the int is stored
#define BRIGHTNESS         1   // TM1637 intensity 0-7

// ── Display ────────────────────────────────────────────────────────────────
TM1637Display display(CLK_PIN, DIO_PIN);

// ── State ──────────────────────────────────────────────────────────────────
int  counter = 0;

// Button debounce state
struct Button {
  uint8_t  pin;
  bool     lastRaw;      // last raw reading
  bool     state;        // debounced state (true = pressed)
  uint32_t lastChangeMs; // when raw last changed
  bool     justPressed;  // single-cycle flag
};

Button btnUp   = { BTN_UP,   false, false, 0, false };
Button btnDown = { BTN_DOWN, false, false, 0, false };

// Both-held reset
bool     bothHeldActive  = false;   // were both pressed together?
uint32_t bothHeldStart   = 0;       // when they were both first pressed

// Display refresh
uint32_t lastDisplayMs   = 0;

// ── EEPROM helpers ─────────────────────────────────────────────────────────
void saveCounter() {
  EEPROM.put(EEPROM_ADDR, counter);
  EEPROM.commit();
}

void loadCounter() {
  EEPROM.get(EEPROM_ADDR, counter);
  // Sanity-check the restored value
  if (counter < -999 || counter > 9999) {
    counter = 0;
    saveCounter();
  }
}

// ── Display helper ─────────────────────────────────────────────────────────

// Segments for '-' (middle bar only)
#define SEG_MINUS 0x40

void updateDisplay() {
  if (counter < 0) {
    // Format: "-XXX"  (values -1 to -999)
    int absVal = abs(counter);
    uint8_t segs[4];
    segs[0] = SEG_MINUS;
    segs[1] = display.encodeDigit((absVal / 100) % 10);
    segs[2] = display.encodeDigit((absVal /  10) % 10);
    segs[3] = display.encodeDigit( absVal         % 10);
    display.setSegments(segs);
  } else {
    // showNumberDec with leading spaces (not zeros)
    display.showNumberDec(counter, false);
  }
}

// ── Button debounce ────────────────────────────────────────────────────────
void readButton(Button &btn) {
  btn.justPressed = false;
  bool raw = !digitalRead(btn.pin);  // inverted: pressed = LOW = true

  if (raw != btn.lastRaw) {
    btn.lastRaw      = raw;
    btn.lastChangeMs = millis();
  }

  if ((millis() - btn.lastChangeMs) >= DEBOUNCE_MS) {
    if (raw != btn.state) {
      btn.state = raw;
      if (btn.state) btn.justPressed = true;
    }
  }
}

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  EEPROM.begin(sizeof(int));
  loadCounter();

  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  display.setBrightness(BRIGHTNESS);
  updateDisplay();

  Serial.printf("[counter] Restored value: %d\n", counter);
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // 1. Read & debounce both buttons
  readButton(btnUp);
  readButton(btnDown);

  // 2. Handle "both held → reset" logic
  bool bothPressed = btnUp.state && btnDown.state;

  if (bothPressed) {
    if (!bothHeldActive) {
      bothHeldActive = true;
      bothHeldStart  = now;
    } else if ((now - bothHeldStart) >= BOTH_HOLD_MS) {
      // Still held after 1 s → reset
      counter = 0;
      saveCounter();
      Serial.printf("[counter] Reset! Value: %d\n", counter);
      bothHeldStart = now;  // prevent repeated resets while held
    }
  } else {
    bothHeldActive = false;

    // 3. Increment (only if Down is NOT also pressed)
    if (btnUp.justPressed && !btnDown.state) {
      counter += 1;
      saveCounter();
      Serial.printf("[counter] Value: %d\n", counter);
    }

    // 4. Decrement (only if Up is NOT also pressed)
    if (btnDown.justPressed && !btnUp.state) {
      if (counter > -999) {
        counter -= 1;
        saveCounter();
        Serial.printf("[counter] Value: %d\n", counter);
      }
    }
  }

  // 5. Refresh display every DISPLAY_INTERVAL ms
  if ((now - lastDisplayMs) >= DISPLAY_INTERVAL) {
    lastDisplayMs = now;
    updateDisplay();
  }
}
