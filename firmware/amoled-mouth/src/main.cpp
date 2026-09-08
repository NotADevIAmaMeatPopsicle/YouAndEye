#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

namespace
{
constexpr int16_t SCREEN_W = 466;
constexpr int16_t SCREEN_H = 466;
constexpr int16_t CENTER = 233;
constexpr uint32_t FRAME_MS = 40;
constexpr size_t COMMAND_MAX = 128;

constexpr int8_t LCD_CS = 12;
constexpr int8_t LCD_SCLK = 38;
constexpr int8_t LCD_D0 = 4;
constexpr int8_t LCD_D1 = 5;
constexpr int8_t LCD_D2 = 6;
constexpr int8_t LCD_D3 = 7;
constexpr int8_t LCD_RST = 39;

enum class MouthMode : uint8_t { AUTO, TEXT, SCROLL, ICON, BLANK, BEAT };
enum class MouthShape : uint8_t {
  NEUTRAL,
  LISTENING,
  THINKING,
  WORKING,
  SMILE,
  SOFT_SMILE,
  REASSURING,
  GRIN,
  PROUD,
  DELIGHTED,
  SURPRISED,
  CURIOUS,
  SKEPTICAL,
  PLAYFUL,
  FROWN,
  WORRIED,
  UNCERTAIN,
  EMBARRASSED,
  PUCKER,
  TENSE,
  SPEAKING
};
enum class MouthStyle : uint8_t { MINIMAL, EXPRESSIVE, TEXT_FRIENDLY };
enum class BeatKind : uint8_t { NONE, ATTENTION, THINKING, SUCCESS, ACKNOWLEDGE, REASSURE, ERROR, PLAYFUL };
enum class Decoration : uint8_t { NONE, THOUGHT, SPARKLE, CHECK, HEART, BLUSH, SWEAT, ALERT };

struct MouthPose
{
  float width;
  float open;
  float curve;
  float skew;
  float roundness;
  float teeth;
  float tongue;
  Decoration decoration;
};

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RST, 0, SCREEN_W, SCREEN_H, 7, 0, 0, 0);
Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel);
Arduino_GFX *surface = canvas;

bool displayReady = false;
MouthMode mode = MouthMode::AUTO;
MouthShape shape = MouthShape::NEUTRAL;
MouthStyle style = MouthStyle::EXPRESSIVE;
BeatKind beat = BeatKind::NONE;
char affectName[24] = "neutral";
char content[65] = {};
char accentName[16] = "blue";
char commandBuffer[COMMAND_MAX + 1] = {};
size_t commandLength = 0;
float intensity = 0.7f;
float warmth = 0.5f;
float confidence = 0.5f;
float urgency = 0.3f;
float energy = 0.5f;
float currentWidth = 0.50f;
float targetWidth = 0.50f;
float currentCurve = 0.02f;
float targetCurve = 0.02f;
float currentOpen = 0.0f;
float targetOpen = 0.0f;
float currentSkew = 0.0f;
float targetSkew = 0.0f;
float currentRoundness = 0.70f;
float targetRoundness = 0.70f;
float currentTeeth = 0.0f;
float targetTeeth = 0.0f;
float currentTongue = 0.0f;
float targetTongue = 0.0f;
Decoration decoration = Decoration::NONE;
uint8_t brightness = 80;
int16_t scrollX = SCREEN_W;
bool scrollComplete = true;
uint32_t lastFrameAtMs = 0;
uint32_t shapeChangedAtMs = 0;
uint32_t beatStartedAtMs = 0;
bool beatRevealed = false;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

uint16_t scaleColor(uint16_t color, float scale)
{
  const uint8_t r = (uint8_t)(((color >> 11) & 0x1f) * 255 / 31);
  const uint8_t g = (uint8_t)(((color >> 5) & 0x3f) * 255 / 63);
  const uint8_t b = (uint8_t)((color & 0x1f) * 255 / 31);
  return rgb565((uint8_t)(r * scale), (uint8_t)(g * scale), (uint8_t)(b * scale));
}

bool equalsIgnoreCase(const char *left, const char *right)
{
  if (!left || !right) return false;
  while (*left && *right)
  {
    if (toupper((unsigned char)*left++) != toupper((unsigned char)*right++)) return false;
  }
  return *left == '\0' && *right == '\0';
}

void copySafe(char *target, size_t capacity, const char *source)
{
  if (!target || capacity == 0) return;
  size_t out = 0;
  if (source)
  {
    while (*source && out + 1 < capacity)
    {
      const unsigned char value = (unsigned char)*source++;
      target[out++] = value >= 32 && value <= 126 ? (char)value : ' ';
    }
  }
  target[out] = '\0';
}

uint16_t accentColor()
{
  if (equalsIgnoreCase(accentName, "cyan")) return rgb565(38, 218, 255);
  if (equalsIgnoreCase(accentName, "violet")) return rgb565(178, 112, 255);
  if (equalsIgnoreCase(accentName, "gold")) return rgb565(255, 188, 68);
  if (equalsIgnoreCase(accentName, "coral")) return rgb565(255, 100, 115);
  if (equalsIgnoreCase(accentName, "mint")) return rgb565(76, 236, 178);
  return rgb565(54, 164, 255);
}

const char *modeLabel()
{
  switch (mode)
  {
  case MouthMode::TEXT: return "text";
  case MouthMode::SCROLL: return "scroll";
  case MouthMode::ICON: return "icon";
  case MouthMode::BLANK: return "blank";
  case MouthMode::BEAT: return "beat";
  case MouthMode::AUTO:
  default: return "auto";
  }
}

