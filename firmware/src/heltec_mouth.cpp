#include "heltec_mouth.h"

#include <Wire.h>
#include <font/glcdfont.h>
#include <ctype.h>
#include <string.h>

namespace
{
constexpr uint8_t OLED_ADDRESS = 0x3c;
constexpr uint8_t OLED_SDA = 4;
constexpr uint8_t OLED_SCL = 15;
constexpr uint8_t OLED_RESET = 16;
constexpr uint32_t OLED_I2C_HZ = 400000;
constexpr size_t MAX_LINE_CHARS = 17;
constexpr size_t MAX_LINES = 5;
constexpr int FRAME_LEFT = 6;
constexpr int FRAME_RIGHT = 121;
constexpr int FRAME_TOP = 10;
constexpr int FRAME_BOTTOM = 53;
constexpr int FRAME_THICKNESS = 1;
constexpr int LOGICAL_COLS = 32;
constexpr int LOGICAL_ROWS = 8;
constexpr int LOGICAL_PITCH = 3;
constexpr int LOGICAL_DOT = 2;
constexpr int CONTENT_LEFT = 16;
constexpr int CONTENT_TOP = 20;
constexpr int CONTENT_RIGHT = CONTENT_LEFT + ((LOGICAL_COLS - 1) * LOGICAL_PITCH) + LOGICAL_DOT - 1;
constexpr int CONTENT_BOTTOM = CONTENT_TOP + ((LOGICAL_ROWS - 1) * LOGICAL_PITCH) + LOGICAL_DOT - 1;
constexpr uint32_t SCROLL_STEP_MS = 90;
constexpr int SCROLL_STEP_PX = 2;
constexpr uint32_t MOUTH_FRAME_MS = 145;

constexpr uint8_t ICON_HEART[] = {0x06, 0x0f, 0x1f, 0x3e, 0x7c, 0x3e, 0x1f, 0x0f, 0x06};
constexpr uint8_t ICON_CHECK[] = {0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
constexpr uint8_t ICON_LEFT[] = {0x08, 0x1c, 0x3e, 0x7f, 0x08, 0x08, 0x08, 0x08, 0x08};
constexpr uint8_t ICON_RIGHT[] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x7f, 0x3e, 0x1c, 0x08};
constexpr uint8_t ICON_LEVEL[] = {0x40, 0x40, 0x00, 0x70, 0x70, 0x00, 0x7c, 0x7c, 0x00, 0x7f, 0x7f};
constexpr uint8_t ICON_BURST[] = {0x08, 0x2a, 0x1c, 0x1c, 0x7f, 0x7f, 0x7f, 0x1c, 0x1c, 0x2a, 0x08};

constexpr uint8_t MOUTH_NEUTRAL[] = {0x00, 0x00, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x08, 0x00, 0x00};
constexpr uint8_t MOUTH_SMILE[] = {0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04};
constexpr uint8_t MOUTH_FROWN[] = {0x10, 0x08, 0x08, 0x04, 0x04, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10};
constexpr uint8_t MOUTH_SURPRISED[] = {0x00, 0x00, 0x00, 0x1c, 0x22, 0x22, 0x41, 0x41, 0x41, 0x41, 0x41, 0x22, 0x22, 0x1c, 0x00, 0x00, 0x00};
constexpr uint8_t MOUTH_SMIRK[] = {0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x10, 0x10, 0x20, 0x00};
constexpr uint8_t MOUTH_SPEAK_CLOSED[] = {0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00};
constexpr uint8_t MOUTH_SPEAK_SMALL[] = {0x00, 0x00, 0x0c, 0x12, 0x12, 0x21, 0x21, 0x21, 0x21, 0x21, 0x12, 0x12, 0x0c, 0x00, 0x00};
constexpr uint8_t MOUTH_SPEAK_WIDE[] = {0x0c, 0x12, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x12, 0x0c};

const char *mouthName(HeltecMouthShape shape)
{
  switch (shape)
  {
  case HeltecMouthShape::SMILE: return "SMILE";
  case HeltecMouthShape::FROWN: return "FROWN";
  case HeltecMouthShape::SURPRISED: return "SURPRISED";
  case HeltecMouthShape::SMIRK: return "SMIRK";
  case HeltecMouthShape::SPEAKING: return "SPEAKING";
  case HeltecMouthShape::NEUTRAL:
  default: return "NEUTRAL";
  }
}
}

