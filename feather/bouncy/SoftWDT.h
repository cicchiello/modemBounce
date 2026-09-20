#ifndef SOFTWDT_H
#define SOFTWDT_H

#include <cstdint>

// Independent TC4-based watchdog implementation for SAMD21
// Uses TC4 peripheral with overflow counter for extended timeouts

// Additionally, this module allocates a user-specified chunk of RAM that is outside the
// normal static- or file-scoped initalization boundaries (to provide a means to squirrel
// away state information across some device reset types).

enum WatchdogResetCause {
  WD_RESET_UNKNOWN = 0,
  WD_RESET_POWER_ON = 1,
  WD_RESET_WATCHDOG = 2,
  WD_RESET_EXTERNAL = 3,
  WD_RESET_BOD = 4,
  WD_RESET_SYST = 5
};

extern volatile unsigned long watchdog_last_pet_time;

class SoftWatchdog {
 public:
  static SoftWatchdog *singleton(uint32_t timeout_ms); // there can be only one

  void pet();

  void *pram();

  WatchdogResetCause reset_cause() const {return mResetCause;}

  const char *reset_cause_name() const;
  
 private:
  SoftWatchdog();

  bool mInitialized;
  WatchdogResetCause mResetCause;
};

#endif
