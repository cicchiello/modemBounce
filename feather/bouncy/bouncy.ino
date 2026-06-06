/*
  Periodically, in the following order, check that:
      1) the wifi router is reachable
      2) the cable modem is reachable
      3) a DNS server is reachable (IP 8.8.8.8)
  
  If any of those checks fail:
      1) trigger power-cycle of router
      2) trigger power-cycle of cable modem
      3) trigger power-cycle of cable modem

  Copyright Joe Cicchiello 2026

  ToDo:
    - consider OTA updates (mostly to enable testing in-circuit, but could change behavior later)
    - define and print enclosure
    - wire up and install circuit

*/


#include <SPI.h>
#include <WiFi101.h>
#include "HttpLogger.h"
#include "Logger.h"
#include "Connector.h"
#include "Indicator.h"
#include "EmailRelay.h"
#include "arduino_secrets.h" 

#define PROGNAME "Bouncy"


enum State {Initialize, Wait, Sleep, APConnect, RouterPing, ModemPing, InternetPing, DnsPing};

static Runtype sTest = PROD;
static State sState = Initialize;
static State sStateAfterWait = Initialize;
static long sWait_ms = 0;

#define TIME_BETWEEN_MONITOR_ATTEMPTS_ms (sTest == PROD ? 30*60*1000 : 2*60*1000) 
#define NUM_FAILURES_TO_BOUNCE (sTest == PROD ? 5 : 3)

static const IPAddress Internet() {return IPAddress(8,8,8,(sTest != test_IP ? 8 : 81));}
static const char *DNS() {return sTest != test_DNS ? "google.com" : "google.comfoo";}
static const IPAddress ModemIP() {return IPAddress(192,168,100, (sTest != test_MODEM ? 1 : 111));}
static const IPAddress RouterIP() {return IPAddress(10,0,0, (sTest != test_ROUTER ? 1 : 111));}

static HttpLogger sHttpLog(LOG_HOST, LOG_PORT, LOG_PATH, SECRET_LOGGING_KEY);
static Logger Log(&sHttpLog, sTest == PROD ? LOG_TO_HTTP : LOG_TO_BOTH);

static const uint8_t FACTORY_RESET_PIN = 6;
static const unsigned long FACTORY_RESET_HOLD_MS = 5000;

static const uint8_t MODEM_SSR_PIN = A5;
static const uint8_t ROUTER_SSR_PIN = 5;
static IndicatorLED sRGB;

static Connector sConnector(sRGB, Log);

static int sFailureCnt = 0;
static const char *sPendingEmail = NULL;


static const char *to_str(const IPAddress &ip)
{
  static char buf[20];
  sprintf(buf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return buf;
}

static const char *to_str(const char *s)
{
  return s;
}


static void enableRouter(bool on) {
  if (on) {
    Log.printf("INFO(%ld): enableRouter turn on\n", millis());
    digitalWrite(ROUTER_SSR_PIN, LOW);  // SSR is normally-closed, so LOW releases it to close
  } else {
    Log.printf("INFO(%ld): enableRouter turn off\n", millis());
    digitalWrite(ROUTER_SSR_PIN, HIGH);  // SSR is normally-closed, so HIGH drives it to open
  }
}


static void enableModem(bool on) {
  if (on) {
    Log.printf("INFO(%ld): enableModem turn on\n", millis());
    digitalWrite(MODEM_SSR_PIN, LOW);  // SSR is normally-closed, so LOW releases it to close
  } else {
    Log.printf("INFO(%ld): enableModem turn off\n", millis());
    digitalWrite(MODEM_SSR_PIN, HIGH);  // SSR is normally-closed, so HIGH drives it to open
  }
}


static void send_reset_email() {
  Log.printf("WARNING(%ld): system restarted; sending an email...\n", millis());
  
  EmailRelay Mailer(EMAIL_HOST, EMAIL_PORT, EMAIL_PATH, SECRET_EMAIL_KEY);
  Mailer.begin();

  char subject[30];
  sprintf(subject, "ALERT %s", PROGNAME);
  if (!Mailer.send(subject, "system restarted")) {
    Log.printf("ERROR(%ld): EmailRelay failed\n", millis());
  } 
}


static void send_email(const char *reason) {
  Log.printf("WARNING(%ld): failure detected(%s); sending an email...\n", millis(), reason);
  
  EmailRelay Mailer(EMAIL_HOST, EMAIL_PORT, EMAIL_PATH, SECRET_EMAIL_KEY);
  Mailer.begin();

  char subject[30];
  sprintf(subject, "ALERT %s", PROGNAME);
  if (!Mailer.send(subject, reason)) {
    Log.printf("ERROR(%ld): EmailRelay failed\n", millis());
  } 
}


static void bounce_modem() {
  Log.printf("WARNING(%ld): bouncing modem...\n", millis());

  bool redState = sRGB.getRed();
  bool greenState = sRGB.getGreen();
  bool blueState = sRGB.getBlue();
  sRGB.off();
  sRGB.setRed(true);

  enableModem(false);
  
  // leave off for 15 seconds
  delay(15000);
  
  enableModem(true);
  
  // wait for 210s while modem initializes
  Log.printf("INFO(%ld): waiting 3 minutes for modem initialization...\n", millis());
  delay(210000);

  sPendingEmail = "modem failure";

  sRGB.setRed(redState);
  sRGB.setGreen(greenState);
  sRGB.setBlue(blueState);

  Log.printf("INFO(%ld): continuing with normal operations after bouncing modem...\n", millis());
}

static void bounce_router() {
  Log.printf("WARNING(%ld): bouncing router...\n", millis());

  bool redState = sRGB.getRed();
  bool greenState = sRGB.getGreen();
  bool blueState = sRGB.getBlue();
  sRGB.off();  
  sRGB.setRed(true);

  enableRouter(false);
  
  // leave off for 15 seconds
  delay(15000);
  
  enableRouter(true);
  
  // wait for 210s while router initializes
  Log.printf("INFO(%ld): waiting 3 minuites for router initialization...\n", millis());
  delay(210000);
  
  sPendingEmail = "router failure";
  
  sRGB.setRed(redState);
  sRGB.setGreen(greenState);
  sRGB.setBlue(blueState);

  Log.printf("INFO(%ld): continuing with normal operations after bouncing router...\n", millis());
}

static void nextState(State nextState, long delay) {
  sStateAfterWait = nextState;
  sState = Wait;
  sWait_ms = delay;  
}


void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Log.printf("INFO(%ld): SSID: %s\n", millis(), WiFi.SSID());

  // print your WiFi shield's IP address:
  Log.printf("INFO(%ld): IP Address: %s\n", millis(), to_str(WiFi.localIP()));

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Log.printf("INFO(%ld): signal strength (RSSI): %ld dBm\n\n", millis(), rssi);
}


