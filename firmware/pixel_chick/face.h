/*
  face.h - pixel face drawing for a 128x64 SSD1306 OLED.

  Everything here only draws into an Adafruit_GFX canvas, so the same file
  is used on the Arduino and on a PC to render preview images.
  To change how an expression looks, edit the EXPRESSIONS table below.
*/
#pragma once
#include <Adafruit_GFX.h>
#include <string.h>

#if defined(__AVR__)
  #include <avr/pgmspace.h>
#else
  #ifndef PROGMEM
    #define PROGMEM
  #endif
  #ifndef memcpy_P
    #define memcpy_P memcpy
  #endif
#endif

#define PX_ON 1
#define PX_OFF 0

// ---- Screen layout ----
#define EYE_Y       26   // vertical centre of the eyes
#define EYE_GAP     16   // gap between the two eyes
#define MOUTH_Y     53   // vertical position of the mouth
#define LOOK_X      14   // how far (px) the eyes move when looking sideways
#define LOOK_Y      7

enum { MOUTH_NONE, MOUTH_FLAT, MOUTH_SMILE, MOUTH_O, MOUTH_SAD, MOUTH_CAT };

struct Face {
  uint8_t eyeW;    // eye width (px)
  uint8_t eyeHL;   // left eye height (px)
  uint8_t eyeHR;   // right eye height (px)
  int8_t  lookX;   // -100 (left) .. 100 (right)
  int8_t  lookY;   // -100 (up) .. 100 (down)
  uint8_t angry;   // 0..100  inner top corners drop (frown)
  uint8_t sad;     // 0..100  outer top corners drop (puppy eyes)
  uint8_t happy;   // 0..100  lower lid rises (smiling eyes ^ ^)
  uint8_t mouth;   // MOUTH_*
};

enum Expression { NEUTRAL, HAPPY, VERY_HAPPY, SURPRISED, SUSPICIOUS, GRUMPY, SAD, SLEEPY, EXPRESSION_COUNT };

const Face EXPRESSIONS[EXPRESSION_COUNT] PROGMEM = {
  // eyeW eyeHL eyeHR lookX lookY angry sad happy mouth
  { 28,  30,   30,    0,    0,    0,   0,   0, MOUTH_FLAT  },  // NEUTRAL
  { 28,  30,   30,    0,    0,    0,   0,  50, MOUTH_SMILE },  // HAPPY
  { 30,  32,   32,    0,  -20,    0,   0,  70, MOUTH_CAT   },  // VERY_HAPPY
  { 26,  34,   34,    0,  -15,    0,   0,   0, MOUTH_O     },  // SURPRISED
  { 28,  14,   26,   45,    0,   30,   0,   0, MOUTH_FLAT  },  // SUSPICIOUS (one eye squinting, looking aside)
  { 28,  26,   26,    0,   15,   75,   0,   0, MOUTH_SAD   },  // GRUMPY (cute pout)
  { 26,  26,   26,    0,   30,    0,  70,   0, MOUTH_SAD   },  // SAD
  { 28,   8,    8,    0,   40,    0,  25,   0, MOUTH_NONE  },  // SLEEPY
};

inline Face getExpression(uint8_t i) {
  Face f;
  memcpy_P(&f, &EXPRESSIONS[i], sizeof(Face));
  return f;
}

inline int16_t min16(int16_t a, int16_t b) { return a < b ? a : b; }

// Smooth blend between two faces; t: 0..256
inline Face blendFaces(const Face& a, const Face& b, uint16_t t) {
  if (t > 256) t = 256;
  uint16_t e = (uint32_t)t * t * (768 - 2 * t) / 65536;   // smoothstep
  Face r;
  r.eyeW  = a.eyeW  + ((int16_t)b.eyeW  - a.eyeW)  * (int16_t)e / 256;
  r.eyeHL = a.eyeHL + ((int16_t)b.eyeHL - a.eyeHL) * (int16_t)e / 256;
  r.eyeHR = a.eyeHR + ((int16_t)b.eyeHR - a.eyeHR) * (int16_t)e / 256;
  r.lookX = a.lookX + ((int16_t)b.lookX - a.lookX) * (int16_t)e / 256;
  r.lookY = a.lookY + ((int16_t)b.lookY - a.lookY) * (int16_t)e / 256;
  r.angry = a.angry + ((int16_t)b.angry - a.angry) * (int16_t)e / 256;
  r.sad   = a.sad   + ((int16_t)b.sad   - a.sad)   * (int16_t)e / 256;
  r.happy = a.happy + ((int16_t)b.happy - a.happy) * (int16_t)e / 256;
  r.mouth = e < 128 ? a.mouth : b.mouth;
  return r;
}

