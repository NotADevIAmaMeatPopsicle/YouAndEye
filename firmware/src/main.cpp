/**
 * Agent-controllable dual eye runtime
 * Heltec WiFi Kit 32 + Waveshare 0.71" DualEye LCD (GC9D01)
 *
 * The board acts as a low-latency renderer. A higher-level agent steers the
 * eyes over the physically attached USB serial connection.
 */

#include <Arduino.h>
#include <Arduino_GFX.h>
#include <databus/Arduino_ESP32SPI.h>
#include <display/Arduino_GC9D01.h>
#include <eye_renderer.h>
#include <esp_heap_caps.h>
#include <emote_motion.h>
#include "heltec_mouth.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef YOUANDEYE_SDF_RENDERER
#define YOUANDEYE_SDF_RENDERER 0
#endif

// Pin definitions for Heltec WiFi Kit 32
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS1 5
#define TFT_CS2 17
#define TFT_DC 21
#define TFT_RST1 22
#define TFT_RST2 19
#define TFT_BL1 25
#define TFT_BL2 26

static const int SCREEN_W = 160;
static const int SCREEN_H = 160;
static const int SCREEN_PIXELS = SCREEN_W * SCREEN_H;
static const int SDF_TRANSFER_H = 144;
static const uint32_t FRAME_INTERVAL_MS = 32;
static const uint32_t FRAME_DEADLINE_US = 33333;
static const int32_t DISPLAY_SPI_HZ = 40000000;
static const uint16_t EYE_BACKGROUND_565 = 0x0021;
static const uint8_t LEFT_DEFAULT_ROTATION = 1;
static const uint8_t RIGHT_DEFAULT_ROTATION = 3;

struct RgbColor
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

enum EyePreset
{
  PRESET_NATURAL,
  PRESET_ALERT,
  PRESET_SLEEPY,
  PRESET_CURIOUS,
  PRESET_FOCUSED
};

enum EyeBehaviorMode
{
  MODE_IDLE,
  MODE_ATTENTIVE,
  MODE_SPEAKING,
  MODE_TRACKING,
  MODE_SLEEPY
};

enum DisplayDiagnosticMode
{
  DISPLAY_LIVE,
  DISPLAY_TEST_BOTH,
  DISPLAY_TEST_LEFT,
  DISPLAY_TEST_RIGHT
};

enum MouthDisplayMode
{
  MOUTH_AUTO,
  MOUTH_CUSTOM,
  MOUTH_SCROLL,
  MOUTH_BLANK,
  MOUTH_SEQUENCE
};

enum CharacterBeatKind
{
  BEAT_NONE,
  BEAT_ATTENTION,
  BEAT_THINKING,
  BEAT_SUCCESS,
  BEAT_ACKNOWLEDGE,
  BEAT_REASSURE,
  BEAT_ERROR,
  BEAT_PLAYFUL
};

struct CharacterBeatState
{
  CharacterBeatKind kind = BEAT_NONE;
  uint32_t startedAtMs = 0;
  bool revealed = false;
  bool followupShown = false;
};

struct EyeCommandState
{
  bool autonomyEnabled = true;
  float targetLookX = 0.0f;
  float targetLookY = 0.0f;
  float targetPupil = 0.34f;
  float targetOpenness = 0.92f;
  float targetFocus = 0.18f;
  float browLift = 0.0f;
  float browRotation = 0.0f;
  float lowerLidLift = 0.10f;
  float asymmetry = 0.02f;
  float arc = 0.0f;
  float intensity = 0.70f;
  uint8_t pupilShape = 0;
  uint8_t palette = 0;
  bool semanticAffectActive = true;
  emote_affect_t affect = EMOTE_NEUTRAL;
  EyeBehaviorMode mode = MODE_ATTENTIVE;
  EyePreset preset = PRESET_NATURAL;
  RgbColor irisOuter = {53, 95, 141};
  RgbColor irisInner = {112, 171, 214};
  RgbColor irisRing = {21, 37, 63};
  RgbColor irisHighlight = {179, 213, 236};
};

struct EyeRuntimeState
{
  float lookX = 0.0f;
  float lookY = 0.0f;
  float pupil = 0.34f;
  float openness = 0.92f;
  float focus = 0.18f;
  float autoLookX = 0.0f;
  float autoLookY = 0.0f;
  float autoLookTargetX = 0.0f;
  float autoLookTargetY = 0.0f;
  float blinkAmount = 0.0f;
  bool blinkActive = false;
  bool blinkRequested = false;
  bool doubleBlinkPending = false;
  unsigned long blinkStartedAtMs = 0;
  unsigned long blinkDurationMs = 160;
  unsigned long doubleBlinkAtMs = 0;
  unsigned long nextBlinkAtMs = 0;
  unsigned long nextSaccadeAtMs = 0;
  unsigned long lastFrameAtMs = 0;
};

Arduino_DataBus *leftBus = new Arduino_ESP32SPI(TFT_DC, TFT_CS1, TFT_SCLK, TFT_MOSI, -1, VSPI);
Arduino_DataBus *rightBus = new Arduino_ESP32SPI(TFT_DC, TFT_CS2, TFT_SCLK, TFT_MOSI, -1, VSPI);
Arduino_GC9D01 *leftEye = new Arduino_GC9D01(leftBus, TFT_RST1, LEFT_DEFAULT_ROTATION, false);
Arduino_GC9D01 *rightEye = new Arduino_GC9D01(rightBus, TFT_RST2, RIGHT_DEFAULT_ROTATION, false);

static EyeCommandState commandState;
static EyeRuntimeState runtimeState;
static uint16_t leftFrameBuffer[SCREEN_PIXELS];
static uint16_t *rightFrameBuffer = nullptr;
static char serialBuffer[96];
static size_t serialBufferLen = 0;
static uint32_t frameCounter = 0;
static bool useSdfRenderer = YOUANDEYE_SDF_RENDERER != 0;
static uint32_t lastFrameUs = 0;
static uint32_t lastComputeUs = 0;
static uint32_t lastTransferUs = 0;
static uint32_t maxFrameUs = 0;
static uint32_t frameDeadlineMisses = 0;
static uint32_t lastShadedPixels = 0;
static uint32_t telemetryWindowStartedAtMs = 0;
static uint32_t telemetryWindowStartedAtFrame = 0;
static float lastMeasuredFps = 0.0f;
static float previousLookX = 0.0f;
static float previousLookY = 0.0f;
static DisplayDiagnosticMode displayDiagnosticMode = DISPLAY_LIVE;
static uint8_t leftRotation = LEFT_DEFAULT_ROTATION;
static uint8_t rightRotation = RIGHT_DEFAULT_ROTATION;
static bool parallelRendererEnabled = true;
static bool parallelRendererReady = false;
static SemaphoreHandle_t rightRenderRequest = nullptr;
static SemaphoreHandle_t rightRenderComplete = nullptr;
static TaskHandle_t rightRenderTaskHandle = nullptr;
struct RightRenderJob
{
  emote_pose_t pose;
  float gazeVelocityX;
  float gazeVelocityY;
  eye_render_metrics_t metrics;
  uint32_t computeUs;
};
static RightRenderJob rightRenderJob = {};
static emote_motion_t semanticMotion;
static emote_target_t semanticTarget;
static bool semanticMotionReady = false;
static uint32_t lastMotionUpdateMs = 0;
static HeltecMouthDisplay mouthDisplay;
static MouthDisplayMode mouthDisplayMode = MOUTH_AUTO;
static char customMouthText[65] = {};
static CharacterBeatState characterBeat;

static void applyPreset(EyePreset preset);
static void applyAffect(emote_affect_t affect);
static void applyAffectWithIntensity(emote_affect_t affect, float intensity);
static void processCommand(char *line);
static void renderFrame();
static void rightEyeRenderTask(void *unused);
static void scheduleNextBlink(unsigned long now);
static void scheduleNextSaccade(unsigned long now);

static const char *mouthModeLabel()
{
  switch (mouthDisplayMode)
  {
  case MOUTH_CUSTOM:
    return "text";
  case MOUTH_SCROLL:
    return "scroll";
  case MOUTH_BLANK:
    return "blank";
  case MOUTH_SEQUENCE:
    return "sequence";
  case MOUTH_AUTO:
  default:
    return "auto";
  }
}

static HeltecMouthShape mouthShapeForAffect(emote_affect_t affect)
{
  switch (affect)
  {
  case EMOTE_HAPPY:
  case EMOTE_ENCOURAGING:
  case EMOTE_REASSURING:
  case EMOTE_DELIGHTED:
  case EMOTE_EXCITED:
  case EMOTE_LOVE:
  case EMOTE_SUCCESS:
    return HeltecMouthShape::SMILE;
  case EMOTE_SURPRISED:
    return HeltecMouthShape::SURPRISED;
  case EMOTE_THINKING:
  case EMOTE_CURIOUS:
  case EMOTE_UNCERTAIN:
  case EMOTE_EMBARRASSED:
  case EMOTE_SUSPICIOUS:
  case EMOTE_PLAYFUL:
    return HeltecMouthShape::SMIRK;
  case EMOTE_SAD:
  case EMOTE_CONCERNED:
  case EMOTE_ERROR:
    return HeltecMouthShape::FROWN;
  case EMOTE_SPEAKING:
    return HeltecMouthShape::SPEAKING;
  case EMOTE_LISTENING:
  case EMOTE_WORKING:
  case EMOTE_NEUTRAL:
  default:
    return HeltecMouthShape::NEUTRAL;
  }
}

static void refreshMouthDisplay()
{
  if (!mouthDisplay.ready())
  {
    return;
  }
  if (mouthDisplayMode == MOUTH_BLANK)
  {
    mouthDisplay.blank();
  }
  else if (mouthDisplayMode == MOUTH_CUSTOM)
  {
    mouthDisplay.showText(customMouthText);
  }
  else if (mouthDisplayMode == MOUTH_SCROLL)
  {
    mouthDisplay.scrollText(customMouthText);
  }
  else if (mouthDisplayMode == MOUTH_SEQUENCE)
  {
    return;
  }
  else
  {
    mouthDisplay.showMouth(mouthShapeForAffect(commandState.affect));
  }
}

static inline float clampf(float value, float minimum, float maximum)
{
  if (value < minimum)
  {
    return minimum;
  }
  if (value > maximum)
  {
    return maximum;
  }
  return value;
}

static inline float smoothstepf(float t)
{
  t = clampf(t, 0.0f, 1.0f);
  return t * t * (3.0f - (2.0f * t));
}

static inline float absf(float value)
{
  return value < 0.0f ? -value : value;
}

static inline RgbColor makeColor(uint8_t r, uint8_t g, uint8_t b)
{
  RgbColor color = {r, g, b};
  return color;
}