const char *shapeLabel()
{
  switch (shape)
  {
  case MouthShape::LISTENING: return "listening";
  case MouthShape::THINKING: return "thinking";
  case MouthShape::WORKING: return "working";
  case MouthShape::SMILE: return "smile";
  case MouthShape::SOFT_SMILE: return "soft-smile";
  case MouthShape::REASSURING: return "reassuring";
  case MouthShape::GRIN: return "grin";
  case MouthShape::PROUD: return "proud";
  case MouthShape::DELIGHTED: return "delighted";
  case MouthShape::FROWN: return "frown";
  case MouthShape::WORRIED: return "worried";
  case MouthShape::UNCERTAIN: return "uncertain";
  case MouthShape::EMBARRASSED: return "embarrassed";
  case MouthShape::SURPRISED: return "surprised";
  case MouthShape::CURIOUS: return "curious";
  case MouthShape::SKEPTICAL: return "skeptical";
  case MouthShape::PLAYFUL: return "playful";
  case MouthShape::PUCKER: return "pucker";
  case MouthShape::TENSE: return "tense";
  case MouthShape::SPEAKING: return "speaking";
  case MouthShape::NEUTRAL:
  default: return "neutral";
  }
}

bool parseBoundedFloat(const char *token, float &result)
{
  if (!token || !*token) return false;
  char *end = nullptr;
  const float value = strtof(token, &end);
  if (!end || *end != '\0' || !isfinite(value) || value < 0.0f || value > 1.0f) return false;
  result = value;
  return true;
}

MouthPose poseForShape(MouthShape value)
{
  switch (value)
  {
  case MouthShape::LISTENING: return {0.48f, 0.035f, 0.24f, -0.02f, 0.72f, 0.0f, 0.0f, Decoration::NONE};
  case MouthShape::THINKING: return {0.40f, 0.025f, 0.10f, 0.28f, 0.76f, 0.0f, 0.0f, Decoration::THOUGHT};
  case MouthShape::WORKING: return {0.42f, 0.018f, -0.04f, 0.0f, 0.68f, 0.0f, 0.0f, Decoration::THOUGHT};
  case MouthShape::SMILE: return {0.61f, 0.13f, 0.68f, 0.0f, 0.70f, 0.20f, 0.08f, Decoration::NONE};
  case MouthShape::SOFT_SMILE: return {0.56f, 0.065f, 0.52f, -0.03f, 0.72f, 0.0f, 0.0f, Decoration::NONE};
  case MouthShape::REASSURING: return {0.50f, 0.040f, 0.36f, 0.0f, 0.76f, 0.0f, 0.0f, Decoration::NONE};
  case MouthShape::GRIN: return {0.68f, 0.40f, 0.70f, 0.0f, 0.64f, 0.34f, 0.16f, Decoration::SPARKLE};
  case MouthShape::PROUD: return {0.62f, 0.24f, 0.56f, -0.08f, 0.68f, 0.28f, 0.08f, Decoration::CHECK};
  case MouthShape::DELIGHTED: return {0.66f, 0.62f, 0.62f, 0.0f, 0.60f, 0.26f, 0.34f, Decoration::SPARKLE};
  case MouthShape::SURPRISED: return {0.36f, 0.96f, 0.0f, 0.0f, 1.18f, 0.0f, 0.10f, Decoration::NONE};
  case MouthShape::CURIOUS: return {0.36f, 0.22f, 0.08f, -0.30f, 1.05f, 0.0f, 0.0f, Decoration::THOUGHT};
  case MouthShape::SKEPTICAL: return {0.52f, 0.025f, 0.22f, 0.72f, 0.68f, 0.0f, 0.0f, Decoration::NONE};
  case MouthShape::PLAYFUL: return {0.60f, 0.34f, 0.58f, 0.34f, 0.64f, 0.12f, 0.56f, Decoration::SPARKLE};
  case MouthShape::FROWN: return {0.53f, 0.035f, -0.64f, 0.0f, 0.72f, 0.0f, 0.0f, Decoration::NONE};
  case MouthShape::WORRIED: return {0.48f, 0.12f, -0.34f, -0.10f, 0.80f, 0.0f, 0.0f, Decoration::SWEAT};
  case MouthShape::UNCERTAIN: return {0.45f, 0.028f, -0.08f, 0.22f, 0.76f, 0.0f, 0.0f, Decoration::SWEAT};
  case MouthShape::EMBARRASSED: return {0.44f, 0.04f, 0.28f, 0.30f, 0.72f, 0.0f, 0.0f, Decoration::BLUSH};
  case MouthShape::PUCKER: return {0.24f, 0.20f, 0.0f, 0.0f, 1.28f, 0.0f, 0.0f, Decoration::HEART};
  case MouthShape::TENSE: return {0.52f, 0.02f, -0.10f, 0.0f, 0.68f, 0.0f, 0.0f, Decoration::ALERT};
  case MouthShape::SPEAKING: return {0.56f, 0.42f, 0.08f, 0.0f, 0.78f, 0.10f, 0.20f, Decoration::NONE};
  case MouthShape::NEUTRAL:
  default: return {0.50f, 0.018f, 0.06f, 0.0f, 0.72f, 0.0f, 0.0f, Decoration::NONE};
  }
}

void applyShape(MouthShape next)
{
  if (shape != next) shapeChangedAtMs = millis();
  shape = next;
  const MouthPose pose = poseForShape(shape);
  const float expression = 0.72f + intensity * 0.34f;
  targetWidth = constrain(pose.width * (0.90f + confidence * 0.16f), 0.20f, 0.76f);
  targetCurve = constrain(pose.curve * expression + (warmth - 0.5f) * 0.16f, -0.82f, 0.90f);
  targetOpen = constrain(pose.open * expression, 0.0f, 1.0f);
  targetSkew = constrain(pose.skew * (0.72f + intensity * 0.32f), -1.0f, 1.0f);
  targetRoundness = pose.roundness;
  targetTeeth = style == MouthStyle::MINIMAL ? 0.0f : pose.teeth;
  targetTongue = style == MouthStyle::MINIMAL ? 0.0f : pose.tongue;
  decoration = style == MouthStyle::EXPRESSIVE ? pose.decoration : Decoration::NONE;
}

