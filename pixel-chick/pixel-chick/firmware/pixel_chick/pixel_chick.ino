/*
  Pixel Chick - a rechargeable OLED keychain / bag charm with a living pixel face
  -----------------------------------------------------------------------------
  Board:   Arduino Pro Mini 3.3 V / 8 MHz (ATmega328P)
  Arduino IDE:  Tools > Board:     "Arduino Pro or Pro Mini"
                Tools > Processor: "ATmega328P (3.3V, 8 MHz)"
  Library (Library Manager): "Adafruit SSD1306" (also accept Adafruit GFX and BusIO)

  What it does:
    - Wakes up when the bag moves: looks surprised first, then smiles.
    - While awake it blinks, looks around and changes expression every few seconds:
      neutral, happy, very happy, surprised, suspicious, grumpy, sad, sleepy,
      heart eyes, a wink, and now and then one of your own messages.
    - Shaken hard, it gets dizzy (X X eyes).
    - After a while without movement it yawns ("zzz"), turns the screen off and
      sleeps at a few microamps.
    - Short button press: new expression right away.
      Hold 1.5 s: toggle "Always on" / "Auto sleep" (saved in EEPROM).
    - Warns when the battery is low; when it is empty it only shows "Charge me".

  Wiring (full details and photos in the README):
    Battery      -> TP4056 module B+ / B-
    TP4056       OUT+ -> power switch -> Pro Mini VCC      OUT- -> Pro Mini GND
    OLED         VCC -> VCC | GND -> GND | SDA -> A4 | SCL -> A5
    Vibration    KY-002 module: S -> D2, "-" -> GND   (+ 100 nF capacitor between D2 and GND)
    Button       one leg -> D3, diagonal leg -> GND
    Charging     (optional) TP4056 red LED, CHRG side -> D4
                 Not wired? The charging screen simply never appears.
*/

#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include "face.h"

// =========================== SETTINGS ===========================
// Set to 1 while testing on the desk powered by the USB-serial adapter (3.3 V).
// It turns the battery checks off so the 3.3 V supply is not mistaken for an
// empty battery. Set back to 0 before running on the battery.
#define DESK_TEST          0

#define AWAKE_MS           30000UL   // go to sleep after this long without movement
#define MAX_AWAKE_MS       180000UL  // stay awake at most this long even if it keeps moving (walking)
#define COOLDOWN_MS        600000UL  // after MAX_AWAKE_MS, ignore movement this long (the button still works)
#define WAKE_SHAKES        1         // shakes within 1 s needed to wake up (1 = most sensitive; use 2 if it wakes too often)
#define DIZZY_SHAKES       8         // shakes within 1 s that make it dizzy (raise it if walking triggers it)
#define DEBOUNCE_MS        40        // the spring sensor chatters on every touch; pulses within this window count as one shake
#define CONTRAST           40        // screen brightness 0-255 (lower = longer battery life)
#if DESK_TEST
  #define BATT_WARN_MV     0
  #define BATT_EMPTY_MV    0
#else
  #define BATT_WARN_MV     3450      // below this a battery warning is shown on wake-up
  #define BATT_EMPTY_MV    3300      // below this no face, only "Charge me"
#endif
#define CHARGE_OFFSET_MV   150       // the voltage reads high while charging; subtract this for the % estimate
#define VCC_CONSTANT       1125300L  // battery reading calibration, see README

// Weights: a bigger number makes it appear more often. 0 = never.
//                                NEUTRAL HAPPY V_HAPPY SURPRISED SUSPICIOUS GRUMPY SAD SLEEPY HEART WINK MESSAGE
const uint8_t WEIGHTS[] PROGMEM = {   22,    18,     9,       7,         7,       5,   4,     4,    8,    8,     8 };

// Messages shown now and then. Up to ~45 characters each.
// Turkish letters (ç ğ ı İ ö ş ü) are allowed and shown as plain letters.
// If you add a message, add it to MESSAGES as well.
const char m0[] PROGMEM = "I love you";
const char m1[] PROGMEM = "So glad you exist";
const char m2[] PROGMEM = "You look great today";
const char m3[] PROGMEM = "Smile :)";
const char m4[] PROGMEM = "<3";
const char* const MESSAGES[] PROGMEM = { m0, m1, m2, m3, m4 };
// ================================================================

