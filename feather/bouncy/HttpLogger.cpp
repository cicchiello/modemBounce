#include "HttpLogger.h"

#include <WiFi101.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

HttpLogger::HttpLogger(const char *host, uint16_t port, const char *path, const char *key)
  : _host(host),
    _port(port),
    _path(path),
    _key(key),
    _wifi_available(false) {
}

bool HttpLogger::print(const char *str) {
  return logLine(str);
}

bool HttpLogger::println(const char *line) {
  return logLine(line);
}

bool HttpLogger::printf(const char *fmt, ...) {
  char buf[256];

  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  return logLine(buf);
}

bool HttpLogger::logLine(const char *line) {
  WiFiClient client;

  if (!client.connect(_host, _port)) {
    Serial.println("HttpLogger: connect failed");
    return false;
  }

  size_t len = strlen(line);

  client.print("PUT ");
  client.print(_path);
  client.println(" HTTP/1.1");

  client.print("Host: ");
  client.println(_host);

  client.print("X-Log-Key: ");
  client.println(_key);

  client.println("Content-Type: text/plain");
  client.print("Content-Length: ");
  client.println(len);
  client.println("Connection: close");
  client.println();

  client.write((const uint8_t *)line, len);

  unsigned long deadline = millis() + 3000;
  while (client.connected() && !client.available() && millis() < deadline) {
    delay(10);
  }

  int status = 0;

  if (client.available()) {
    String statusLine = client.readStringUntil('\n');

    int firstSpace = statusLine.indexOf(' ');
    if (firstSpace >= 0) {
      status = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
    }
  }

  client.stop();

  return status == 204;
}
