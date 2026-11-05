#pragma once
#include <Arduino.h>
#include <ctime>
#define WIFI_STA 1
#define WIFI_OFF 0
#define WL_CONNECTED 3
struct IPAddress { bool fromString(const char*); String toString() const; };
struct WiFiC {
  int status(); void mode(int); void begin(const char*,const char*); void disconnect(bool=false);
  void setSleep(bool); void setAutoReconnect(bool);
  bool config(IPAddress, IPAddress, IPAddress, IPAddress);
  IPAddress localIP(); int RSSI();
};
extern WiFiC WiFi;
void configTime(long,int,const char*,const char*);
bool getLocalTime(struct tm*, uint32_t);
