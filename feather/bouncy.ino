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
    - send email
    - provide blinking status indicator light
    - find a way to headless-log
    - sleep mode between connectivity tests
    - find a way to persist the config stuff
    - provide access point for config stuff: definitions of SSID,PSWD,email-smtp-details
*/


#include <SPI.h>
#include <WiFi101.h>

static const char *Progname = "Bouncy";

#include "arduino_secrets.h" 
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
static const char ssid[] = SECRET_SSID;        // your network SSID (name)
static const char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
static const int keyIndex = 0;            // your network key Index number (needed only for WEP)



enum Runtype {PROD, test_PROD, test_ROUTER, test_IP, test_MODEM, test_DNS, test_BOUNCE_MODEM, test_BOUNCE_ROUTER, test_EMAIL};
enum State {Initialize, Wait, Sleep, RouterPing, ModemPing, InternetPing, DnsPing};

static const Runtype sTest = test_DNS;
static State sState = Initialize;
static State sStateAfterWait = Initialize;
static long sWait_ms = 0;

#define TIME_BETWEEN_MONITOR_ATTEMPTS_ms (sTest == PROD ? 30*60*1000 : 3*60*1000) 
#define NUM_FAILURES_TO_BOUNCE (sTest == PROD ? 5 : 3)

static const IPAddress Internet(8,8,8,(sTest != test_IP ? 8 : 81));
static const char *DNS = (sTest != test_DNS ? "google.com" : "google.comfoo");
static const IPAddress ModemIP(192, 168, 100, (sTest != test_MODEM ? 1 : 111));
static const IPAddress RouterIP(10, 0, 0, (sTest != test_ROUTER ? 1 : 111));




static int sFailureCnt = 0;


static const char *to_str(const IPAddress &ip) {
  static char buf[20];
  sprintf(buf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return buf;
}

static const char *to_str(const char *s) {return s;}


static void enableRouter(bool on) {
  Serial.printf("WARNING(%ld): enableRouter not implemented yet...\n", millis());
  if (on) {
    Serial.printf("INFO(%ld): enableRouter turn on\n", millis());
  } else {
    Serial.printf("INFO(%ld): enableRouter turn off\n", millis());
  }
}


static void enableModem(bool on) {
  Serial.printf("WARNING(%ld): enableModem not implemented yet...\n", millis());
  if (on) {
    Serial.printf("INFO(%ld): enableModem turn on\n", millis());
  } else {
    Serial.printf("INFO(%ld): enableModem turn off\n", millis());
  }
}


static void bounce_modem() {
  Serial.printf("WARNING(%ld): bouncing modem...\n", millis());
  enableModem(false);
  delay(15*1000);
  enableModem(true);
  delay(210*1000);
  Serial.printf("INFO(%ld): continuing with normal operations after bouncing router...\n", millis());
}

static void bounce_router() {
  Serial.printf("WARNING(%ld): bouncing router...\n", millis());
  enableRouter(false);
  delay(15*1000);
  enableRouter(true);
  delay(210*1000);
  Serial.printf("INFO(%ld): continuing with normal operations after bouncing router...\n", millis());
}

static void send_email(const char *msg) {
  Serial.printf("WARNING(%ld): send_email not implemented yet (%s)\n", millis(), msg);
}


static void nextState(State nextState, long delay) {
  sStateAfterWait = nextState;
  sState = Wait;
  sWait_ms = delay;  
}


void printWiFiStatus() {
  Serial.printf("INFO(%ld): Connected to wifi\n", millis());

  // print the SSID of the network you're attached to:
  Serial.printf("INFO(%ld): SSID: %s\n", millis(), WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.printf("INFO(%ld): IP Address: ", millis());
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.printf("INFO(%ld): signal strength (RSSI): %ld dBm\n\n", millis(), rssi);
}


static int ping_test(const char *devname, const char *ipstr, State thisState, State successState, void (*bounceFtor)()) {
  Serial.printf("INFO(%ld): pinging %s @ %s\n", millis(), devname, ipstr);
  if (WiFi.ping(ipstr) > 0) {
    Serial.printf("INFO(%ld):: successful ping of %s @ %s\n", millis(), devname, ipstr);
    nextState(successState, 2*1000);
  } else {
    sFailureCnt++;
    Serial.printf("ERROR(%ld): couldn't connect to %s @ %s (failure %d/%d)\n", millis(), devname, ipstr, sFailureCnt, NUM_FAILURES_TO_BOUNCE);
    if (sFailureCnt < NUM_FAILURES_TO_BOUNCE) {
      nextState(thisState, 60*1000);
    } else {
      // all tries exhausted...  have to bounce the device
      Serial.printf("WARNING(%ld): All %s tries exhausted; bouncing %s...\n", millis(), devname, devname);
      bounceFtor();
      sFailureCnt = 0;
      nextState(Initialize, 1*1000);
    }
  }
  return 0;
}


void setup() {
  //Configure pins for Adafruit ATWINC1500 Feather
  WiFi.setPins(8,7,4,2);

  sState = Initialize;

  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.printf("ERROR(%ld): WiFi shield not present; can't proceed.\n", millis());

    // can't/don't continue:
    while (true);
  }

  Serial.printf("\n\n");

  // attempt to connect to WiFi network:
  int wifi_status = WL_IDLE_STATUS;
  while (wifi_status != WL_CONNECTED) {
    Serial.printf("INFO(%ld): Attempting to connect to SSID: %s\n", millis(), ssid);

    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifi_status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
  printWiFiStatus();

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
    Serial.printf("INFO(%ld): %s testing email delivery\n", millis(), "foo");
    send_email("testing email");
  }

}


void loop() {

  switch (sState) {
    case Initialize: 
      sStateAfterWait = RouterPing;
      sState = Wait;
      sWait_ms = 1000;
      break;
    case RouterPing: 
      ping_test("router", to_str(RouterIP), RouterPing, ModemPing, bounce_router);
      break;
    case ModemPing: 
      ping_test("modem", to_str(ModemIP), ModemPing, InternetPing, bounce_modem);
      break;
    case InternetPing: 
      ping_test("internet", to_str(Internet), InternetPing, DnsPing, bounce_modem);
      break;
    case DnsPing: 
      ping_test("dns", DNS, DnsPing, Sleep, bounce_modem);
      break;
    case Sleep: 
      Serial.printf("INFO(%ld): sleeping until next check in %d mins\n", millis(), TIME_BETWEEN_MONITOR_ATTEMPTS_ms/1000/60);
      nextState(Initialize, TIME_BETWEEN_MONITOR_ATTEMPTS_ms);
      sFailureCnt = 0;
      break;
    case Wait: 
      delay(1);
      if (--sWait_ms == 0) {
        sState = sStateAfterWait;
        sStateAfterWait = Initialize;
      }
      break;
  }

}



