#pragma once


// forward declarations
class WifiCredStore;
class IndicatorLED;
class Logger;


class Provision {
 public:
  Provision(WifiCredStore &wifiStore, IndicatorLED &rgb, Logger &logger);

  bool runFlow();
  
 private:
  WifiCredStore &mWifiStore;
  IndicatorLED &mRGB;
  Logger &mLogger;
};
