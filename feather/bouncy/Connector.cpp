#include "Connector.h" // include first to ensure it's self-contained

#include <WiFi101.h>
#include "WifiCredStore.h"
#include "Provision.h"
#include "Indicator.h"
#include "Logger.h"

static WifiCredStore sWifiStore;

static char sSSID[WifiCredStore::MAX_SSID_LEN + 1];
static char sPSWD[WifiCredStore::MAX_PSWD_LEN + 1];

extern void watchdog_delay(unsigned long ms);

Connector::Connector(IndicatorLED &rgb, Logger &logger)
  : mRGB(rgb), mLogger(logger)
{
  sWifiStore.begin();
}


static bool attempt(WifiCredStore &wifiStore, char *ssid, char *pswd, IndicatorLED &rgb, Logger &logger) {
  rgb.setBlue(true);
  if (!wifiStore.load(ssid, WifiCredStore::MAX_SSID_LEN + 1, pswd, WifiCredStore::MAX_PSWD_LEN + 1)) {
    logger.printf("INFO(%lu): No valid WiFi credentials in flash\n", millis());
    return false;
  }

  logger.printf("INFO(%lu): Trying WiFi SSID: %s\n", millis(), ssid);

  int status = WiFi.begin(ssid, pswd);

  unsigned long startMs = millis();

  while (status != WL_CONNECTED && millis() - startMs < 20000) {
    // wait 500ms between status checks
    watchdog_delay(500);
    status = WiFi.status();
  }

  if (status == WL_CONNECTED) {
    logger.printf("INFO(%lu): WiFi connected\n", millis());
    rgb.setBlue(false);
    return true;
  }
  // timeout

  WiFi.end();
  
  // wait 1s for clean shutdown
  watchdog_delay(1000);
  rgb.setBlue(false);
  
  return false;
}


bool Connector::isConnected() const
{
  int status = WiFi.status();
  return status == WL_CONNECTED;
}


bool Connector::reconnect(int max_tries)
{
  bool redState = mRGB.getRed();
  bool greenState = mRGB.getGreen();
  bool blueState = mRGB.getBlue();
  
  mRGB.off();
  mRGB.setBlue(true);
  
  WiFi.end();
  
  // wait 1s for clean shutdown
  watchdog_delay(1000);

  bool connected = connectWithCreds(sSSID, sPSWD, max_tries);

  mRGB.setRed(redState);
  mRGB.setGreen(greenState);
  mRGB.setBlue(blueState);

  return connected;
}


void Connector::connectUsingStoredCreds(Runtype &rtype) {
  mRGB.off();
  
  while (true) {
    if (!sWifiStore.load(sSSID, sizeof(sSSID), sPSWD, sizeof(sPSWD))) {
      mLogger.printf("WARNING(%lu): No stored WiFi credentials; entering provisioning mode\n", millis());

      Provision provision(sWifiStore, mRGB, rtype, mLogger);
      if (!provision.runFlow()) {
        mLogger.printf("ERROR(%lu): Provisioning failed to start\n", millis());
	
    	  // pause for 5s before retrying
        watchdog_delay(5000);
      }

      // Whether provisioning succeeded or failed, loop back and re-check flash.
      continue;
    }

    mLogger.printf("INFO(%lu): Stored WiFi credentials found for SSID: %s\n", millis(), sSSID);

    if (attempt(sWifiStore, sSSID, sPSWD, mRGB, mLogger)) {
      mRGB.off();
      return;
    }

    mRGB.setRed(true);
    mLogger.printf("WARNING(%lu): WiFi connect failed with stored credentials; will retry\n", millis());

    WiFi.disconnect();
    
    // pause for 10s before retrying
    watchdog_delay(10000);
    mRGB.setRed(false);
  }
}


void Connector::factoryReset() 
{
  sWifiStore.clear();
  watchdog_delay(1000);
  mLogger.printf("INFO(%lu): Cleared provisioned credentials; Provisioning will be required\n", millis());
}


bool Connector::connectWithCreds(const char *ssid, const char *pswd, int max_tries) 
{
  strcpy(sSSID, ssid);
  strcpy(sPSWD, pswd);
  
  int wifi_status = WL_IDLE_STATUS;
  int num_tries = 0; // max_tries == -1 indicates infinite
  while ((wifi_status != WL_CONNECTED) && ((max_tries == -1) || (max_tries > num_tries))) {
    mRGB.setBlue(true);
    if (num_tries == 0) {
        // always post a message on first try
        mLogger.printf("INFO(%lu): Attempting to connect to SSID: %s\n", millis(), sSSID);
    } else if ((num_tries == 1) && (max_tries == -1)){
        // if not in a max-try loop, post on second try to tell user we're not going to post forever
        mLogger.printf("WARNING(%lu): Failed; will silently continue retrying every 10s\n", millis());
    } else if (max_tries != -1) {
        // if we're not in an infinite retry loop, post a message every time
        mLogger.printf("INFO(%lu): Attempting to connect to SSID: %s\n", millis(), sSSID);
    }
    
    num_tries++;
    
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifi_status = WiFi.begin(sSSID, sPSWD);
    
    // wait 10 seconds for connection:
    watchdog_delay(10000);
  }
  if ((wifi_status == WL_CONNECTED) && (num_tries > 1)) {
      mLogger.printf("INFO(%lu): Connected after %d tries\n", millis(), num_tries);
  }

  mRGB.setBlue(false);
  return wifi_status == WL_CONNECTED;
}

