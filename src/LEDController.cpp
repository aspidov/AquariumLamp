#include "LEDController.h"
#include <Adafruit_NeoPixel.h>

namespace LEDController
{

  // Internal state
  static Adafruit_NeoPixel *s_strip1 = nullptr;
  static Adafruit_NeoPixel *s_strip2 = nullptr;
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
  // smoothing: timestamp of last LED update (used for per-frame blending)
  static unsigned long s_lastLedUpdate = 0;
  // Christmas animation helpers
  static unsigned long s_christmasAllOffUntil = 0;
  static unsigned long s_christmasLastStep = 0;
  static uint8_t s_christmasPhaseOffset = 0;

  void initPwm(int pin, int channel, int freq, int res, uint8_t initialDuty)
  {
    ledcSetup(channel, freq, res);
    ledcAttachPin(pin, channel);
    ledcWrite(channel, initialDuty);
  }

  void registerStrips(Adafruit_NeoPixel &strip1, Adafruit_NeoPixel &strip2)
  {
    s_strip1 = &strip1;
    s_strip2 = &strip2;
    if (s_strip1)
    {
      s_strip1->begin();
      s_strip1->clear();
    }
    if (s_strip2)
    {
      s_strip2->begin();
      s_strip2->clear();
    }
  }

  void setPwmDuty(int channel, uint8_t duty)
  {
    ledcWrite(channel, duty);
  }

  void setStripSolid(Adafruit_NeoPixel &strip, const StripState &st)
  {
    uint8_t b = st.on ? st.brightness : 0;
    strip.setBrightness(b);
    // remember which strip we updated
    if (&strip == s_strip1)
      s_ws1Brightness = b;
    if (&strip == s_strip2)
      s_ws2Brightness = b;
    uint32_t c = strip.Color(st.r, st.g, st.b);
    strip.fill(c, 0, strip.numPixels());
    strip.show();
  }

  void markDirty(int stripIndex)
  {
    if (stripIndex == 1)
      s_ws1Dirty = true;
    if (stripIndex == 2)
      s_ws2Dirty = true;
  }