inline void drawEye(Adafruit_GFX& g, int16_t x, int16_t cy, int16_t w, int16_t h,
                    uint8_t angry, uint8_t sad, uint8_t happy, bool innerOnRight) {
  if (h <= 4) {                                   // closed eye: thin line
    g.fillRect(x + 1, cy - 1, w - 2, 3, PX_ON);
    return;
  }
  int16_t y = cy - h / 2;
  int16_t r = min16(min16(w, h) / 4 + 2, 9);
  g.fillRoundRect(x, y, w, h, r, PX_ON);

  if (angry) {                                    // inner top corner goes down
    int16_t d = (int32_t)h * angry / 150;
    if (innerOnRight) g.fillTriangle(x - 1, y - 1, x + w, y - 1, x + w, y + d, PX_OFF);
    else              g.fillTriangle(x - 1, y - 1, x + w, y - 1, x - 1, y + d, PX_OFF);
  }
  if (sad) {                                      // outer top corner goes down
    int16_t d = (int32_t)h * sad / 150;
    if (innerOnRight) g.fillTriangle(x - 1, y - 1, x + w, y - 1, x - 1, y + d, PX_OFF);
    else              g.fillTriangle(x - 1, y - 1, x + w, y - 1, x + w, y + d, PX_OFF);
  }
  if (happy) {                                    // lower lid: dome-shaped black cover
    int16_t d  = (int32_t)h * happy / 140;
    int16_t rb = (w + 4) / 2;
    g.fillRoundRect(x - 2, y + h - d, w + 4, 2 * rb + 2, rb, PX_OFF);
  }
}

inline void drawMouth(Adafruit_GFX& g, uint8_t type, int16_t mx, int16_t my) {
  switch (type) {
    case MOUTH_FLAT:
      g.fillRect(mx - 6, my, 12, 2, PX_ON);
      break;
    case MOUTH_SMILE:
      for (int8_t dx = -7; dx <= 7; dx++) g.fillRect(mx + dx, my - 1 + (49 - dx * dx) / 14, 1, 2, PX_ON);
      break;
    case MOUTH_O:
      g.fillCircle(mx, my + 1, 4, PX_ON);
      g.fillCircle(mx, my + 1, 2, PX_OFF);
      break;
    case MOUTH_SAD:
      for (int8_t dx = -7; dx <= 7; dx++) g.fillRect(mx + dx, my - 1 + dx * dx / 14, 1, 2, PX_ON);
      break;
    case MOUTH_CAT:                               // ω
      for (int8_t dx = -3; dx <= 3; dx++) {
        int8_t dy = (9 - dx * dx) / 3;
        g.fillRect(mx - 3 + dx, my - 1 + dy, 1, 2, PX_ON);
        g.fillRect(mx + 3 + dx, my - 1 + dy, 1, 2, PX_ON);
      }
      break;
  }
}

// blinkL / blinkR: 0 = open, 100 = fully closed
inline void drawFace(Adafruit_GFX& g, const Face& f, uint8_t blinkL, uint8_t blinkR) {
  int16_t ox = (int16_t)f.lookX * LOOK_X / 100;
  int16_t oy = (int16_t)f.lookY * LOOK_Y / 100;
  // the eye on the side being looked at shrinks a little (fake 3D)
  int16_t hL = (int32_t)f.eyeHL * (800 + f.lookX) / 800;
  int16_t hR = (int32_t)f.eyeHR * (800 - f.lookX) / 800;
  hL = (int32_t)hL * (100 - blinkL) / 100;
  hR = (int32_t)hR * (100 - blinkR) / 100;
  int16_t w  = f.eyeW;
  int16_t xL = 64 - EYE_GAP / 2 - w + ox;
  int16_t xR = 64 + EYE_GAP / 2 + ox;
  int16_t cy = EYE_Y + oy;
  drawEye(g, xL, cy, w, hL, f.angry, f.sad, f.happy, true);
  drawEye(g, xR, cy, w, hR, f.angry, f.sad, f.happy, false);
  drawMouth(g, f.mouth, 64 + ox / 2, MOUTH_Y + oy / 2);
}

inline void drawHeart(Adafruit_GFX& g, int16_t cx, int16_t cy, int16_t r) {
  g.fillCircle(cx - r, cy - r / 2, r, PX_ON);
  g.fillCircle(cx + r, cy - r / 2, r, PX_ON);
  g.fillTriangle(cx - 2 * r + 1, cy - r / 2 + r / 3, cx + 2 * r - 1, cy - r / 2 + r / 3,
                 cx, cy + r + r / 2 + 2, PX_ON);
}

// Heart eyes; beat 0..100 is how big the heartbeat is
inline void drawHeartFace(Adafruit_GFX& g, uint8_t beat) {
  int16_t r = 8 + beat * 2 / 100;
  drawHeart(g, 64 - EYE_GAP / 2 - 14, EYE_Y - 1, r);
  drawHeart(g, 64 + EYE_GAP / 2 + 14, EYE_Y - 1, r);
  drawMouth(g, MOUTH_SMILE, 64, MOUTH_Y);
}

