#pragma once

// forward declarations
class HttpLogger;

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

  LogMode getMode() const {
    return _mode;
  }

  void println(const char *line);

  void printf(const char *fmt, ...);

  void flush();

private:
  HttpLogger *_httpLogger;
  LogMode _mode;
};