MouthShape shapeForAffect(const char *name)
{
  if (style == MouthStyle::TEXT_FRIENDLY && !equalsIgnoreCase(name, "speaking"))
    return MouthShape::NEUTRAL;
  if (equalsIgnoreCase(name, "listening")) return MouthShape::LISTENING;
  if (equalsIgnoreCase(name, "thinking")) return MouthShape::THINKING;
  if (equalsIgnoreCase(name, "working")) return MouthShape::WORKING;
  if (equalsIgnoreCase(name, "happy")) return MouthShape::SMILE;
  if (equalsIgnoreCase(name, "encouraging")) return MouthShape::SOFT_SMILE;
  if (equalsIgnoreCase(name, "reassuring")) return MouthShape::REASSURING;
  if (equalsIgnoreCase(name, "excited")) return MouthShape::GRIN;
  if (equalsIgnoreCase(name, "success")) return MouthShape::PROUD;
  if (equalsIgnoreCase(name, "delighted")) return MouthShape::DELIGHTED;
  if (equalsIgnoreCase(name, "surprised")) return MouthShape::SURPRISED;
  if (equalsIgnoreCase(name, "curious")) return MouthShape::CURIOUS;
  if (equalsIgnoreCase(name, "suspicious")) return MouthShape::SKEPTICAL;
  if (equalsIgnoreCase(name, "playful")) return MouthShape::PLAYFUL;
  if (equalsIgnoreCase(name, "sad")) return MouthShape::FROWN;
  if (equalsIgnoreCase(name, "concerned")) return MouthShape::WORRIED;
  if (equalsIgnoreCase(name, "uncertain")) return MouthShape::UNCERTAIN;
  if (equalsIgnoreCase(name, "embarrassed")) return MouthShape::EMBARRASSED;
  if (equalsIgnoreCase(name, "love")) return MouthShape::PUCKER;
  if (equalsIgnoreCase(name, "error")) return MouthShape::TENSE;
  if (equalsIgnoreCase(name, "speaking")) return MouthShape::SPEAKING;
  return MouthShape::NEUTRAL;
}

void setAffect(const char *name, float nextIntensity, float nextWarmth, float nextConfidence, float nextUrgency)
{
  copySafe(affectName, sizeof(affectName), name);
  for (char *p = affectName; *p; ++p) *p = (char)tolower((unsigned char)*p);
  intensity = nextIntensity;
  warmth = nextWarmth;
  confidence = nextConfidence;
  urgency = nextUrgency;
  if (mode == MouthMode::AUTO || mode == MouthMode::BEAT) applyShape(shapeForAffect(affectName));
}

void drawThickSegment(float x0, float y0, float x1, float y1, int radius, uint16_t color)
{
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const int steps = max(1, (int)(sqrtf(dx * dx + dy * dy) / max(2, radius)));
  for (int step = 0; step <= steps; ++step)
  {
    const float t = (float)step / (float)steps;
    surface->fillCircle((int16_t)(x0 + dx * t), (int16_t)(y0 + dy * t), radius, color);
  }
}

void drawQuadratic(float x0, float y0, float cx, float cy, float x1, float y1, int radius, uint16_t color)
{
  float px = x0;
  float py = y0;
  for (int step = 1; step <= 32; ++step)
  {
    const float t = (float)step / 32.0f;
    const float u = 1.0f - t;
    const float x = u * u * x0 + 2.0f * u * t * cx + t * t * x1;
    const float y = u * u * y0 + 2.0f * u * t * cy + t * t * y1;
    drawThickSegment(px, py, x, y, radius, color);
    px = x;
    py = y;
  }
}

void drawHeart(int16_t x, int16_t y, int16_t size, uint16_t color)
{
  const int16_t r = size / 4;
  surface->fillCircle(x - r, y - r / 2, r, color);
  surface->fillCircle(x + r, y - r / 2, r, color);
  for (int16_t row = 0; row < size; ++row)
  {
    const int16_t half = (size - row) / 2;
    surface->drawFastHLine(x - half, y + row / 2, half * 2 + 1, color);
  }
}

void drawCheck(int16_t x, int16_t y, int16_t size, uint16_t color)
{
  drawThickSegment(x - size / 2, y, x - size / 8, y + size / 3, 8, color);
  drawThickSegment(x - size / 8, y + size / 3, x + size / 2, y - size / 3, 8, color);
}

void drawBurst(int16_t x, int16_t y, int16_t size, uint16_t color)
{
  for (int angle = 0; angle < 360; angle += 45)
  {
    const float radians = angle * 0.0174532925f;
    drawThickSegment(
        x + cosf(radians) * size * 0.35f,
        y + sinf(radians) * size * 0.35f,
        x + cosf(radians) * size * 0.58f,
        y + sinf(radians) * size * 0.58f,
        4,
        color);
  }
  surface->fillCircle(x, y, size / 4, color);
}

void drawSparkle(int16_t x, int16_t y, int16_t size, uint16_t color)
{
  drawThickSegment(x - size, y, x + size, y, max(2, size / 9), color);
  drawThickSegment(x, y - size, x, y + size, max(2, size / 9), color);
  drawThickSegment(x - size * 2 / 3, y - size * 2 / 3,
                   x + size * 2 / 3, y + size * 2 / 3, max(1, size / 13), color);
  drawThickSegment(x + size * 2 / 3, y - size * 2 / 3,
                   x - size * 2 / 3, y + size * 2 / 3, max(1, size / 13), color);
}

