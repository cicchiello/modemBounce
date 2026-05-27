#pragma once

#include <Arduino.h>

class HttpLogger {
public:
  HttpLogger(const char *host, uint16_t port, const char *path, const char *key);

  bool is_wifi_available() const {return _wifi_available;}
  void set_wifi_available() {_wifi_available = true;}

  bool logLine(const char *line);
  bool println(const char *line);
  bool print(const char *str);
  bool printf(const char *fmt, ...);

private:
  const char *_host;
  uint16_t _port;
  const char *_path;
  const char *_key;
  bool _wifi_available;
};

