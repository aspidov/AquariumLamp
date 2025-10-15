#include "Scheduler.h"
#include "TimeService.h"

namespace Scheduler {

struct Entry {
  int hour;
  int minute;
  bool isUtc;
  LEDController::Animation anim;
  unsigned long durationMs; // configured duration in ms
  int followUpAction;
  bool firedToday;
  unsigned long endAtMs; // runtime: when the running animation should end (millis)
};

static StripState* s_dimState = nullptr;
static StripState* s_ws1State = nullptr;
static StripState* s_ws2State = nullptr;
static Adafruit_NeoPixel* s_strip1 = nullptr;
static Adafruit_NeoPixel* s_strip2 = nullptr;

static Entry s_entries[8];
static int s_entryCount = 0;

static unsigned long s_lastCheck = 0;

void init(StripState& dimState, StripState& ws1State, StripState& ws2State, Adafruit_NeoPixel& strip1, Adafruit_NeoPixel& strip2)
{
  s_dimState = &dimState;
  s_ws1State = &ws1State;
  s_ws2State = &ws2State;
  s_strip1 = &strip1;
  s_strip2 = &strip2;
  s_entryCount = 0;

  // Default schedule requested by user:
  // 06:00 local run sunrise (20 min) then set waves
  addDailyEntry(6, 0, true, LEDController::Animation::Sunrise, 60UL * 60UL * 1000UL, 1);
  // 20:30 UTC run sunset (20 min) then stop and turn off
  addDailyEntry(20, 10, true, LEDController::Animation::Sunset, 60UL * 60UL * 1000UL, 3);
  addDailyEntry(10, 00, true, LEDController::Animation::Police, 30UL * 1000UL, 4);
}

void addDailyEntry(int hour, int minute, bool isUtc, LEDController::Animation anim, unsigned long durationMs, int followUpAction)
{
  if (s_entryCount >= (int)(sizeof(s_entries)/sizeof(s_entries[0]))) return;
  s_entries[s_entryCount++] = { hour, minute, isUtc, anim, durationMs, followUpAction, false, 0 };
}

static void performFollowUp(int action)
{
  switch (action) {
    case 0: break;
    case 1: // waves
      LEDController::startAnimation(LEDController::Animation::Waves, 0);
      break;
    case 2: // stop all animations
      LEDController::stopAnimation();
      break;
    case 3: // turn off everything
      LEDController::stopAnimation();
      // turn off PWM
      LEDController::setPwmDuty(0, 0);
      // clear addressable strips
      if (s_strip1) { s_strip1->clear(); s_strip1->show(); }
      if (s_strip2) { s_strip2->clear(); s_strip2->show(); }
      break;
    case 4:
      LEDController::stopAnimation();
      LEDController::startAnimation(LEDController::Animation::Sunrise, 0);
      LEDController::setPwmDuty(0, 255);
  }
}

void loop()
{
  unsigned long nowMs = millis();
  if (nowMs - s_lastCheck < 1000) return; // check once per second
  s_lastCheck = nowMs;

  time_t t = TimeService::now();
  struct tm tm;
  gmtime_r(&t, &tm);

  for (int i = 0; i < s_entryCount; ++i) {
    Entry& e = s_entries[i];
    // choose hour/minute based on UTC or local (TimeService::now uses TZ env)
    int curHour = tm.tm_hour;
    int curMin = tm.tm_min;
    if (e.isUtc) {
      // To get UTC hour/min, we call gmtime_r on t (already used). The tm above is UTC.
      curHour = tm.tm_hour;
      curMin = tm.tm_min;
    } else {
      // local time: use localtime
      struct tm ltm;
      localtime_r(&t, &ltm);
      curHour = ltm.tm_hour;
      curMin = ltm.tm_min;
    }

    if (curHour == e.hour && curMin == e.minute) {
      if (!e.firedToday) {
        // start animation
          LEDController::startAnimation(e.anim, e.durationMs);
          // schedule follow-up via a delayed timer: record end timestamp in endAtMs
          e.endAtMs = millis() + e.durationMs;
        e.firedToday = true;
      }
    } else {
      // reset firedToday when minute rolls over (guard for next day)
      e.firedToday = false;
    }
    // If we have a running entry whose durationMs holds endAt, check and run follow-up
      if (e.firedToday && e.endAtMs != 0) {
        unsigned long nowu = millis();
        if (nowu >= e.endAtMs) {
          performFollowUp(e.followUpAction);
          // clear end marker so follow-up only runs once
          e.endAtMs = 0;
        }
      }
  }
}

  String getScheduleJson()
  {
    String json = "[";
    for (int i = 0; i < s_entryCount; ++i) {
      Entry& e = s_entries[i];
      String animName = "None";
      switch (e.anim) {
        case LEDController::Animation::Sunrise: animName = "Sunrise"; break;
        case LEDController::Animation::Sunset: animName = "Sunset"; break;
        case LEDController::Animation::Waves: animName = "Waves"; break;
        case LEDController::Animation::Police: animName = "Police"; break;
        default: break;
      }
      if (i) json += ",";
      json += "{";
      json += String("\"hour\":") + e.hour + ",";
      json += String("\"minute\":") + e.minute + ",";
      json += String("\"isUtc\":") + (e.isUtc ? "true" : "false") + ",";
      json += String("\"anim\":\"") + animName + "\",";
      json += String("\"durationMs\":") + e.durationMs + ",";
      json += String("\"followUp\":") + e.followUpAction;
      json += "}";
    }
    json += "]";
    return json;
  }

} // namespace Scheduler
