#pragma once

#include <Arduino.h>
#include <FastLED.h>

// A single addressable pixel that eases from its current color toward a
// target color over successive animation frames.
class pixel {
public:
  pixel();
  pixel(int index, CRGB color);

  CRGB getColor() const;
  void setColor(CRGB color);
  void setTargetColor(CRGB target);
  void setIndex(int index);
  int getIndex() const;

  // Advances the blend by one frame. Call once per rendered frame.
  void animationCycle();
  bool isBlending() const;

  // Per-frame blend progress, in the 0-255 units blend() expects.
  static const uint8_t BLEND_STEP = 5;

  int index = 0;
  CRGB color = CRGB(0, 0, 0);
  CRGB startColor = CRGB(0, 0, 0);
  CRGB targetColor = CRGB(0, 0, 0);
  uint8_t blendAmount = 255;  // 255 == settled on targetColor
};

pixel::pixel() {
  this->index = 0;
  this->color = CRGB(0, 0, 0);
}

pixel::pixel(int index, CRGB color) {
  this->index = index;
  this->color = color;
  this->startColor = color;
  this->targetColor = color;
}

void pixel::setIndex(int index) {
  this->index = index;
}

int pixel::getIndex() const {
  return this->index;
}

CRGB pixel::getColor() const {
  return this->color;
}

// Jumps straight to a color, cancelling any in-flight blend.
void pixel::setColor(CRGB color) {
  this->color = color;
  this->startColor = color;
  this->targetColor = color;
  this->blendAmount = 255;
}

// Starts a new blend from wherever the pixel currently is.
void pixel::setTargetColor(CRGB target) {
  this->startColor = this->color;
  this->targetColor = target;
  this->blendAmount = 0;
}

bool pixel::isBlending() const {
  return this->blendAmount < 255;
}

void pixel::animationCycle() {
  if (!this->isBlending()) {
    return;
  }
  // qadd8 saturates at 255, so the blend lands exactly on targetColor.
  this->blendAmount = qadd8(this->blendAmount, BLEND_STEP);
  this->color = blend(this->startColor, this->targetColor, this->blendAmount);
}