#define MOTION_PIN  2   // PD2 / PCINT18
#define BUTTON_PIN  3   // PD3 / PCINT19
#define CHARGE_PIN  4   // PD4: TP4056 CHRG (LOW while charging)
#define EE_COUNTER  0   // EEPROM: boot counter (random seed)
#define EE_MODE     2   // EEPROM: always-on mode

enum { PICK_HEART = EXPRESSION_COUNT, PICK_WINK, PICK_MESSAGE, PICK_COUNT };
static_assert(sizeof(WEIGHTS) == PICK_COUNT, "WEIGHTS needs 11 numbers");
const uint8_t MESSAGE_COUNT = sizeof(MESSAGES) / sizeof(MESSAGES[0]);

enum { SPECIAL_NONE, SPECIAL_HEART, SPECIAL_DIZZY, SPECIAL_ZZZ, SPECIAL_BATTERY, SPECIAL_MESSAGE };
enum { WAKE_BUTTON, WAKE_MOTION, WAKE_CHARGER };

Adafruit_SSD1306 display(128, 64, &Wire, -1);

volatile uint8_t motionCount = 0;
volatile bool buttonFlag = false;
volatile bool wdtFlag = false;

uint8_t seenMotion = 0;
uint32_t sleptMs = 0;          // time spent in power-down while awake (millis() stops then)
uint32_t lastShakeMs = 0;      // time of the last shake (in now() units)
Face cur;
uint8_t lastExpression = NEUTRAL, lastPick = NEUTRAL;
uint8_t blinkL = 0, blinkR = 0;
uint8_t special = SPECIAL_NONE, phase = 0, heartBeat = 0, batteryPct = 100;
char messageBuf[48];
bool alwaysOn = false;

// ---------------- Interrupts ----------------
ISR(PCINT2_vect) {
  if (!(PIND & _BV(PD3))) buttonFlag = true;   // button pressed
  else motionCount++;                          // vibration (or button released, harmless)
}

ISR(WDT_vect) { wdtFlag = true; }

// ---------------- Power management ----------------
uint32_t now() { return millis() + sleptMs; }

// wdtCode < 0: wake only on a pin change (lowest current)
void powerDown(int8_t wdtCode) {
  ADCSRA &= ~_BV(ADEN);
  if (wdtCode >= 0) {
    uint8_t bits = (wdtCode & 7) | ((wdtCode & 8) ? _BV(WDP3) : 0);
    cli();
    MCUSR &= ~_BV(WDRF);
    WDTCSR = _BV(WDCE) | _BV(WDE);
    WDTCSR = _BV(WDIE) | bits;
    wdt_reset();
    sei();
  }
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_enable();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  sleep_disable();
  if (wdtCode >= 0) wdt_disable();
}

// Is the charger plugged in and charging? (pull-up only on while reading, so no current flows during sleep)
bool isCharging() {
  PORTD |= _BV(PD4);
  delayMicroseconds(30);
  return !(PIND & _BV(PD4));
}
void chargePullupOff() { PORTD &= ~_BV(PD4); }

bool eventPending() { return buttonFlag || motionCount != seenMotion; }

// Wait with the CPU asleep between events while the screen stays on; returns early on an event
void idle(uint32_t ms) {
  static const uint16_t STEP_MS[] PROGMEM = { 8000, 4000, 2000, 1000, 500, 250, 128, 64, 32, 16 };
  static const int8_t STEP_WDT[] PROGMEM = { WDTO_8S, WDTO_4S, WDTO_2S, WDTO_1S, WDTO_500MS,
                                            WDTO_250MS, WDTO_120MS, WDTO_60MS, WDTO_30MS, WDTO_15MS };
  while (ms >= 16 && !eventPending()) {
    uint8_t i = 0;
    while (pgm_read_word(&STEP_MS[i]) > ms) i++;
    // right after a shake use short steps so the clock stays accurate (for dizzy detection)
    if (now() - lastShakeMs < 2000) i = 9;
    uint16_t step = pgm_read_word(&STEP_MS[i]);
    wdtFlag = false;
    powerDown((int8_t)pgm_read_byte(&STEP_WDT[i]));
    if (!wdtFlag) break;             // woken by motion or the button
    sleptMs += step;
    ms -= step;
  }
}

