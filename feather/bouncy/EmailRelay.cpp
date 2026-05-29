#include "EmailRelay.h"  // included first to ensure it's self-contained

#include "EmailRelay.h"

#include <Arduino.h>
#include <WiFi101.h>
#include <string.h>
#include <stdio.h>


static const size_t MAX_SUBJECT_LEN = 120;
static const size_t MAX_MESSAGE_LEN = 2000;
static const size_t MAX_JSON_LEN = 2300;



static size_t jsonEscapedLength(const char *s) {
  size_t n = 0;

  while (*s) {
    char c = *s++;

    switch (c) {
      case '"':
      case '\\':
      case '\n':
      case '\r':
      case '\t':
        n += 2;
        break;

      default:
        if ((uint8_t)c >= 0x20) {
          n += 1;
        }
        break;
    }
  }

  return n;
}


static bool appendJsonEscaped(char *out, size_t outLen, size_t &pos, const char *s) {
  while (*s) {
    char c = *s++;

    switch (c) {
      case '"':
      case '\\':
        if (pos + 2 >= outLen) {
          return false;
        }
        out[pos++] = '\\';
        out[pos++] = c;
        break;

      case '\n':
        if (pos + 2 >= outLen) {
          return false;
        }
        out[pos++] = '\\';
        out[pos++] = 'n';
        break;

      case '\r':
        if (pos + 2 >= outLen) {
          return false;
        }
        out[pos++] = '\\';
        out[pos++] = 'r';
        break;

      case '\t':
        if (pos + 2 >= outLen) {
          return false;
        }
        out[pos++] = '\\';
        out[pos++] = 't';
        break;

      default:
        if ((uint8_t)c < 0x20) {
          // Drop other control characters.
          break;
        }

        if (pos + 1 >= outLen) {
          return false;
        }

        out[pos++] = c;
        break;
    }
  }

  out[pos] = '\0';
  return true;
}


static bool appendLiteral(char *out, size_t outLen, size_t &pos, const char *s) {
  while (*s) {
    if (pos + 1 >= outLen) {
      return false;
    }

    out[pos++] = *s++;
  }

  out[pos] = '\0';
  return true;
}


static bool writeJsonEscaped(char *out,
			     size_t outLen,
			     const char *subject,
			     const char *message) {
  size_t pos = 0;

  if (!appendLiteral(out, outLen, pos, "{\"subject\":\"")) {
    return false;
  }

  if (!appendJsonEscaped(out, outLen, pos, subject)) {
    return false;
  }

  if (!appendLiteral(out, outLen, pos, "\",\"message\":\"")) {
    return false;
  }

  if (!appendJsonEscaped(out, outLen, pos, message)) {
    return false;
  }

  if (!appendLiteral(out, outLen, pos, "\"}")) {
    return false;
  }

  if (pos >= outLen) {
    return false;
  }

  out[pos] = '\0';
  return true;
}


static int readHttpStatus(WiFiClient &client) {
  unsigned long deadline = millis() + 10000;

  while (client.connected() && !client.available() && millis() < deadline) {
    delay(10);
  }

  if (!client.available()) {
    return 0;
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();

  // Example: HTTP/1.0 200 OK
  int firstSpace = statusLine.indexOf(' ');
  if (firstSpace < 0) {
    return 0;
  }

  return statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
}




EmailRelay::EmailRelay(const char *host,
                       uint16_t port,
                       const char *path,
                       const char *key)
  : _host(host),
    _port(port),
    _path(path),
    _key(key) {
}

bool EmailRelay::begin() {
  return true;
}

bool EmailRelay::send(const char *subject, const char *message) {
  if (!subject || !message) {
    return false;
  }

  if (strlen(subject) == 0 || strlen(subject) > MAX_SUBJECT_LEN) {
    return false;
  }

  if (strlen(message) == 0 || strlen(message) > MAX_MESSAGE_LEN) {
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  char body[MAX_JSON_LEN];

  if (!writeJsonEscaped(body, sizeof(body), subject, message)) {
    return false;
  }

  WiFiClient client;

  if (!client.connect(_host, _port)) {
    return false;
  }

  size_t len = strlen(body);

  client.print("POST ");
  client.print(_path);
  client.println(" HTTP/1.1");

  client.print("Host: ");
  client.println(_host);

  client.print("X-Email-Relay-Key: ");
  client.println(_key);

  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(len);
  client.println("Connection: close");
  client.println();

  client.write((const uint8_t *)body, len);

  int status = readHttpStatus(client);

  client.stop();

  return status == 200;
}

