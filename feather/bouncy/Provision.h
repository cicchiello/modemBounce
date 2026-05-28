#pragma once


// forward declarations
class WifiCredStore;
class Logger;


class Provision {
 public:
  Provision(WifiCredStore &wifiStore, Logger &logger);

  bool runFlow();
  
 private:
  WifiCredStore &mWifiStore;
  Logger &mLogger;
};
