#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class HeltecMouthShape : uint8_t
{
  NEUTRAL,
  SMILE,
  FROWN,
  SURPRISED,
  SMIRK,
  SPEAKING
};

class HeltecMouthDisplay
{
public:
  bool begin();
  void showMouth(HeltecMouthShape shape);
  void showText(const char *text);
  void scrollText(const char *text);
  void blank();
  void tick(uint32_t nowMs);
  bool ready() const { return initialized; }
  bool scrolling() const { return scrollActive; }
  const char *text() const { return currentText; }

private:
  static constexpr uint8_t WIDTH = 128;
  static constexpr uint8_t HEIGHT = 64;
  static constexpr size_t BUFFER_BYTES = WIDTH * HEIGHT / 8;
  static constexpr size_t MAX_LOGICAL_COLUMNS = 65 * 6;

  bool initialized = false;
  bool scrollActive = false;
  bool mouthActive = false;
  SemaphoreHandle_t mutex = nullptr;
  int16_t scrollX = 0;
  uint32_t lastScrollAtMs = 0;
  uint32_t lastMouthFrameAtMs = 0;
  uint8_t mouthFrame = 0;
  HeltecMouthShape currentMouth = HeltecMouthShape::NEUTRAL;
  char currentText[65] = {};
  uint8_t logicalColumns[MAX_LOGICAL_COLUMNS] = {};
  size_t logicalWidth = 0;
  uint8_t pixels[BUFFER_BYTES] = {};

  bool writeCommands(const uint8_t *commands, size_t count);
  bool takeLock();
  void releaseLock();
  bool flush();
  bool flushPages(uint8_t firstPage, uint8_t lastPage);
  bool setScroll(bool enabled);
  void clearPixels();
  void setPixel(int x, int y);
  void drawChar(int x, int y, char value, uint8_t scale);
  void drawTextCentered(const char *value, int y, uint8_t scale);
  void buildLogicalText();
  void drawLogicalText(int startX);
  void drawFrame();
  void renderMouthFrame(bool fullRefresh);
  void renderText();
  void renderScrollFrame(bool fullRefresh);
  void copyText(const char *text);
};
