#pragma once

#include <stdint.h>

class EmailRelay {
public:
  EmailRelay(const char *host,
             uint16_t port,
             const char *path,
             const char *key);

  bool begin();

  bool send(const char *subject, const char *message);

private:
  const char *_host;
  uint16_t _port;
  const char *_path;
  const char *_key;
};
