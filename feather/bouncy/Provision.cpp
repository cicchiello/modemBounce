#include "Provision.h" // included first to ensure it's self-contained

#include <WiFi101.h>
#include "WifiCredStore.h"
#include "Indicator.h"
#include "Logger.h"


struct HttpRequest {
  String method;
  String path;
  size_t contentLength;
};

static const size_t MAX_FORM_BODY = 256;



static void sendSetupPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();

  client.println("<!doctype html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<title>Bouncy WiFi Setup</title>");
  client.println("<style>");
  client.println("body{font-family:sans-serif;margin:2em;max-width:30em;}");  
  client.println("label{display:block;margin-top:1em;font-weight:bold;}");
  client.println("input[type=text],input[type=password]{width:100%;font-size:1.1em;padding:.4em;box-sizing:border-box;}");
  client.println("input[type=radio]{width:auto;margin-right:.5em;}");
  client.println("fieldset{margin-top:1.5em;border:1px solid #ccc;padding:1em;}");
  client.println("legend{font-weight:bold;}");
  client.println("button{margin-top:1.5em;font-size:1.1em;padding:.6em 1em;}");
  client.println(".note{color:#555;font-size:.9em;}");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  client.println("<h1>Bouncy WiFi Setup</h1>");
  client.println("<p>Enter the WiFi network credentials for this device.</p>");
  client.println("<form method='POST' action='/save'>");

  client.println("<label for='ssid'>WiFi SSID</label>");
  client.println("<input id='ssid' name='ssid' type='text' maxlength='32' required>");
  
  client.println("<label for='pswd'>WiFi Password</label>");
  client.println("<input id='pswd' name='pswd' type='password' maxlength='64'>");

  client.println("<fieldset>");
  client.println("<legend>Run mode</legend>");

  client.println("<label><input type='radio' name='rtype' value='PROD' checked> Normal production mode</label>");
  client.println("<label><input type='radio' name='rtype' value='test_PROD'> Test: production path</label>");
  client.println("<label><input type='radio' name='rtype' value='test_ROUTER'> Test: router reachability</label>");
  client.println("<label><input type='radio' name='rtype' value='test_IP'> Test: IP connectivity</label>");
  client.println("<label><input type='radio' name='rtype' value='test_MODEM'> Test: modem reachability</label>");
  client.println("<label><input type='radio' name='rtype' value='test_DNS'> Test: DNS</label>");
  client.println("<label><input type='radio' name='rtype' value='test_BOUNCE_MODEM'> Test: bounce modem</label>");
  client.println("<label><input type='radio' name='rtype' value='test_BOUNCE_ROUTER'> Test: bounce router</label>");
  client.println("<label><input type='radio' name='rtype' value='test_EMAIL'> Test: email</label>");

  client.println("</fieldset>");  

  client.println("<button type='submit'>Save WiFi Settings</button>");
  client.println("</form>");
  client.println("<p class='note'>After saving, restart the device or wait for it to reconnect.</p>");
  client.println("</body>");
  client.println("</html>");
}


static bool readRequest(WiFiClient &client, HttpRequest &req) {
  req.method = "";
  req.path = "";
  req.contentLength = 0;

  unsigned long deadline = millis() + 3000;

  while (!client.available() && millis() < deadline) {
    delay(5);
  }

  if (!client.available()) {
    return false;
  }

  String requestLine = client.readStringUntil('\n');
  requestLine.trim();

  int sp1 = requestLine.indexOf(' ');
  int sp2 = requestLine.indexOf(' ', sp1 + 1);
 
  if (sp1 < 0 || sp2 < 0) {
    return false;
  }

  req.method = requestLine.substring(0, sp1);
  req.path = requestLine.substring(sp1 + 1, sp2);

  while (client.connected() && millis() < deadline) {
    String line = client.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
      return true;
    }

    String lower = line;
    lower.toLowerCase();

    if (lower.startsWith("content-length:")) {
      String v = line.substring(strlen("content-length:"));
      v.trim();
      req.contentLength = v.toInt();
    }
  }

  return false;
}


static void sendBadRequest(WiFiClient &client, const char *msg) {
  client.println("HTTP/1.1 400 Bad Request");
  client.println("Content-Type: text/plain; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.println(msg);
}


static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}


static String urlDecode(const String &s) {
  String out;

  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];

    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      int hi = hexVal(s[i + 1]);
      int lo = hexVal(s[i + 2]);

      if (hi >= 0 && lo >= 0) {
        out += (char)((hi << 4) | lo);
        i += 2;
      } else {
        out += c;
      }
    } else {
      out += c;
    }
  }

  return out;
}


static String formValue(const String &body, const char *name) {
  String prefix = String(name) + "=";

  size_t start = 0;
  while (start < body.length()) {
    int amp = body.indexOf('&', start);
    if (amp < 0) amp = body.length();

    String pair = body.substring(start, amp);

    if (pair.startsWith(prefix)) {
      return urlDecode(pair.substring(prefix.length()));
    }

    start = amp + 1;
  }

  return "";
}


static void sendSavedPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();

  client.println("<!doctype html>");
  client.println("<html><head>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<title>Bouncy WiFi Saved</title>");
  client.println("</head><body>");
  client.println("<h1>WiFi Settings Saved</h1>");
  client.println("<p>The device has saved the WiFi credentials.</p>");
  client.println("<p>You may now disconnect from this setup network and restart the device.</p>");
  client.println("</body></html>");
}


