/*
  TC4-Based Watchdog Test

  Tests a TC4-based hardware watchdog for SAMD21.
  Pets the watchdog periodically, logs status, and can intentionally hang to trigger reset.
*/

#include "SoftWDT.h"

volatile unsigned long loopCount = 0;

#define RESET_CNT_MAGIC (0xDEADBEEF)

struct PRAM_BLOCK {
  uint32_t magic;
  int resetCnt;       
};

SoftWatchdog *sWatchdog = NULL;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    delay(10);
  }

  // Set up 16-second watchdog (uses overflow counter for extended timeout)
  sWatchdog = SoftWatchdog::singleton(16000);
  
  delay(1000);
  Serial.println("\n=== TC4-Based Watchdog Test ===");
  Serial.println("Type 'hang' to trigger watchdog, 'info' for status");

  PRAM_BLOCK *pram = (PRAM_BLOCK*) sWatchdog->pram();
  
  if (pram->magic == RESET_CNT_MAGIC) {
    pram->resetCnt++;
  } else {
    pram->magic = RESET_CNT_MAGIC;
    pram->resetCnt = 0;
  }
  
  // Report reset cause
  WatchdogResetCause cause = sWatchdog->reset_cause();
  Serial.print("Reset cause: ");
  switch (cause) {
    case WD_RESET_POWER_ON:
       Serial.println("Power-on");
       pram->resetCnt = 0;
       break;
    case WD_RESET_WATCHDOG:
       Serial.print("WATCHDOG TIMEOUT; resetCnt: "); Serial.println(pram->resetCnt);
       break;
    case WD_RESET_EXTERNAL:
       Serial.print("External reset; resetCnt: "); Serial.println(pram->resetCnt);
       break;
    case WD_RESET_BOD:
       Serial.print("Brown-out; resetCnt: "); Serial.println(pram->resetCnt);
       break;
    case WD_RESET_SYST:
       Serial.println("System reset");
       pram->resetCnt = 0;
       break;
    default: Serial.println("Unknown"); break;
  }

}

void loop() {
  loopCount++;
  unsigned long now = millis();

  // Pet the watchdog every second
  if (now - watchdog_last_pet_time >= 1000) {
    sWatchdog->pet();

    Serial.printf("[%ld] Watchdog petted (loop count: %ld)\n", now, loopCount);
  }

  // Check for serial input
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "hang") {
      Serial.println("Intentionally hanging for 20 seconds...");
      Serial.flush();
      delay(20000);  // Hang - watchdog should fire after 16 seconds
    }
    else if (cmd == "info") {
      Serial.printf("Loop count: %ld, Time since last pet: %ld ms\n",
                    loopCount, now - watchdog_last_pet_time);
    }
    else {
      Serial.println("Commands: 'hang', 'info'");
    }
  }

  delay(10);
}