bool HeltecMouthDisplay::writeCommands(const uint8_t *commands, size_t count)
{
  Wire.beginTransmission(OLED_ADDRESS);
  Wire.write((uint8_t)0x00);
  for (size_t index = 0; index < count; ++index)
  {
    Wire.write(commands[index]);
  }
  return Wire.endTransmission() == 0;
}

bool HeltecMouthDisplay::begin()
{
  mutex = xSemaphoreCreateMutex();
  if (!mutex)
  {
    return false;
  }
  pinMode(OLED_RESET, OUTPUT);
  digitalWrite(OLED_RESET, LOW);
  delay(12);
  digitalWrite(OLED_RESET, HIGH);
  delay(12);

  Wire.begin(OLED_SDA, OLED_SCL, OLED_I2C_HZ);
  Wire.beginTransmission(OLED_ADDRESS);
  if (Wire.endTransmission() != 0)
  {
    initialized = false;
    return false;
  }

  static const uint8_t initSequence[] = {
      0xae,       // display off
      0xd5, 0x80, // clock divide
      0xa8, 0x3f, // multiplex 1/64
      0xd3, 0x00, // display offset
      0x40,       // start line 0
      0x8d, 0x14, // charge pump on
      0x20, 0x00, // horizontal addressing
      0xa1,       // segment remap
      0xc8,       // COM scan decrement
      0xda, 0x12, // COM pins
      0x81, 0xff, // maximum safe panel contrast
      0xd9, 0xf1, // precharge
      0xdb, 0x40, // VCOM detect
      0xa4,       // display follows RAM
      0xa6,       // normal pixels
      0x2e,       // scrolling off
      0xaf,       // display on
  };
  initialized = writeCommands(initSequence, sizeof(initSequence));
  if (!initialized)
  {
    return false;
  }

  powered = true;

  showMouth(HeltecMouthShape::NEUTRAL);
  return initialized;
}