void drawDrop(int16_t x, int16_t y, int16_t size, uint16_t color)
{
  surface->fillCircle(x, y + size / 4, size / 3, color);
  surface->fillTriangle(x, y - size, x - size / 3, y + size / 4,
                        x + size / 3, y + size / 4, color);
}

void drawDecorations(uint32_t nowMs, int16_t mouthY, uint16_t accent)
{
  if (decoration == Decoration::NONE) return;
  const float pulse = 0.5f + 0.5f * sinf((float)nowMs / (135.0f - urgency * 35.0f));
  const uint16_t glow = scaleColor(accent, 0.20f);
  switch (decoration)
  {
  case Decoration::THOUGHT:
    surface->fillCircle(318, mouthY - 88, 6, glow);
    surface->fillCircle(340, mouthY - 110, 9, scaleColor(accent, 0.48f));
    surface->fillCircle(370, mouthY - 135, 12 + (int16_t)(pulse * 3.0f), accent);
    break;
  case Decoration::SPARKLE:
    drawSparkle(112, mouthY - 100, 14 + (int16_t)(pulse * 8.0f), accent);
    drawSparkle(360, mouthY + 88, 9 + (int16_t)((1.0f - pulse) * 6.0f), scaleColor(accent, 0.72f));
    break;
  case Decoration::CHECK:
    drawCheck(350, mouthY - 105, 48, scaleColor(accent, 0.28f));
    drawCheck(350, mouthY - 105, 38, accent);
    break;
  case Decoration::HEART:
    drawHeart(340, mouthY - 104 - (int16_t)(pulse * 7.0f), 42, scaleColor(accent, 0.28f));
    drawHeart(340, mouthY - 104 - (int16_t)(pulse * 7.0f), 31, accent);
    break;
  case Decoration::BLUSH:
    for (int offset = -16; offset <= 16; offset += 16)
    {
      drawThickSegment(88 + offset, mouthY + 54, 101 + offset, mouthY + 66, 3, scaleColor(accent, 0.62f));
      drawThickSegment(365 + offset, mouthY + 66, 378 + offset, mouthY + 54, 3, scaleColor(accent, 0.62f));
    }
    break;
  case Decoration::SWEAT:
    drawDrop(354, mouthY - 105 + (int16_t)(pulse * 4.0f), 24, scaleColor(accent, 0.84f));
    break;
  case Decoration::ALERT:
    drawThickSegment(94, mouthY - 76, 78, mouthY - 52, 4, glow);
    drawThickSegment(78, mouthY - 52, 99, mouthY - 50, 4, glow);
    drawThickSegment(99, mouthY - 50, 83, mouthY - 25, 4, accent);
    drawThickSegment(372, mouthY - 76, 388, mouthY - 52, 4, glow);
    drawThickSegment(388, mouthY - 52, 367, mouthY - 50, 4, glow);
    drawThickSegment(367, mouthY - 50, 383, mouthY - 25, 4, accent);
    break;
  case Decoration::NONE:
    break;
  }
}

void drawCenteredText(const char *text, int16_t y, uint8_t textSize, uint16_t color, int16_t xOverride = -32768)
{
  const int16_t width = (int16_t)(strlen(text) * 6 * textSize);
  const int16_t x = xOverride == -32768 ? (SCREEN_W - width) / 2 : xOverride;
  surface->setTextWrap(false);
  surface->setTextSize(textSize);
  surface->setTextColor(color);
  surface->setCursor(x, y);
  surface->print(text);
}

void renderTextContent(uint16_t accent)
{
  const size_t length = strlen(content);
  uint8_t textSize = length <= 7 ? 7 : length <= 11 ? 5 : length <= 17 ? 4 : 3;
  const int16_t textY = CENTER - (textSize * 4);
  if (mode == MouthMode::SCROLL)
  {
    drawCenteredText(content, textY + 3, textSize, scaleColor(accent, 0.15f), scrollX + 3);
    drawCenteredText(content, textY, textSize, accent, scrollX);
  }
  else
  {
    drawCenteredText(content, textY + 3, textSize, scaleColor(accent, 0.15f));
    drawCenteredText(content, textY, textSize, accent);
  }
}

void renderIconContent(uint16_t accent)
{
  const uint16_t glow = scaleColor(accent, 0.18f);
  if (equalsIgnoreCase(content, "HEART") || equalsIgnoreCase(content, "LOVE"))
  {
    drawHeart(CENTER, CENTER - 34, 92, glow);
    drawHeart(CENTER, CENTER - 34, 70, accent);
  }
  else if (equalsIgnoreCase(content, "CHECK") || equalsIgnoreCase(content, "SUCCESS"))
  {
    drawCheck(CENTER, CENTER, 150, glow);
    drawCheck(CENTER, CENTER, 124, accent);
  }
  else if (equalsIgnoreCase(content, "BURST") || equalsIgnoreCase(content, "SPARK"))
  {
    drawBurst(CENTER, CENTER, 150, accent);
  }
  else if (equalsIgnoreCase(content, "ELLIPSIS") || equalsIgnoreCase(content, "DOTS") ||
           equalsIgnoreCase(content, "PROCESSING"))
  {
    const float pulse = 0.5f + 0.5f * sinf((float)millis() / 145.0f);
    for (int index = -1; index <= 1; ++index)
    {
      const int radius = 13 + (index == (int)(millis() / 180u) % 3 - 1 ? (int)(pulse * 8.0f) : 0);
      surface->fillCircle(CENTER + index * 58, CENTER, radius + 10, glow);
      surface->fillCircle(CENTER + index * 58, CENTER, radius, accent);
    }
  }
  else if (equalsIgnoreCase(content, "QUESTION"))
  {
    drawCenteredText("?", CENTER - 72, 12, accent);
  }
  else if (equalsIgnoreCase(content, "EXCLAMATION") || equalsIgnoreCase(content, "ALERT"))
  {
    drawCenteredText("!", CENTER - 72, 12, accent);
  }
  else if (equalsIgnoreCase(content, "LEFT") || equalsIgnoreCase(content, "ARROW LEFT") ||
           equalsIgnoreCase(content, "RIGHT") || equalsIgnoreCase(content, "ARROW RIGHT"))
  {
    const int direction = (equalsIgnoreCase(content, "LEFT") || equalsIgnoreCase(content, "ARROW LEFT")) ? -1 : 1;
    drawThickSegment(CENTER - direction * 100, CENTER, CENTER + direction * 100, CENTER, 9, accent);
    drawThickSegment(CENTER + direction * 100, CENTER, CENTER + direction * 45, CENTER - 55, 9, accent);
    drawThickSegment(CENTER + direction * 100, CENTER, CENTER + direction * 45, CENTER + 55, 9, accent);
  }
  else
  {
    renderTextContent(accent);
  }
}

