#pragma once

// forward declarations
class Logger;


class Connector {
 public:
  Connector(Logger &logger);

  void connectUsingStoredCreds();
  void connectWithCreds(const char *ssid, const char *pswd);

 private:
  Logger &mLogger;
};
