#ifndef SECRETS_TEMPLATE_H
#define SECRETS_TEMPLATE_H

#include <Arduino.h>

// Copy this file to "secrets.h" and configure your credentials.
// Use the xor_generator.py tool to generate XOR bytes.
namespace Secrets {
  const uint8_t XOR_KEY = 0x5E;

  const uint8_t AP_SSID_A[]  = { /* XOR Bytes for Node A Wi-Fi SSID */ };
  const uint8_t AP_SSID_B[]  = { /* XOR Bytes for Node B Wi-Fi SSID */ };
  const uint8_t AP_PASS[]    = { /* XOR Bytes for Access Point WPA2 Password */ };
  const uint8_t WEB_PASS[]   = { /* XOR Bytes for login Web Password */ };
  const uint8_t ADMIN_PASS[] = { /* XOR Bytes for Administrative commands Password */ };

  inline String get(const uint8_t* enc, size_t len) {
    String out = "";
    for (size_t i = 0; i < len; i++) {
      out += (char)(enc[i] ^ XOR_KEY);
    }
    return out;
  }
}

#endif
