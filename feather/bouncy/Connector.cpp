#include "Connector.h" // include first to ensure it's self-contained

#include <WiFi101.h>
#include "WifiCredStore.h"
#include "Provision.h"
#include "Indicator.h"
#include "Logger.h"

static WifiCredStore sWifiStore;

static char sSSID[WifiCredStore::MAX_SSID_LEN + 1];
static char sPSWD[WifiCredStore::MAX_PSWD_LEN + 1];


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


bool Connector::isConnected() const
{
  int status = WiFi.status();
  //Serial.printf("DEBUG(%d): wifi status: %d (WL_CONNECTED==%d)\n", millis(), status, WL_CONNECTED);
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
  delay(1000);

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
      mLogger.printf("WARNING(%ld): No stored WiFi credentials; entering provisioning mode\n", millis());

      Provision provision(sWifiStore, mRGB, rtype, mLogger);
      if (!provision.runFlow()) {
        mLogger.printf("ERROR(%ld): Provisioning failed to start\n", millis());
	
	// pause for 5s before retrying
	delay(5000);
      }

      // Whether provisioning succeeded or failed, loop back and re-check flash.
      continue;
    }

    mLogger.printf("INFO(%ld): Stored WiFi credentials found for SSID: %s\n", millis(), sSSID);

    if (attempt(sWifiStore, sSSID, sPSWD, mRGB, mLogger)) {
      mRGB.off();
      return;
    }

    mRGB.setRed(true);
    mLogger.printf("WARNING(%ld): WiFi connect failed with stored credentials; will retry\n", millis());

    WiFi.disconnect();
    
    // pause for 10s before retrying
    delay(10000);
    mRGB.setRed(false);
  }
}


void Connector::factoryReset() 
{
  sWifiStore.clear();
  delay(1000);
  mLogger.printf("INFO(%ld): Cleared provisioned credentials; Provisioning will be required\n", millis());
}


bool Connector::connectWithCreds(const char *ssid, const char *pswd, int max_tries) 
{
  strcpy(sSSID, ssid);
  strcpy(sPSWD, pswd);
  
  int wifi_status = WL_IDLE_STATUS;
  int num_tries = 0; // max_tries == -1 indicates infinite
  while ((wifi_status != WL_CONNECTED) && ((max_tries == -1) || (max_tries > num_tries))) {
    num_tries++;
    
    mRGB.setBlue(true);
    mLogger.printf("INFO(%ld): Attempting to connect to SSID: %s\n", millis(), sSSID);
    
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifi_status = WiFi.begin(sSSID, sPSWD);
    
    // wait 10 seconds for connection:
    delay(10000);
  }

  mRGB.setBlue(false);
  return wifi_status == WL_CONNECTED;
}