void mouthBounds(float normalizedX, float centerY, float halfGap,
                 float &top, float &bottom)
{
  const float edge = sqrtf(max(0.0f, 1.0f - normalizedX * normalizedX));
  const float envelope = powf(edge, max(0.35f, currentRoundness));
  const float centerLine = centerY + currentCurve * 68.0f * (1.0f - normalizedX * normalizedX)
                           - currentSkew * 30.0f * normalizedX;
  const float gap = halfGap * envelope;
  top = centerLine - gap * 0.82f;
  bottom = centerLine + gap * 1.18f;
}

void renderOpenMouth(float centerY, float halfWidth, uint16_t accent, uint16_t glow)
{
  const int16_t left = (int16_t)(CENTER - halfWidth);
  const int16_t right = (int16_t)(CENTER + halfWidth);
  const float halfGap = 8.0f + currentOpen * 80.0f;
  const uint16_t cavity = rgb565(10, 4, 13);
  const uint16_t tooth = rgb565(248, 244, 226);
  const uint16_t tongue = rgb565(247, 105, 143);

  for (int16_t x = left; x <= right; ++x)
  {
    const float normalizedX = (float)(x - CENTER) / max(1.0f, halfWidth);
    float top, bottom;
    mouthBounds(normalizedX, centerY, halfGap, top, bottom);
    const int16_t y0 = (int16_t)top;
    const int16_t y1 = (int16_t)bottom;
    if (y1 >= y0) surface->drawFastVLine(x, y0, y1 - y0 + 1, cavity);

    const int16_t gap = max(0, y1 - y0);
    if (currentTeeth > 0.02f && fabsf(normalizedX) < 0.80f && gap > 12)
    {
      const int16_t toothBottom = min(y1 - 3, y0 + (int16_t)(gap * currentTeeth));
      if (toothBottom > y0 + 2) surface->drawFastVLine(x, y0 + 2, toothBottom - y0 - 1, tooth);
    }
    if (currentTongue > 0.02f && fabsf(normalizedX) < 0.72f && gap > 14)
    {
      const int16_t tongueTop = max(y0 + 5, y1 - (int16_t)(gap * currentTongue));
      if (tongueTop < y1 - 2) surface->drawFastVLine(x, tongueTop, y1 - tongueTop - 1, tongue);
    }
  }

  for (int16_t x = left; x <= right; x += 4)
  {
    const float normalizedX = (float)(x - CENTER) / max(1.0f, halfWidth);
    float top, bottom;
    mouthBounds(normalizedX, centerY, halfGap, top, bottom);
    surface->fillCircle(x, (int16_t)top, style == MouthStyle::MINIMAL ? 6 : 11, glow);
    surface->fillCircle(x, (int16_t)bottom, style == MouthStyle::MINIMAL ? 6 : 11, glow);
  }
  for (int16_t x = left; x <= right; x += 3)
  {
    const float normalizedX = (float)(x - CENTER) / max(1.0f, halfWidth);
    float top, bottom;
    mouthBounds(normalizedX, centerY, halfGap, top, bottom);
    surface->fillCircle(x, (int16_t)top, style == MouthStyle::MINIMAL ? 4 : 7, accent);
    surface->fillCircle(x, (int16_t)bottom, style == MouthStyle::MINIMAL ? 4 : 7, accent);
  }
}

