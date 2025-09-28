#include "LEDController.h"
#include <Adafruit_NeoPixel.h>

namespace LEDController {

// Internal state
static Adafruit_NeoPixel* s_strip1 = nullptr;
static Adafruit_NeoPixel* s_strip2 = nullptr;
static volatile bool s_ws1Dirty = false;
static volatile bool s_ws2Dirty = false;
// Track last-set brightness for strips (0..255). Use 0 as off.
static uint8_t s_ws1Brightness = 0;
static uint8_t s_ws2Brightness = 0;
// Animation state
static LEDController::Animation s_currentAnim = LEDController::Animation::None;
static unsigned long s_animStart = 0;
static unsigned long s_animDur = 0;
// Wave animation phase
static float s_wavePhase = 0.0f;
// Police animation state
static unsigned long s_policeLastToggle = 0;
static bool s_policeBlue = false;
static uint16_t s_policeSegmentSize = 3; // number of pixels per color segment
static uint8_t s_savedPwmDuty = 0;

void initPwm(int pin, int channel, int freq, int res, uint8_t initialDuty)
{
  ledcSetup(channel, freq, res);
  ledcAttachPin(pin, channel);
  ledcWrite(channel, initialDuty);
}

void registerStrips(Adafruit_NeoPixel& strip1, Adafruit_NeoPixel& strip2)
{
  s_strip1 = &strip1;
  s_strip2 = &strip2;
  if (s_strip1) { s_strip1->begin(); s_strip1->clear(); }
  if (s_strip2) { s_strip2->begin(); s_strip2->clear(); }
}

void setPwmDuty(int channel, uint8_t duty)
{
  ledcWrite(channel, duty);
}

void setStripSolid(Adafruit_NeoPixel& strip, const StripState& st)
{
  uint8_t b = st.on ? st.brightness : 0;
  strip.setBrightness(b);
  // remember which strip we updated
  if (&strip == s_strip1) s_ws1Brightness = b;
  if (&strip == s_strip2) s_ws2Brightness = b;
  uint32_t c = strip.Color(st.r, st.g, st.b);
  strip.fill(c, 0, strip.numPixels());
  strip.show();
}

void markDirty(int stripIndex)
{
  if (stripIndex == 1) s_ws1Dirty = true;
  if (stripIndex == 2) s_ws2Dirty = true;
}

void loop(StripState& ws1State, StripState& ws2State)
{
  unsigned long now = millis();

  // Handle animations first (override static color)
  if (s_currentAnim != LEDController::Animation::None) {
    // Combined strips length (left = s_strip2, right = s_strip1)
    uint16_t n2 = s_strip2 ? s_strip2->numPixels() : 0;
    uint16_t n1 = s_strip1 ? s_strip1->numPixels() : 0;
    uint16_t total = n2 + n1;
    unsigned long elapsed = now - s_animStart;
    unsigned long totalDur = max(1UL, s_animDur);
    float overallP = (float)elapsed / (float)totalDur;
    if (overallP > 1.0f) overallP = 1.0f;

    // Helper to map combined index -> strip & pixel. For sunrise/sunset we
    // reuse this to allow directional fill.
    auto mapCombinedToStrip = [&](uint16_t combinedIdx, Adafruit_NeoPixel*& outStrip, uint16_t& outPixel)->void {
      uint16_t logical = combinedIdx;
      if (logical < n2) {
        // Left strip (s_strip2): invert index so combined 0 -> last pixel on left strip
        outStrip = s_strip2;
        outPixel = (n2 > 0) ? (n2 - 1 - logical) : 0;
      } else {
        // Right strip (s_strip1): normal mapping left-to-right inside right strip
        outStrip = s_strip1;
        outPixel = (n1 > 0) ? (logical - n2) : 0;
      }
    };

    if (s_currentAnim == LEDController::Animation::Sunrise) {
      // Two-stage sunrise:
      // - Stage 1 (0.0 .. 0.5 overallP): addressable fill from dark -> red; PWM remains off.
      // - Stage 2 (0.5 .. 1.0 overallP): addressable transition red -> white; PWM fades 0 -> 255.
      if (total > 0) {
        if (overallP < 0.5f) {
          float stageP = overallP * 2.0f; // 0..1 for stage 1
          uint16_t numRed = (uint16_t)ceil(stageP * (float)total);
          uint8_t redIntensity = (uint8_t)min(255, (int)(5 + stageP * 200.0f));
          for (uint16_t ci = 0; ci < total; ++ci) {
            Adafruit_NeoPixel* strip; uint16_t pix;
            mapCombinedToStrip(ci, strip, pix);
            if (!strip) continue;
            if (ci < numRed) {
              strip->setPixelColor(pix, strip->Color(redIntensity, 0, 0));
            } else {
              strip->setPixelColor(pix, 0);
            }
          }
          // keep addressable brightness moderate during red draw
          if (s_strip2) { s_strip2->setBrightness(120); s_strip2->show(); }
          if (s_strip1) { s_strip1->setBrightness(120); s_strip1->show(); }
          // PWM remains off during first stage
          ledcWrite(0, 0);
        } else {
          float stageP = (overallP - 0.5f) * 2.0f; // 0..1 for stage 2
          // Transition addressable LEDs from red -> white
          uint8_t r = (uint8_t)min(255, (int)(150 + stageP * 105.0f));
          uint8_t g = (uint8_t)min(255, (int)(stageP * 255.0f));
          uint8_t b = (uint8_t)min(255, (int)(stageP * 255.0f));
          for (uint16_t ci = 0; ci < total; ++ci) {
            Adafruit_NeoPixel* strip; uint16_t pix;
            mapCombinedToStrip(ci, strip, pix);
            if (!strip) continue;
            strip->setPixelColor(pix, strip->Color(r, g, b));
          }
          // ensure addressable brightness is full so color shows correctly
          if (s_strip2) { s_strip2->setBrightness(255); s_strip2->show(); }
          if (s_strip1) { s_strip1->setBrightness(255); s_strip1->show(); }
          // PWM fades in across stage 2
          uint8_t pwmDuty = (uint8_t)min(255, (int)(stageP * 255.0f + 0.5f));
          ledcWrite(0, pwmDuty);
        }
      }
      if (overallP >= 1.0f) {
        // finalize fully white and PWM max
        for (uint16_t ci = 0; ci < total; ++ci) {
          Adafruit_NeoPixel* strip; uint16_t pix;
          mapCombinedToStrip(ci, strip, pix);
          if (!strip) continue;
          strip->setPixelColor(pix, strip->Color(255,255,255));
        }
        if (s_strip2) { s_strip2->setBrightness(255); s_strip2->show(); }
        if (s_strip1) { s_strip1->setBrightness(255); s_strip1->show(); }
        ledcWrite(0, 255);
        s_currentAnim = LEDController::Animation::None;
      }
      return;
    }

    if (s_currentAnim == LEDController::Animation::Sunset) {
      // Two-stage sunset (mirror of sunrise):
      // - Stage 1 (0.0 .. 0.5): addressable go from white -> red; PWM stays at full
      // - Stage 2 (0.5 .. 1.0): addressable red -> off; PWM fades 255 -> 0
      if (total > 0) {
        if (overallP < 0.5f) {
          float stageP = overallP * 2.0f; // 0..1
          // white -> red: reduce green/blue, keep red high
          uint8_t r = (uint8_t)min(255, (int)(255 - stageP * 50.0f));
          uint8_t g = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
          uint8_t b = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
          for (uint16_t ci = 0; ci < total; ++ci) {
            Adafruit_NeoPixel* strip; uint16_t pix;
            // reverse direction so sunset flows opposite of sunrise
            mapCombinedToStrip((total - 1 - ci), strip, pix);
            if (!strip) continue;
            strip->setPixelColor(pix, strip->Color(r, g, b));
          }
          // keep addressable brightness full to show color
          if (s_strip2) { s_strip2->setBrightness(255); s_strip2->show(); }
          if (s_strip1) { s_strip1->setBrightness(255); s_strip1->show(); }
          // PWM dims partially during stage 1 (255 -> 128)
          uint8_t pwmStage1 = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
          ledcWrite(0, pwmStage1);
        } else {
          float stageP = (overallP - 0.5f) * 2.0f; // 0..1
          // red -> off: progressively reduce red intensity and turn pixels off
          uint8_t redIntensity = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
          uint16_t numLit = (uint16_t)max(0, (int)ceil((1.0f - stageP) * (float)total));
          for (uint16_t ci = 0; ci < total; ++ci) {
            Adafruit_NeoPixel* strip; uint16_t pix;
            mapCombinedToStrip((total - 1 - ci), strip, pix);
            if (!strip) continue;
            if (ci < numLit) {
              strip->setPixelColor(pix, strip->Color(redIntensity, 0, 0));
            } else {
              strip->setPixelColor(pix, 0);
            }
          }
          // reduce addressable brightness slightly as it goes dark
          uint8_t wsBrightness = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
          if (s_strip2) { s_strip2->setBrightness(wsBrightness); s_strip2->show(); }
          if (s_strip1) { s_strip1->setBrightness(wsBrightness); s_strip1->show(); }
          ledcWrite(0, 0);
        }
      }
      if (overallP >= 1.0f) {
        // finalize: all off
        for (uint16_t ci = 0; ci < total; ++ci) {
          Adafruit_NeoPixel* strip; uint16_t pix;
          mapCombinedToStrip((total - 1 - ci), strip, pix);
          if (!strip) continue;
          strip->setPixelColor(pix, 0);
        }
        if (s_strip2) { s_strip2->show(); }
        if (s_strip1) { s_strip1->show(); }
        ledcWrite(0, 0);
        s_currentAnim = LEDController::Animation::None;
      }
      return;
    }

    if (s_currentAnim == LEDController::Animation::Waves) {
      // Waves: slower and more contrasted
      s_wavePhase += 0.02f; // slower
      for (int stripIdx = 0; stripIdx < 2; ++stripIdx) {
        Adafruit_NeoPixel* strip = (stripIdx == 0) ? s_strip2 : s_strip1; // strip2 is left
        if (!strip) continue;
        uint16_t n = strip->numPixels();
        for (uint16_t i = 0; i < n; ++i) {
          float x = (float)i / (float)(n ? n : 1);
          float wave = (sinf((x * 6.28318f) + s_wavePhase) + 1.0f) / 2.0f; // 0..1
          // increase contrast by applying a simple curve
          wave = powf(wave, 1.8f);
          // color from deep blue -> cyan with higher contrast
          uint8_t r = (uint8_t)(5 * wave);
          uint8_t g = (uint8_t)(50 + 180 * wave);
          uint8_t b = (uint8_t)(100 + 155 * wave);
          strip->setPixelColor(i, strip->Color(r, g, b));
        }
        strip->setBrightness(220);
        strip->show();
      }
      return;
    }

    if (s_currentAnim == LEDController::Animation::Police) {
      // Police: faster double-blink per color.
      // Cycle (800ms):
      // 0..100ms   -> RED flash 1
      // 100..170ms -> short off
      // 170..270ms -> RED flash 2
      // 270..350ms -> off (gap)
      // 350..450ms -> BLUE flash 1
      // 450..520ms -> short off
      // 520..620ms -> BLUE flash 2
      // 620..800ms -> longer blackout
      unsigned long nowMs = millis();
      unsigned long phase = (nowMs - s_animStart) % 800UL;
      uint8_t r = 0, g = 0, b = 0;
      bool show = false;
      if (phase < 100) {
        // RED flash 1
        r = 220; show = true;
      } else if (phase < 170) {
        show = false;
      } else if (phase < 270) {
        // RED flash 2
        r = 220; show = true;
      } else if (phase < 350) {
        show = false;
      } else if (phase < 450) {
        // BLUE flash 1
        b = 220; show = true;
      } else if (phase < 520) {
        show = false;
      } else if (phase < 620) {
        // BLUE flash 2
        b = 220; show = true;
      } else {
        // longer blackout for realism
        show = false;
      }

      // Ensure dimmable white (PWM channel 0) is off while police runs
      // s_savedPwmDuty should have been saved in startAnimation; enforce off here too
      ledcWrite(0, 0);

      // Apply the chosen color (or clear) across both strips
      if (s_strip2) {
        if (show) {
          uint32_t col = s_strip2->Color(r, g, b);
          for (uint16_t i = 0; i < s_strip2->numPixels(); ++i) s_strip2->setPixelColor(i, col);
          s_strip2->setBrightness(255);
          s_strip2->show();
        } else {
          s_strip2->clear(); s_strip2->show();
        }
      }
      if (s_strip1) {
        if (show) {
          uint32_t col = s_strip1->Color(r, g, b);
          for (uint16_t i = 0; i < s_strip1->numPixels(); ++i) s_strip1->setPixelColor(i, col);
          s_strip1->setBrightness(255);
          s_strip1->show();
        } else {
          s_strip1->clear(); s_strip1->show();
        }
      }
      return;
    }
  }

  // If not animating, handle dirty updates
  if (s_ws1Dirty && s_strip1) {
    s_ws1Dirty = false;
    setStripSolid(*s_strip1, ws1State);
  }
  if (s_ws2Dirty && s_strip2) {
    s_ws2Dirty = false;
    setStripSolid(*s_strip2, ws2State);
  }
}

void startAnimation(LEDController::Animation anim, unsigned long durationMs)
{
  s_currentAnim = anim;
  s_animStart = millis();
  s_animDur = durationMs;
  s_wavePhase = 0.0f;
  // If sunrise/sunset, ensure regular PWM strip is off initially and set a single dim pixel
  if (anim == LEDController::Animation::Sunrise) {
    // PWM channel 0 -> off
    ledcWrite(0, 0);
    // left strip (s_strip2) first pixel dim red
    if (s_strip2) {
      s_strip2->clear();
      s_strip2->setPixelColor(0, s_strip2->Color(5, 0, 0));
      s_strip2->setBrightness(50);
      s_strip2->show();
    }
    if (s_strip1) { s_strip1->clear(); s_strip1->show(); }
  } else if (anim == LEDController::Animation::Sunset) {
    // initialize all addressable LEDs to white and PWM at full
    if (s_strip2) {
      uint32_t col = s_strip2->Color(255,255,255);
      for (uint16_t i = 0; i < s_strip2->numPixels(); ++i) s_strip2->setPixelColor(i, col);
      s_strip2->setBrightness(255);
      s_strip2->show();
    }
    if (s_strip1) {
      uint32_t col = s_strip1->Color(255,255,255);
      for (uint16_t i = 0; i < s_strip1->numPixels(); ++i) s_strip1->setPixelColor(i, col);
      s_strip1->setBrightness(255);
      s_strip1->show();
    }
    ledcWrite(0, 255);
  // store PWM duty knowledge implicitly (channel 0)
  // ledcRead is available to read back if needed
  }

  if (anim == LEDController::Animation::Police) {
  // reset police timing state
  s_policeLastToggle = 0;
  s_policeBlue = false;
  // save current PWM duty and force off while police runs
  s_savedPwmDuty = getPwmDuty(0);
  ledcWrite(0, 0);
  // ensure strips are cleared/prepared
  if (s_strip2) { s_strip2->clear(); s_strip2->show(); }
  if (s_strip1) { s_strip1->clear(); s_strip1->show(); }
  }
}

void stopAnimation()
{
  // If we are stopping Police, restore saved PWM duty
  if (s_currentAnim == LEDController::Animation::Police) {
    // restore PWM duty
    ledcWrite(0, s_savedPwmDuty);
    s_savedPwmDuty = 0;
  }
  s_currentAnim = LEDController::Animation::None;
}

LEDController::Animation currentAnimation()
{
  return s_currentAnim;
}

uint8_t getPwmDuty(int channel)
{
  // ledcRead returns 0..(2^resolution-1). Our resolution is 8-bit in this project.
  int v = ledcRead(channel);
  if (v < 0) return 0;
  if (v > 255) v = 255;
  return (uint8_t)v;
}

bool readStripHardware(int stripIndex, StripState& out)
{
  Adafruit_NeoPixel* s = (stripIndex == 1) ? s_strip1 : s_strip2;
  if (!s) return false;
  // We can read brightness via Adafruit_NeoPixel::getBrightness()
  uint8_t b = s->getBrightness();
  out.brightness = b;
  out.on = (b > 0);
  // We don't have a safe way to read per-strip global color from the library
  // without tracking it; return last requested color as a best-effort (0/0/0)
  // Keep r/g/b as 255 so status page displays a neutral white if unknown.
  out.r = 255; out.g = 255; out.b = 255;
  return true;
}

} // namespace LEDController