static inline RgbColor mixColor(const RgbColor &a, const RgbColor &b, float t)
{
  t = clampf(t, 0.0f, 1.0f);
  return makeColor(
      (uint8_t)(a.r + ((b.r - a.r) * t) + 0.5f),
      (uint8_t)(a.g + ((b.g - a.g) * t) + 0.5f),
      (uint8_t)(a.b + ((b.b - a.b) * t) + 0.5f));
}

static inline uint16_t toBigEndian565(const RgbColor &color)
{
  uint16_t value = (uint16_t)(((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3));
  return (uint16_t)((value << 8) | (value >> 8));
}

static void drawDisplayDiagnostic(Arduino_GFX *display, bool enabled, const char *label, uint16_t accent)
{
  display->fillScreen(enabled ? 0x0000 : 0x2104);
  if (!enabled)
  {
    return;
  }
  display->fillRect(0, 0, SCREEN_W, 28, accent);
  display->fillRect(0, SCREEN_H - 16, SCREEN_W, 16, 0x001F);
  display->drawRect(2, 2, SCREEN_W - 4, SCREEN_H - 4, 0xFFFF);
  display->setTextColor(0xFFFF);
  display->setTextSize(2);
  display->setCursor(58, 7);
  display->print("TOP");
  display->setTextSize(3);
  display->setCursor(52, 68);
  display->print(label);
}

static void renderDisplayDiagnostic()
{
  const bool leftEnabled = displayDiagnosticMode != DISPLAY_TEST_RIGHT;
  const bool rightEnabled = displayDiagnosticMode != DISPLAY_TEST_LEFT;
  drawDisplayDiagnostic(leftEye, leftEnabled, "LEFT", 0xF800);
  drawDisplayDiagnostic(rightEye, rightEnabled, "RIGHT", 0x07E0);
}

static void rightEyeRenderTask(void *unused)
{
  (void)unused;
  for (;;)
  {
    xSemaphoreTake(rightRenderRequest, portMAX_DELAY);
    const uint32_t startedAtUs = micros();
    eye_renderer_render_rgb565(
        rightFrameBuffer,
        SCREEN_W,
        SCREEN_H,
        &rightRenderJob.pose,
        false,
        rightRenderJob.gazeVelocityX,
        rightRenderJob.gazeVelocityY,
        &rightRenderJob.metrics);
    rightRenderJob.computeUs = micros() - startedAtUs;
    xSemaphoreGive(rightRenderComplete);
    // The right-eye worker is otherwise idle while the main core transfers the
    // finished frame. Use that window for smooth, independent OLED marquee work.
    mouthDisplay.tick(millis());
  }
}

static void startParallelRenderer()
{
  rightFrameBuffer = static_cast<uint16_t *>(
      heap_caps_malloc(SCREEN_PIXELS * sizeof(uint16_t), MALLOC_CAP_8BIT));
  if (!rightFrameBuffer)
  {
    Serial.println("WARN parallel renderer buffer unavailable; using sequential path");
    return;
  }

  rightRenderRequest = xSemaphoreCreateBinary();
  rightRenderComplete = xSemaphoreCreateBinary();
  if (!rightRenderRequest || !rightRenderComplete)
  {
    Serial.println("WARN parallel renderer semaphores unavailable; using sequential path");
    if (rightRenderRequest) vSemaphoreDelete(rightRenderRequest);
    if (rightRenderComplete) vSemaphoreDelete(rightRenderComplete);
    rightRenderRequest = nullptr;
    rightRenderComplete = nullptr;
    heap_caps_free(rightFrameBuffer);
    rightFrameBuffer = nullptr;
    return;
  }

  const BaseType_t created = xTaskCreatePinnedToCore(
      rightEyeRenderTask,
      "right-eye-render",
      4096,
      nullptr,
      1,
      &rightRenderTaskHandle,
      0);
  parallelRendererReady = created == pdPASS;
  if (!parallelRendererReady)
  {
    Serial.println("WARN parallel renderer task unavailable; using sequential path");
    vSemaphoreDelete(rightRenderRequest);
    vSemaphoreDelete(rightRenderComplete);
    rightRenderRequest = nullptr;
    rightRenderComplete = nullptr;
    heap_caps_free(rightFrameBuffer);
    rightFrameBuffer = nullptr;
  }
}

static inline float hashUnit(int x, int y, int seed)
{
  uint32_t n = (uint32_t)(x * 374761393) + (uint32_t)(y * 668265263) + (uint32_t)(seed * 2246822519UL);
  n = (n ^ (n >> 13)) * 1274126177UL;
  n ^= (n >> 16);
  return (float)(n & 0xFFFF) / 65535.0f;
}

static char upperAscii(char c)
{
  return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

static bool tokenEquals(const char *lhs, const char *rhs)
{
  if (!lhs || !rhs)
  {
    return false;
  }

  while (*lhs && *rhs)
  {
    if (upperAscii(*lhs) != upperAscii(*rhs))
    {
      return false;
    }
    ++lhs;
    ++rhs;
  }

  return (*lhs == '\0') && (*rhs == '\0');
}

static bool parseFloatToken(const char *token, float &value)
{
  if (!token)
  {
    return false;
  }

  char *end = nullptr;
  value = strtof(token, &end);
  return end != token && *end == '\0';
}

static bool parseRotationToken(const char *token, uint8_t &value)
{
  if (!token)
  {
    return false;
  }

  char *end = nullptr;
  const long parsed = strtol(token, &end, 10);
  if (end == token || *end != '\0' || parsed < 0 || parsed > 3)
  {
    return false;
  }

  value = (uint8_t)parsed;
  return true;
}

static const char *presetLabel(EyePreset preset)
{
  switch (preset)
  {
  case PRESET_ALERT:
    return "alert";
  case PRESET_SLEEPY:
    return "sleepy";
  case PRESET_CURIOUS:
    return "curious";
  case PRESET_FOCUSED:
    return "focused";
  case PRESET_NATURAL:
  default:
    return "natural";
  }
}

static const char *behaviorModeLabel(EyeBehaviorMode mode)
{
  switch (mode)
  {
  case MODE_IDLE:
    return "idle";
  case MODE_TRACKING:
    return "tracking";
  case MODE_SPEAKING:
    return "speaking";
  case MODE_SLEEPY:
    return "sleepy";
  case MODE_ATTENTIVE:
  default:
    return "attentive";
  }
}

static const char *displayDiagnosticLabel()
{
  switch (displayDiagnosticMode)
  {
  case DISPLAY_TEST_BOTH:
    return "test-both";
  case DISPLAY_TEST_LEFT:
    return "test-left";
  case DISPLAY_TEST_RIGHT:
    return "test-right";
  case DISPLAY_LIVE:
  default:
    return "live";
  }
}

static emote_mode_t semanticMode(EyeBehaviorMode mode)
{
  switch (mode)
  {
  case MODE_IDLE:
    return EMOTE_MODE_IDLE;
  case MODE_TRACKING:
    return EMOTE_MODE_TRACKING;
  case MODE_SPEAKING:
    return EMOTE_MODE_SPEAKING;
  case MODE_SLEEPY:
    return EMOTE_MODE_SLEEPY;
  case MODE_ATTENTIVE:
  default:
    return EMOTE_MODE_ATTENTIVE;
  }
}

static void submitSemanticTarget()
{
  if (!semanticMotionReady || !commandState.semanticAffectActive)
  {
    return;
  }

  const uint32_t now = millis();
  semanticTarget.mode = semanticMode(commandState.mode);
  semanticTarget.autonomy = commandState.autonomyEnabled;
  emote_motion_apply(&semanticMotion, &semanticTarget, now);
  lastMotionUpdateMs = now;
}

static void requestBlink()
{
  if (semanticMotionReady && commandState.semanticAffectActive)
  {
    emote_motion_request_blink(&semanticMotion, millis());
  }
  else
  {
    runtimeState.blinkRequested = true;
  }
}

static void setAutonomyEnabled(bool enabled)
{
  commandState.autonomyEnabled = enabled;
  if (commandState.semanticAffectActive)
  {
    semanticTarget.autonomy = enabled;
    submitSemanticTarget();
  }
  else if (enabled)
  {
    scheduleNextBlink(millis());
    scheduleNextSaccade(millis());
  }
  else
  {
    runtimeState.autoLookX = 0.0f;
    runtimeState.autoLookY = 0.0f;
    runtimeState.autoLookTargetX = 0.0f;
    runtimeState.autoLookTargetY = 0.0f;
  }
}

static void setLookTarget(float x, float y, bool centered)
{
  commandState.targetLookX = clampf(x, -1.0f, 1.0f);
  commandState.targetLookY = clampf(y, -1.0f, 1.0f);
  if (commandState.semanticAffectActive)
  {
    semanticTarget.gaze_mode = centered ? EMOTE_GAZE_USER : EMOTE_GAZE_POINT;
    semanticTarget.gaze_x = commandState.targetLookX;
    semanticTarget.gaze_y = commandState.targetLookY;
    submitSemanticTarget();
  }
}

static void activateManualControls()
{
  commandState.semanticAffectActive = false;
}

static const char *rendererLabel()
{
  return useSdfRenderer ? "sdf" : "legacy";
}

static const char *affectLabel()
{
  return commandState.semanticAffectActive ? emote_affect_name(commandState.affect) : "legacy-preset";
}

static int32_t settledMs(uint32_t value)
{
  return value == UINT32_MAX ? -1 : (int32_t)value;
}

static bool applyRendererByName(const char *value)
{
  if (tokenEquals(value, "SDF"))
  {
    useSdfRenderer = true;
    return true;
  }
  if (tokenEquals(value, "LEGACY"))
  {
    useSdfRenderer = false;
    return true;
  }
  return false;
}

static bool parseOnOffValue(const char *value, bool &enabled)
{
  if (tokenEquals(value, "ON") || tokenEquals(value, "TRUE") || tokenEquals(value, "1"))
  {
    enabled = true;
    return true;
  }

  if (tokenEquals(value, "OFF") || tokenEquals(value, "FALSE") || tokenEquals(value, "0"))
  {
    enabled = false;
    return true;
  }

  return false;
}

static bool applyPresetByName(const char *value)
{
  if (tokenEquals(value, "NATURAL"))
  {
    applyPreset(PRESET_NATURAL);
    return true;
  }
  if (tokenEquals(value, "ALERT"))
  {
    applyPreset(PRESET_ALERT);
    return true;
  }
  if (tokenEquals(value, "SLEEPY"))
  {
    applyPreset(PRESET_SLEEPY);
    return true;
  }
  if (tokenEquals(value, "CURIOUS"))
  {
    applyPreset(PRESET_CURIOUS);
    return true;
  }
  if (tokenEquals(value, "FOCUSED"))
  {
    applyPreset(PRESET_FOCUSED);
    return true;
  }

  return false;
}

static bool applyBehaviorModeByName(const char *value)
{
  if (tokenEquals(value, "IDLE"))
  {
    commandState.mode = MODE_IDLE;
    return true;
  }
  if (tokenEquals(value, "ATTENTIVE"))
  {
    commandState.mode = MODE_ATTENTIVE;
    return true;
  }
  if (tokenEquals(value, "SPEAKING"))
  {
    commandState.mode = MODE_SPEAKING;
    return true;
  }
  if (tokenEquals(value, "TRACKING"))
  {
    commandState.mode = MODE_TRACKING;
    return true;
  }
  if (tokenEquals(value, "SLEEPY"))
  {
    commandState.mode = MODE_SLEEPY;
    return true;
  }

  return false;
}

static void scheduleNextBlink(unsigned long now)
{
  unsigned long minDelay = 2400UL;
  unsigned long maxDelay = 5200UL;

  switch (commandState.mode)
  {
  case MODE_IDLE:
    minDelay = 3000UL;
    maxDelay = 6800UL;
    break;
  case MODE_SPEAKING:
    minDelay = 3200UL;
    maxDelay = 6200UL;
    break;
  case MODE_TRACKING:
    minDelay = 2600UL;
    maxDelay = 5200UL;
    break;
  case MODE_SLEEPY:
    minDelay = 1300UL;
    maxDelay = 3200UL;
    break;
  case MODE_ATTENTIVE:
  default:
    minDelay = 2200UL;
    maxDelay = 4800UL;
    break;
  }

  if (commandState.preset == PRESET_ALERT)
  {
    minDelay = minDelay > 300UL ? (minDelay - 300UL) : minDelay;
  }
  else if (commandState.preset == PRESET_SLEEPY)
  {
    maxDelay += 600UL;
  }

  runtimeState.nextBlinkAtMs = now + (unsigned long)random((long)minDelay, (long)maxDelay);
}

static void scheduleNextSaccade(unsigned long now)
{
  unsigned long minDelay = 550UL;
  unsigned long maxDelay = 1800UL;

  switch (commandState.mode)
  {
  case MODE_IDLE:
    minDelay = 1300UL;
    maxDelay = 2800UL;
    break;
  case MODE_SPEAKING:
    minDelay = 900UL;
    maxDelay = 2200UL;
    break;
  case MODE_TRACKING:
    minDelay = 180UL;
    maxDelay = 520UL;
    break;
  case MODE_SLEEPY:
    minDelay = 1800UL;
    maxDelay = 3400UL;
    break;
  case MODE_ATTENTIVE:
  default:
    minDelay = 550UL;
    maxDelay = 1800UL;
    break;
  }

  runtimeState.nextSaccadeAtMs = now + (unsigned long)random((long)minDelay, (long)maxDelay);
}

static void triggerBlink(unsigned long now)
{
  runtimeState.blinkActive = true;
  runtimeState.blinkRequested = false;
  runtimeState.blinkStartedAtMs = now;
  runtimeState.blinkDurationMs = (unsigned long)random(135, 205);
  if (commandState.autonomyEnabled && !runtimeState.doubleBlinkPending && random(0, 100) < 14)
  {
    runtimeState.doubleBlinkPending = true;
    runtimeState.doubleBlinkAtMs = now + (unsigned long)random(85, 165);
  }
}

static void applyIrisBaseColor(const RgbColor &base)
{
  commandState.irisOuter = mixColor(makeColor(18, 28, 42), base, 0.70f);
  commandState.irisInner = mixColor(base, makeColor(210, 235, 248), 0.30f);
  commandState.irisRing = mixColor(makeColor(5, 10, 16), base, 0.38f);
  commandState.irisHighlight = mixColor(base, makeColor(235, 245, 255), 0.55f);
}

static void applyPreset(EyePreset preset)
{
  commandState.preset = preset;
  commandState.semanticAffectActive = false;
  commandState.browRotation = 0.0f;
  commandState.asymmetry = 0.02f;
  commandState.arc = 0.0f;
  commandState.intensity = 0.70f;
  commandState.pupilShape = 0;
  commandState.palette = 0;

  switch (preset)
  {
  case PRESET_ALERT:
    commandState.targetPupil = 0.23f;
    commandState.targetOpenness = 1.0f;
    commandState.targetFocus = 0.24f;
    commandState.browLift = 0.25f;
    commandState.browRotation = 0.02f;
    commandState.lowerLidLift = 0.16f;
    commandState.intensity = 0.92f;
    applyIrisBaseColor(makeColor(62, 146, 204));
    break;

  case PRESET_SLEEPY:
    commandState.targetPupil = 0.40f;
    commandState.targetOpenness = 0.58f;
    commandState.targetFocus = 0.10f;
    commandState.browLift = -0.18f;
    commandState.browRotation = -0.05f;
    commandState.lowerLidLift = 0.20f;
    commandState.asymmetry = 0.07f;
    commandState.intensity = 0.65f;
    applyIrisBaseColor(makeColor(109, 86, 56));
    break;

  case PRESET_CURIOUS:
    commandState.targetPupil = 0.29f;
    commandState.targetOpenness = 0.90f;
    commandState.targetFocus = 0.32f;
    commandState.browLift = 0.10f;
    commandState.browRotation = -0.12f;
    commandState.lowerLidLift = 0.14f;
    commandState.asymmetry = 0.13f;
    commandState.intensity = 0.72f;
    applyIrisBaseColor(makeColor(84, 161, 116));
    break;

  case PRESET_FOCUSED:
    commandState.targetPupil = 0.24f;
    commandState.targetOpenness = 0.94f;
    commandState.targetFocus = 0.42f;
    commandState.browLift = 0.18f;
    commandState.browRotation = -0.24f;
    commandState.lowerLidLift = 0.12f;
    commandState.asymmetry = 0.08f;
    commandState.intensity = 0.76f;
    applyIrisBaseColor(makeColor(84, 129, 188));
    break;

  case PRESET_NATURAL:
  default:
    commandState.targetPupil = 0.34f;
    commandState.targetOpenness = 0.92f;
    commandState.targetFocus = 0.18f;
    commandState.browLift = 0.0f;
    commandState.lowerLidLift = 0.10f;
    applyIrisBaseColor(makeColor(76, 125, 176));
    break;
  }
}

static void applyAffect(emote_affect_t affect)
{
  applyAffectWithIntensity(affect, emote_pose_for_affect(affect).intensity);
}

static void applyAffectWithIntensity(emote_affect_t affect, float intensity)
{
  const emote_pose_t pose = emote_pose_for_affect_intensity(affect, clampf(intensity, 0.0f, 1.0f));
  commandState.semanticAffectActive = true;
  commandState.affect = affect;
  commandState.targetLookX = pose.gaze_x;
  commandState.targetLookY = pose.gaze_y;
  commandState.targetPupil = pose.pupil;
  commandState.targetOpenness = pose.open;
  commandState.browLift = pose.brow_y;
  commandState.browRotation = pose.brow_rotation;
  commandState.lowerLidLift = pose.lower_lid;
  commandState.asymmetry = pose.asymmetry;
  commandState.arc = pose.arc;
  commandState.intensity = pose.intensity;
  commandState.pupilShape = pose.pupil_shape;
  commandState.palette = pose.palette;

  semanticTarget.affect = affect;
  semanticTarget.intensity = pose.intensity;
  semanticTarget.mode = semanticMode(commandState.mode);
  semanticTarget.autonomy = commandState.autonomyEnabled;
  semanticTarget.gaze_mode = EMOTE_GAZE_WANDER;
  semanticTarget.gaze_x = pose.gaze_x;
  semanticTarget.gaze_y = pose.gaze_y;
  semanticTarget.ttl_ms = 600000;
  semanticTarget.decay = EMOTE_DECAY_EASE_OUT;
  semanticTarget.decay_duration_ms = 1200;
  semanticTarget.priority = affect == EMOTE_ERROR ? EMOTE_PRIORITY_ALERT : EMOTE_PRIORITY_NORMAL;
  if (mouthDisplayMode == MOUTH_AUTO)
  {
    refreshMouthDisplay();
  }
  submitSemanticTarget();
}

static const char *characterBeatLabel(CharacterBeatKind kind)
{
  switch (kind)
  {
  case BEAT_ATTENTION: return "attention";
  case BEAT_THINKING: return "thinking";
  case BEAT_SUCCESS: return "success";
  case BEAT_ACKNOWLEDGE: return "acknowledge";
  case BEAT_REASSURE: return "reassure";
  case BEAT_ERROR: return "error";
  case BEAT_PLAYFUL: return "playful";
  case BEAT_NONE:
  default: return "none";
  }
}

static bool characterBeatFromName(const char *name, CharacterBeatKind &kind)
{
  if (tokenEquals(name, "ATTENTION")) kind = BEAT_ATTENTION;
  else if (tokenEquals(name, "THINKING")) kind = BEAT_THINKING;
  else if (tokenEquals(name, "SUCCESS") || tokenEquals(name, "CELEBRATE")) kind = BEAT_SUCCESS;
  else if (tokenEquals(name, "ACK") || tokenEquals(name, "ACKNOWLEDGE")) kind = BEAT_ACKNOWLEDGE;
  else if (tokenEquals(name, "REASSURE")) kind = BEAT_REASSURE;
  else if (tokenEquals(name, "ERROR")) kind = BEAT_ERROR;
  else if (tokenEquals(name, "PLAYFUL")) kind = BEAT_PLAYFUL;
  else return false;
  return true;
}

static emote_affect_t characterBeatAffect(CharacterBeatKind kind)
{
  switch (kind)
  {
  case BEAT_ATTENTION: return EMOTE_LISTENING;
  case BEAT_THINKING: return EMOTE_THINKING;
  case BEAT_SUCCESS: return EMOTE_ENCOURAGING;
  case BEAT_ACKNOWLEDGE: return EMOTE_SUCCESS;
  case BEAT_REASSURE: return EMOTE_REASSURING;
  case BEAT_ERROR: return EMOTE_ERROR;
  case BEAT_PLAYFUL: return EMOTE_PLAYFUL;
  case BEAT_NONE:
  default: return EMOTE_NEUTRAL;
  }
}

static uint32_t characterBeatDurationMs(CharacterBeatKind kind)
{
  switch (kind)
  {
  case BEAT_THINKING:
  case BEAT_SUCCESS:
  case BEAT_REASSURE:
    return 2400;
  case BEAT_ERROR:
    return 1900;
  default:
    return 1700;
  }
}

static void showCharacterBeatText(const char *text, bool scroll)
{
  snprintf(customMouthText, sizeof(customMouthText), "%s", text);
  if (scroll) mouthDisplay.scrollText(customMouthText);
  else mouthDisplay.showText(customMouthText);
}

static void cancelCharacterBeat()
{
  characterBeat = {};
  if (mouthDisplayMode == MOUTH_SEQUENCE)
  {
    mouthDisplayMode = MOUTH_AUTO;
  }
}

static bool startCharacterBeat(CharacterBeatKind kind)
{
  if (kind == BEAT_NONE || !mouthDisplay.ready()) return false;
  cancelCharacterBeat();
  characterBeat.kind = kind;
  characterBeat.startedAtMs = millis();
  mouthDisplayMode = MOUTH_SEQUENCE;
  mouthDisplay.showMouth(HeltecMouthShape::NEUTRAL);
  applyAffect(characterBeatAffect(kind));
  return true;
}

static void tickCharacterBeat(uint32_t nowMs)
{
  if (characterBeat.kind == BEAT_NONE) return;
  const uint32_t elapsed = nowMs - characterBeat.startedAtMs;
  if (!characterBeat.revealed && elapsed >= 300)
  {
    characterBeat.revealed = true;
    switch (characterBeat.kind)
    {
    case BEAT_ATTENTION: showCharacterBeatText("HEY YOU!", false); break;
    case BEAT_THINKING: showCharacterBeatText("PLEASE WAIT...", true); break;
    case BEAT_SUCCESS: showCharacterBeatText("CHECK", false); break;
    case BEAT_ACKNOWLEDGE: showCharacterBeatText("GOT IT", false); break;
    case BEAT_REASSURE: showCharacterBeatText("YOU GOT THIS", true); break;
    case BEAT_ERROR: showCharacterBeatText("OOPS!", false); break;
    case BEAT_PLAYFUL: showCharacterBeatText("HEHEHE", false); break;
    case BEAT_NONE: break;
    }
  }
  if (characterBeat.kind == BEAT_SUCCESS && characterBeat.revealed &&
      !characterBeat.followupShown && elapsed >= 900)
  {
    characterBeat.followupShown = true;
    showCharacterBeatText("GOOD JOB!", true);
  }
  const bool waitingForScroll = mouthDisplay.scrolling() && !mouthDisplay.scrollCycleCompleted();
  if (elapsed >= characterBeatDurationMs(characterBeat.kind) && !waitingForScroll)
  {
    characterBeat = {};
    mouthDisplayMode = MOUTH_AUTO;
    customMouthText[0] = '\0';
    applyAffect(EMOTE_NEUTRAL);
  }
}

static bool applyAffectByName(const char *value, float intensity = -1.0f)
{
  if (!value) return false;
  for (int index = 0; index < EMOTE_AFFECT_COUNT; ++index)
  {
    const emote_affect_t affect = (emote_affect_t)index;
    if (tokenEquals(value, emote_affect_name(affect)))
    {
      if (intensity < 0.0f) applyAffect(affect);
      else applyAffectWithIntensity(affect, intensity);
      return true;
    }
  }
  return false;
}

static void printHelp()
{
  Serial.println("OK commands:");
  Serial.println("  HELP");
  Serial.println("  STATUS");
  Serial.println("  MOTION                  shared motion telemetry");
  Serial.println("  AUTONOMY ON|OFF");
  Serial.println("  MODE IDLE|ATTENTIVE|SPEAKING|TRACKING|SLEEPY");
  Serial.println("  LOOK <x> <y>          range -1.0..1.0");
  Serial.println("  PUPIL <value>         range 0.12..0.86");
  Serial.println("  OPEN <value>          range 0.02..1.30");
  Serial.println("  FOCUS <value>         range 0.00..0.60");
  Serial.println("  PRESET NATURAL|ALERT|SLEEPY|CURIOUS|FOCUSED");
  Serial.println("  EMOTE <canonical affect> [0.0..1.0]  neutral, happy, thinking, error, ...");
  Serial.println("  MOUTH AUTO|BLANK|STATUS  built-in Heltec OLED mode");
  Serial.println("  TEXT <message>|CLEAR|BLANK  show text on the built-in OLED");
  Serial.println("  SCROLL <message>        framed software-scroll OLED text");
  Serial.println("  QUESTION <message>      listening eyes plus question text");
  Serial.println("  ACK [message]           success eyes plus acknowledgement");
  Serial.println("  BEAT ATTENTION|THINKING|SUCCESS|ACKNOWLEDGE|REASSURE|ERROR|PLAYFUL");
  Serial.println("  RENDERER SDF|LEGACY   live fallback, no reflash");
  Serial.println("  PIPELINE ON|OFF       dual-core SDF rendering fallback");
  Serial.println("  DISPLAY LIVE|TEST|LEFT|RIGHT  static panel-path diagnostic");
  Serial.println("  ORIENT <left 0..3> <right 0..3>  live panel rotation");
  Serial.println("  IRIS <r> <g> <b>      range 0..255");
  Serial.println("  BLINK");
  Serial.println("  CENTER");
}

static void printStatus()
{
  Serial.printf(
      "STATUS renderer=%s pipeline=%d/%d display=%s rotations=%u,%u mouth=%d/%s affect=%s autonomy=%d mode=%s preset=%s targetLookX=%.2f targetLookY=%.2f liveLookX=%.2f liveLookY=%.2f pupil=%.2f open=%.2f focus=%.2f blink=%.2f frameUs=%lu computeUs=%lu transferUs=%lu maxFrameUs=%lu fps=%.1f misses=%lu shaded=%lu iris=%u,%u,%u\n",
      rendererLabel(),
      parallelRendererEnabled ? 1 : 0,
      parallelRendererReady ? 1 : 0,
      displayDiagnosticLabel(),
      leftRotation,
      rightRotation,
      mouthDisplay.ready() ? 1 : 0,
      mouthModeLabel(),
      affectLabel(),
      commandState.autonomyEnabled ? 1 : 0,
      behaviorModeLabel(commandState.mode),
      presetLabel(commandState.preset),
      commandState.targetLookX,
      commandState.targetLookY,
      runtimeState.lookX,
      runtimeState.lookY,
      commandState.targetPupil,
      commandState.targetOpenness,
      commandState.targetFocus,
      runtimeState.blinkAmount,
      (unsigned long)lastFrameUs,
      (unsigned long)lastComputeUs,
      (unsigned long)lastTransferUs,
      (unsigned long)maxFrameUs,
      lastMeasuredFps,
      (unsigned long)frameDeadlineMisses,
      (unsigned long)lastShadedPixels,
      commandState.irisInner.r,
      commandState.irisInner.g,
      commandState.irisInner.b);
}

static void printMotionStatus()
{
  const emote_motion_telemetry_t *motion = emote_motion_telemetry(&semanticMotion);
  Serial.printf(
      "MOTION shared=%d reactionMs=%lu settleGazeMs=%ld settleLidsMs=%ld settleBrowsMs=%ld settlePupilMs=%ld blinkLeft=%.3f blinkRight=%.3f gazeVelocity=%.3f gazeVelocityX=%.3f gazeVelocityY=%.3f breathing=%.3f attentionDecay=%.3f\n",
      commandState.semanticAffectActive ? 1 : 0,
      (unsigned long)semanticMotion.reaction_ms,
      (long)settledMs(motion->transition_ms[EMOTE_CHANNEL_GAZE]),
      (long)settledMs(motion->transition_ms[EMOTE_CHANNEL_LIDS]),
      (long)settledMs(motion->transition_ms[EMOTE_CHANNEL_BROWS]),
      (long)settledMs(motion->transition_ms[EMOTE_CHANNEL_PUPIL]),
      motion->blink_left,
      motion->blink_right,
      motion->gaze_velocity,
      motion->gaze_velocity_x,
      motion->gaze_velocity_y,
      motion->breathing,
      motion->attention_decay);
}

static void processCommand(char *line)
{
  char *token = strtok(line, " \t");
  if (!token)
  {
    return;
  }

  if (tokenEquals(token, "EMOTE") || tokenEquals(token, "PRESET") ||
      tokenEquals(token, "MOUTH") || tokenEquals(token, "TEXT") ||
      tokenEquals(token, "SCROLL") || tokenEquals(token, "QUESTION") ||
      tokenEquals(token, "ACK"))
  {
    cancelCharacterBeat();
  }

  if (tokenEquals(token, "HELP"))
  {
    printHelp();
    return;
  }

  if (tokenEquals(token, "STATUS"))
  {
    printStatus();
    printMotionStatus();
    return;
  }

  if (tokenEquals(token, "MOTION"))
  {
    printMotionStatus();
    return;
  }

  if (tokenEquals(token, "BLINK"))
  {
    requestBlink();
    Serial.println("OK BLINK");
    return;
  }

  if (tokenEquals(token, "BEAT"))
  {
    CharacterBeatKind kind = BEAT_NONE;
    if (!characterBeatFromName(strtok(nullptr, " \t"), kind))
    {
      Serial.println("ERR BEAT expects ATTENTION, THINKING, SUCCESS, ACKNOWLEDGE, REASSURE, ERROR, or PLAYFUL");
      return;
    }
    if (!startCharacterBeat(kind))
    {
      Serial.println("ERR BEAT built-in OLED unavailable");
      return;
    }
    Serial.printf("OK BEAT %s\n", characterBeatLabel(kind));
    return;
  }

  if (tokenEquals(token, "CENTER"))
  {
    setLookTarget(0.0f, 0.0f, true);
    Serial.println("OK CENTER");
    return;
  }

  if (tokenEquals(token, "AUTONOMY"))
  {
    char *value = strtok(nullptr, " \t");
    bool enabled = false;
    if (parseOnOffValue(value, enabled))
    {
      setAutonomyEnabled(enabled);
      Serial.printf("OK AUTONOMY %s\n", enabled ? "ON" : "OFF");
      return;
    }

    Serial.println("ERR AUTONOMY expects ON or OFF");
    return;
  }

  if (tokenEquals(token, "MODE"))
  {
    char *value = strtok(nullptr, " \t");
    if (!applyBehaviorModeByName(value))
    {
      Serial.println("ERR MODE expects IDLE, ATTENTIVE, SPEAKING, TRACKING, or SLEEPY");
      return;
    }

    if (commandState.semanticAffectActive)
    {
      submitSemanticTarget();
    }
    else if (commandState.autonomyEnabled)
    {
      scheduleNextBlink(millis());
      scheduleNextSaccade(millis());
    }
    Serial.printf("OK MODE %s\n", behaviorModeLabel(commandState.mode));
    return;
  }

  if (tokenEquals(token, "RENDERER"))
  {
    char *value = strtok(nullptr, " \t");
    if (!applyRendererByName(value))
    {
      Serial.println("ERR RENDERER expects SDF or LEGACY");
      return;
    }
    Serial.printf("OK RENDERER %s\n", rendererLabel());
    return;
  }

  if (tokenEquals(token, "PIPELINE"))
  {
    bool enabled = false;
    if (!parseOnOffValue(strtok(nullptr, " \t"), enabled))
    {
      Serial.println("ERR PIPELINE expects ON or OFF");
      return;
    }
    parallelRendererEnabled = enabled;
    Serial.printf(
        "OK PIPELINE %s ready=%d\n",
        parallelRendererEnabled ? "ON" : "OFF",
        parallelRendererReady ? 1 : 0);
    return;
  }

  if (tokenEquals(token, "DISPLAY"))
  {
    char *value = strtok(nullptr, " \t");
    if (tokenEquals(value, "LIVE"))
    {
      displayDiagnosticMode = DISPLAY_LIVE;
      leftEye->fillScreen(EYE_BACKGROUND_565);
      rightEye->fillScreen(EYE_BACKGROUND_565);
      renderFrame();
    }
    else if (tokenEquals(value, "TEST") || tokenEquals(value, "BOTH"))
    {
      displayDiagnosticMode = DISPLAY_TEST_BOTH;
      renderDisplayDiagnostic();
    }
    else if (tokenEquals(value, "LEFT"))
    {
      displayDiagnosticMode = DISPLAY_TEST_LEFT;
      renderDisplayDiagnostic();
    }
    else if (tokenEquals(value, "RIGHT"))
    {
      displayDiagnosticMode = DISPLAY_TEST_RIGHT;
      renderDisplayDiagnostic();
    }
    else
    {
      Serial.println("ERR DISPLAY expects LIVE, TEST, LEFT, or RIGHT");
      return;
    }

    Serial.printf("OK DISPLAY %s\n", displayDiagnosticLabel());
    return;
  }

  if (tokenEquals(token, "ORIENT"))
  {
    uint8_t nextLeftRotation = 0;
    uint8_t nextRightRotation = 0;
    if (!parseRotationToken(strtok(nullptr, " \t"), nextLeftRotation) ||
        !parseRotationToken(strtok(nullptr, " \t"), nextRightRotation))
    {
      Serial.println("ERR ORIENT expects two integers in range 0..3");
      return;
    }

    leftRotation = nextLeftRotation;
    rightRotation = nextRightRotation;
    leftEye->setRotation(leftRotation);
    rightEye->setRotation(rightRotation);
    if (displayDiagnosticMode == DISPLAY_LIVE)
    {
      renderFrame();
    }
    else
    {
      renderDisplayDiagnostic();
    }
    Serial.printf("OK ORIENT %u %u\n", leftRotation, rightRotation);
    return;
  }

  if (tokenEquals(token, "LOOK"))
  {
    float x = 0.0f;
    float y = 0.0f;
    if (parseFloatToken(strtok(nullptr, " \t"), x) && parseFloatToken(strtok(nullptr, " \t"), y))
    {
      setLookTarget(x, y, false);
      Serial.printf("OK LOOK %.2f %.2f\n", commandState.targetLookX, commandState.targetLookY);
      return;
    }

    Serial.println("ERR LOOK expects two floats");
    return;
  }

  if (tokenEquals(token, "PUPIL"))
  {
    float pupil = 0.0f;
    if (parseFloatToken(strtok(nullptr, " \t"), pupil))
    {
      activateManualControls();
      commandState.targetPupil = clampf(pupil, 0.12f, 0.86f);
      Serial.printf("OK PUPIL %.2f\n", commandState.targetPupil);
      return;
    }

    Serial.println("ERR PUPIL expects one float");
    return;
  }

  if (tokenEquals(token, "OPEN"))
  {
    float openness = 0.0f;
    if (parseFloatToken(strtok(nullptr, " \t"), openness))
    {
      activateManualControls();
      commandState.targetOpenness = clampf(openness, 0.02f, 1.30f);
      Serial.printf("OK OPEN %.2f\n", commandState.targetOpenness);
      return;
    }

    Serial.println("ERR OPEN expects one float");
    return;
  }

  if (tokenEquals(token, "FOCUS"))
  {
    float focus = 0.0f;
    if (parseFloatToken(strtok(nullptr, " \t"), focus))
    {
      activateManualControls();
      commandState.targetFocus = clampf(focus, 0.0f, 0.60f);
      Serial.printf("OK FOCUS %.2f\n", commandState.targetFocus);
      return;
    }

    Serial.println("ERR FOCUS expects one float");
    return;
  }

  if (tokenEquals(token, "IRIS"))
  {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (parseFloatToken(strtok(nullptr, " \t"), r) &&
        parseFloatToken(strtok(nullptr, " \t"), g) &&
        parseFloatToken(strtok(nullptr, " \t"), b))
    {
      applyIrisBaseColor(makeColor(
          (uint8_t)clampf(r, 0.0f, 255.0f),
          (uint8_t)clampf(g, 0.0f, 255.0f),
          (uint8_t)clampf(b, 0.0f, 255.0f)));
      Serial.printf("OK IRIS %u %u %u\n", commandState.irisInner.r, commandState.irisInner.g, commandState.irisInner.b);
      return;
    }

    Serial.println("ERR IRIS expects three 0..255 values");
    return;
  }

  if (tokenEquals(token, "PRESET"))
  {
    char *value = strtok(nullptr, " \t");
    if (!applyPresetByName(value))
    {
      Serial.println("ERR PRESET expects NATURAL, ALERT, SLEEPY, CURIOUS, or FOCUSED");
      return;
    }

    Serial.printf("OK PRESET %s\n", presetLabel(commandState.preset));
    return;
  }

  if (tokenEquals(token, "EMOTE"))
  {
    char *value = strtok(nullptr, " \t");
    char *intensityToken = strtok(nullptr, " \t");
    float intensity = -1.0f;
    if (intensityToken && (!parseFloatToken(intensityToken, intensity) || intensity < 0.0f || intensity > 1.0f))
    {
      Serial.println("ERR EMOTE intensity must be in range 0.0..1.0");
      return;
    }
    if (!applyAffectByName(value, intensity))
    {
      Serial.println("ERR EMOTE expects a canonical affect name");
      return;
    }
    Serial.printf("OK EMOTE %s %.2f\n", affectLabel(), semanticTarget.intensity);
    return;
  }

  if (tokenEquals(token, "MOUTH"))
  {
    char *action = strtok(nullptr, " \t");
    if (tokenEquals(action, "STATUS"))
    {
      Serial.printf(
          "MOUTH ready=%d mode=%s text=%s\n",
          mouthDisplay.ready() ? 1 : 0,
          mouthModeLabel(),
          mouthDisplay.text());
      return;
    }
    if (!mouthDisplay.ready())
    {
      Serial.println("ERR MOUTH built-in OLED unavailable");
      return;
    }
    if (tokenEquals(action, "AUTO"))
    {
      mouthDisplayMode = MOUTH_AUTO;
      refreshMouthDisplay();
      Serial.println("OK MOUTH auto");
      return;
    }
    if (tokenEquals(action, "BLANK"))
    {
      mouthDisplayMode = MOUTH_BLANK;
      refreshMouthDisplay();
      Serial.println("OK MOUTH blank");
      return;
    }
    Serial.println("ERR MOUTH expects AUTO, BLANK, or STATUS");
    return;
  }

  if (tokenEquals(token, "TEXT"))
  {
    char *value = strtok(nullptr, "\r\n");
    while (value && (*value == ' ' || *value == '\t'))
    {
      ++value;
    }
    if (!value || value[0] == '\0')
    {
      Serial.println("ERR TEXT expects a message, CLEAR, or BLANK");
      return;
    }
    if (!mouthDisplay.ready())
    {
      Serial.println("ERR TEXT built-in OLED unavailable");
      return;
    }
    if (tokenEquals(value, "CLEAR"))
    {
      mouthDisplayMode = MOUTH_AUTO;
      customMouthText[0] = '\0';
      refreshMouthDisplay();
      Serial.println("OK TEXT auto");
      return;
    }
    if (tokenEquals(value, "BLANK"))
    {
      mouthDisplayMode = MOUTH_BLANK;
      customMouthText[0] = '\0';
      refreshMouthDisplay();
      Serial.println("OK TEXT blank");
      return;
    }
    snprintf(customMouthText, sizeof(customMouthText), "%s", value);
    mouthDisplayMode = MOUTH_CUSTOM;
    refreshMouthDisplay();
    Serial.printf("OK TEXT %s\n", customMouthText);
    return;
  }

  if (tokenEquals(token, "SCROLL"))
  {
    char *value = strtok(nullptr, "\r\n");
    while (value && (*value == ' ' || *value == '\t'))
    {
      ++value;
    }
    if (!value || value[0] == '\0')
    {
      Serial.println("ERR SCROLL expects a message");
      return;
    }
    if (!mouthDisplay.ready())
    {
      Serial.println("ERR SCROLL built-in OLED unavailable");
      return;
    }
    snprintf(customMouthText, sizeof(customMouthText), "%s", value);
    mouthDisplayMode = MOUTH_SCROLL;
    refreshMouthDisplay();
    Serial.printf("OK SCROLL %s\n", customMouthText);
    return;
  }

  if (tokenEquals(token, "QUESTION"))
  {
    char *value = strtok(nullptr, "\r\n");
    while (value && (*value == ' ' || *value == '\t'))
    {
      ++value;
    }
    if (!value || value[0] == '\0')
    {
      Serial.println("ERR QUESTION expects a message");
      return;
    }
    if (!mouthDisplay.ready())
    {
      Serial.println("ERR QUESTION built-in OLED unavailable");
      return;
    }
    snprintf(customMouthText, sizeof(customMouthText), "? %.61s", value);
    mouthDisplayMode = strlen(customMouthText) > 19 ? MOUTH_SCROLL : MOUTH_CUSTOM;
    refreshMouthDisplay();
    applyAffect(EMOTE_LISTENING);
    Serial.printf("OK QUESTION %s\n", customMouthText);
    return;
  }

  if (tokenEquals(token, "ACK"))
  {
    char *value = strtok(nullptr, "\r\n");
    while (value && (*value == ' ' || *value == '\t'))
    {
      ++value;
    }
    if (!mouthDisplay.ready())
    {
      Serial.println("ERR ACK built-in OLED unavailable");
      return;
    }
    snprintf(customMouthText, sizeof(customMouthText), "%s", value && value[0] ? value : "OK");
    mouthDisplayMode = MOUTH_CUSTOM;
    refreshMouthDisplay();
    applyAffect(EMOTE_SUCCESS);
    Serial.printf("OK ACK %s\n", customMouthText);
    return;
  }

  Serial.println("ERR Unknown command");
}

static void processSerialInput()
{
  while (Serial.available() > 0)
  {
    const char incoming = (char)Serial.read();
    if (incoming == '\r')
    {
      continue;
    }

    if (incoming == '\n')
    {
      serialBuffer[serialBufferLen] = '\0';
      processCommand(serialBuffer);
      serialBufferLen = 0;
      continue;
    }

    if (serialBufferLen < (sizeof(serialBuffer) - 1))
    {
      serialBuffer[serialBufferLen++] = incoming;
    }
  }
}

static void updateAutonomy(unsigned long now)
{
  if (!commandState.autonomyEnabled)
  {
    runtimeState.autoLookTargetX = 0.0f;
    runtimeState.autoLookTargetY = 0.0f;
    return;
  }

  if (now >= runtimeState.nextSaccadeAtMs)
  {
    int largerMoveChance = 28;
    int smallMoveDampenChance = 24;
    int blinkOnLargeMoveChance = 35;
    float xSpanLarge = 0.58f;
    float xSpanSmall = 0.22f;
    float ySpanLarge = 0.30f;
    float ySpanSmall = 0.12f;
    float centerBiasScaleX = 0.09f;
    float centerBiasScaleY = 0.06f;
    float clampX = 0.62f;
    float clampY = 0.34f;

    switch (commandState.mode)
    {
    case MODE_IDLE:
      largerMoveChance = 18;
      smallMoveDampenChance = 40;
      blinkOnLargeMoveChance = 18;
      xSpanLarge = 0.44f;
      xSpanSmall = 0.16f;
      ySpanLarge = 0.22f;
      ySpanSmall = 0.08f;
      centerBiasScaleX = 0.07f;
      centerBiasScaleY = 0.05f;
      clampX = 0.48f;
      clampY = 0.24f;
      break;
    case MODE_SPEAKING:
      largerMoveChance = 10;
      smallMoveDampenChance = 34;
      blinkOnLargeMoveChance = 10;
      xSpanLarge = 0.16f;
      xSpanSmall = 0.07f;
      ySpanLarge = 0.08f;
      ySpanSmall = 0.03f;
      centerBiasScaleX = 0.03f;
      centerBiasScaleY = 0.02f;
      clampX = 0.18f;
      clampY = 0.10f;
      break;
    case MODE_TRACKING:
      largerMoveChance = 12;
      smallMoveDampenChance = 8;
      blinkOnLargeMoveChance = 12;
      xSpanLarge = 0.26f;
      xSpanSmall = 0.10f;
      ySpanLarge = 0.18f;
      ySpanSmall = 0.07f;
      centerBiasScaleX = 0.04f;
      centerBiasScaleY = 0.03f;
      clampX = 0.28f;
      clampY = 0.18f;
      break;
    case MODE_SLEEPY:
      largerMoveChance = 10;
      smallMoveDampenChance = 48;
      blinkOnLargeMoveChance = 20;
      xSpanLarge = 0.20f;
      xSpanSmall = 0.08f;
      ySpanLarge = 0.10f;
      ySpanSmall = 0.04f;
      centerBiasScaleX = 0.05f;
      centerBiasScaleY = 0.03f;
      clampX = 0.20f;
      clampY = 0.12f;
      break;
    case MODE_ATTENTIVE:
    default:
      break;
    }

    const bool largerMove = random(0, 100) < largerMoveChance;
    const float xSpan = largerMove ? xSpanLarge : xSpanSmall;
    const float ySpan = largerMove ? ySpanLarge : ySpanSmall;
    const float centerBiasX = ((float)random(-100, 101) / 100.0f) * centerBiasScaleX;
    const float centerBiasY = ((float)random(-100, 101) / 100.0f) * centerBiasScaleY;

    runtimeState.autoLookTargetX = centerBiasX + (((float)random(-100, 101) / 100.0f) * xSpan);
    runtimeState.autoLookTargetY = centerBiasY + (((float)random(-100, 101) / 100.0f) * ySpan);
    runtimeState.autoLookTargetX = clampf(runtimeState.autoLookTargetX, -clampX, clampX);
    runtimeState.autoLookTargetY = clampf(runtimeState.autoLookTargetY, -clampY, clampY);
    if (!largerMove && random(0, 100) < smallMoveDampenChance)
    {
      runtimeState.autoLookTargetX *= 0.55f;
      runtimeState.autoLookTargetY *= 0.55f;
    }
    if (largerMove && random(0, 100) < blinkOnLargeMoveChance)
    {
      runtimeState.blinkRequested = true;
    }
    scheduleNextSaccade(now);
  }

  if (now >= runtimeState.nextBlinkAtMs)
  {
    runtimeState.blinkRequested = true;
    scheduleNextBlink(now);
  }
}

static void updateRuntime()
{
  const unsigned long now = millis();
  if (semanticMotionReady && commandState.semanticAffectActive)
  {
    const uint32_t elapsedMs = lastMotionUpdateMs == 0 ? FRAME_INTERVAL_MS : now - lastMotionUpdateMs;
    lastMotionUpdateMs = now;
    emote_motion_step(&semanticMotion, now, (float)elapsedMs / 1000.0f);

    const emote_pose_t &pose = semanticMotion.current;
    const emote_motion_telemetry_t *metrics = emote_motion_telemetry(&semanticMotion);
    runtimeState.lookX = pose.gaze_x;
    runtimeState.lookY = pose.gaze_y;
    runtimeState.pupil = pose.pupil;
    runtimeState.openness = pose.open;
    runtimeState.blinkAmount = metrics ? metrics->blink_left : 0.0f;
    runtimeState.blinkActive = semanticMotion.blink_active;
    commandState.browLift = pose.brow_y;
    commandState.browRotation = pose.brow_rotation;
    commandState.lowerLidLift = pose.lower_lid;
    commandState.asymmetry = pose.asymmetry;
    commandState.arc = pose.arc;
    commandState.intensity = pose.intensity;
    commandState.pupilShape = pose.pupil_shape;
    commandState.palette = pose.palette;
    return;
  }

  updateAutonomy(now);

  if (runtimeState.doubleBlinkPending && !runtimeState.blinkActive && now >= runtimeState.doubleBlinkAtMs)
  {
    runtimeState.doubleBlinkPending = false;
    triggerBlink(now);
  }

  if (runtimeState.blinkRequested && !runtimeState.blinkActive)
  {
    triggerBlink(now);
  }

  if (runtimeState.blinkActive)
  {
    const float progress = (float)(now - runtimeState.blinkStartedAtMs) / (float)runtimeState.blinkDurationMs;
    if (progress >= 1.0f)
    {
      runtimeState.blinkActive = false;
      runtimeState.blinkAmount = 0.0f;
    }
    else if (progress < 0.40f)
    {
      runtimeState.blinkAmount = smoothstepf(progress / 0.40f);
    }
    else if (progress < 0.52f)
    {
      runtimeState.blinkAmount = 1.0f;
    }
    else
    {
      runtimeState.blinkAmount = smoothstepf((1.0f - progress) / 0.48f);
    }
  }
  else
  {
    runtimeState.blinkAmount = 0.0f;
  }

  runtimeState.autoLookX += (runtimeState.autoLookTargetX - runtimeState.autoLookX) * 0.11f;
  runtimeState.autoLookY += (runtimeState.autoLookTargetY - runtimeState.autoLookY) * 0.11f;

  const float pupilWave =
      commandState.autonomyEnabled
          ? ((sinf((float)now * 0.0016f) * 0.03f) + (sinf((float)now * 0.00031f) * 0.015f))
          : 0.0f;
  float driftScaleX = 1.0f;
  float driftScaleY = 1.0f;
  float opennessWaveScale = 1.0f;
  float lookResponseBoost = 0.0f;

  switch (commandState.mode)
  {
  case MODE_IDLE:
    driftScaleX = 0.85f;
    driftScaleY = 0.75f;
    opennessWaveScale = 0.65f;
    break;
  case MODE_SPEAKING:
    driftScaleX = 0.35f;
    driftScaleY = 0.22f;
    opennessWaveScale = 0.24f;
    lookResponseBoost = 0.04f;
    break;
  case MODE_TRACKING:
    driftScaleX = 0.30f;
    driftScaleY = 0.25f;
    opennessWaveScale = 0.20f;
    lookResponseBoost = 0.12f;
    break;
  case MODE_SLEEPY:
    driftScaleX = 0.40f;
    driftScaleY = 0.28f;
    opennessWaveScale = 1.35f;
    break;
  case MODE_ATTENTIVE:
  default:
    driftScaleX = 1.0f;
    driftScaleY = 1.0f;
    opennessWaveScale = 1.0f;
    break;
  }

  const float idleDriftX = commandState.autonomyEnabled
                               ? (((sinf((float)now * 0.00042f) * 0.045f) + (sinf((float)now * 0.00011f + 1.2f) * 0.085f)) * driftScaleX)
                               : 0.0f;
  const float idleDriftY = commandState.autonomyEnabled
                               ? (((sinf((float)now * 0.00037f + 0.8f) * 0.030f) + (sinf((float)now * 0.00015f + 2.7f) * 0.020f)) * driftScaleY)
                               : 0.0f;
  const float opennessWave = commandState.autonomyEnabled
                                 ? (((sinf((float)now * 0.00052f + 0.4f) * 0.018f) + (sinf((float)now * 0.00019f + 1.7f) * 0.010f)) * opennessWaveScale)
                                 : 0.0f;

  const float lookTargetX = clampf(commandState.targetLookX + runtimeState.autoLookX + idleDriftX, -1.0f, 1.0f);
  const float lookTargetY = clampf(commandState.targetLookY + runtimeState.autoLookY + idleDriftY, -1.0f, 1.0f);
  const float pupilTarget = clampf(commandState.targetPupil + pupilWave, 0.12f, 0.86f);
  const float opennessTarget = clampf(commandState.targetOpenness - opennessWave, 0.02f, 1.30f);
  const float lookLerp = 0.10f + lookResponseBoost + (clampf(absf(lookTargetX - runtimeState.lookX) + absf(lookTargetY - runtimeState.lookY), 0.0f, 1.0f) * 0.16f);

  runtimeState.lookX += (lookTargetX - runtimeState.lookX) * lookLerp;
  runtimeState.lookY += (lookTargetY - runtimeState.lookY) * lookLerp;
  runtimeState.pupil += (pupilTarget - runtimeState.pupil) * 0.10f;
  runtimeState.openness += (opennessTarget - runtimeState.openness) * 0.12f;
  runtimeState.focus += (commandState.targetFocus - runtimeState.focus) * 0.10f;
}

static void renderLegacyEye(uint16_t *frame, bool isLeftEye)
{
  const float centerX = (SCREEN_W - 1) * 0.5f;
  const float centerY = (SCREEN_H - 1) * 0.5f;
  const float eyeballRadius = 74.0f;
  const float eyeballRadiusSq = eyeballRadius * eyeballRadius;
  const float lateralEyeBias = isLeftEye ? -0.9f : 0.9f;
  const float verticalEyeBias = isLeftEye ? 0.45f : -0.25f;

  const float vergence = runtimeState.focus * 8.0f;
  const float irisCenterX = centerX + lateralEyeBias + (runtimeState.lookX * 22.0f) + (isLeftEye ? vergence : -vergence);
  const float irisCenterY = centerY + verticalEyeBias + (runtimeState.lookY * 18.0f);
  const float irisRadius = 27.0f;
  const float irisRadiusSq = irisRadius * irisRadius;
  const float pupilRadius = 7.5f + (runtimeState.pupil * 18.0f);
  const float pupilRadiusSq = pupilRadius * pupilRadius;
  const float squint = clampf((absf(runtimeState.lookX) * 0.05f) + (runtimeState.focus * 0.07f), 0.0f, 0.11f);
  const float effectiveOpen = clampf(runtimeState.openness - squint - (runtimeState.blinkAmount * 0.97f), 0.03f, 1.0f);

  const RgbColor bgColor = makeColor(0, 0, 0);
  const RgbColor scleraCenter = makeColor(245, 244, 240);
  const RgbColor scleraEdge = makeColor(196, 202, 214);
  const RgbColor scleraCool = makeColor(218, 227, 236);
  const RgbColor lidFill = makeColor(34, 25, 28);
  const RgbColor lidCrease = makeColor(90, 60, 70);
  const RgbColor lashColor = makeColor(8, 6, 8);
  const RgbColor pupilColor = makeColor(5, 7, 10);
  const RgbColor highlightPrimary = makeColor(255, 255, 252);
  const RgbColor highlightSecondary = makeColor(178, 212, 240);
  const RgbColor lidShadow = makeColor(18, 12, 15);
  const RgbColor limbalShadow = makeColor(9, 12, 20);
  const RgbColor irisOcclusion = makeColor(18, 24, 34);
  const RgbColor caruncleColor = makeColor(160, 92, 102);
  const RgbColor lowerWetline = makeColor(214, 170, 176);

  int16_t upperEdge[SCREEN_W];
  int16_t lowerEdge[SCREEN_W];

  const float upperBase = centerY - (effectiveOpen * 48.0f) - (commandState.browLift * 8.0f) + (runtimeState.lookY * 2.8f);
  const float lowerBase = centerY + (effectiveOpen * 43.0f) + (commandState.lowerLidLift * 7.0f) + (runtimeState.lookY * 5.8f);
  const float upperCurveSize = 22.0f - (commandState.browLift * 6.0f);
  const float lowerCurveSize = 11.0f + (commandState.lowerLidLift * 7.0f);

  for (int x = 0; x < SCREEN_W; ++x)
  {
    const float nx = ((float)x - centerX) / eyeballRadius;
    const float curve = nx * nx;
    const float cornerTighten = absf(nx) * 4.0f;
    const float cornerLift = curve * 2.2f;
    upperEdge[x] = (int16_t)(upperBase + (curve * upperCurveSize) + cornerLift - cornerTighten);
    lowerEdge[x] = (int16_t)(lowerBase - (curve * lowerCurveSize) - (curve * 1.2f));
    if (lowerEdge[x] < upperEdge[x] + 2)
    {
      lowerEdge[x] = upperEdge[x] + 2;
    }
  }

  int index = 0;
  for (int y = 0; y < SCREEN_H; ++y)
  {
    for (int x = 0; x < SCREEN_W; ++x, ++index)
    {
      const float dx = (float)x - centerX;
      const float dy = (float)y - centerY;
      const float globeRatio = ((dx * dx) + (dy * dy)) / eyeballRadiusSq;

      if (globeRatio > 1.0f)
      {
        frame[index] = toBigEndian565(bgColor);
        continue;
      }

      if (y < upperEdge[x] || y > lowerEdge[x])
      {
        const float lidDepth = clampf((float)(abs((int)(y - (int)centerY))) / centerY, 0.0f, 1.0f);
        RgbColor lidPixel = mixColor(lidFill, lidShadow, lidDepth * 0.35f);
        const float creaseMix = clampf((float)(y - (upperEdge[x] - 7)) / 4.0f, 0.0f, 1.0f) * clampf((float)(upperEdge[x] - y + 3) / 6.0f, 0.0f, 1.0f);
        if (creaseMix > 0.0f)
        {
          lidPixel = mixColor(lidPixel, lidCrease, creaseMix * 0.28f);
        }
        frame[index] = toBigEndian565(lidPixel);
        continue;
      }

      RgbColor pixel = mixColor(scleraCenter, scleraEdge, clampf(globeRatio * 0.78f, 0.0f, 1.0f));
      const float scleraShade = clampf(((float)y - centerY + 18.0f) / 100.0f, 0.0f, 1.0f);
      pixel = mixColor(pixel, makeColor(228, 232, 236), scleraShade * 0.18f);
      const float scleraRim = clampf((globeRatio - 0.74f) / 0.26f, 0.0f, 1.0f);
      pixel = mixColor(pixel, scleraCool, scleraRim * 0.24f);

      const float innerCornerDx = ((float)x - (isLeftEye ? 26.0f : (SCREEN_W - 26.0f))) / 14.0f;
      const float innerCornerDy = ((float)y - (centerY + 1.0f)) / 16.0f;
      const float innerCornerMix = clampf(1.0f - ((innerCornerDx * innerCornerDx) + (innerCornerDy * innerCornerDy)), 0.0f, 1.0f);
      if (innerCornerMix > 0.0f)
      {
        pixel = mixColor(pixel, makeColor(236, 224, 224), innerCornerMix * 0.12f);
      }

      const float lidShadowMix = clampf((float)(upperEdge[x] + 9 - y) / 14.0f, 0.0f, 1.0f);
      if (lidShadowMix > 0.0f)
      {
        pixel = mixColor(pixel, lidShadow, lidShadowMix * 0.35f);
      }

      const float irisDx = (float)x - irisCenterX;
      const float irisDy = (float)y - irisCenterY;
      const float irisRatio = ((irisDx * irisDx) + (irisDy * irisDy)) / irisRadiusSq;

      if (irisRatio <= 1.0f)
      {
        pixel = mixColor(commandState.irisOuter, commandState.irisInner, clampf(1.0f - irisRatio, 0.0f, 1.0f));
        const float spokeSeed = hashUnit((int)(irisDx * 5.0f), (int)(irisDy * 5.0f), isLeftEye ? 41 : 53);
        const float spokeWave = 0.45f + (spokeSeed * 0.55f);
        const float spokeMix = spokeWave * clampf(1.0f - irisRatio, 0.0f, 1.0f) * 0.18f;
        pixel = mixColor(pixel, commandState.irisHighlight, spokeMix);
        const float collaretteMix = clampf(1.0f - (absf(irisRatio - 0.36f) / 0.22f), 0.0f, 1.0f);
        if (collaretteMix > 0.0f)
        {
          pixel = mixColor(pixel, commandState.irisRing, collaretteMix * 0.16f);
        }

        const float ringMix = clampf((irisRatio - 0.70f) / 0.30f, 0.0f, 1.0f);
        if (ringMix > 0.0f)
        {
          pixel = mixColor(pixel, commandState.irisRing, ringMix * 0.9f);
        }

        const float fleckSeed = hashUnit(x, y, isLeftEye ? 17 : 29);
        const float fleckMix = fleckSeed * clampf(1.0f - irisRatio, 0.0f, 1.0f) * 0.24f;
        if (fleckMix > 0.0f)
        {
          pixel = mixColor(pixel, commandState.irisHighlight, fleckMix);
        }

        const float irisGlowDx = (irisDx + 4.0f);
        const float irisGlowDy = (irisDy + 7.0f);
        const float irisGlowMix = clampf(1.0f - (((irisGlowDx * irisGlowDx) + (irisGlowDy * irisGlowDy)) / 150.0f), 0.0f, 1.0f);
        if (irisGlowMix > 0.0f)
        {
          pixel = mixColor(pixel, commandState.irisHighlight, irisGlowMix * 0.12f);
        }

        const float pupilRatio = ((irisDx * irisDx) + (irisDy * irisDy)) / pupilRadiusSq;
        if (pupilRatio <= 1.0f)
        {
          pixel = mixColor(pupilColor, makeColor(0, 0, 0), clampf(1.0f - pupilRatio, 0.0f, 1.0f) * 0.65f);
        }

        const float pupilEdgeMix = clampf((pupilRatio - 0.82f) / 0.18f, 0.0f, 1.0f);
        if (pupilEdgeMix > 0.0f)
        {
          pixel = mixColor(pixel, limbalShadow, pupilEdgeMix * 0.42f);
        }

        const float limbalMix = clampf((irisRatio - 0.82f) / 0.18f, 0.0f, 1.0f);
        if (limbalMix > 0.0f)
        {
          pixel = mixColor(pixel, limbalShadow, limbalMix * 0.52f);
        }

        const float irisOcclusionMix = clampf((float)(upperEdge[x] + 18 - y) / 19.0f, 0.0f, 1.0f);
        if (irisOcclusionMix > 0.0f)
        {
          pixel = mixColor(pixel, irisOcclusion, irisOcclusionMix * 0.18f);
        }
      }

      const float highlightBaseX = centerX - 18.0f + (runtimeState.lookX * 2.4f);
      const float highlightBaseY = centerY - 18.0f + (runtimeState.lookY * 1.2f);
      const float primaryDx = (float)x - highlightBaseX;
      const float primaryDy = (float)y - highlightBaseY;
      const float primaryRatio = ((primaryDx * primaryDx) + (primaryDy * primaryDy)) / 20.0f;
      if (primaryRatio < 1.0f)
      {
        pixel = mixColor(pixel, highlightPrimary, clampf(1.0f - primaryRatio, 0.0f, 1.0f));
      }

      const float secondaryDx = (float)x - (highlightBaseX + 15.0f);
      const float secondaryDy = (float)y - (highlightBaseY + 12.0f);
      const float secondaryRatio = ((secondaryDx * secondaryDx) + (secondaryDy * secondaryDy)) / 10.0f;
      if (secondaryRatio < 1.0f)
      {
        pixel = mixColor(pixel, highlightSecondary, clampf(1.0f - secondaryRatio, 0.0f, 1.0f) * 0.7f);
      }

      const float ambientDx = (float)x - (highlightBaseX + 5.0f);
      const float ambientDy = (float)y - (highlightBaseY + 4.0f);
      const float ambientRatio = ((ambientDx * ambientDx) + (ambientDy * ambientDy)) / 58.0f;
      if (ambientRatio < 1.0f)
      {
        pixel = mixColor(pixel, highlightPrimary, clampf(1.0f - ambientRatio, 0.0f, 1.0f) * 0.14f);
      }

      const float upperLashMix = clampf((float)(upperEdge[x] + 2 - y) / 2.0f, 0.0f, 1.0f);
      if (upperLashMix > 0.0f)
      {
        pixel = mixColor(pixel, lashColor, upperLashMix * 0.85f);
      }

      const float lowerShadowMix = clampf((float)(y - (lowerEdge[x] - 2)) / 3.0f, 0.0f, 1.0f);
      if (lowerShadowMix > 0.0f)
      {
        pixel = mixColor(pixel, lidShadow, lowerShadowMix * 0.25f);
      }

      const float wetlineMix = clampf((float)(y - (lowerEdge[x] - 1)) / 1.6f, 0.0f, 1.0f);
      if (wetlineMix > 0.0f)
      {
        pixel = mixColor(pixel, lowerWetline, wetlineMix * 0.42f);
      }

      const float caruncleCenterX = isLeftEye ? 24.0f : (SCREEN_W - 24.0f);
      const float caruncleDx = ((float)x - caruncleCenterX) / 8.0f;
      const float caruncleDy = ((float)y - (centerY + 2.0f)) / 11.0f;
      const float caruncleRatio = (caruncleDx * caruncleDx) + (caruncleDy * caruncleDy);
      if (caruncleRatio < 1.0f)
      {
        pixel = mixColor(pixel, caruncleColor, clampf(1.0f - caruncleRatio, 0.0f, 1.0f) * 0.34f);
      }

      frame[index] = toBigEndian565(pixel);
    }
  }
}

static void renderFrame()
{
  if (displayDiagnosticMode != DISPLAY_LIVE)
  {
    return;
  }

  const uint32_t startedAtUs = micros();
  uint32_t computeUs = 0;
  uint32_t transferUs = 0;
  if (useSdfRenderer)
  {
    emote_pose_t leftPose;
    emote_pose_t rightPose;
    float gazeVelocityX = 0.0f;
    float gazeVelocityY = 0.0f;
    if (semanticMotionReady && commandState.semanticAffectActive)
    {
      const uint32_t now = millis();
      leftPose = emote_motion_render_pose(&semanticMotion, true, now);
      rightPose = emote_motion_render_pose(&semanticMotion, false, now);
      const emote_motion_telemetry_t *metrics = emote_motion_telemetry(&semanticMotion);
      gazeVelocityX = metrics ? metrics->gaze_velocity_x : 0.0f;
      gazeVelocityY = metrics ? metrics->gaze_velocity_y : 0.0f;
    }
    else
    {
      leftPose = emote_pose_for_affect(EMOTE_NEUTRAL);
      leftPose.open = clampf(runtimeState.openness - (runtimeState.blinkAmount * 0.97f), 0.02f, 1.30f);
      leftPose.lower_lid = commandState.lowerLidLift;
      leftPose.gaze_x = runtimeState.lookX;
      leftPose.gaze_y = runtimeState.lookY;
      leftPose.pupil = clampf(runtimeState.pupil, 0.12f, 0.86f);
      leftPose.brow_y = commandState.browLift;
      leftPose.brow_rotation = commandState.browRotation;
      leftPose.asymmetry = commandState.asymmetry;
      leftPose.arc = commandState.arc;
      leftPose.intensity = commandState.intensity;
      leftPose.pupil_shape = commandState.pupilShape;
      leftPose.palette = commandState.palette;
      rightPose = leftPose;

      const float deltaX = runtimeState.lookX - previousLookX;
      const float deltaY = runtimeState.lookY - previousLookY;
      gazeVelocityX = deltaX * (1000.0f / (float)FRAME_INTERVAL_MS);
      gazeVelocityY = deltaY * (1000.0f / (float)FRAME_INTERVAL_MS);
    }
    previousLookX = runtimeState.lookX;
    previousLookY = runtimeState.lookY;

    eye_render_metrics_t leftMetrics = {};
    eye_render_metrics_t rightMetrics = {};
    const bool useParallelRenderer = parallelRendererEnabled && parallelRendererReady;
    if (useParallelRenderer)
    {
      rightRenderJob.pose = rightPose;
      rightRenderJob.gazeVelocityX = gazeVelocityX;
      rightRenderJob.gazeVelocityY = gazeVelocityY;
      rightRenderJob.metrics = {};
      rightRenderJob.computeUs = 0;
      xSemaphoreGive(rightRenderRequest);
    }
    uint32_t phaseStartedAtUs = micros();
    eye_renderer_render_rgb565(
        leftFrameBuffer, SCREEN_W, SCREEN_H, &leftPose, true,
        gazeVelocityX, gazeVelocityY, &leftMetrics);
    computeUs += micros() - phaseStartedAtUs;
    phaseStartedAtUs = micros();
    leftEye->draw16bitRGBBitmap(0, 0, leftFrameBuffer, SCREEN_W, SDF_TRANSFER_H);
    transferUs += micros() - phaseStartedAtUs;
    if (useParallelRenderer)
    {
      xSemaphoreTake(rightRenderComplete, portMAX_DELAY);
      rightMetrics = rightRenderJob.metrics;
      computeUs += rightRenderJob.computeUs;
    }
    else
    {
      phaseStartedAtUs = micros();
      eye_renderer_render_rgb565(
          leftFrameBuffer, SCREEN_W, SCREEN_H, &rightPose, false,
          gazeVelocityX, gazeVelocityY, &rightMetrics);
      computeUs += micros() - phaseStartedAtUs;
    }
    phaseStartedAtUs = micros();
    rightEye->draw16bitRGBBitmap(
        0,
        0,
        useParallelRenderer ? rightFrameBuffer : leftFrameBuffer,
        SCREEN_W,
        SDF_TRANSFER_H);
    transferUs += micros() - phaseStartedAtUs;
    lastShadedPixels = leftMetrics.shaded_pixels + rightMetrics.shaded_pixels;
  }
  else
  {
    uint32_t phaseStartedAtUs = micros();
    renderLegacyEye(leftFrameBuffer, true);
    computeUs += micros() - phaseStartedAtUs;
    phaseStartedAtUs = micros();
    leftEye->draw16bitBeRGBBitmap(0, 0, leftFrameBuffer, SCREEN_W, SCREEN_H);
    transferUs += micros() - phaseStartedAtUs;
    phaseStartedAtUs = micros();
    renderLegacyEye(leftFrameBuffer, false);
    computeUs += micros() - phaseStartedAtUs;
    phaseStartedAtUs = micros();
    rightEye->draw16bitBeRGBBitmap(0, 0, leftFrameBuffer, SCREEN_W, SCREEN_H);
    transferUs += micros() - phaseStartedAtUs;
    lastShadedPixels = SCREEN_PIXELS * 2;
  }
  ++frameCounter;
  lastFrameUs = micros() - startedAtUs;
  lastComputeUs = computeUs;
  lastTransferUs = transferUs;
  if (lastFrameUs > maxFrameUs)
  {
    maxFrameUs = lastFrameUs;
  }
  if (lastFrameUs > FRAME_DEADLINE_US)
  {
    ++frameDeadlineMisses;
  }
}

static void reportFrameTelemetry(unsigned long now)
{
  if (telemetryWindowStartedAtMs == 0)
  {
    telemetryWindowStartedAtMs = now;
    telemetryWindowStartedAtFrame = frameCounter;
    return;
  }
  const unsigned long elapsed = now - telemetryWindowStartedAtMs;
  if (elapsed < 2000UL)
  {
    return;
  }
  const uint32_t rendered = frameCounter - telemetryWindowStartedAtFrame;
  lastMeasuredFps = elapsed > 0 ? ((float)rendered * 1000.0f) / (float)elapsed : 0.0f;
  Serial.printf(
      "FRAME renderer=%s fps=%.1f lastUs=%lu computeUs=%lu transferUs=%lu maxUs=%lu misses=%lu shaded=%lu/%lu\n",
      rendererLabel(),
      lastMeasuredFps,
      (unsigned long)lastFrameUs,
      (unsigned long)lastComputeUs,
      (unsigned long)lastTransferUs,
      (unsigned long)maxFrameUs,
      (unsigned long)frameDeadlineMisses,
      (unsigned long)lastShadedPixels,
      (unsigned long)(SCREEN_PIXELS * 2));
  telemetryWindowStartedAtMs = now;
  telemetryWindowStartedAtFrame = frameCounter;
}

void setup()
{
  Serial.begin(115200);
  delay(600);

  pinMode(TFT_BL1, OUTPUT);
  pinMode(TFT_BL2, OUTPUT);
  digitalWrite(TFT_BL1, HIGH);
  digitalWrite(TFT_BL2, HIGH);

  pinMode(TFT_CS1, OUTPUT);
  pinMode(TFT_CS2, OUTPUT);
  digitalWrite(TFT_CS1, HIGH);
  digitalWrite(TFT_CS2, HIGH);

  leftEye->begin(DISPLAY_SPI_HZ);
  rightEye->begin(DISPLAY_SPI_HZ);
  leftEye->fillScreen(EYE_BACKGROUND_565);
  rightEye->fillScreen(EYE_BACKGROUND_565);
  startParallelRenderer();
  const bool mouthReady = mouthDisplay.begin();

  randomSeed((uint32_t)micros());
  const uint32_t motionStartedAtMs = millis();
  emote_target_neutral(&semanticTarget);
  emote_motion_init(&semanticMotion, (uint32_t)micros(), motionStartedAtMs);
  semanticMotionReady = true;
  lastMotionUpdateMs = motionStartedAtMs;
  applyAffect(EMOTE_NEUTRAL);
  scheduleNextBlink(millis());
  scheduleNextSaccade(millis());
  renderFrame();

  Serial.println();
  Serial.println("=== Agent Eye Runtime Ready ===");
  Serial.printf("Renderer: %s (switch live with RENDERER SDF|LEGACY).\n", rendererLabel());
  Serial.printf("Heltec mouth OLED: %s.\n", mouthReady ? "ready" : "unavailable");
  Serial.println("Send HELP for commands.");
  printStatus();
}

void loop()
{
  processSerialInput();
  tickCharacterBeat(millis());
  updateRuntime();

  const unsigned long now = millis();
  if (now - runtimeState.lastFrameAtMs >= FRAME_INTERVAL_MS)
  {
    runtimeState.lastFrameAtMs = now;
    renderFrame();
  }
  reportFrameTelemetry(now);
}
