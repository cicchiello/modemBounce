/*
  Periodically, in the following order, check that:
      1) the wifi router is reachable (AP Connection)
      2) the router is reachable (ping to 10.0.0.1)
      2) the cable modem is reachable (ping to 192.168.100.1)
      3) a DNS server is reachable (ping 8.8.8.8)
      4) RemoteBounce has not been requested: GET of http://10.0.0.211:5984/bouncy/bounce
          (10.0.0.211 is the CouchDb @ Mediaserver)
  
  If any of those checks fail:
      1) trigger power-cycle of router, or
      2) trigger power-cycle of cable modem

  Consistent logging is sent via web service @ http://10.0.0.214:8091/log
  Significant events (start, reset, power-events) trigger email messages via web service @ http://10.0.0.214:8093/send
  
  Copyright Joe Cicchiello 2026

*/


#include <SPI.h>
#include <WiFi101.h>
#include "SoftWDT.h"
#include "HttpLogger.h"
#include "Logger.h"
#include "Connector.h"
#include "Indicator.h"
#include "EmailRelay.h"
#include "arduino_secrets.h" 

#define PROGNAME "Bouncy"


enum State {Initialize, Wait, Sleep, APConnect, RouterPing, ModemPing, InternetPing, DnsPing, RemoteBounceCheck};
static const char *StateNames[] = 
           {"Initialize","Wait","Sleep","APConnect","RouterPing","ModemPing","InternetPing","DnsPing","RemoteBounceCheck"};

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

static const char *COUCHDB_HOST = "10.0.0.211";
static const uint16_t COUCHDB_PORT = 5984;
static const char *COUCHDB_DOC_PATH = "/bouncy/bounce";

static HttpLogger sHttpLog(LOG_HOST, LOG_PORT, LOG_PATH, SECRET_LOGGING_KEY);
static Logger Log(&sHttpLog, sTest == PROD ? LOG_TO_HTTP : LOG_TO_BOTH);

static const uint8_t FACTORY_RESET_PIN = A0;
static const unsigned long FACTORY_RESET_HOLD_MS = 3000;

static const uint8_t MODEM_RELAY_PIN = A5;
static const uint8_t ROUTER_RELAY_PIN = 5;
static IndicatorLED sRGB;

static Connector sConnector(sRGB, Log);

static int sFailureCnt = 0;
static int sCycleCnt = 0;
static const char *sPendingEmail = NULL;

static unsigned long sDiagnosticBlinkNextMs = 0;
static bool sTestWDT = false;

#define STATE_LOG_MAGIC (0xDEADBEEF)

// WDT Timeout chosen based on worst-case delay expected.
// If DNS lookup fails, it could take 25s: 
//   - DNS resolution: up to 20 seconds
//   - Ping: up to 5 seconds
#define WDT_TIMEOUT_MS (26000)


struct StateLog {
  uint32_t magic;
  int resetCnt;
  State lastState;
  State pendingState;
};

SoftWatchdog *sWatchdog = NULL;



static const char *StateName(State s) {
  if (((int) s < Initialize) || ((int) s > RemoteBounceCheck)) return "unknown";
  else return StateNames[(int) s];
}


static void diagnosticBlink(int cause) {
  bool savedRed = sRGB.getRed();
  bool savedGreen = sRGB.getGreen();
  bool savedBlue = sRGB.getBlue();
  
  int numBlinks = 0;
  bool isRed = false, isGreen = false, isBlue = false;

  isGreen = isBlue = true;
  switch (cause) {
    case WD_RESET_POWER_ON:    numBlinks = 1; break;
    case WD_RESET_WATCHDOG:    numBlinks = 2; break;
    case WD_RESET_EXTERNAL:    numBlinks = 3; break;
    case WD_RESET_BOD:         numBlinks = 4; break;
    case WD_RESET_SYST:        numBlinks = 5; break;
    case WD_RESET_UNKNOWN:
    default:                   numBlinks = 6; break;
  }

  for (int i = 0; i < numBlinks; i++) {
    sRGB.off();
    watchdog_delay(150);
    if (isGreen) sRGB.setGreen(true);
    if (isRed) sRGB.setRed(true);
    if (isBlue) sRGB.setBlue(true);
    watchdog_delay(150);
  }

  sRGB.off();
  watchdog_delay(500);

  sRGB.setRed(savedRed);
  sRGB.setGreen(savedGreen);
  sRGB.setBlue(savedBlue);
  watchdog_delay(500);
}