static int ping_test(const char *devname, const char *ipstr, State thisState, State successState, void (*bounceFtor)()) {
  //Log.printf("INFO(%ld): pinging %s @ %s\n", millis(), devname, ipstr);
  if (WiFi.ping(ipstr) > 0) {
    Log.printf("INFO(%ld): successful ping of %s @ %s\n", millis(), devname, ipstr);
    nextState(successState, 2*1000);
    sFailureCnt = 0;
  } else {
    sFailureCnt++;
    Log.printf("WARNING(%ld): couldn't connect to %s @ %s (failure %d/%d)\n",
               millis(), devname, ipstr, sFailureCnt, NUM_FAILURES_TO_BOUNCE);
    if (sFailureCnt < NUM_FAILURES_TO_BOUNCE) {
      Log.printf("INFO(%ld): retrying in 60s...\n", millis());
      nextState(thisState, 60*1000);
    } else {
      // all tries exhausted...  have to bounce the device
      Log.printf("WARNING(%ld): All %s tries exhausted; bouncing %s...\n", millis(), devname, devname);
      bounceFtor();
      sFailureCnt = 0;
      nextState(Initialize, 1*1000);
    }
  }
  return 0;
}


static bool checkFactoryResetButton() {
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);

  // Let the pin settle after reset.
  delay(20);

  unsigned long start = millis();

  Log.printf("INFO(%ld): Reset button held; considering factory-reset\n", millis());

  while (digitalRead(FACTORY_RESET_PIN) == LOW) {
    unsigned long held = millis() - start;

    if (held >= FACTORY_RESET_HOLD_MS) {
      Log.printf("WARNING(%ld): Factory reset requested\n", millis());

      // Wait for release before continuing, so the button press
      // does not immediately trigger another reset cycle.
      while (digitalRead(FACTORY_RESET_PIN) == LOW) {
        delay(50);
        sRGB.toggleRed();
      }
      sRGB.setRed(false);

      delay(100);
      return true;
    }

    delay(20);
  }

  Log.printf("INFO(%ld): Reset button released before factory-reset timeout\n", millis());
  return false;
}






