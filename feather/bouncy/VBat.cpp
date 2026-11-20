#include "VBat.h" // included first to ensure it's self-contained

#include <Arduino.h>
#include <stdint.h>

static const uint8_t  ADC_BITS            = 12;
static const float    ADC_REF_VOLTS       = 3.3f;
static const uint8_t  SAMPLES_PER_READING = 16;
static const uint8_t  VBAT_PIN            = A7;    // on-board VBAT/2 divider
static const float    DIVIDER_RATIO       = 2.0f;  // both dividers are 2:1

VBat::VBat()
{
  analogReadResolution(ADC_BITS);
  analogReference(AR_DEFAULT);   // 3.3 V
}


float VBat::readVolts() const
{
  // The SAMD21 ADC's first sample after a channel change can be stale;
  // throw it away, then average the rest.
  (void)analogRead(VBAT_PIN);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < SAMPLES_PER_READING; i++) {
    sum += analogRead(VBAT_PIN);
  }
  const float counts = (float)sum / SAMPLES_PER_READING;
  const float fullScale = (float)((1UL << ADC_BITS) - 1);
  return counts * ADC_REF_VOLTS / fullScale * DIVIDER_RATIO;
}


/* STATIC */
VBat *VBat::singleton()
{
  static VBat sVBat;

  return &sVBat;
}
