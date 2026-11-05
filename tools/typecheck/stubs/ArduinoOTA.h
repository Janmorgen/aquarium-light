#pragma once
#include <Arduino.h>
#include <functional>
#define U_FLASH 0
enum ota_error_t { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR,
                   OTA_RECEIVE_ERROR, OTA_END_ERROR };
struct ArduinoOTAC {
  void setPort(int); void setHostname(const char*); void setPassword(const char*);
  void onStart(std::function<void()>); void onEnd(std::function<void()>);
  void onProgress(std::function<void(unsigned int, unsigned int)>);
  void onError(std::function<void(ota_error_t)>);
  void begin(); void handle(); int getCommand();
};
extern ArduinoOTAC ArduinoOTA;