static const char *to_str(const IPAddress &ip)
{
  static char buf[20];
  sprintf(buf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return buf;
}


static void enableRouter(bool on) {
  if (on) {
    Log.printf("INFO(%ld): enableRouter turn on\n", millis());
    digitalWrite(ROUTER_RELAY_PIN, LOW);  // relay is normally-closed, so LOW de-energizes it (contacts close, AC on)
  } else {
    Log.printf("INFO(%ld): enableRouter turn off\n", millis());
    digitalWrite(ROUTER_RELAY_PIN, HIGH);  // relay is normally-closed, so HIGH energizes it (contacts open, AC off)
  }
}


static void enableModem(bool on) {
  if (on) {
    Log.printf("INFO(%ld): enableModem turn on\n", millis());
    digitalWrite(MODEM_RELAY_PIN, LOW);  // relay is normally-closed, so LOW de-energizes it (contacts close, AC on)
  } else {
    Log.printf("INFO(%ld): enableModem turn off\n", millis());
    digitalWrite(MODEM_RELAY_PIN, HIGH);  // relay is normally-closed, so HIGH energizes it (contacts open, AC off)
  }
}


static void send_reset_email(const char *reason) {
  Log.printf("INFO(%ld): system restarted (due to %s); sending an email...\n", millis(), reason);
  
  EmailRelay Mailer(EMAIL_HOST, EMAIL_PORT, EMAIL_PATH, SECRET_EMAIL_KEY);
  Mailer.begin();

  char subject[30];
  sprintf(subject, "ALERT %s", PROGNAME);
  char body[50];
  sprintf(body, "system restarted (due to %s)", reason);
  if (!Mailer.send(subject, body)) {
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


void watchdog_delay(unsigned long ms)
{
   sWatchdog->pet();
   while (ms > 100) {
     delay(100);
     sWatchdog->pet();
     ms -= 100;
   }
   if (ms > 0) {
     delay(ms);
     sWatchdog->pet();
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
  watchdog_delay(15000);
  
  enableModem(true);
  
  // wait for 210s while modem initializes
  Log.printf("INFO(%ld): waiting 3 minutes for modem initialization...\n", millis());
  watchdog_delay(210000);

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
  watchdog_delay(15000);
  
  enableRouter(true);
  
  // wait for 210s while router initializes
  Log.printf("INFO(%ld): waiting 3 minuites for router initialization...\n", millis());
  watchdog_delay(210000);
  
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
  StateLog *pram = (StateLog*) sWatchdog->pram();
  pram->lastState = sState;
  pram->pendingState = nextState;
}


void printWiFiStatus() {
  // the SSID of the network has already been reported in the attempt message
  
  // print your WiFi shield's IP address:
  Log.printf("INFO(%ld): IP Address: %s\n", millis(), to_str(WiFi.localIP()));

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Log.printf("INFO(%ld): signal strength (RSSI): %ld dBm\n\n", millis(), rssi);
}


static int ping_test(const char *devname, const char *ipstr, State thisState, State successState, void (*bounceFtor)()) {
  //Log.printf("DEBUG(%ld): pinging %s @ %s\n", millis(), devname, ipstr);
  if (WiFi.ping(ipstr) > 0) {
    Log.printf("INFO(%ld): successful ping of %s @ %s\n", millis(), devname, ipstr);
    nextState(successState, 2*1000);
    sFailureCnt = 0;
  } else {
    sFailureCnt++;
    Log.printf("WARNING(%ld): couldn't connect to %s @ %s (failure %d/%d)\n",
               millis(), devname, ipstr, sFailureCnt, NUM_FAILURES_TO_BOUNCE);
    if (sFailureCnt < NUM_FAILURES_TO_BOUNCE) {
      Log.printf("INFO(%ld): retrying in %ds...\n", millis(), sTest == PROD ? 60 : 10);
      nextState(thisState, sTest == PROD ? 60*1000 : 10*1000);
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
  watchdog_delay(20);

  unsigned long start = millis();

  Log.printf("INFO(%ld): System Reset; considering Factory reset\n", millis());

  while (digitalRead(FACTORY_RESET_PIN) == LOW) {
    unsigned long held = millis() - start;

    if (held >= FACTORY_RESET_HOLD_MS) {
      Log.printf("WARNING(%ld): Factory reset requested\n", millis());

      // Wait for release before continuing, so the button bounce
      // does not immediately trigger another reset cycle.
      while (digitalRead(FACTORY_RESET_PIN) == LOW) {
        watchdog_delay(50);
        sRGB.toggleRed();
      }
      sRGB.setRed(false);

      watchdog_delay(100);
      return true;
    }

    watchdog_delay(20);
  }

  Log.printf("INFO(%ld): Factory reset not requested\n", millis());
  return false;
}




static bool extractJsonStringValue(const char *json, const char *key, char *out, size_t outLen) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

  const char *start = strstr(json, pattern);
  if (!start) return false;

  start += strlen(pattern);
  const char *end = strchr(start, '"');
  if (!end) return false;

  size_t len = end - start;
  if (len >= outLen) return false;

  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}


static bool clearRemoteBounceFlag(const char *rev) {
  WiFiClient client;

  if (!client.connect(COUCHDB_HOST, COUCHDB_PORT)) {
    Log.printf("WARNING(%ld): RemoteBounce: couldn't connect to CouchDB for PUT\n", millis());
    return false;
  }

  char body[256];
  snprintf(body, sizeof(body),
           "{\"_id\":\"bounce\",\"_rev\":\"%s\",\"device\":null}", rev);
  size_t len = strlen(body);

  client.print("PUT ");
  client.print(COUCHDB_DOC_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(COUCHDB_HOST);
  client.print("Authorization: Basic ");
  client.println(SECRET_COUCHDB_BASIC_AUTH);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(len);
  client.println("Connection: close");
  client.println();
  client.write((const uint8_t *)body, len);

  unsigned long deadline = millis() + 10000;
  while (client.connected() && !client.available() && millis() < deadline) {
    watchdog_delay(10);
  }

  int status = 0;
  if (client.available()) {
    String statusLine = client.readStringUntil('\n');
    int firstSpace = statusLine.indexOf(' ');
    if (firstSpace >= 0)
      status = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
  }

  client.stop();

  if (status == 201) {
    Log.printf("INFO(%ld): RemoteBounce: cleared remote bounce flag\n", millis());
    return true;
  } else {
    Log.printf("WARNING(%ld): RemoteBounce: failed to clear flag (HTTP %d)\n", millis(), status);
    return false;
  }
}


static void checkRemoteBounce() {
  WiFiClient client;

  if (!client.connect(COUCHDB_HOST, COUCHDB_PORT)) {
    Log.printf("WARNING(%ld): RemoteBounce: couldn't connect to CouchDB\n", millis());
    return;
  }

  client.print("GET ");
  client.print(COUCHDB_DOC_PATH);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(COUCHDB_HOST);
  client.print("Authorization: Basic ");
  client.println(SECRET_COUCHDB_BASIC_AUTH);
  client.println("Accept: application/json");
  client.println("Connection: close");
  client.println();

  unsigned long deadline = millis() + 10000;
  while (client.connected() && !client.available() && millis() < deadline) {
    watchdog_delay(10);
  }

  if (!client.available()) {
    Log.printf("WARNING(%ld): RemoteBounce: no response from CouchDB\n", millis());
    client.stop();
    return;
  }

  String statusLine = client.readStringUntil('\n');
  int firstSpace = statusLine.indexOf(' ');
  int status = (firstSpace >= 0)
    ? statusLine.substring(firstSpace + 1, firstSpace + 4).toInt() : 0;

  if (status != 200) {
    Log.printf("WARNING(%ld): RemoteBounce: HTTP status %d from CouchDB\n", millis(), status);
    client.stop();
    return;
  }

  // skip headers
  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }

  // read body
  char body[512];
  size_t bodyLen = 0;
  deadline = millis() + 5000;
  while (bodyLen < sizeof(body) - 1 && millis() < deadline) {
    if (client.available()) {
      body[bodyLen++] = client.read();
    } else if (!client.connected()) {
      break;
    } else {
      watchdog_delay(1);
    }
  }
  body[bodyLen] = '\0';

  client.stop();

  bool wantModem = strstr(body, "\"device\":\"modem\"") != NULL;
  bool wantRouter = strstr(body, "\"device\":\"router\"") != NULL;

  if (!wantModem && !wantRouter) {
    Log.printf("INFO(%ld): RemoteBounce: not requested\n", millis());
    return;
  }

  char rev[128];
  if (!extractJsonStringValue(body, "_rev", rev, sizeof(rev))) {
    Log.printf("WARNING(%ld): RemoteBounce: couldn't parse _rev; skipping to avoid repeat bounces\n", millis());
    return;
  }

  // clear the request before bouncing anything to avoid a bounce infinite loop
  clearRemoteBounceFlag(rev);
  
  if (wantModem) {
    Log.printf("INFO(%ld): RemoteBounce: remote modem bounce requested\n", millis());
    bounce_modem();
  } else {
    Log.printf("INFO(%ld): RemoteBounce: remote router bounce requested\n", millis());
    bounce_router();
  }
}


void setup() {
  //Configure pins for Adafruit ATWINC1500 Feather
  WiFi.setPins(8,7,4,2);

  pinMode(MODEM_RELAY_PIN, OUTPUT);
  digitalWrite(MODEM_RELAY_PIN, LOW);
  pinMode(ROUTER_RELAY_PIN, OUTPUT);
  digitalWrite(ROUTER_RELAY_PIN, LOW);

  sState = Initialize;

  // Set up 16-second watchdog (uses overflow counter for extended timeout)
  sWatchdog = SoftWatchdog::singleton(WDT_TIMEOUT_MS);
  
  diagnosticBlink(sWatchdog->reset_cause());
  diagnosticBlink(sWatchdog->reset_cause());
  sDiagnosticBlinkNextMs = millis() + 30000;
  
  if ((Log.getMode() == LOG_TO_BOTH) || (Log.getMode() == LOG_TO_SERIAL)) {
    //Initialize serial and wait for port to open:
    Serial.begin(9600);
    while (!Serial) {
      ; // wait for serial port to connect. Needed for native USB port only
    }

    Serial.printf("\n\n");
  }

  bool factoryResetEnabled = checkFactoryResetButton();
  
  Log.printf("INFO(%ld): Watchdog enabled with timeout (ms): %ld\n", millis(), WDT_TIMEOUT_MS);
  
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

  sWatchdog->pet();
  send_reset_email(sWatchdog->reset_cause_name());
  sWatchdog->pet();

  StateLog *pram = (StateLog*) sWatchdog->pram();
  
  if (pram->magic == STATE_LOG_MAGIC) {
    pram->resetCnt++;
    Log.printf("WARNING(%ld): RAM valid (%d times); States: last: %s, pending: %s\n",
               millis(), pram->resetCnt, StateName(pram->lastState), StateName(pram->pendingState));
  } else {
    pram->magic = STATE_LOG_MAGIC;
    pram->resetCnt = 0;
    Log.printf("INFO(%ld): Power-on reset (RAM cleared)\n", millis());
  }
  
  sWatchdog->pet();
  pram->lastState = sState;
  pram->pendingState = sStateAfterWait;

  // consider testing mode: should one of the addresses be perturbed, or directly trigger something
  Log.printf("INFO(%ld): run mode: %s\n", millis(), Provision::RuntypeName(sTest));
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
    send_email("testing email");
  } else if (sTest == test_WDT) {
    sTestWDT = true;
  }

}


void loop() {

  StateLog *pram = (StateLog*) sWatchdog->pram();
  
  switch (sState) {
    case Initialize:
      sStateAfterWait = APConnect;
      sState = Wait;
      sWait_ms = 1000;
      pram->lastState = Initialize;
      pram->pendingState = APConnect;
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
            Log.printf("WARNING(%ld): failed; retrying in %ds..\n", millis(), sTest == PROD ? 60 : 10);
            nextState(APConnect, sTest == PROD ? 60*1000 : 10*1000);
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
      if (sTestWDT && (sCycleCnt == 1)) {
        // delay longer than WDT timeout
	Log.printf("DEBUG(%ld): testing WDT with long delay...\n", millis());
        delay(WDT_TIMEOUT_MS+1000); // note specific use of delay (instead of watchdog_delay)
	sTestWDT = false;
      } else {
        ping_test("dns", DNS(), DnsPing, RemoteBounceCheck, bounce_modem);
      }
      break;
    case RemoteBounceCheck:
      checkRemoteBounce();
      nextState(Sleep, 1000);
      break;
    case Sleep: 
      Log.printf("INFO(%ld): sleeping until next check in %d mins\n", millis(), TIME_BETWEEN_MONITOR_ATTEMPTS_ms/1000/60);
      nextState(Initialize, TIME_BETWEEN_MONITOR_ATTEMPTS_ms);
      sFailureCnt = 0;
      sCycleCnt++;
      break;
    case Wait:
      Log.flush();
      if (millis() >= sDiagnosticBlinkNextMs) {
        diagnosticBlink(sWatchdog->reset_cause());
        diagnosticBlink(sWatchdog->reset_cause());
        sDiagnosticBlinkNextMs = millis() + 30000;
      }
      watchdog_delay(1);
      if (--sWait_ms == 0) {
        if (sPendingEmail) {
          send_email(sPendingEmail);
          sWait_ms = 30000; // wait some more for email to send+settle
          sPendingEmail = NULL;
        } else {
	  pram->lastState = Wait;
	  pram->pendingState = sStateAfterWait;
          sState = sStateAfterWait;
          sStateAfterWait = Initialize;
        }
      }
      break;
  }

}