bool HeltecMouthDisplay::takeLock()
{
  return mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

void HeltecMouthDisplay::releaseLock()
{
  if (mutex)
  {
    xSemaphoreGive(mutex);
  }
}

void HeltecMouthDisplay::clearPixels()
{
  memset(pixels, 0, sizeof(pixels));
}

void HeltecMouthDisplay::setPixel(int x, int y)
{
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
  {
    return;
  }
  pixels[x + ((y / 8) * WIDTH)] |= (uint8_t)(1u << (y & 7));
}

void HeltecMouthDisplay::drawChar(int x, int y, char value, uint8_t scale)
{
  uint8_t code = (uint8_t)value;
  if (code < 32 || code > 126)
  {
    code = '?';
  }
  for (int column = 0; column < 5; ++column)
  {
    const uint8_t bits = font[(code * 5) + column];
    for (int row = 0; row < 7; ++row)
    {
      if ((bits & (1u << row)) == 0)
      {
        continue;
      }
      for (uint8_t sy = 0; sy < scale; ++sy)
      {
        for (uint8_t sx = 0; sx < scale; ++sx)
        {
          setPixel(x + (column * scale) + sx, y + (row * scale) + sy);
        }
      }
    }
  }
}

void HeltecMouthDisplay::buildLogicalText()
{
  logicalWidth = 0;
  const uint8_t *icon = nullptr;
  size_t iconWidth = 0;
  if (strcmp(currentText, "HEART") == 0)
  {
    icon = ICON_HEART;
    iconWidth = sizeof(ICON_HEART);
  }
  else if (strcmp(currentText, "CHECK") == 0)
  {
    icon = ICON_CHECK;
    iconWidth = sizeof(ICON_CHECK);
  }
  else if (strcmp(currentText, "LEFT") == 0 || strcmp(currentText, "ARROW LEFT") == 0)
  {
    icon = ICON_LEFT;
    iconWidth = sizeof(ICON_LEFT);
  }
  else if (strcmp(currentText, "RIGHT") == 0 || strcmp(currentText, "ARROW RIGHT") == 0)
  {
    icon = ICON_RIGHT;
    iconWidth = sizeof(ICON_RIGHT);
  }
  else if (strcmp(currentText, "LEVEL") == 0 || strcmp(currentText, "BARS") == 0)
  {
    icon = ICON_LEVEL;
    iconWidth = sizeof(ICON_LEVEL);
  }
  else if (strcmp(currentText, "BURST") == 0 || strcmp(currentText, "SPARK") == 0)
  {
    icon = ICON_BURST;
    iconWidth = sizeof(ICON_BURST);
  }
  if (icon)
  {
    memcpy(logicalColumns, icon, iconWidth);
    logicalWidth = iconWidth;
    return;
  }
  for (size_t index = 0; currentText[index] != '\0'; ++index)
  {
    uint8_t code = (uint8_t)currentText[index];
    if (code < 32 || code > 126)
    {
      code = '?';
    }
    if (logicalWidth > 0 && logicalWidth < MAX_LOGICAL_COLUMNS)
    {
      logicalColumns[logicalWidth++] = 0;
    }
    const uint8_t *glyph = font + (code * 5);
    int first = 0;
    int last = 4;
    while (first < last && glyph[first] == 0) ++first;
    while (last > first && glyph[last] == 0) --last;
    if (code == ' ')
    {
      first = 0;
      last = 0;
    }
    for (int column = first; column <= last && logicalWidth < MAX_LOGICAL_COLUMNS; ++column)
    {
      logicalColumns[logicalWidth++] = glyph[column];
    }
  }
}

void HeltecMouthDisplay::drawLogicalText(int startX)
{
  for (size_t column = 0; column < logicalWidth; ++column)
  {
    const int x = startX + ((int)column * LOGICAL_PITCH);
    if (x + LOGICAL_DOT - 1 < CONTENT_LEFT || x > CONTENT_RIGHT)
    {
      continue;
    }
    const uint8_t bits = logicalColumns[column];
    for (int row = 0; row < 7; ++row)
    {
      if ((bits & (1u << row)) == 0) continue;
      const int y = CONTENT_TOP + (row * LOGICAL_PITCH);
      for (int dy = 0; dy < LOGICAL_DOT; ++dy)
      {
        for (int dx = 0; dx < LOGICAL_DOT; ++dx)
        {
          const int pixelX = x + dx;
          const int pixelY = y + dy;
          if (pixelX >= CONTENT_LEFT && pixelX <= CONTENT_RIGHT &&
              pixelY >= CONTENT_TOP && pixelY <= CONTENT_BOTTOM)
          {
            setPixel(pixelX, pixelY);
          }
        }
      }
    }
  }
}

void HeltecMouthDisplay::drawTextCentered(const char *value, int y, uint8_t scale)
{
  const size_t length = strlen(value);
  const int width = length == 0 ? 0 : (int)(length * 6u * scale) - scale;
  int x = (WIDTH - width) / 2;
  for (size_t index = 0; index < length; ++index)
  {
    drawChar(x, y, value[index], scale);
    x += 6 * scale;
  }
}

void HeltecMouthDisplay::drawFrame()
{
  for (int thickness = 0; thickness < FRAME_THICKNESS; ++thickness)
  {
    for (int x = FRAME_LEFT; x <= FRAME_RIGHT; ++x)
    {
      setPixel(x, FRAME_TOP + thickness);
      setPixel(x, FRAME_BOTTOM - thickness);
    }
    for (int y = FRAME_TOP; y <= FRAME_BOTTOM; ++y)
    {
      setPixel(FRAME_LEFT + thickness, y);
      setPixel(FRAME_RIGHT - thickness, y);
    }
  }
}

void HeltecMouthDisplay::renderMouthFrame(bool fullRefresh)
{
  const uint8_t *columns = MOUTH_NEUTRAL;
  size_t width = sizeof(MOUTH_NEUTRAL);
  switch (currentMouth)
  {
  case HeltecMouthShape::SMILE:
    columns = MOUTH_SMILE; width = sizeof(MOUTH_SMILE); break;
  case HeltecMouthShape::FROWN:
    columns = MOUTH_FROWN; width = sizeof(MOUTH_FROWN); break;
  case HeltecMouthShape::SURPRISED:
    columns = MOUTH_SURPRISED; width = sizeof(MOUTH_SURPRISED); break;
  case HeltecMouthShape::SMIRK:
    columns = MOUTH_SMIRK; width = sizeof(MOUTH_SMIRK); break;
  case HeltecMouthShape::SPEAKING:
  {
    static const uint8_t *const frames[] = {MOUTH_SPEAK_CLOSED, MOUTH_SPEAK_SMALL, MOUTH_SPEAK_WIDE, MOUTH_SPEAK_SMALL};
    static const size_t widths[] = {sizeof(MOUTH_SPEAK_CLOSED), sizeof(MOUTH_SPEAK_SMALL), sizeof(MOUTH_SPEAK_WIDE), sizeof(MOUTH_SPEAK_SMALL)};
    const uint8_t frameIndex = mouthFrame % 4;
    columns = frames[frameIndex]; width = widths[frameIndex];
    break;
  }
  case HeltecMouthShape::NEUTRAL:
  default:
    break;
  }

  clearPixels();
  drawFrame();
  memcpy(logicalColumns, columns, width);
  logicalWidth = width;
  const int contentWidth = ((int)logicalWidth - 1) * LOGICAL_PITCH + LOGICAL_DOT;
  drawLogicalText(CONTENT_LEFT + ((CONTENT_RIGHT - CONTENT_LEFT + 1 - contentWidth) / 2));
  if (fullRefresh) flush(); else flushPages(2, 4);
}

void HeltecMouthDisplay::showMouth(HeltecMouthShape shape)
{
  if (!initialized || !takeLock())
  {
    return;
  }
  setScroll(false);
  completedScrollCycle = false;
  currentMouth = shape;
  mouthActive = true;
  mouthFrame = 0;
  lastMouthFrameAtMs = millis();
  copyText(mouthName(shape));
  renderMouthFrame(true);
  releaseLock();
}

void HeltecMouthDisplay::renderText()
{
  clearPixels();
  drawFrame();
  buildLogicalText();
  if (logicalWidth == 0)
  {
    flush();
    return;
  }
  if (logicalWidth > LOGICAL_COLS)
  {
    scrollX = CONTENT_RIGHT + 1;
    lastScrollAtMs = millis();
    renderScrollFrame(true);
    scrollActive = initialized;
    return;
  }
  const int contentWidth = logicalWidth == 0 ? 0 : ((int)logicalWidth - 1) * LOGICAL_PITCH + LOGICAL_DOT;
  drawLogicalText(CONTENT_LEFT + ((CONTENT_RIGHT - CONTENT_LEFT + 1 - contentWidth) / 2));
  flush();
}

bool HeltecMouthDisplay::flushPages(uint8_t firstPage, uint8_t lastPage)
{
  if (!initialized)
  {
    return false;
  }
  const uint8_t addressWindow[] = {0x21, 0x00, 0x7f, 0x22, firstPage, lastPage};
  if (!writeCommands(addressWindow, sizeof(addressWindow)))
  {
    initialized = false;
    return false;
  }
  const size_t firstOffset = (size_t)firstPage * WIDTH;
  const size_t finalOffset = ((size_t)lastPage + 1u) * WIDTH;
  // The ESP32 Wire buffer is 128 bytes. Leave one byte for the data-control
  // prefix and batch almost a full page per transaction.
  constexpr size_t I2C_DATA_CHUNK = 120;
  for (size_t offset = firstOffset; offset < finalOffset; offset += I2C_DATA_CHUNK)
  {
    const size_t remaining = finalOffset - offset;
    const size_t count = remaining < I2C_DATA_CHUNK ? remaining : I2C_DATA_CHUNK;
    Wire.beginTransmission(OLED_ADDRESS);
    Wire.write((uint8_t)0x40);
    Wire.write(pixels + offset, count);
    if (Wire.endTransmission() != 0)
    {
      initialized = false;
      return false;
    }
  }
  return true;
}

bool HeltecMouthDisplay::flush()
{
  return flushPages(0, 7);
}

bool HeltecMouthDisplay::setScroll(bool enabled)
{
  if (!initialized)
  {
    return false;
  }
  static const uint8_t stop[] = {0x2e};
  if (!writeCommands(stop, sizeof(stop)))
  {
    initialized = false;
    return false;
  }
  scrollActive = false;
  scrollActive = enabled;
  return true;
}

void HeltecMouthDisplay::copyText(const char *text)
{
  size_t index = 0;
  for (; text[index] != '\0' && index < sizeof(currentText) - 1; ++index)
  {
    const unsigned char value = (unsigned char)text[index];
    currentText[index] = value >= 32 && value <= 126 ? (char)toupper(value) : ' ';
  }
  currentText[index] = '\0';
}

void HeltecMouthDisplay::showText(const char *text)
{
  if (!initialized || !text || !takeLock())
  {
    return;
  }
  setScroll(false);
  completedScrollCycle = false;
  mouthActive = false;
  copyText(text);
  renderText();
  releaseLock();
}

void HeltecMouthDisplay::scrollText(const char *text)
{
  if (!initialized || !text || !takeLock())
  {
    return;
  }
  setScroll(false);
  completedScrollCycle = false;
  mouthActive = false;
  copyText(text);
  buildLogicalText();
  scrollX = CONTENT_RIGHT + 1;
  lastScrollAtMs = millis();
  renderScrollFrame(true);
  setScroll(initialized);
  releaseLock();
}

void HeltecMouthDisplay::renderScrollFrame(bool fullRefresh)
{
  clearPixels();
  drawLogicalText(scrollX);
  drawFrame();
  if (fullRefresh)
  {
    flush();
  }
  else
  {
    // The separated-dot glyphs occupy pages 2–4. Updating exactly those pages
    // keeps every row moving, leaves the frame stationary, and bounds I2C work.
    flushPages(2, 4);
  }
}

void HeltecMouthDisplay::tick(uint32_t nowMs)
{
  if (!initialized || !powered)
  {
    return;
  }
  const bool scrollDue = scrollActive && nowMs - lastScrollAtMs >= SCROLL_STEP_MS;
  const bool mouthDue = mouthActive && currentMouth == HeltecMouthShape::SPEAKING &&
                        nowMs - lastMouthFrameAtMs >= MOUTH_FRAME_MS;
  if ((!scrollDue && !mouthDue) || !takeLock())
  {
    return;
  }
  if (scrollDue)
  {
    lastScrollAtMs = nowMs;
    scrollX -= SCROLL_STEP_PX;
    const int messageWidth = logicalWidth == 0 ? 0 : ((int)logicalWidth - 1) * LOGICAL_PITCH + LOGICAL_DOT;
    if (scrollX + messageWidth < CONTENT_LEFT)
    {
      completedScrollCycle = true;
      scrollX = CONTENT_RIGHT + 1;
    }
    renderScrollFrame(false);
  }
  else
  {
    lastMouthFrameAtMs = nowMs;
    mouthFrame = (mouthFrame + 1) % 4;
    renderMouthFrame(false);
  }
  releaseLock();
}

void HeltecMouthDisplay::blank()
{
  if (!initialized || !takeLock())
  {
    return;
  }
  setScroll(false);
  completedScrollCycle = false;
  mouthActive = false;
  currentText[0] = '\0';
  clearPixels();
  flush();
  releaseLock();
}

void HeltecMouthDisplay::sleep()
{
  if (!initialized || !powered || !takeLock())
  {
    return;
  }
  setScroll(false);
  completedScrollCycle = false;
  mouthActive = false;
  currentText[0] = '\0';
  clearPixels();
  flush();
  static const uint8_t displayOff[] = {0xae};
  if (writeCommands(displayOff, sizeof(displayOff)))
  {
    powered = false;
  }
  releaseLock();
}

void HeltecMouthDisplay::wake()
{
  if (!initialized || powered || !takeLock())
  {
    return;
  }
  static const uint8_t displayOn[] = {0xaf};
  if (writeCommands(displayOn, sizeof(displayOn)))
  {
    powered = true;
  }
  releaseLock();
}
