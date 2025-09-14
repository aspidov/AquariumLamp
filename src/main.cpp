#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ------------------- CONFIG -------------------
#define DIM_STRIP_PIN 4   // regular dimmable LED strip
#define WS1_PIN 17        // first WS2812 strip
#define WS2_PIN 18        // second WS2812 strip

#define WS1_COUNT 15
#define WS2_COUNT 15

// ------------------- OBJECTS -------------------
Adafruit_NeoPixel strip1(WS1_COUNT, WS1_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(WS2_COUNT, WS2_PIN, NEO_GRB + NEO_KHZ800);

// ------------------- STATE VARIABLES -------------------
// Master breathing envelope (used for both addressable strips and the dimmable strip)
unsigned long lastBreathUpdate = 0;
const int breathInterval = 20; // ms per envelope update
const int breathPeriod = 3000; // ms for a full breath in/out cycle

// Hue animation for addressable strips
unsigned long lastHueUpdate = 0;
const int hueInterval = 30; // ms per hue step
int hueOffset = 0;

// PWM (dimmable) base brightness (0-255) before breathing is applied
const int pwmBase = 255; // max base, breathing scales this

// Blink behavior for addressable strips
unsigned long lastBlinkEvent = 0;
unsigned long blinkStart = 0;
bool blinkActive = false;
int nextBlinkInterval = 5000; // ms until next blink (randomized)
const int blinkIntervalBase = 5000; // base interval ms
const int blinkJitter = 2000; // +/- jitter
const int blinkDuration = 200; // ms blink length

// ------------------- HELPERS -------------------
uint32_t Wheel(byte WheelPos) {
  if (WheelPos < 85) {
    return Adafruit_NeoPixel::Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return Adafruit_NeoPixel::Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
    WheelPos -= 170;
    return Adafruit_NeoPixel::Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

// Scale a 24-bit color by an 0-255 scale (0 = off, 255 = full)
uint32_t scaleColor(uint32_t color, uint8_t scale) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  uint8_t rs = (uint16_t(r) * scale) / 255;
  uint8_t gs = (uint16_t(g) * scale) / 255;
  uint8_t bs = (uint16_t(b) * scale) / 255;
  return Adafruit_NeoPixel::Color(rs, gs, bs);
}

// ------------------- SETUP -------------------
void setup() {
  // PWM setup for dimmable strip
  ledcSetup(0, 5000, 8);
  ledcAttachPin(DIM_STRIP_PIN, 0);

  // WS2812 setup
  strip1.begin();
  strip1.show();
  strip2.begin();
  strip2.show();

  // seed RNG for blink timing jitter
  randomSeed(esp_random());
}

// ------------------- LOOP -------------------
void loop() {
  unsigned long now = millis();

  // --- Update master breathing envelope ---
  if (now - lastBreathUpdate >= breathInterval) {
    lastBreathUpdate = now;

    // Breath position in [0..1]
    static unsigned long breathStart = millis();
    unsigned long t = (now - breathStart) % breathPeriod;
    // use a smooth sinusoidal envelope: 0..1
    float phase = (float)t / (float)breathPeriod; // 0..1
    float env = (sinf(phase * 2.0f * M_PI) + 1.0f) * 0.5f; // 0..1
    uint8_t env8 = uint8_t(env * 255.0f);

    // Apply to PWM dimmable strip (scale pwmBase by envelope)
    int pwmVal = (uint16_t(pwmBase) * env8) / 255;
    ledcWrite(0, pwmVal);

    // Blink event scheduling
    if (!blinkActive && (now - lastBlinkEvent >= (unsigned long)nextBlinkInterval)) {
      blinkActive = true;
      blinkStart = now;
      lastBlinkEvent = now;
      // pick next interval with jitter
      nextBlinkInterval = blinkIntervalBase + (int)random(-blinkJitter, blinkJitter);
      if (nextBlinkInterval < 500) nextBlinkInterval = 500;
    }
    if (blinkActive && (now - blinkStart >= (unsigned long)blinkDuration)) {
      blinkActive = false;
    }

    // Update addressable strips colors. Keep them at full brightness normally.
    // If blinkActive, flash to white for the duration.
    if (blinkActive) {
      uint32_t white = strip1.Color(255, 255, 255);
      for (int i = 0; i < strip1.numPixels(); i++) strip1.setPixelColor(i, white);
      strip1.show();
      for (int i = 0; i < strip2.numPixels(); i++) strip2.setPixelColor(i, white);
      strip2.show();
    } else {
      // Normal per-LED animated colors at full brightness
      for (int i = 0; i < strip1.numPixels(); i++) {
        byte hue = (i * 256 / strip1.numPixels() + hueOffset) & 255;
        uint32_t c = Wheel(hue);
        strip1.setPixelColor(i, c);
      }
      strip1.show();

      for (int i = 0; i < strip2.numPixels(); i++) {
        byte hue = (i * 256 / strip2.numPixels() + hueOffset + 64) & 255;
        uint32_t c = Wheel(hue);
        strip2.setPixelColor(i, c);
      }
      strip2.show();
    }
  }

  // --- Update hue animation separately (controls changing colors over time) ---
  if (now - lastHueUpdate >= hueInterval) {
    lastHueUpdate = now;
    hueOffset++;
    if (hueOffset >= 256) hueOffset = 0;
  }
}
