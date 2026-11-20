// BAT_test — stand-alone LiPo health monitor for the Adafruit Feather M0.
//
// Reads the cell voltage through the Feather's on-board 2:1 divider on A7
// (pin 9) and prints one line every REPORT_INTERVAL_MS to the serial monitor.
// Also tracks min/max since boot and gives a rough state label so trends are
// easy to eyeball while the wall wart is unplugged, plugged back in, etc.
//
// Optional: wire a 2:1 divider (two equal resistors, 100k each is fine) from
// the USB pin (5 V rail) to a spare analog pin and set USB_SENSE_PIN below.
// Since 5 V / 2 = 2.5 V is below the 3.3 V ADC reference, that reads the wart
// directly and shows whether the battery has taken over.
//
// Wiring assumptions (Feather M0 stock):
//   A7 (D9)  -> VBAT through 100k/100k divider (on the board, nothing to add)
//   USB pin  -> external 5 V from the enclosure supply
//   BAT      -> LiPo via the JST connector

#include <Arduino.h>

#include "VBat.h"

// ---------------------------------------------------------------- config ---
//static const uint8_t  VBAT_PIN            = A7;    // on-board VBAT/2 divider
static const int8_t   USB_SENSE_PIN       = -1;    // e.g. A1 once a divider is fitted; -1 = disabled
//static const float    DIVIDER_RATIO       = 2.0f;  // both dividers are 2:1
//static const float    ADC_REF_VOLTS       = 3.3f;
//static const uint8_t  ADC_BITS            = 12;
//static const uint8_t  SAMPLES_PER_READING = 16;
static const uint32_t REPORT_INTERVAL_MS  = 5000;
static const uint8_t  HEARTBEAT_PIN       = 13;    // red LED next to the USB jack

// Thresholds for the human-readable state column. Rough, for a 1S LiPo.
static const float V_FULL     = 4.15f;  // charger at or near termination
static const float V_NOMINAL  = 3.70f;
static const float V_LOW      = 3.50f;
static const float V_CRITICAL = 3.30f;  // regulator dropout territory
static const float V_ABSENT   = 2.50f;  // nothing plugged into BAT

// ----------------------------------------------------------------- state ---
static float sMinVbat = 99.0f;
static float sMaxVbat = 0.0f;
static uint32_t sReportCount = 0;

static VBat *sVBat = VBat::singleton();

#ifdef HIDE
// ------------------------------------------------------------- functions ---
static float readVolts(uint8_t pin)
{
  // The SAMD21 ADC's first sample after a channel change can be stale;
  // throw it away, then average the rest.
  (void)analogRead(pin);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < SAMPLES_PER_READING; i++) {
    sum += analogRead(pin);
  }
  const float counts = (float)sum / SAMPLES_PER_READING;
  const float fullScale = (float)((1UL << ADC_BITS) - 1);
  return counts * ADC_REF_VOLTS / fullScale * DIVIDER_RATIO;
}
#endif

static const char *batteryState(float v)
{
  if (v < V_ABSENT)   return "ABSENT?";
  if (v < V_CRITICAL) return "CRITICAL";
  if (v < V_LOW)      return "LOW";
  if (v < V_NOMINAL)  return "OK";
  if (v < V_FULL)     return "GOOD";
  return "FULL/CHARGING";
}

static void printUptime()
{
  const uint32_t s = millis() / 1000;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  Serial.print(buf);
}

// ----------------------------------------------------------------- setup ---
void setup()
{
  pinMode(HEARTBEAT_PIN, OUTPUT);
  digitalWrite(HEARTBEAT_PIN, LOW);

  Serial.begin(115200);
  // Wait for the serial monitor, but not forever: the sketch must also run
  // when nothing is on the USB jack.
  const uint32_t start = millis();
  while (!Serial && (millis() - start) < 5000) { delay(10); }

//  analogReadResolution(ADC_BITS);
//  analogReference(AR_DEFAULT);   // 3.3 V

  Serial.println();
  Serial.println(F("BAT_test: Feather M0 LiPo health monitor"));
  Serial.print (F("  VBAT on pin A7, "));
//  Serial.print (SAMPLES_PER_READING);
  Serial.print (VBAT_SAMPLES_PER_READING);
  Serial.print (F(" samples/reading, report every "));
  Serial.print (REPORT_INTERVAL_MS / 1000);
  Serial.println(F(" s"));
  if (USB_SENSE_PIN >= 0) {
    Serial.println(F("  USB rail sense enabled"));
  } else {
    Serial.println(F("  USB rail sense disabled (set USB_SENSE_PIN to enable)"));
  }
  Serial.println(F("uptime    | vbat   | min    | max    | state          | usb"));
}

// ------------------------------------------------------------------ loop ---
void loop()
{
//  const float vbat = readVolts(VBAT_PIN);
  const float vbat = sVBat->readVolts();
  if (vbat < sMinVbat) sMinVbat = vbat;
  if (vbat > sMaxVbat) sMaxVbat = vbat;
  sReportCount++;

  digitalWrite(HEARTBEAT_PIN, HIGH);

  printUptime();
  Serial.print(F(" | "));
  Serial.print(vbat, 3);     Serial.print(F(" | "));
  Serial.print(sMinVbat, 3); Serial.print(F(" | "));
  Serial.print(sMaxVbat, 3); Serial.print(F(" | "));

  char state[16];
  snprintf(state, sizeof(state), "%-14s", batteryState(vbat));
  Serial.print(state);
  Serial.print(F(" | "));

#ifdef HIDE
if (USB_SENSE_PIN >= 0) {
    const float vusb = readVolts((uint8_t)USB_SENSE_PIN);
    Serial.print(vusb, 3);
    Serial.print(vusb < 4.5f ? F(" (SAG/OFF: on battery)") : F(" (ok)"));
  } else {
    Serial.print(F("n/a"));
  }
#endif

  Serial.println();

  delay(50);
  digitalWrite(HEARTBEAT_PIN, LOW);
  delay(REPORT_INTERVAL_MS - 50);
}
