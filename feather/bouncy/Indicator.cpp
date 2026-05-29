#include "Indicator.h" // include first to ensure it's self-contained

#include <Arduino.h>
#include <stdint.h>


static const uint8_t LED_R_PIN = A4;
static const uint8_t LED_G_PIN = A1;
static const uint8_t LED_B_PIN = A2;

IndicatorLED::IndicatorLED()
{
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);

  off();
}


void IndicatorLED::off() {
  setRed(false);
  setGreen(false);
  setBlue(false);
}

void IndicatorLED::setRed(bool v)
{
  if (v == mR) return;
  
  digitalWrite(LED_R_PIN, v ? HIGH : LOW);
  mR = v;
}

void IndicatorLED::setGreen(bool v)
{
  if (v == mG) return;
  
  digitalWrite(LED_G_PIN, v ? HIGH : LOW);
  mG = v;
}

void IndicatorLED::setBlue(bool v)
{
  if (v == mB) return;
  
  digitalWrite(LED_B_PIN, v ? HIGH : LOW);
  mB = v;
}