// Battery voltage in mV, measured against the internal 1.1 V reference (no extra parts)
uint16_t readVcc() {
  ADCSRA |= _BV(ADEN);
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  for (uint8_t i = 0; i < 2; i++) {
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC));
  }
  uint16_t adc = ADC;
  ADCSRA &= ~_BV(ADEN);
  return adc ? VCC_CONSTANT / adc : 0;
}

uint8_t batteryPercent(uint16_t mv) {
  if (mv <= 3300) return 0;
  if (mv >= 4150) return 100;
  return (uint32_t)(mv - 3300) * 100 / 850;
}

// ---------------- Display ----------------
void screenOn() {
  display.ssd1306_command(0x8D);   // charge pump on
  display.ssd1306_command(0x14);
  display.ssd1306_command(SSD1306_DISPLAYON);
}

void screenOff() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  display.ssd1306_command(0x8D);   // charge pump off: a few uA while sleeping
  display.ssd1306_command(0x10);
}

void render() {
  display.clearDisplay();
  switch (special) {
    case SPECIAL_HEART:   drawHeartFace(display, heartBeat); break;
    case SPECIAL_DIZZY:   drawDizzy(display, phase); break;
    case SPECIAL_ZZZ:     drawFace(display, cur, 0, 0); drawZzz(display, phase); break;
    case SPECIAL_BATTERY: drawBattery(display, batteryPct); break;
    case SPECIAL_MESSAGE: drawMessage(display, messageBuf); break;
    default:              drawFace(display, cur, blinkL, blinkR); break;
  }
  display.display();
}

// ---------------- Animations ----------------
void morphTo(const Face& target, uint16_t durationMs) {
  Face from = cur;
  uint32_t t0 = millis();
  for (;;) {
    uint32_t e = millis() - t0;
    if (e >= durationMs) break;
    cur = blendFaces(from, target, (uint32_t)e * 256 / durationMs);
    render();
  }
  cur = target;
  render();
}

void showExpression(uint8_t i) {
  lastExpression = i;
  morphTo(getExpression(i), 280);
}

void openEyes() {
  static const uint8_t STEPS[] = { 100, 85, 60, 30, 10, 0 };
  for (uint8_t i = 0; i < sizeof(STEPS); i++) { blinkL = blinkR = STEPS[i]; render(); }
}

void blink(bool rightOnly) {
  static const uint8_t STEPS[] = { 45, 100, 100, 55, 15, 0 };
  for (uint8_t i = 0; i < sizeof(STEPS); i++) {
    blinkR = STEPS[i];
    if (!rightOnly) blinkL = STEPS[i];
    render();
    if (rightOnly && i == 2) delay(180);   // hold the wink
  }
  blinkL = blinkR = 0;
}

void lookAround() {
  Face f = cur;
  if (random(100) < 35) {            // back to centre
    Face base = getExpression(lastExpression);
    f.lookX = base.lookX;
    f.lookY = base.lookY;
  } else {
    f.lookX = random(-90, 91);
    f.lookY = random(-60, 61);
  }
  morphTo(f, 150);
}

void heartAnimation() {
  special = SPECIAL_HEART;
  uint32_t t0 = millis();
  while (millis() - t0 < 3000 && !buttonFlag) {
    uint16_t p = (millis() - t0) % 800;      // double-beat heartbeat
    if (p < 120)      heartBeat = p * 100 / 120;
    else if (p < 240) heartBeat = (240 - p) * 100 / 120;
    else if (p < 360) heartBeat = (p - 240) * 70 / 120;
    else if (p < 480) heartBeat = (480 - p) * 70 / 120;
    else              heartBeat = 0;
    render();
  }
  special = SPECIAL_NONE;
  lastExpression = HAPPY;
  cur = getExpression(HAPPY);
  openEyes();
}

void dizzyAnimation() {
  special = SPECIAL_DIZZY;
  for (phase = 0; phase < 70; phase++) render();      // ~2 s
  special = SPECIAL_NONE;
  lastExpression = NEUTRAL;
  cur = getExpression(NEUTRAL);
  openEyes();
}

