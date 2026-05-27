#pragma once

#include <Arduino.h>
#include "HttpLogger.h"

enum LogMode {
  LOG_TO_SERIAL,
  LOG_TO_HTTP,
  LOG_TO_BOTH
};

class Logger {
public:
  Logger(HttpLogger *httpLogger, LogMode mode)
    : _httpLogger(httpLogger), _mode(mode) {}

  void setMode(LogMode mode) {
    _mode = mode;
  }

  void print(const char *str) {
    if (_mode == LOG_TO_SERIAL || _mode == LOG_TO_BOTH) {
      Serial.print(str);
    }

    if ((_mode == LOG_TO_HTTP || _mode == LOG_TO_BOTH) && _httpLogger && _httpLogger->is_wifi_available()) {
      _httpLogger->print(str);
    }
  }

  void println(const char *line) {
    if (_mode == LOG_TO_SERIAL || _mode == LOG_TO_BOTH) {
      Serial.println(line);
    }

    if ((_mode == LOG_TO_HTTP || _mode == LOG_TO_BOTH) && _httpLogger && _httpLogger->is_wifi_available()) {
      _httpLogger->println(line);
    }
  }

  void printf(const char *fmt, ...) {
    char buf[256];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    print(buf);
  }

private:
  HttpLogger *_httpLogger;
  LogMode _mode;
};