// When shaken hard: X X eyes + wobbly mouth; phase increases by 1 per frame
inline void drawDizzy(Adafruit_GFX& g, uint8_t phase) {
  int8_t t = phase % 8;
  int16_t s = t < 4 ? t - 2 : 6 - t;                 // -2..2 wobble
  int16_t cx[2] = { (int16_t)(64 - EYE_GAP / 2 - 14 + s), (int16_t)(64 + EYE_GAP / 2 + 14 + s) };
  for (uint8_t i = 0; i < 2; i++) {
    for (int8_t k = -1; k <= 1; k++) {
      g.drawLine(cx[i] - 9 + k, EYE_Y - 9, cx[i] + 9 + k, EYE_Y + 9, PX_ON);
      g.drawLine(cx[i] - 9 + k, EYE_Y + 9, cx[i] + 9 + k, EYE_Y - 9, PX_ON);
    }
  }
  for (int8_t dx = -10; dx <= 10; dx++) {
    uint8_t v = (uint8_t)(dx + 16 + phase) & 7;
    int8_t dy = v < 4 ? v : 8 - v;
    g.fillRect(64 + s + dx, MOUTH_Y - 2 + dy, 1, 2, PX_ON);
  }
}

// z's that appear one by one while falling asleep
inline void drawZzz(Adafruit_GFX& g, uint8_t phase) {
  uint8_t count = (phase / 6) % 4;
  g.setTextColor(PX_ON);
  g.setTextSize(1);
  if (count >= 1) { g.setCursor(102, 15); g.print('z'); }
  if (count >= 2) { g.setCursor(110, 8);  g.print('z'); }
  if (count >= 3) { g.setCursor(118, 1);  g.print('Z'); }
}

inline void drawBattery(Adafruit_GFX& g, uint8_t percent) {
  g.drawRect(40, 16, 44, 24, PX_ON);
  g.drawRect(41, 17, 42, 22, PX_ON);
  g.fillRect(84, 23, 4, 10, PX_ON);
  g.fillRect(44, 20, (int16_t)36 * percent / 100, 16, PX_ON);
  if (percent < 25) {
    g.setTextSize(1);
    g.setTextColor(PX_ON);
    g.setCursor(34, 50);
    g.print("Charge me");
  }
}

// Charging screen: filling battery + lightning bolt + approximate percentage.
// phase increases by 1 per frame.
inline void drawCharging(Adafruit_GFX& g, uint8_t percent, uint8_t phase, bool full) {
  const int16_t X = 30, Y = 10, W = 70, H = 30;
  g.drawRoundRect(X, Y, W, H, 4, PX_ON);
  g.drawRoundRect(X + 1, Y + 1, W - 2, H - 2, 3, PX_ON);
  g.fillRect(X + W, Y + 9, 4, 12, PX_ON);
  // 5 segments; filled ones stay on, the next one blinks while charging
  uint8_t filled = full ? 5 : (uint8_t)(percent / 20);
  if (filled > 5) filled = 5;
  for (uint8_t i = 0; i < 5; i++) {
    bool on = i < filled || (!full && i == filled && (phase & 2));
    if (on) g.fillRect(X + 4 + i * 13, Y + 4, 10, H - 8, PX_ON);
  }
  // lightning bolt (left of the battery)
  g.fillTriangle(14, 10, 6, 27, 14, 25, PX_ON);
  g.fillTriangle(12, 24, 20, 22, 10, 40, PX_ON);
  g.setTextColor(PX_ON);
  g.setTextWrap(false);
  g.setTextSize(2);
  if (full) {
    g.setCursor(34, 47);
    g.print("Full<3");
  } else {
    int16_t w = (percent >= 10 ? 2 : 1) * 12;
    g.setCursor(64 - (w + 12) / 2, 47);
    g.print((int)percent);
    g.print('%');
  }
}

// Centred text that does not split words (non-ASCII letters must be folded first)
inline void drawMessage(Adafruit_GFX& g, const char* s) {
  uint8_t len = strlen(s);
  uint8_t start[8], lineLen[8], n = 0;
  uint8_t size = 2;
  for (;;) {
    uint8_t cols = size == 2 ? 10 : 21, i = 0;
    n = 0;
    while (i < len && n < 8) {
      while (i < len && s[i] == ' ') i++;
      if (i >= len) break;
      uint8_t L = len - i;
      if (L > cols) {
        L = cols;
        while (L > 0 && s[i + L] != ' ') L--;
        if (L == 0) L = cols;
      }
      start[n] = i; lineLen[n] = L; n++;
      i += L;
    }
    if (size == 2 && n > 3) { size = 1; continue; }
    break;
  }
  uint8_t h = 8 * size + (size == 2 ? 3 : 1);
  int16_t y0 = (64 - (int16_t)n * h) / 2;
  g.setTextSize(size);
  g.setTextColor(PX_ON);
  g.setTextWrap(false);
  for (uint8_t k = 0; k < n; k++) {
    g.setCursor((128 - (int16_t)lineLen[k] * 6 * size) / 2, y0 + k * h);
    for (uint8_t j = 0; j < lineLen[k]; j++) g.write(s[start[k] + j]);
  }
}
