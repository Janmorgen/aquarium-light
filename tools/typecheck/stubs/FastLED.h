#pragma once
#include <Arduino.h>
enum EOrder { GRB, RGB };
enum ELedType { WS2812B, NEOPIXEL };
uint8_t scale8(uint8_t,uint8_t); uint8_t qadd8(uint8_t,uint8_t);
uint8_t random8(); uint8_t random8(uint8_t,uint8_t); void random16_add_entropy(uint32_t);
struct CRGB { uint8_t r=0,g=0,b=0;
  CRGB(){} CRGB(uint8_t rr,uint8_t gg,uint8_t bb):r(rr),g(gg),b(bb){} CRGB(uint32_t){}
  CRGB& operator+=(const CRGB& o){ r=qadd8(r,o.r); g=qadd8(g,o.g); b=qadd8(b,o.b); return *this; }
  static const CRGB Black; };
CRGB blend(const CRGB&, const CRGB&, uint8_t);
struct FastLEDC { template<ELedType T,int P,EOrder O> void addLeds(CRGB*,int){}
  void setBrightness(uint8_t); void setMaxPowerInVoltsAndMilliamps(int,int);
  void clear(); void show(); };
extern FastLEDC FastLED;
