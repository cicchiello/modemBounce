#include "SoftWDT.h"

#include <Arduino.h>
#include <sam.h>

// Independent TC4-based watchdog implementation for SAMD21
// Uses TC4 peripheral with overflow counter for extended timeouts


volatile unsigned long watchdog_last_pet_time = 0;
volatile uint32_t watchdog_timeout_ms = 16000;
volatile uint8_t watchdog_overflow_count = 0;
volatile uint8_t watchdog_overflow_target = 12;  // ~16 seconds with DIV1024

static const uint32_t WDT_MAGIC = 0xDEADBEEF;

static const char *sWatchdogResetCauseNames[] = {"UNKNOWN","POWER_ON","WATCHDOG","RESET_EXTERNAL","BOD","SYST"};

extern char __bss_end__[];

// This is the struct that will be allocated at the end of the RAM block.
// The application's piece (size specified to singleton) will be beyond magic:
//    uint8_t *user_block = ((uint8_t*) PRAM_ptr) + sizeof(uint32_t);
struct PRAM_Block {
  uint32_t magic;
  bool watchdog_triggered;
};

// Variable that points to the PRAM block that gets allocated by singleton
// HACK: adding 256 to ensure it's away from the working RAM (it was being overwritten for reasons I couldn't find)
volatile PRAM_Block * PRAM_ptr = (volatile PRAM_Block*)( ((uint32_t)__bss_end__ + 256 + 3) & ~0x03 );


SoftWatchdog::SoftWatchdog()
  : mInitialized(false), mResetCause(WD_RESET_UNKNOWN)
{
}


/* static */
SoftWatchdog *SoftWatchdog::singleton(uint32_t timeout_ms)
{
  static SoftWatchdog sTheWatchdog;

  watchdog_timeout_ms = timeout_ms;
  watchdog_last_pet_time = millis();
  watchdog_overflow_count = 0;

  // Read hardware reset cause from PM->RCAUSE
  uint8_t rcause = PM->RCAUSE.reg;

  // Clear the register
  PM->RCAUSE.reg = rcause;

  // Map hardware reset causes to our enum
  if (rcause == 0) {
    sTheWatchdog.mResetCause = WD_RESET_POWER_ON; // Power-on reset
  } else if (rcause & PM_RCAUSE_SYST) {
    // Check if this is a watchdog-triggered reset (via PRAM_ptr->magic)
    if ((PRAM_ptr->magic == WDT_MAGIC) && PRAM_ptr->watchdog_triggered) {
      sTheWatchdog.mResetCause = WD_RESET_WATCHDOG; // SoftWDT-triggered reset
    } else {
      PRAM_ptr->magic = WDT_MAGIC;
      sTheWatchdog.mResetCause = WD_RESET_SYST; // system (software-triggered) reset
    }
    PRAM_ptr->watchdog_triggered = false;
  } else if (rcause & PM_RCAUSE_WDT) {
    sTheWatchdog.mResetCause = WD_RESET_WATCHDOG; // Native WDT -- shouldn't happen !
  } else if (rcause & PM_RCAUSE_EXT) {
    sTheWatchdog.mResetCause = WD_RESET_EXTERNAL; // External reset (button)
  } else if (rcause & PM_RCAUSE_BOD33) {
    sTheWatchdog.mResetCause = WD_RESET_BOD; // Brown-out reset
  } else {
    sTheWatchdog.mResetCause = WD_RESET_UNKNOWN;
  }

  // Calculate overflow target: each overflow is ~1398ms (65535 / 46875)
  // For timeout_ms, divide and round UP to ensure we don't trigger early
  watchdog_overflow_target = (timeout_ms + 1397) / 1398;  // Round up
  if (watchdog_overflow_target == 0) watchdog_overflow_target = 1;

  // Configure TC4 clock
  PM->APBCMASK.bit.TC4_ = 1;  // Enable TC4 clock
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN(0) | GCLK_CLKCTRL_ID(TC4_GCLK_ID);
  while (GCLK->STATUS.bit.SYNCBUSY);

  // Reset TC4
  TC4->COUNT16.CTRLA.bit.SWRST = 1;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  // Configure TC4 as 16-bit counter:
  // - MODE: 16-bit counter (COUNT16)
  // - PRESCALER: DIV1024 (48 MHz / 1024 = 46875 Hz)
  // - Uses full 16-bit range (65535) with overflow interrupt for extended timeouts
  TC4->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 | TC_CTRLA_PRESCALER_DIV1024;

  // Set to overflow mode (count to max and wrap around)
  // Each overflow is ~1.4 seconds
  TC4->COUNT16.CC[0].reg = 65535;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  // Enable overflow interrupt
  TC4->COUNT16.INTENSET.reg = TC_INTENSET_OVF;

  // Enable TC4 interrupt in NVIC
  NVIC_EnableIRQ(TC4_IRQn);

  // Start the timer
  TC4->COUNT16.CTRLA.bit.ENABLE = 1;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  return &sTheWatchdog;
}


void SoftWatchdog::pet()
{
  watchdog_last_pet_time = millis();
  watchdog_overflow_count = 0;  // Reset overflow counter

  // Reset TC4 counter
  TC4->COUNT16.CTRLA.bit.ENABLE = 0;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  TC4->COUNT16.COUNT.reg = 0;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  TC4->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;

  TC4->COUNT16.CTRLA.bit.ENABLE = 1;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
}


void *SoftWatchdog::pram()
{
  uint8_t *user_block = ((uint8_t*) PRAM_ptr) + sizeof(PRAM_Block);
  return user_block;
}


const char *SoftWatchdog::reset_cause_name() const
{
  if (((int) mResetCause < WD_RESET_UNKNOWN) || ((int) mResetCause > WD_RESET_SYST)) return "<unknown>";
  else return sWatchdogResetCauseNames[(int) mResetCause];
}


void TC4_Handler() {
  TC4->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;

  watchdog_overflow_count++;

  if (watchdog_overflow_count >= watchdog_overflow_target) {
    Serial.println("\n!!! WATCHDOG TIMEOUT - RESETTING !!!\n");
    Serial.flush();
    
    PRAM_ptr->watchdog_triggered = true;
  
    // Disable TC4 before reset
    TC4->COUNT16.CTRLA.bit.ENABLE = 0;
    while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

    delay(500);

    // Perform a system reset
    NVIC_SystemReset();
  }
}