// Fold Turkish UTF-8 letters to plain ASCII (the built-in font has no accents)
char foldUtf8(uint8_t a, uint8_t b) {
  if (a == 0xC3) switch (b) { case 0xA7: return 'c'; case 0x87: return 'C';
                              case 0xB6: return 'o'; case 0x96: return 'O';
                              case 0xBC: return 'u'; case 0x9C: return 'U';
                              case 0xA2: return 'a'; case 0xAE: return 'i'; case 0xBB: return 'u'; }
  if (a == 0xC4) switch (b) { case 0x9F: return 'g'; case 0x9E: return 'G';
                              case 0xB1: return 'i'; case 0xB0: return 'I'; }
  if (a == 0xC5) switch (b) { case 0x9F: return 's'; case 0x9E: return 'S'; }
  return '?';
}

void loadMessage(uint8_t i) {
  const char* p = (const char*)pgm_read_ptr(&MESSAGES[i]);
  uint8_t n = 0, c;
  while ((c = pgm_read_byte(p++)) && n < sizeof(messageBuf) - 1) {
    if (c == 0xC3 || c == 0xC4 || c == 0xC5) {
      uint8_t c2 = pgm_read_byte(p);
      if (!c2) break;
      p++;
      messageBuf[n++] = foldUtf8(c, c2);
    } else if (c < 128) {
      messageBuf[n++] = c;           // other non-ASCII (emoji etc.) is dropped
    }
  }
  messageBuf[n] = 0;
}

void showMessage() {
  loadMessage(random(MESSAGE_COUNT));
  special = SPECIAL_MESSAGE;
  render();
  idle(3500);
  special = SPECIAL_NONE;
  lastExpression = HAPPY;
  cur = getExpression(HAPPY);
  openEyes();
}

void nextExpression() {
  uint16_t total = 0;
  for (uint8_t i = 0; i < PICK_COUNT; i++) total += pgm_read_byte(&WEIGHTS[i]);
  if (!total) return;
  uint8_t s = NEUTRAL;
  for (uint8_t tries = 0; tries < 4; tries++) {   // avoid the same pick twice in a row
    uint16_t r = random(total);
    for (s = 0; s < PICK_COUNT - 1; s++) {
      uint8_t w = pgm_read_byte(&WEIGHTS[s]);
      if (r < w) break;
      r -= w;
    }
    if (s != lastPick) break;
  }
  lastPick = s;

  if (s < EXPRESSION_COUNT) {
    showExpression(s);
  } else if (s == PICK_HEART) {
    heartAnimation();
  } else if (s == PICK_WINK) {
    if (lastExpression != HAPPY && lastExpression != VERY_HAPPY) showExpression(HAPPY);
    delay(250);
    blink(true);
  } else {
    showMessage();
  }
}

void fallAsleep() {
  lastExpression = SLEEPY;
  morphTo(getExpression(SLEEPY), 700);
  special = SPECIAL_ZZZ;
  for (phase = 0; phase < 24; phase++) { render(); delay(50); }
  delay(500);
  special = SPECIAL_NONE;
  screenOff();
}

void batteryEmpty() {
  special = SPECIAL_BATTERY;
  batteryPct = 0;
  render();
  delay(2500);
  special = SPECIAL_NONE;
  screenOff();
}

bool longPress() {
  uint32_t t0 = millis();
  while (!(PIND & _BV(PD3))) {
    if (millis() - t0 > 1500) return true;
    delay(10);
  }
  return false;
}

void toggleMode() {
  alwaysOn = !alwaysOn;
  EEPROM.update(EE_MODE, alwaysOn ? 1 : 0);
  strcpy_P(messageBuf, alwaysOn ? PSTR("Always on") : PSTR("Auto sleep"));
  special = SPECIAL_MESSAGE;
  render();
  while (!(PIND & _BV(PD3))) delay(10);      // wait for release
  delay(1200);
  special = SPECIAL_NONE;
  buttonFlag = false;
  openEyes();
}

