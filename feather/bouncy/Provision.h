#pragma once


// forward declarations
class WifiCredStore;
class IndicatorLED;
class Logger;

enum Runtype {
  PROD, test_PROD, test_ROUTER, test_IP, test_MODEM, test_DNS, test_BOUNCE_MODEM, test_BOUNCE_ROUTER, test_EMAIL
};


class Provision {
 public:
  Provision(WifiCredStore &wifiStore, IndicatorLED &rgb, Runtype &rtype, Logger &logger);

  bool runFlow();
  
 private:
  WifiCredStore &mWifiStore;
  IndicatorLED &mRGB;
  Runtype &mRtype;
  Logger &mLogger;
};