  void loop(StripState &ws1State, StripState &ws2State)
  {
    unsigned long now = millis();

    // Handle animations first (override static color)
    if (s_currentAnim != LEDController::Animation::None)
    {
      // Combined strips length (left = s_strip2, right = s_strip1)
      uint16_t n2 = s_strip2 ? s_strip2->numPixels() : 0;
      uint16_t n1 = s_strip1 ? s_strip1->numPixels() : 0;
      uint16_t total = n2 + n1;
      unsigned long elapsed = now - s_animStart;
      unsigned long totalDur = max(1UL, s_animDur);
      float overallP = (float)elapsed / (float)totalDur;
      if (overallP > 1.0f)
        overallP = 1.0f;

      // Helper to map combined index -> strip & pixel. For sunrise/sunset we
      // reuse this to allow directional fill.
      auto mapCombinedToStrip = [&](uint16_t combinedIdx, Adafruit_NeoPixel *&outStrip, uint16_t &outPixel) -> void
      {
        uint16_t logical = combinedIdx;
        if (logical < n2)
        {
          // Left strip (s_strip2): invert index so combined 0 -> last pixel on left strip
          outStrip = s_strip2;
          outPixel = (n2 > 0) ? (n2 - 1 - logical) : 0;
        }
        else
        {
          // Right strip (s_strip1): normal mapping left-to-right inside right strip
          outStrip = s_strip1;
          outPixel = (n1 > 0) ? (logical - n2) : 0;
        }
      };

      if (s_currentAnim == LEDController::Animation::Sunrise)
      {
        // Two-stage sunrise:
        // - Stage 1 (0.0 .. 0.5 overallP): addressable fill from dark -> red; PWM remains off.
        // - Stage 2 (0.5 .. 1.0 overallP): addressable transition red -> white; PWM fades 0 -> 255.
        if (total > 0)
        {
          if (overallP < 0.5f)
          {
            float stageP = overallP * 2.0f; // 0..1 for stage 1
            uint16_t numRed = (uint16_t)ceil(stageP * (float)total);
            uint8_t redIntensity = (uint8_t)min(255, (int)(5 + stageP * 200.0f));
            for (uint16_t ci = 0; ci < total; ++ci)
            {
              Adafruit_NeoPixel *strip;
              uint16_t pix;
              mapCombinedToStrip(ci, strip, pix);
              if (!strip)
                continue;
              if (ci < numRed)
              {
                strip->setPixelColor(pix, strip->Color(redIntensity, 0, 0));
              }
              else
              {
                strip->setPixelColor(pix, 0);
              }
            }
            // keep addressable brightness moderate during red draw
            if (s_strip2)
            {
              s_strip2->setBrightness(120);
              s_strip2->show();
            }
            if (s_strip1)
            {
              s_strip1->setBrightness(120);
              s_strip1->show();
            }
            // PWM remains off during first stage
            ledcWrite(0, 0);
          }
          else
          {
            float stageP = (overallP - 0.5f) * 2.0f; // 0..1 for stage 2
            // Transition addressable LEDs from red -> white
            uint8_t r = (uint8_t)min(255, (int)(150 + stageP * 105.0f));
            uint8_t g = (uint8_t)min(255, (int)(stageP * 255.0f));
            uint8_t b = (uint8_t)min(255, (int)(stageP * 255.0f));
            for (uint16_t ci = 0; ci < total; ++ci)
            {
              Adafruit_NeoPixel *strip;
              uint16_t pix;
              mapCombinedToStrip(ci, strip, pix);
              if (!strip)
                continue;
              strip->setPixelColor(pix, strip->Color(r, g, b));
            }
            // ensure addressable brightness is full so color shows correctly
            if (s_strip2)
            {
              s_strip2->setBrightness(255);
              s_strip2->show();
            }
            if (s_strip1)
            {
              s_strip1->setBrightness(255);
              s_strip1->show();
            }
            // PWM fades in across stage 2
            uint8_t pwmDuty = (uint8_t)min(255, (int)(stageP * 255.0f + 0.5f));
            ledcWrite(0, pwmDuty);
          }
        }
        if (overallP >= 1.0f)
        {
          // finalize fully white and PWM max
          for (uint16_t ci = 0; ci < total; ++ci)
          {
            Adafruit_NeoPixel *strip;
            uint16_t pix;
            mapCombinedToStrip(ci, strip, pix);
            if (!strip)
              continue;
            strip->setPixelColor(pix, strip->Color(255, 255, 255));
          }
          if (s_strip2)
          {
            s_strip2->setBrightness(255);
            s_strip2->show();
          }
          if (s_strip1)
          {
            s_strip1->setBrightness(255);
            s_strip1->show();
          }
          ledcWrite(0, 255);
          s_currentAnim = LEDController::Animation::None;
        }
        return;
      }
      if (s_currentAnim == LEDController::Animation::Christmas)
      {
        // Christmas: groups of 5 LEDs flash in gold (no smooth transitions).
        // Behavior: scan forward over combined LED array in groups of `groupSize`.
        // Each step: group is ON (gold) for `onMs`, then ALL OFF for `offMs`,
        // then advance to next group. This creates a festive strobbing band.
        uint8_t pwm = getPwmDuty(0);
        if (pwm > 0) { markDirty(1); markDirty(2); return; }

        uint16_t n2 = s_strip2 ? s_strip2->numPixels() : 0;
        uint16_t n1 = s_strip1 ? s_strip1->numPixels() : 0;
        uint32_t total = (uint32_t)n2 + (uint32_t)n1;
        if (total == 0) return;

        // make the gold slightly more orange
        // make the gold slightly more orange
        // Orange color
        // Make the color more golden-orange (no pure green phase)
        // Golden-orange: keep red dominant, reduce green to avoid greenish hue,
        // and a small blue component to warm toward a gold tint.
        const uint8_t warmR = 255;
        const uint8_t warmG = 110; // toned down to avoid green cast
        const uint8_t warmB = 10;  // tiny blue to push toward gold

        const uint16_t groupSize = 6; // LEDs per group (user requested)
        // base on/off durations (will be modulated)
        // Increased for a much slower animation per user request
        const float baseOnMs = 1500.0f;
        const float baseOffMs = 800.0f;
        // shorter fade window (ms) at the start/end of ON phase (base)
        const unsigned long baseFadeMs = 50UL;

        // modulation: slow sine wave to make pattern speed ebb and flow
        float tSec = (now - s_animStart) / 1000.0f;
        // frequency (radians per second) - ~6.28 rad => period ~6.28s when freq=1.0
        const float freq = 0.9f;
        float osc = (sinf(tSec * freq) + 1.0f) * 0.5f; // 0..1
        // multiplier range: 0.6 .. 1.2 (slower -> faster). Reduce peak speed.
        float mult = 0.6f + 0.6f * osc;

        unsigned long onMs = (unsigned long)(baseOnMs * mult + 0.5f);
        unsigned long offMs = (unsigned long)(baseOffMs * mult + 0.5f);
        unsigned long period = onMs + offMs;
        unsigned long fadeMs = (unsigned long)min((float)baseFadeMs, (float)onMs * 0.45f);

        uint32_t groupCount = (total + groupSize - 1) / groupSize;
        unsigned long rel = (unsigned long)(now - s_animStart);
        unsigned long step = (period > 0) ? (rel / period) : 0;
        // offset of the pattern shifts every step so the alternating groups move
        unsigned long offset = step % (unsigned long)groupCount;

        unsigned long within = rel % period;
        bool isOnPhase = (within < onMs);

        // smoothing configuration (how quickly pixels blend toward their target)
        const unsigned long smoothMs = 120UL; // blending time constant in ms
        unsigned long dt = (s_lastLedUpdate == 0) ? 0 : (now - s_lastLedUpdate);
        float blendA = 1.0f;
        if (smoothMs > 0)
        {
          blendA = (float)dt / (float)smoothMs;
          if (blendA < 0.02f) blendA = 0.02f; // always make measurable progress
          if (blendA > 1.0f) blendA = 1.0f;
        }

        // Render combined strips with alternating groups: off, on, off, on, ...
        for (uint32_t ci = 0; ci < total; ++ci)
        {
          Adafruit_NeoPixel *strip;
          uint16_t pix;
          mapCombinedToStrip((uint16_t)ci, strip, pix);
          if (!strip) continue;

          // compute which group this pixel belongs to
          uint32_t gidx = ci / (uint32_t)groupSize;
          // determine if this group is scheduled to be ON for this step
          bool groupShouldBeOn = (((gidx + offset) % 2UL) == 0UL);

          // If this group should be off, make it fully off (no residual blending).
          if (!(isOnPhase && groupShouldBeOn))
          {
            strip->setPixelColor(pix, 0);
            continue;
          }

          // compute eased alpha for the ON group (use cosine ease for smoothness)
          float alpha = 1.0f;
          if (within < fadeMs)
          {
            float t = (float)within / (float)fadeMs; // 0..1
            alpha = 0.5f - 0.5f * cosf(t * 3.14159265f);
          }
          else if (within > (onMs - fadeMs))
          {
            unsigned long rem = onMs - within;
            float t = (float)rem / (float)fadeMs; // 0..1
            alpha = 0.5f - 0.5f * cosf(t * 3.14159265f);
          }
          // apply slight perceptual curve so mid-brightness looks smooth
          alpha = powf(alpha, 1.8f);

          // compute target color based on eased alpha
          uint8_t tr = (uint8_t)((float)warmR * alpha + 0.5f);
          uint8_t tg = (uint8_t)((float)warmG * alpha + 0.5f);
          uint8_t tb = (uint8_t)((float)warmB * alpha + 0.5f);

          // read previous pixel and blend toward target to avoid hard steps
          uint32_t prevCol = strip->getPixelColor(pix);
          uint8_t pr = (uint8_t)((prevCol >> 16) & 0xFF);
          uint8_t pg = (uint8_t)((prevCol >> 8) & 0xFF);
          uint8_t pb = (uint8_t)(prevCol & 0xFF);

          uint8_t br = (uint8_t)((float)pr + (float)(tr - pr) * blendA + 0.5f);
          uint8_t bg = (uint8_t)((float)pg + (float)(tg - pg) * blendA + 0.5f);
          uint8_t bb = (uint8_t)((float)pb + (float)(tb - pb) * blendA + 0.5f);

          strip->setPixelColor(pix, strip->Color(br, bg, bb));
        }

        if (s_strip2) { s_strip2->setBrightness(255); s_strip2->show(); }
        if (s_strip1) { s_strip1->setBrightness(255); s_strip1->show(); }

        s_lastLedUpdate = now;

        return;
      }

      if (s_currentAnim == LEDController::Animation::Sunset)
      {
        // Two-stage sunset (mirror of sunrise):
        // - Stage 1 (0.0 .. 0.5): addressable go from white -> red; PWM stays at full
        // - Stage 2 (0.5 .. 1.0): addressable red -> off; PWM fades 255 -> 0
        if (total > 0)
        {
          if (overallP < 0.5f)
          {
            float stageP = overallP * 2.0f; // 0..1
            // white -> red: reduce green/blue, keep red high
            uint8_t r = (uint8_t)min(255, (int)(255 - stageP * 50.0f));
            uint8_t g = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
            uint8_t b = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
            for (uint16_t ci = 0; ci < total; ++ci)
            {
              Adafruit_NeoPixel *strip;
              uint16_t pix;
              // reverse direction so sunset flows opposite of sunrise
              mapCombinedToStrip((total - 1 - ci), strip, pix);
              if (!strip)
                continue;
              strip->setPixelColor(pix, strip->Color(r, g, b));
            }
            // keep addressable brightness full to show color
            if (s_strip2)
            {
              s_strip2->setBrightness(255);
              s_strip2->show();
            }
            if (s_strip1)
            {
              s_strip1->setBrightness(255);
              s_strip1->show();
            }
            // PWM dims partially during stage 1 (255 -> 128)
            uint8_t pwmStage1 = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
            ledcWrite(0, pwmStage1);
          }
          else
          {
            float stageP = (overallP - 0.5f) * 2.0f; // 0..1
            // red -> off: progressively reduce red intensity and turn pixels off
            uint8_t redIntensity = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
            uint16_t numLit = (uint16_t)max(0, (int)ceil((1.0f - stageP) * (float)total));
            for (uint16_t ci = 0; ci < total; ++ci)
            {
              Adafruit_NeoPixel *strip;
              uint16_t pix;
              mapCombinedToStrip((total - 1 - ci), strip, pix);
              if (!strip)
                continue;
              if (ci < numLit)
              {
                strip->setPixelColor(pix, strip->Color(redIntensity, 0, 0));
              }
              else
              {
                strip->setPixelColor(pix, 0);
              }
            }
            // reduce addressable brightness slightly as it goes dark
            uint8_t wsBrightness = (uint8_t)max(0, (int)(255 - stageP * 255.0f));
            if (s_strip2)
            {
              s_strip2->setBrightness(wsBrightness);
              s_strip2->show();
            }
            if (s_strip1)
            {
              s_strip1->setBrightness(wsBrightness);
              s_strip1->show();
            }
            ledcWrite(0, 0);
          }
        }
        if (overallP >= 1.0f)
        {
          // finalize: all off
          for (uint16_t ci = 0; ci < total; ++ci)
          {
            Adafruit_NeoPixel *strip;
            uint16_t pix;
            mapCombinedToStrip((total - 1 - ci), strip, pix);
            if (!strip)
              continue;
            strip->setPixelColor(pix, 0);
          }
          if (s_strip2)
          {
            s_strip2->show();
          }
          if (s_strip1)
          {
            s_strip1->show();
          }
          ledcWrite(0, 0);
          s_currentAnim = LEDController::Animation::None;
        }
        return;
      }

      if (s_currentAnim == LEDController::Animation::Waves)
      {
        if (overallP >= 1.0f)
        {
          s_currentAnim = LEDController::Animation::None;
          markDirty(1);
          markDirty(2);
          return;
        }
        // Waves: slower and more contrasted
        s_wavePhase += 0.02f; // slower
        for (int stripIdx = 0; stripIdx < 2; ++stripIdx)
        {
          Adafruit_NeoPixel *strip = (stripIdx == 0) ? s_strip2 : s_strip1; // strip2 is left
          if (!strip)
            continue;
          uint16_t n = strip->numPixels();
          for (uint16_t i = 0; i < n; ++i)
          {
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

      if (s_currentAnim == LEDController::Animation::Police)
      {
        if (overallP >= 1.0f)
        {
          s_currentAnim = LEDController::Animation::None;
          markDirty(1);
          markDirty(2);
          return;
        }
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
        if (phase < 100)
        {
          // RED flash 1
          r = 220;
          show = true;
        }
        else if (phase < 170)
        {
          show = false;
        }
        else if (phase < 270)
        {
          // RED flash 2
          r = 220;
          show = true;
        }
        else if (phase < 350)
        {
          show = false;
        }
        else if (phase < 450)
        {
          // BLUE flash 1
          b = 220;
          show = true;
        }
        else if (phase < 520)
        {
          show = false;
        }
        else if (phase < 620)
        {
          // BLUE flash 2
          b = 220;
          show = true;
        }
        else
        {
          // longer blackout for realism
          show = false;
        }

        // Ensure dimmable white (PWM channel 0) is off while police runs
        // s_savedPwmDuty should have been saved in startAnimation; enforce off here too
        ledcWrite(0, 0);

        // Apply the chosen color (or clear) across both strips
        if (s_strip2)
        {
          if (show)
          {
            uint32_t col = s_strip2->Color(r, g, b);
            for (uint16_t i = 0; i < s_strip2->numPixels(); ++i)
              s_strip2->setPixelColor(i, col);
            s_strip2->setBrightness(255);
            s_strip2->show();
          }
          else
          {
            s_strip2->clear();
            s_strip2->show();
          }
        }
        if (s_strip1)
        {
          if (show)
          {
            uint32_t col = s_strip1->Color(r, g, b);
            for (uint16_t i = 0; i < s_strip1->numPixels(); ++i)
              s_strip1->setPixelColor(i, col);
            s_strip1->setBrightness(255);
            s_strip1->show();
          }
          else
          {
            s_strip1->clear();
            s_strip1->show();
          }
        }
        return;
      }
    }

    // If not animating, handle dirty updates
    if (s_ws1Dirty && s_strip1)
    {
      s_ws1Dirty = false;
      setStripSolid(*s_strip1, ws1State);
    }
    if (s_ws2Dirty && s_strip2)
    {
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
    if (anim == LEDController::Animation::Christmas) {
      s_christmasAllOffUntil = 0;
      s_christmasLastStep = 0;
      s_christmasPhaseOffset = (uint8_t)(s_animStart % 5);
    }
    // If sunrise/sunset, ensure regular PWM strip is off initially and set a single dim pixel
    if (anim == LEDController::Animation::Sunrise)
    {
      // PWM channel 0 -> off
      ledcWrite(0, 0);
      // left strip (s_strip2) first pixel dim red
      if (s_strip2)
      {
        s_strip2->clear();
        s_strip2->setPixelColor(0, s_strip2->Color(5, 0, 0));
        s_strip2->setBrightness(50);
        s_strip2->show();
      }
      if (s_strip1)
      {
        s_strip1->clear();
        s_strip1->show();
      }
    }
    else if (anim == LEDController::Animation::Sunset)
    {
      // initialize all addressable LEDs to white and PWM at full
      if (s_strip2)
      {
        uint32_t col = s_strip2->Color(255, 255, 255);
        for (uint16_t i = 0; i < s_strip2->numPixels(); ++i)
          s_strip2->setPixelColor(i, col);
        s_strip2->setBrightness(255);
        s_strip2->show();
      }
      if (s_strip1)
      {
        uint32_t col = s_strip1->Color(255, 255, 255);
        for (uint16_t i = 0; i < s_strip1->numPixels(); ++i)
          s_strip1->setPixelColor(i, col);
        s_strip1->setBrightness(255);
        s_strip1->show();
      }
      ledcWrite(0, 255);
      // store PWM duty knowledge implicitly (channel 0)
      // ledcRead is available to read back if needed
    }

    if (anim == LEDController::Animation::Police)
    {
      // reset police timing state
      s_policeLastToggle = 0;
      s_policeBlue = false;
      // save current PWM duty and force off while police runs
      s_savedPwmDuty = getPwmDuty(0);
      ledcWrite(0, 0);
      // ensure strips are cleared/prepared
      if (s_strip2)
      {
        s_strip2->clear();
        s_strip2->show();
      }
      if (s_strip1)
      {
        s_strip1->clear();
        s_strip1->show();
      }
    }
  }

  void stopAnimation()
  {
    // If we are stopping Police, restore saved PWM duty
    if (s_currentAnim == LEDController::Animation::Police)
    {
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
    if (v < 0)
      return 0;
    if (v > 255)
      v = 255;
    return (uint8_t)v;
  }

  bool readStripHardware(int stripIndex, StripState &out)
  {
    Adafruit_NeoPixel *s = (stripIndex == 1) ? s_strip1 : s_strip2;
    if (!s)
      return false;
    // We can read brightness via Adafruit_NeoPixel::getBrightness()
    uint8_t b = s->getBrightness();
    out.brightness = b;
    out.on = (b > 0);
    // We don't have a safe way to read per-strip global color from the library
    // without tracking it; return last requested color as a best-effort (0/0/0)
    // Keep r/g/b as 255 so status page displays a neutral white if unknown.
    out.r = 255;
    out.g = 255;
    out.b = 255;
    return true;
  }

} // namespace LEDController
