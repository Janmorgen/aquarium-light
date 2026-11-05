#pragma once
#include <Arduino.h>
#include <functional>
enum HTTPMethod { HTTP_GET, HTTP_POST };
struct WebServer {
  WebServer(int){}
  void on(const char*, HTTPMethod, std::function<void()>);
  void onNotFound(std::function<void()>);
  void begin(); void handleClient();
  void send(int, const char*, const String&);
  void send(int, const char*, const char*);
  void send_P(int, const char*, const char*);
  String arg(const char*);
  bool hasArg(const char*);
  bool authenticate(const char*, const char*); void requestAuthentication();
};