void renderClosedMouth(float centerY, float halfWidth, uint16_t accent,
                       uint16_t glowWide, uint16_t glowNear, uint32_t nowMs)
{
  const int radius = style == MouthStyle::MINIMAL ? 5 : 8;
  if (shape == MouthShape::TENSE)
  {
    const float jitter = sinf((float)nowMs / 58.0f) * (1.0f + urgency * 2.0f);
    const float segment = halfWidth * 0.5f;
    drawThickSegment(CENTER - halfWidth, centerY + jitter,
                     CENTER - segment, centerY - 13.0f + jitter, radius + 10, glowWide);
    drawThickSegment(CENTER - segment, centerY - 13.0f + jitter,
                     CENTER, centerY + 10.0f + jitter, radius + 10, glowWide);
    drawThickSegment(CENTER, centerY + 10.0f + jitter,
                     CENTER + segment, centerY - 13.0f + jitter, radius + 10, glowWide);
    drawThickSegment(CENTER + segment, centerY - 13.0f + jitter,
                     CENTER + halfWidth, centerY + jitter, radius + 10, glowWide);
    drawThickSegment(CENTER - halfWidth, centerY + jitter,
                     CENTER - segment, centerY - 13.0f + jitter, radius, accent);
    drawThickSegment(CENTER - segment, centerY - 13.0f + jitter,
                     CENTER, centerY + 10.0f + jitter, radius, accent);
    drawThickSegment(CENTER, centerY + 10.0f + jitter,
                     CENTER + segment, centerY - 13.0f + jitter, radius, accent);
    drawThickSegment(CENTER + segment, centerY - 13.0f + jitter,
                     CENTER + halfWidth, centerY + jitter, radius, accent);
    return;
  }
  if (shape == MouthShape::UNCERTAIN)
  {
    const float wobble = sinf((float)nowMs / 68.0f) * 3.0f;
    drawQuadratic(CENTER - halfWidth, centerY + currentSkew * 18.0f + wobble,
                  CENTER - halfWidth * 0.52f, centerY - 11.0f - wobble,
                  CENTER - halfWidth * 0.08f, centerY + 2.0f + wobble,
                  radius + 10, glowWide);
    drawQuadratic(CENTER - halfWidth * 0.08f, centerY + 2.0f + wobble,
                  CENTER + halfWidth * 0.42f, centerY + 15.0f - wobble,
                  CENTER + halfWidth, centerY - currentSkew * 18.0f + wobble,
                  radius + 10, glowWide);
    drawQuadratic(CENTER - halfWidth, centerY + currentSkew * 18.0f + wobble,
                  CENTER - halfWidth * 0.52f, centerY - 11.0f - wobble,
                  CENTER - halfWidth * 0.08f, centerY + 2.0f + wobble,
                  radius, accent);
    drawQuadratic(CENTER - halfWidth * 0.08f, centerY + 2.0f + wobble,
                  CENTER + halfWidth * 0.42f, centerY + 15.0f - wobble,
                  CENTER + halfWidth, centerY - currentSkew * 18.0f + wobble,
                  radius, accent);
    return;
  }

  const float leftY = centerY + currentSkew * 24.0f;
  const float rightY = centerY - currentSkew * 24.0f;
  const float controlY = centerY + currentCurve * 78.0f;
  drawQuadratic(CENTER - halfWidth, leftY, CENTER, controlY,
                CENTER + halfWidth, rightY, radius + 12, glowWide);
  drawQuadratic(CENTER - halfWidth, leftY, CENTER, controlY,
                CENTER + halfWidth, rightY, radius + 5, glowNear);
  drawQuadratic(CENTER - halfWidth, leftY, CENTER, controlY,
                CENTER + halfWidth, rightY, radius, accent);
  if (shape == MouthShape::LISTENING || shape == MouthShape::SOFT_SMILE)
  {
    surface->fillCircle((int16_t)(CENTER - halfWidth), (int16_t)leftY, radius + 2, accent);
    surface->fillCircle((int16_t)(CENTER + halfWidth), (int16_t)rightY, radius + 2, accent);
  }
}

void renderMouth(uint32_t nowMs, uint16_t accent)
{
  float desiredOpen = targetOpen;
  float desiredWidth = targetWidth;
  if (shape == MouthShape::SPEAKING)
  {
    static const float visemeOpen[] = {0.08f, 0.30f, 0.62f, 0.36f, 0.78f, 0.24f};
    static const float visemeWidth[] = {0.48f, 0.56f, 0.62f, 0.50f, 0.42f, 0.58f};
    const uint8_t phase = (uint8_t)((nowMs / 125u) % 6u);
    desiredOpen = visemeOpen[phase] * (0.72f + intensity * 0.34f);
    desiredWidth = visemeWidth[phase];
  }

  currentWidth += (desiredWidth - currentWidth) * 0.15f;
  currentCurve += (targetCurve - currentCurve) * 0.12f;
  currentOpen += (desiredOpen - currentOpen) * 0.22f;
  currentSkew += (targetSkew - currentSkew) * 0.10f;
  currentRoundness += (targetRoundness - currentRoundness) * 0.14f;
  currentTeeth += (targetTeeth - currentTeeth) * 0.16f;
  currentTongue += (targetTongue - currentTongue) * 0.13f;

  const uint32_t transitionMs = nowMs - shapeChangedAtMs;
  float anticipationScale = 1.0f;
  if (transitionMs < 72u)
    anticipationScale = 1.0f - 0.08f * sinf((float)transitionMs / 72.0f * PI);
  else if (transitionMs < 260u)
    anticipationScale = 1.0f + 0.045f * sinf((float)(transitionMs - 72u) / 188.0f * PI);

  const float life = sinf((float)nowMs / (980.0f - energy * 260.0f));
  float centerY = CENTER + life * (1.2f + energy * 2.6f);
  if (shape == MouthShape::UNCERTAIN) centerY += sinf((float)nowMs / 68.0f) * 2.2f;
  if (shape == MouthShape::DELIGHTED || shape == MouthShape::GRIN)
    centerY -= fabsf(sinf((float)nowMs / 210.0f)) * (1.0f + energy * 2.0f);

  float halfWidth = SCREEN_W * 0.5f * currentWidth * anticipationScale;
  if (style == MouthStyle::TEXT_FRIENDLY) halfWidth *= 0.78f;
  const uint16_t glowWide = scaleColor(accent, 0.10f);
  const uint16_t glowNear = scaleColor(accent, 0.28f);

  if (currentOpen > 0.085f)
    renderOpenMouth(centerY, halfWidth, accent, glowNear);
  else
    renderClosedMouth(centerY, halfWidth, accent, glowWide, glowNear, nowMs);
  drawDecorations(nowMs, (int16_t)centerY, accent);
}

void renderFrame(uint32_t nowMs)
{
  if (!displayReady) return;
  const uint16_t accent = accentColor();
  surface->fillScreen(rgb565(0, 0, 0));
  const uint16_t rim = scaleColor(accent, 0.08f + urgency * 0.05f);
  for (int ring = 0; ring < 3; ++ring)
    surface->drawCircle(CENTER, CENTER, 208 - ring, rim);

  if (mode == MouthMode::TEXT || mode == MouthMode::SCROLL)
    renderTextContent(accent);
  else if (mode == MouthMode::ICON)
    renderIconContent(accent);
  else if (mode != MouthMode::BLANK)
    renderMouth(nowMs, accent);

  canvas->flush();
}

