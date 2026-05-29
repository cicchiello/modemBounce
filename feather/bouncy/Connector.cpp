#include "Connector.h" // include first to ensure it's self-contained

#include <WiFi101.h>
#include "WifiCredStore.h"
#include "Provision.h"
#include "Indicator.h"
#include "Logger.h"

static WifiCredStore sWifiStore;

Connector::Connector(IndicatorLED &rgb, Logger &logger)
  : mRGB(rgb), mLogger(logger)
{
  sWifiStore.begin();
}


static bool attempt(WifiCredStore &wifiStore, char *ssid, char *pswd, IndicatorLED &rgb, Logger &logger) {
  rgb.setBlue(true);
  if (!wifiStore.load(ssid, WifiCredStore::MAX_SSID_LEN + 1, pswd, WifiCredStore::MAX_PSWD_LEN + 1)) {
    logger.printf("INFO(%ld): No valid WiFi credentials in flash\n", millis());
    return false;
  }

  logger.printf("INFO(%ld): Trying WiFi SSID: %s\n", millis(), ssid);

  int status = WiFi.begin(ssid, pswd);

  unsigned long deadline = millis() + 20000;

  while (status != WL_CONNECTED && millis() < deadline) {
    // wait 500ms between status checks
    delay(500);
    status = WiFi.status();
  }

  if (status == WL_CONNECTED) {
    logger.printf("INFO(%ld): WiFi connected\n", millis());
    rgb.setBlue(false);
    return true;
  }
  // timeout

  WiFi.end();
  
  // wait 1s for clean shutdown
  delay(1000);
  rgb.setBlue(false);
  
  return false;
}


void Connector::connectUsingStoredCreds() {
  char ssid[WifiCredStore::MAX_SSID_LEN + 1];
  char pswd[WifiCredStore::MAX_PSWD_LEN + 1];

  mRGB.off();
  
  while (true) {
    if (!sWifiStore.load(ssid, sizeof(ssid), pswd, sizeof(pswd))) {
      mLogger.printf("WARNING(%ld): No stored WiFi credentials; entering provisioning mode\n", millis());

      Provision provision(sWifiStore, mRGB, mLogger);
      if (!provision.runFlow()) {
        mLogger.printf("ERROR(%ld): Provisioning failed to start\n", millis());
	
	// pause for 5s before retrying
	delay(5000);
      }

      // Whether provisioning succeeded or failed, loop back and re-check flash.
      continue;
    }

    mLogger.printf("INFO(%ld): Stored WiFi credentials found for SSID: %s\n", millis(), ssid);

    if (attempt(sWifiStore, ssid, pswd, mRGB, mLogger)) {
      mRGB.off();
      return;
    }

    mLogger.printf("WARNING(%ld): WiFi connect failed with stored credentials; will retry\n", millis());

    WiFi.disconnect();
    
    // pause for 10s before retrying
    delay(10000);
  }
}


void Connector::factoryReset() 
{
  sWifiStore.clear();
  delay(1000);
  mLogger.printf("INFO(%ld): Cleared provisioned credentials; Provisioning will be required\n", millis());
}


void Connector::connectWithCreds(const char *ssid, const char *pswd) 
{
  int wifi_status = WL_IDLE_STATUS;
  while (wifi_status != WL_CONNECTED) {
    mRGB.setBlue(true);
    mLogger.printf("INFO(%ld): Attempting to connect to SSID: %s\n", millis(), ssid);
    
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifi_status = WiFi.begin(ssid, pswd);
    
    // wait 10 seconds for connection:
    delay(10000);
  }
  mRGB.setBlue(false);
}

