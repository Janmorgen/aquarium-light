#pragma once
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
using std::min; using std::max; using std::abs;
struct __FlashStringHelper;
#define F(x) ((const __FlashStringHelper*)(x))
#define INPUT 0
#define INPUT_PULLUP 1
#define INPUT_PULLDOWN 2
#define HIGH 1
#define LOW 0
typedef int gpio_num_t;
#define GPIO_NUM_19 19
uint32_t millis(); uint32_t micros(); void delay(uint32_t); void delayMicroseconds(uint32_t);
long random(long); long random(long,long); void randomSeed(unsigned long);
void pinMode(int,int); int digitalRead(int); int analogRead(int);
long map(long,long,long,long,long);
template<class T> T constrain(T v, T lo, T hi){ return v<lo?lo:(v>hi?hi:v); }
struct SerialC { void begin(long); void println(); void println(const char*); void println(int);
  void println(const __FlashStringHelper*);
  void print(const char*); void print(int); void print(const __FlashStringHelper*); };
extern SerialC Serial;