void setContentMode(MouthMode nextMode, const char *value)
{
  mode = nextMode;
  copySafe(content, sizeof(content), value);
  scrollX = SCREEN_W;
  scrollComplete = nextMode != MouthMode::SCROLL;
}

BeatKind beatFromName(const char *name)
{
  if (equalsIgnoreCase(name, "ATTENTION")) return BeatKind::ATTENTION;
  if (equalsIgnoreCase(name, "THINKING")) return BeatKind::THINKING;
  if (equalsIgnoreCase(name, "SUCCESS")) return BeatKind::SUCCESS;
  if (equalsIgnoreCase(name, "ACKNOWLEDGE")) return BeatKind::ACKNOWLEDGE;
  if (equalsIgnoreCase(name, "REASSURE")) return BeatKind::REASSURE;
  if (equalsIgnoreCase(name, "ERROR")) return BeatKind::ERROR;
  if (equalsIgnoreCase(name, "PLAYFUL")) return BeatKind::PLAYFUL;
  return BeatKind::NONE;
}

void startBeat(BeatKind next)
{
  beat = next;
  beatStartedAtMs = millis();
  beatRevealed = false;
  mode = MouthMode::BEAT;
}

void tickBeat(uint32_t nowMs)
{
  if (beat == BeatKind::NONE) return;
  const uint32_t elapsed = nowMs - beatStartedAtMs;
  if (!beatRevealed && elapsed >= 280)
  {
    beatRevealed = true;
    switch (beat)
    {
    case BeatKind::ATTENTION: setContentMode(MouthMode::TEXT, "HEY YOU!"); break;
    case BeatKind::THINKING: setContentMode(MouthMode::SCROLL, "PLEASE WAIT..."); break;
    case BeatKind::SUCCESS: setContentMode(MouthMode::ICON, "CHECK"); break;
    case BeatKind::ACKNOWLEDGE: setContentMode(MouthMode::TEXT, "GOT IT"); break;
    case BeatKind::REASSURE: setContentMode(MouthMode::SCROLL, "YOU GOT THIS"); break;
    case BeatKind::ERROR: setContentMode(MouthMode::TEXT, "OOPS!"); break;
    case BeatKind::PLAYFUL: setContentMode(MouthMode::TEXT, "HEHEHE"); break;
    case BeatKind::NONE: break;
    }
  }
  if (elapsed >= 2300 && (mode != MouthMode::SCROLL || scrollComplete))
  {
    beat = BeatKind::NONE;
    mode = MouthMode::AUTO;
    content[0] = '\0';
    setAffect("neutral", 0.65f, warmth, confidence, urgency);
  }
}

void tickScroll()
{
  if (mode != MouthMode::SCROLL) return;
  const uint8_t textSize = strlen(content) <= 17 ? 4 : 3;
  const int16_t width = (int16_t)(strlen(content) * 6 * textSize);
  scrollX -= 5;
  if (scrollX + width < 20)
  {
    scrollComplete = true;
    scrollX = SCREEN_W;
  }
}

void printStatus()
{
  Serial.printf(
      "STATUS product=youandeye-mouth firmware=0.4.0 display=co5300 size=466x466 mode=%s affect=%s shape=%s animation=expressive-v2 accent=%s brightness=%u scrolling=%d scrollComplete=%d psram=%d\n",
      modeLabel(), affectName, shapeLabel(), accentName, brightness,
      mode == MouthMode::SCROLL ? 1 : 0, scrollComplete ? 1 : 0,
      psramFound() ? 1 : 0);
}

void printHelp()
{
  Serial.println("YouAndEye AMOLED mouth accepts semantic commands only:");
  Serial.println("  PROFILE MINIMAL|EXPRESSIVE|TEXT_FRIENDLY BLUE|CYAN|VIOLET|GOLD|CORAL|MINT <energy 0..1>");
  Serial.println("  AFFECT <canonical state> <intensity> <warmth> <confidence> <urgency>");
  Serial.println("  MOUTH AUTO|BLANK|STATUS");
  Serial.println("  TEXT <message> | SCROLL <message> | ICON HEART|CHECK|BURST|ELLIPSIS|QUESTION|EXCLAMATION|LEFT|RIGHT");
  Serial.println("  BEAT ATTENTION|THINKING|SUCCESS|ACKNOWLEDGE|REASSURE|ERROR|PLAYFUL");
  Serial.println("  BRIGHTNESS <10..100> | CANCEL | STATUS");
}

bool setAccent(const char *name)
{
  static const char *const allowed[] = {"BLUE", "CYAN", "VIOLET", "GOLD", "CORAL", "MINT"};
  for (const char *candidate : allowed)
  {
    if (equalsIgnoreCase(name, candidate))
    {
      copySafe(accentName, sizeof(accentName), candidate);
      for (char *p = accentName; *p; ++p) *p = (char)tolower((unsigned char)*p);
      return true;
    }
  }
  return false;
}