static bool parseRuntype(const String &s, Runtype &rtype) {
  if (s == "PROD") {
    rtype = PROD;
    return true;
  }

  if (s == "test_PROD") {
    rtype = test_PROD;
    return true;
  }

  if (s == "test_ROUTER") {
    rtype = test_ROUTER;
    return true;
  }

  if (s == "test_IP") {
    rtype = test_IP;
    return true;
  }

  if (s == "test_MODEM") {
    rtype = test_MODEM;
    return true;
  }

  if (s == "test_DNS") {
    rtype = test_DNS;
    return true;
  }

  if (s == "test_BOUNCE_MODEM") {
    rtype = test_BOUNCE_MODEM;
    return true;
  }

  if (s == "test_BOUNCE_ROUTER") {
    rtype = test_BOUNCE_ROUTER;
    return true;
  }

  if (s == "test_EMAIL") {
    rtype = test_EMAIL;
    return true;
  }

  return false;
}


static bool handleSaveRequest(WiFiClient &client,
			      const HttpRequest &req,
			      WifiCredStore &wifiStore,
			      Runtype &rtype) {
  if (req.contentLength <= 0 || req.contentLength > MAX_FORM_BODY) {
    sendBadRequest(client, "Invalid form size");
    return false;
  }

  String body;
  body.reserve(req.contentLength);

  unsigned long deadline = millis() + 3000;
  while (body.length() < req.contentLength && millis() < deadline) {
    while (client.available() && body.length() < req.contentLength) {
      body += (char)client.read();
    }
    delay(2);
  }

  if (body.length() != req.contentLength) {
    sendBadRequest(client, "Timed out reading form");
    return false;
  }

  String ssid = formValue(body, "ssid");
  String pswd = formValue(body, "pswd");

  if (ssid.length() == 0) {
    sendBadRequest(client, "SSID is required");
    return false;
  }

  if (ssid.length() > WifiCredStore::MAX_SSID_LEN) {
    sendBadRequest(client, "SSID is too long");
    return false;
  }

  if (pswd.length() > WifiCredStore::MAX_PSWD_LEN) {
    sendBadRequest(client, "Password is too long");
    return false;
  }


  String rtypeText = formValue(body, "rtype");

  if (rtypeText.length() == 0) {
    sendBadRequest(client, "Run mode is required");
    return false;
  }

  Runtype selectedRtype;
  if (!parseRuntype(rtypeText, selectedRtype)) {
    sendBadRequest(client, "Invalid run mode");
    return false;
  }

  if (!wifiStore.save(ssid.c_str(), pswd.c_str())) {
    sendBadRequest(client, "Failed to save credentials");
    return false;
  }

  rtype = selectedRtype;

  sendSavedPage(client);
  return true;
}


static void sendRedirectRoot(WiFiClient &client) {
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /");
  client.println("Connection: close");
  client.println();
}


static bool handleProvisioningClient(WiFiClient &client, WifiCredStore &wifiStore, Runtype &rtype) {
  HttpRequest req;

  if (!readRequest(client, req)) {
    return false;
  }

  if (req.method == "GET") {
    sendSetupPage(client);
    return false;
  }

  if (req.method == "POST" && req.path == "/save") {
    return handleSaveRequest(client, req, wifiStore, rtype);
  }

  sendRedirectRoot(client);
  return false;
}




Provision::Provision(WifiCredStore &wifiStore, IndicatorLED &rgb,
		     Runtype &rtype, Logger &logger)
  : mWifiStore(wifiStore), mRGB(rgb), mRtype(rtype), mLogger(logger)
{
}


bool Provision::runFlow() {
  mLogger.printf("INFO(%ld): Starting AP for provisioning...\n", millis());

  mRGB.off();
  mRGB.setRed(true);
  
  int apStatus = WiFi.beginAP("Bouncy-Setup");

  if (apStatus != WL_AP_LISTENING) {
    mLogger.printf("FATAL(%ld): Failed to start AP, status=%d\n", millis(), apStatus);
    return false;
  }

  IPAddress ip = WiFi.localIP();

  char ipstr[20];
  snprintf(ipstr, sizeof(ipstr), "%u.%u.%u.%u",
           ip[0], ip[1], ip[2], ip[3]);

  mLogger.printf("INFO(%ld): AP started\n", millis());

  WiFiServer ProvisionServer(80);
  ProvisionServer.begin();

  mLogger.printf("INFO(%ld): Provisioning Web Server started\n", millis());
  mLogger.printf("INFO(%ld): Open http://%s/\n", millis(), ipstr);

  bool saved = false;
  while (!saved) {
    WiFiClient client = ProvisionServer.available();

    if (!client) {
      mRGB.toggleRed();
      delay(50);
      mRGB.toggleRed();
      delay(50);
      continue;
    }

    saved = handleProvisioningClient(client, mWifiStore, mRtype);

    mRGB.toggleRed();
    delay(50);
    mRGB.toggleRed();
    delay(50);
    
    client.stop();

    if (saved) {
      mLogger.printf("INFO(%ld): credentials saved\n", millis());
      mRGB.setRed(false);
      mRGB.setBlue(true);
    }
  }

  // Leave AP mode before caller tries station mode.
  WiFi.end();
  
  // pause for 1s while Wifi settles
  delay(10000);

  return true;
}


