void setup() {
  //Configure pins for Adafruit ATWINC1500 Feather
  WiFi.setPins(8,7,4,2);

  pinMode(MODEM_SSR_PIN, OUTPUT);
  digitalWrite(MODEM_SSR_PIN, LOW);
  pinMode(ROUTER_SSR_PIN, OUTPUT);
  digitalWrite(ROUTER_SSR_PIN, LOW);

  sState = Initialize;

  bool factoryResetEnabled = checkFactoryResetButton();

  if ((Log.getMode() == LOG_TO_BOTH) || (Log.getMode() == LOG_TO_SERIAL)) {
    //Initialize serial and wait for port to open:
    Serial.begin(9600);
    while (!Serial) {
      ; // wait for serial port to connect. Needed for native USB port only
    }

    Serial.printf("\n\n");
  }


  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Log.printf("FATAL(%ld): WiFi shield not present; can't proceed.\n", millis());

    // can't/don't continue:
    while (true) {}
  }

  if (sTest == PROD) {
    if (factoryResetEnabled) {
      sConnector.factoryReset();
    }
    sConnector.connectUsingStoredCreds(sTest);
  } else {
    // attempt to connect to WiFi network using secret creds
    sConnector.connectWithCreds(SECRET_SSID, SECRET_PASS);
  }
  // control will only return if connected
  sRGB.setGreen(true);

  
  WiFi.maxLowPowerMode();
  sHttpLog.set_wifi_available(); // now all logging can go thru the Log facace

  printWiFiStatus();

  send_reset_email();

  // consider testing mode: should one of the addresses be perturbed, or directly trigger something
  if (sTest == test_ROUTER) {
    // RouterIP is already set to *.*.*.111
  } else if (sTest == test_IP) {
    // InternetIP is already set to *.*.*.11
  } else if (sTest == test_MODEM) {
    // ModemIP is already set to *.*.*.111
  } else if (sTest == test_DNS) {
    // DNS is already set to google.comfoo
  } else if (sTest == test_BOUNCE_MODEM) {
    bounce_modem();
  } else if (sTest == test_BOUNCE_ROUTER) {
    bounce_router();
  } else if (sTest == test_EMAIL) {      
    Log.printf("INFO(%ld): testing email delivery\n", millis());
    send_email("testing email");
  }

}


void loop() {

  switch (sState) {
    case Initialize: 
      sStateAfterWait = APConnect;
      sState = Wait;
      sWait_ms = 1000;
      break;
    case APConnect:
      if (!sConnector.isConnected()) {
        sRGB.off();
        sRGB.setBlue(true);
       
        sFailureCnt++;
        Log.printf("INFO(%ld): AP disconnect detected (failure %d/%d)\n",
                   millis(), sFailureCnt, NUM_FAILURES_TO_BOUNCE);

        if (sFailureCnt < NUM_FAILURES_TO_BOUNCE) {
          Log.printf("WARNING(%d): attempting wifi reconnect\n", millis());

          // make one try at reconnecting
          const int num_tries = 1;
          bool connected = sConnector.reconnect(num_tries);
          if (connected) {
            Log.printf("INFO(%ld): success; continuing as normal\n", millis());
            sRGB.off();
            sRGB.setGreen(true);
            sPendingEmail = "AP disconnect";
            nextState(RouterPing, 2*1000);
          } else {
            Log.printf("WARNING(%ld): failed; retrying in 60s..\n", millis());
            nextState(APConnect, 60*1000);
          }

        } else {
          // all tries exhausted...  have to bounce the router
          Log.printf("WARNING(%ld): All AP connect tests exhausted; bouncing router...\n", millis());
                     bounce_router();

          // reconnect -- try forever
          sConnector.reconnect();

          sRGB.off();
          sRGB.setGreen(true);
          sPendingEmail = "AP disconnect";

          sFailureCnt = 0;
          nextState(Initialize, 1*1000);
        }
      } else {
        sRGB.setBlue(false);
        sRGB.setGreen(true);

        Log.printf("INFO(%ld): AP connection detected\n", millis());
        nextState(RouterPing, 2*1000);
        sFailureCnt = 0;
      }
      break;
    case RouterPing: 
      ping_test("router", to_str(RouterIP()), RouterPing, ModemPing, bounce_router);
      break;
    case ModemPing: 
      ping_test("modem", to_str(ModemIP()), ModemPing, InternetPing, bounce_modem);
      break;
    case InternetPing: 
      ping_test("internet", to_str(Internet()), InternetPing, DnsPing, bounce_modem);
      break;
    case DnsPing: 
      ping_test("dns", DNS(), DnsPing, Sleep, bounce_modem);
      break;
    case Sleep: 
      Log.printf("INFO(%ld): sleeping until next check in %d mins\n", millis(), TIME_BETWEEN_MONITOR_ATTEMPTS_ms/1000/60);
      nextState(Initialize, TIME_BETWEEN_MONITOR_ATTEMPTS_ms);
      sFailureCnt = 0;
      break;
    case Wait: 
      Log.flush();
      delay(1);
      if (--sWait_ms == 0) {
        if (sPendingEmail) {
          send_email(sPendingEmail);
          sWait_ms = 30000; // wait some more for email to send+settle
          sPendingEmail = NULL;
        } else {
          sState = sStateAfterWait;
          sStateAfterWait = Initialize;
        }
      }
      break;
  }

}

