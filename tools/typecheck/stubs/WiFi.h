#pragma once
#include <Arduino.h>
#include <ctime>
#define WIFI_STA 1
#define WIFI_OFF 0
#define WL_CONNECTED 3
struct WiFiC { int status(); void mode(int); void begin(const char*,const char*); void disconnect(bool); };
extern WiFiC WiFi;
void configTime(long,int,const char*,const char*);
bool getLocalTime(struct tm*, uint32_t);
