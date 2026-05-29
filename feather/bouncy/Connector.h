#pragma once

// forward declarations
class IndicatorLED;
class Logger;


class Connector {
 public:
  Connector(IndicatorLED &rgb, Logger &logger);

  void connectUsingStoredCreds();
  void connectWithCreds(const char *ssid, const char *pswd);

  void factoryReset();
  
 private:
  Logger &mLogger;
  IndicatorLED &mRGB;
};
