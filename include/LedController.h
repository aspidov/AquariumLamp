#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Shared simple struct for strip state
struct StripState {
  uint8_t brightness;     // 0..255 (for both types)
  uint8_t r, g, b;        // solid color for addressable strips
  bool on;
};

namespace LEDController {
  void initPwm(int pin, int channel, int freq, int res, uint8_t initialDuty);
  void registerStrips(Adafruit_NeoPixel& strip1, Adafruit_NeoPixel& strip2);
  void setPwmDuty(int channel, uint8_t duty);
  void setStripSolid(Adafruit_NeoPixel& strip, const StripState& st);
  void markDirty(int stripIndex);
  void loop(StripState& ws1State, StripState& ws2State);
  
  // Animations for addressable strips (affect both strips together)
  enum class Animation { None = 0, Sunrise, Sunset, Waves, Police, Christmas };
  // Start an animation; durationMs is used for sunrise/sunset (default 30000ms)
  void startAnimation(Animation anim, unsigned long durationMs = 30000);
  void stopAnimation();
  Animation currentAnimation();

  // Readback helpers (report actual hardware state)
  uint8_t getPwmDuty(int channel); // read LEDC duty (0..255)
  // Populate a StripState from the actual hardware for stripIndex (1 or 2).
  // Returns true if strip exists and out was filled.
  bool readStripHardware(int stripIndex, StripState& out);
}
