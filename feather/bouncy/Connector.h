#pragma once

#include "Provision.h"

// forward declarations
class IndicatorLED;
class Logger;


class Connector {
 public:
  Connector(IndicatorLED &rgb, Logger &logger);
  
  void begin() {}

  void connectUsingStoredCreds(Runtype &runtype);

  // max_tries == -1 indicates infinite#
  // returns true on success
  bool connectWithCreds(const char *ssid, const char *pswd, int max_tries = -1);

  bool isConnected() const;

  // max_tries == -1 indicates infinite
  // returns true on success
  bool reconnect(int max_tries = -1);
  
  void factoryReset();
  
 private:
  IndicatorLED &mRGB;
  Logger &mLogger;
};