void processCommand(char *line)
{
  char *command = strtok(line, " \t");
  if (!command) return;

  if (equalsIgnoreCase(command, "STATUS"))
  {
    printStatus();
    return;
  }
  if (equalsIgnoreCase(command, "HELP"))
  {
    printHelp();
    return;
  }
  if (equalsIgnoreCase(command, "PROFILE"))
  {
    char *styleToken = strtok(nullptr, " \t");
    char *accentToken = strtok(nullptr, " \t");
    char *energyToken = strtok(nullptr, " \t");
    float nextEnergy = energy;
    if (!styleToken || !accentToken || !parseBoundedFloat(energyToken, nextEnergy))
    {
      Serial.println("ERR PROFILE expects style accent energy");
      return;
    }
    if (equalsIgnoreCase(styleToken, "MINIMAL")) style = MouthStyle::MINIMAL;
    else if (equalsIgnoreCase(styleToken, "EXPRESSIVE")) style = MouthStyle::EXPRESSIVE;
    else if (equalsIgnoreCase(styleToken, "TEXT_FRIENDLY")) style = MouthStyle::TEXT_FRIENDLY;
    else
    {
      Serial.println("ERR PROFILE unsupported style");
      return;
    }
    if (!setAccent(accentToken))
    {
      Serial.println("ERR PROFILE unsupported accent");
      return;
    }
    energy = nextEnergy;
    if (mode == MouthMode::AUTO) applyShape(shapeForAffect(affectName));
    Serial.printf("OK PROFILE %s %s %.2f\n", styleToken, accentName, energy);
    return;
  }
  if (equalsIgnoreCase(command, "AFFECT"))
  {
    char *name = strtok(nullptr, " \t");
    char *i = strtok(nullptr, " \t");
    char *w = strtok(nullptr, " \t");
    char *c = strtok(nullptr, " \t");
    char *u = strtok(nullptr, " \t");
    float nextI, nextW, nextC, nextU;
    if (!name || !parseBoundedFloat(i, nextI) || !parseBoundedFloat(w, nextW) ||
        !parseBoundedFloat(c, nextC) || !parseBoundedFloat(u, nextU))
    {
      Serial.println("ERR AFFECT expects state plus four values from 0..1");
      return;
    }
    setAffect(name, nextI, nextW, nextC, nextU);
    Serial.printf("OK AFFECT %s %.2f\n", affectName, intensity);
    return;
  }
  if (equalsIgnoreCase(command, "MOUTH"))
  {
    char *action = strtok(nullptr, " \t");
    if (equalsIgnoreCase(action, "STATUS")) { printStatus(); return; }
    if (equalsIgnoreCase(action, "AUTO"))
    {
      beat = BeatKind::NONE;
      mode = MouthMode::AUTO;
      content[0] = '\0';
      applyShape(shapeForAffect(affectName));
      Serial.println("OK MOUTH auto");
      return;
    }
    if (equalsIgnoreCase(action, "BLANK"))
    {
      beat = BeatKind::NONE;
      setContentMode(MouthMode::BLANK, "");
      Serial.println("OK MOUTH blank");
      return;
    }
    Serial.println("ERR MOUTH expects AUTO, BLANK, or STATUS");
    return;
  }
  if (equalsIgnoreCase(command, "TEXT") || equalsIgnoreCase(command, "SCROLL") || equalsIgnoreCase(command, "ICON"))
  {
    char *value = strtok(nullptr, "\r\n");
    while (value && (*value == ' ' || *value == '\t')) ++value;
    if (!value || !*value)
    {
      Serial.println("ERR content command expects a value");
      return;
    }
    beat = BeatKind::NONE;
    const MouthMode next = equalsIgnoreCase(command, "TEXT") ? MouthMode::TEXT :
                           equalsIgnoreCase(command, "SCROLL") ? MouthMode::SCROLL : MouthMode::ICON;
    setContentMode(next, value);
    Serial.printf("OK %s %s\n", command, content);
    return;
  }
  if (equalsIgnoreCase(command, "BEAT"))
  {
    const BeatKind next = beatFromName(strtok(nullptr, " \t"));
    if (next == BeatKind::NONE)
    {
      Serial.println("ERR BEAT unsupported sequence");
      return;
    }
    startBeat(next);
    Serial.println("OK BEAT");
    return;
  }
  if (equalsIgnoreCase(command, "BRIGHTNESS"))
  {
    char *value = strtok(nullptr, " \t");
    const int requested = value ? atoi(value) : 0;
    if (requested < 10 || requested > 100)
    {
      Serial.println("ERR BRIGHTNESS expects 10..100");
      return;
    }
    brightness = (uint8_t)requested;
    panel->setBrightness((uint8_t)map(brightness, 10, 100, 26, 255));
    Serial.printf("OK BRIGHTNESS %u\n", brightness);
    return;
  }
  if (equalsIgnoreCase(command, "CANCEL"))
  {
    beat = BeatKind::NONE;
    mode = MouthMode::AUTO;
    setAffect("neutral", 0.65f, warmth, confidence, urgency);
    Serial.println("OK CANCEL");
    return;
  }
  Serial.println("ERR unknown command");
}

void processSerial()
{
  while (Serial.available())
  {
    const char value = (char)Serial.read();
    if (value == '\n' || value == '\r')
    {
      if (commandLength > 0)
      {
        commandBuffer[commandLength] = '\0';
        processCommand(commandBuffer);
        commandLength = 0;
      }
    }
    else if (commandLength < COMMAND_MAX)
    {
      commandBuffer[commandLength++] = value;
    }
    else
    {
      commandLength = 0;
      Serial.println("ERR command too long");
    }
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(500);
  displayReady = canvas->begin();
  if (displayReady)
  {
    panel->setBrightness((uint8_t)map(brightness, 10, 100, 26, 255));
    surface->fillScreen(rgb565(0, 0, 0));
    canvas->flush(true);
  }
  Serial.println();
  Serial.println("=== YouAndEye AMOLED Mouth Ready ===");
  printStatus();
}

void loop()
{
  processSerial();
  const uint32_t nowMs = millis();
  tickBeat(nowMs);
  if (nowMs - lastFrameAtMs >= FRAME_MS)
  {
    lastFrameAtMs = nowMs;
    tickScroll();
    renderFrame(nowMs);
  }
  delay(1);
}
