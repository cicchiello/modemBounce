#include "WifiCredStore.h"
#include "arduino_secrets.h"

#define EEPROM_EMULATION_SIZE 512
#include <FlashStorage_SAMD.h>

#include <Crypto.h>
#include <AES.h>
#include <CTR.h>
#include <SHA256.h>
#include <string.h>
#include <stddef.h>

static const int WIFI_CREDS_ADDR = 0;

bool WifiCredStore::begin() {
  return true;
}

bool WifiCredStore::constantTimeEqual(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;

  for (size_t i = 0; i < len; i++) {
    diff |= a[i] ^ b[i];
  }

  return diff == 0;
}

void WifiCredStore::makeIV(uint32_t counter, uint8_t iv[16]) {
  memset(iv, 0, 16);

  // Fixed prefix just identifies this use of the key.
  iv[0] = 'B';
  iv[1] = 'o';
  iv[2] = 'u';
  iv[3] = 'n';
  iv[4] = 'c';
  iv[5] = 'y';

  // Counter makes the CTR IV change on each save.
  iv[8]  = (uint8_t)((counter >> 24) & 0xff);
  iv[9]  = (uint8_t)((counter >> 16) & 0xff);
  iv[10] = (uint8_t)((counter >> 8) & 0xff);
  iv[11] = (uint8_t)(counter & 0xff);
}



void WifiCredStore::computeTag(const StoredWifiCreds &stored, uint8_t tagOut[32]) {
  SHA256 hash;

  hash.resetHMAC(SECRET_WIFI_MAC_KEY, sizeof(SECRET_WIFI_MAC_KEY));

  // Authenticate everything except the tag itself.
  hash.update((const uint8_t *)&stored, offsetof(StoredWifiCreds, tag));

  hash.finalizeHMAC(
    SECRET_WIFI_MAC_KEY,
    sizeof(SECRET_WIFI_MAC_KEY),
    tagOut,
    32
  );

  hash.clear();
}


bool WifiCredStore::verifyTag(const StoredWifiCreds &stored) {
  uint8_t expected[32];

  computeTag(stored, expected);

  return constantTimeEqual(expected, stored.tag, sizeof(stored.tag));
}

bool WifiCredStore::isValid(const StoredWifiCreds &stored) {
  if (stored.magic != StoredWifiCreds::MAGIC) {
    return false;
  }

  if (stored.version != StoredWifiCreds::VERSION) {
    return false;
  }

  return verifyTag(stored);
}

bool WifiCredStore::hasCredentials() {
  StoredWifiCreds stored;

  EEPROM.get(WIFI_CREDS_ADDR, stored);

  return isValid(stored);
}

bool WifiCredStore::load(char *ssidOut, size_t ssidOutLen,
                         char *pswdOut, size_t pswdOutLen) {
  if (!ssidOut || !pswdOut || ssidOutLen == 0 || pswdOutLen == 0) {
    return false;
  }

  ssidOut[0] = '\0';
  pswdOut[0] = '\0';

  StoredWifiCreds stored;
  EEPROM.get(WIFI_CREDS_ADDR, stored);

  if (!isValid(stored)) {
    return false;
  }

  PlainWifiCreds plain;
  memset(&plain, 0, sizeof(plain));

  CTR<AES128> ctr;

  if (!ctr.setKey(SECRET_WIFI_ENC_KEY, sizeof(SECRET_WIFI_ENC_KEY))) {
    return false;
  }

  if (!ctr.setIV(stored.iv, sizeof(stored.iv))) {
    return false;
  }

  ctr.decrypt((uint8_t *)&plain, stored.ciphertext, sizeof(plain));
  ctr.clear();

  plain.ssid[PlainWifiCreds::MAX_SSID_LEN] = '\0';
  plain.pswd[PlainWifiCreds::MAX_PSWD_LEN] = '\0';

  strncpy(ssidOut, plain.ssid, ssidOutLen - 1);
  ssidOut[ssidOutLen - 1] = '\0';

  strncpy(pswdOut, plain.pswd, pswdOutLen - 1);
  pswdOut[pswdOutLen - 1] = '\0';

  memset(&plain, 0, sizeof(plain));

  return true;
}

bool WifiCredStore::save(const char *ssid, const char *pswd) {
  if (!ssid || !pswd) {
    return false;
  }

  if (strlen(ssid) > MAX_SSID_LEN || strlen(pswd) > MAX_PSWD_LEN) {
    return false;
  }

  StoredWifiCreds oldStored;
  EEPROM.get(WIFI_CREDS_ADDR, oldStored);

  uint32_t nextCounter = 1;
  if (isValid(oldStored) && oldStored.counter < 0xffffffffUL) {
    nextCounter = oldStored.counter + 1;
  }

  PlainWifiCreds plain;
  memset(&plain, 0, sizeof(plain));

  strncpy(plain.ssid, ssid, PlainWifiCreds::MAX_SSID_LEN);
  plain.ssid[PlainWifiCreds::MAX_SSID_LEN] = '\0';

  strncpy(plain.pswd, pswd, PlainWifiCreds::MAX_PSWD_LEN);
  plain.pswd[PlainWifiCreds::MAX_PSWD_LEN] = '\0';

  StoredWifiCreds stored;
  memset(&stored, 0, sizeof(stored));

  stored.magic = StoredWifiCreds::MAGIC;
  stored.version = StoredWifiCreds::VERSION;
  stored.counter = nextCounter;

  makeIV(stored.counter, stored.iv);

  CTR<AES128> ctr;

  if (!ctr.setKey(SECRET_WIFI_ENC_KEY, sizeof(SECRET_WIFI_ENC_KEY))) {
    memset(&plain, 0, sizeof(plain));
    return false;
  }

  if (!ctr.setIV(stored.iv, sizeof(stored.iv))) {
    ctr.clear();
    memset(&plain, 0, sizeof(plain));
    return false;
  }

  ctr.encrypt(stored.ciphertext, (const uint8_t *)&plain, sizeof(plain));
  ctr.clear();

  computeTag(stored, stored.tag);

  EEPROM.put(WIFI_CREDS_ADDR, stored);
  EEPROM.commit();

  memset(&plain, 0, sizeof(plain));

  return true;
}

bool WifiCredStore::clear() {
  StoredWifiCreds stored;
  memset(&stored, 0, sizeof(stored));

  EEPROM.put(WIFI_CREDS_ADDR, stored);
  EEPROM.commit();

  return true;
}

