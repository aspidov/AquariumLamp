#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "LEDController.h"
#include "LEDController.h"
#include "LEDController.h"

// Lightweight flexible scheduler for daily tasks.
namespace Scheduler {

  // Initialize scheduler with references to the shared StripState objects and strips
  void init(StripState& dimState, StripState& ws1State, StripState& ws2State, Adafruit_NeoPixel& strip1, Adafruit_NeoPixel& strip2);

  // Call from main loop frequently
  void loop();

  // Add a schedule entry programmatically (optional use)
  void addDailyEntry(int hour, int minute, bool isUtc, LEDController::Animation anim, unsigned long durationMs, int followUpAction /* 0=none,1=waves,2=stopall,3=turnoff */);

  // Return JSON array of scheduled entries. Caller receives a String containing
  // an array like [{"hour":6,"minute":0,"isUtc":false,"anim":"Sunrise","durationMs":1200000,"followUp":1},...]
  String getScheduleJson();

} // namespace Scheduler
