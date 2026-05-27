#pragma once

#include <Arduino.h>

struct PlainWifiCreds {
  static const size_t MAX_SSID_LEN = 32;
  static const size_t MAX_PSWD_LEN = 64;

  char ssid[MAX_SSID_LEN + 1];
  char pswd[MAX_PSWD_LEN + 1];
};

struct StoredWifiCreds {
  static const uint32_t MAGIC = 0x57464352;  // "WFCR"
  static const uint16_t VERSION = 2;

  uint32_t magic;
  uint16_t version;
  uint16_t reserved;

  uint32_t counter;
  uint8_t iv[16];

  uint8_t ciphertext[sizeof(PlainWifiCreds)];
  uint8_t tag[32];   // HMAC-SHA256
};

class WifiCredStore {
public:
  static const size_t MAX_SSID_LEN = PlainWifiCreds::MAX_SSID_LEN;
  static const size_t MAX_PSWD_LEN = PlainWifiCreds::MAX_PSWD_LEN;

  bool begin();

  bool hasCredentials();
  bool load(char *ssidOut, size_t ssidOutLen,
            char *pswdOut, size_t pswdOutLen);

  bool save(const char *ssid, const char *pswd);
  bool clear();

private:
  bool isValid(const StoredWifiCreds &stored);
  bool verifyTag(const StoredWifiCreds &stored);
  void computeTag(const StoredWifiCreds &stored, uint8_t tagOut[32]);

  void makeIV(uint32_t counter, uint8_t iv[16]);
  bool constantTimeEqual(const uint8_t *a, const uint8_t *b, size_t len);
};