// ---------------- Charging screen (only if D4 is wired) ----------------
void chargingMode() {
  screenOn();
  lastExpression = VERY_HAPPY;              // happy to be plugged in
  cur = getExpression(VERY_HAPPY);
  openEyes();
  idle(1200);
  uint8_t f = 0;
  uint32_t nextRead = 0;
  uint16_t mv = readVcc();
  while (isCharging()) {
    if ((int32_t)(now() - nextRead) >= 0) {
      mv = readVcc();
      nextRead = now() + 5000;
    }
    uint8_t pct = batteryPercent(mv > CHARGE_OFFSET_MV ? mv - CHARGE_OFFSET_MV : 0);
    if (pct > 99) pct = 99;
    display.clearDisplay();
    drawCharging(display, pct, f++, false);
    display.display();
    idle(300);
    buttonFlag = false;
    seenMotion = motionCount;
  }
  // charging finished or cable unplugged
  mv = readVcc();
  if (mv >= 4100) {
    display.clearDisplay();
    drawCharging(display, 100, 0, true);
    display.display();
    idle(3000);
    heartAnimation();
  } else {
    lastExpression = HAPPY;
    cur = getExpression(HAPPY);
    openEyes();
  }
  buttonFlag = false;
  seenMotion = motionCount;
}

// ---------------- One awake session ----------------
// Returns true if MAX_AWAKE_MS ran out or the battery is empty (cool-down follows)
bool awakeSession(uint8_t reason) {
  buttonFlag = false;
  seenMotion = motionCount;

  if (reason == WAKE_CHARGER || isCharging()) chargingMode();

  uint16_t mv = readVcc();
  batteryPct = batteryPercent(mv);
  screenOn();
  if (mv < BATT_EMPTY_MV && !isCharging()) { batteryEmpty(); return true; }

  lastExpression = (reason == WAKE_MOTION) ? SURPRISED : HAPPY;
  cur = getExpression(lastExpression);
  openEyes();
  if (mv < BATT_WARN_MV) {
    special = SPECIAL_BATTERY; render(); delay(1500);
    special = SPECIAL_NONE; render();
  }
  if (reason == WAKE_MOTION) {              // surprised first, then happy to see you
    idle(1000);
    showExpression(HAPPY);
  }

  uint32_t t = now();
  uint32_t start = t, lastActivity = t, lastDizzy = t - 10000UL;
  uint32_t nextExpr   = t + random(4000, 9000);
  uint32_t nextBlink  = t + random(1500, 4000);
  uint32_t nextLook   = t + random(2000, 5000);
  uint32_t nextBattery = t + 60000UL;
  uint32_t shakeWindow = t;
  uint16_t shakes = 0;

  for (;;) {
    t = now();

    // Vibration: the burst of pulses from one touch counts as a single shake
    if (motionCount != seenMotion) {
      delay(DEBOUNCE_MS);
      seenMotion = motionCount;
      t = now();
      lastActivity = t;
      lastShakeMs = t;
      if (t - shakeWindow > 1000) { shakeWindow = t; shakes = 0; }
      shakes++;
      if (shakes >= DIZZY_SHAKES && (int32_t)(t - lastDizzy) > 10000) {
        dizzyAnimation();
        t = now();
        shakes = 0;
        lastDizzy = t;
        nextExpr = t + random(5000, 10000);
        seenMotion = motionCount;
        continue;
      }
    }

    // Charger plugged in?
    if (isCharging()) {
      chargingMode();
      t = now();
      start = lastActivity = t;
      nextExpr = t + random(4000, 9000);
      continue;
    }

    // Button
    if (buttonFlag) {
      buttonFlag = false;
      if (longPress()) toggleMode();
      else nextExpression();
      t = now();
      lastActivity = t;
      nextExpr = t + random(6000, 14000);
      seenMotion = motionCount;
      continue;
    }

    // Time to sleep?
    if (!alwaysOn) {
      if ((int32_t)(t - lastActivity) >= (int32_t)AWAKE_MS) { fallAsleep(); return false; }
      if ((int32_t)(t - start) >= (int32_t)MAX_AWAKE_MS)    { fallAsleep(); return true; }
    }

    // Battery check (matters in always-on mode)
    if ((int32_t)(t - nextBattery) >= 0) {
      mv = readVcc();
      batteryPct = batteryPercent(mv);
      if (mv < BATT_EMPTY_MV) { batteryEmpty(); return true; }
      nextBattery = t + 60000UL;
    }

    // Scheduled events
    if ((int32_t)(t - nextExpr) >= 0) {
      nextExpression();
      t = now();
      nextExpr  = t + random(6000, 15000);
      nextBlink = t + random(1500, 4000);
      nextLook  = t + random(2000, 5000);
      continue;
    }
    if ((int32_t)(t - nextBlink) >= 0) {
      blink(false);
      if (random(100) < 20) blink(false);      // sometimes a double blink
      nextBlink = now() + random(2000, 6000);
      continue;
    }
    if ((int32_t)(t - nextLook) >= 0) {
      lookAround();
      nextLook = now() + random(1500, 5000);
      continue;
    }

    // Sleep the CPU until the next event
    uint32_t next = nextExpr;
    if ((int32_t)(nextBlink   - next) < 0) next = nextBlink;
    if ((int32_t)(nextLook    - next) < 0) next = nextLook;
    if ((int32_t)(nextBattery - next) < 0) next = nextBattery;
    if (!alwaysOn) {
      uint32_t u1 = lastActivity + AWAKE_MS, u2 = start + MAX_AWAKE_MS;
      if ((int32_t)(u1 - next) < 0) next = u1;
      if ((int32_t)(u2 - next) < 0) next = u2;
    }
    if ((int32_t)(next - t) > 0) idle(next - t);
  }
}

// ---------------- Deep sleep (screen off) ----------------
uint8_t deepSleep(bool cooldown) {
  chargePullupOff();
  if (cooldown) {
    PCMSK2 = _BV(PCINT19);                   // only the button may wake it
    buttonFlag = false;
    for (uint32_t left = COOLDOWN_MS; left >= 8000 && !buttonFlag; ) {
      wdtFlag = false;
      powerDown(WDTO_8S);
      if (wdtFlag) {
        left -= 8000; sleptMs += 8000;
        bool c = isCharging(); chargePullupOff();
        if (c) { PCMSK2 = _BV(PCINT18) | _BV(PCINT19); return WAKE_CHARGER; }
      }
    }
    PCMSK2 = _BV(PCINT18) | _BV(PCINT19);
    if (buttonFlag) return WAKE_BUTTON;
  }

  for (;;) {
    buttonFlag = false;
    seenMotion = motionCount;
    wdtFlag = false;
    powerDown(WDTO_8S);                      // motion/button, or a charger check every 8 s
    if (buttonFlag) return WAKE_BUTTON;
    if (wdtFlag) {
      sleptMs += 8000;
      bool c = isCharging(); chargePullupOff();
      if (c) return WAKE_CHARGER;
      continue;
    }

    // First shake arrived. If WAKE_SHAKES > 1, wait up to 1 s for more.
    uint8_t count = 1;
    delay(DEBOUNCE_MS);
    seenMotion = motionCount;
    uint32_t t0 = millis();
    while (count < WAKE_SHAKES && millis() - t0 < 1000) {
      if (buttonFlag) return WAKE_BUTTON;
      if (motionCount != seenMotion) {
        count++;
        delay(DEBOUNCE_MS);
        seenMotion = motionCount;
      }
    }
    if (buttonFlag) return WAKE_BUTTON;
    if (count >= WAKE_SHAKES) { lastShakeMs = now(); return WAKE_MOTION; }
  }
}

// ---------------- Setup ----------------
void setup() {
  // floating pins draw current: pull-ups on all of them
  for (uint8_t p = 0; p <= A3; p++) pinMode(p, INPUT_PULLUP);
  pinMode(13, OUTPUT);                       // no current through the LED on D13
  digitalWrite(13, LOW);
  ACSR = _BV(ACD);                           // analog comparator off
  power_spi_disable();
  power_timer1_disable();
  power_timer2_disable();
  power_usart0_disable();

  alwaysOn = EEPROM.read(EE_MODE) == 1;
  uint16_t boots;
  EEPROM.get(EE_COUNTER, boots);
  boots++;
  EEPROM.put(EE_COUNTER, boots);
  randomSeed(boots * 7919UL + readVcc());

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;) powerDown(-1);                  // no display found: check the OLED wiring
  }
  digitalWrite(SDA, LOW);                    // internal pull-ups off (the OLED module has its own;
  digitalWrite(SCL, LOW);                    //  leaving them on wastes current in sleep)
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(CONTRAST);
  display.setTextWrap(false);

  PCICR |= _BV(PCIE2);
  PCMSK2 = _BV(PCINT18) | _BV(PCINT19);
}

uint8_t wakeReason = WAKE_BUTTON;

void loop() {
  bool cooldown = awakeSession(wakeReason);
  wakeReason = deepSleep(cooldown);
}
