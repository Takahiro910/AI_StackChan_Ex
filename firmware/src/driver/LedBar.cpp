#if defined(USE_LED_BAR)

#include <Arduino.h>
#include <math.h>
#define FASTLED_INTERNAL   // バージョン表示のプラグマメッセージを抑制
// FastLED 独自の RMT 割り込みドライバは Wi-Fi/TLS の割り込み負荷下で show() が固まることがある。
// ESP-IDF 標準の RMT ドライバ（ブロッキング・タイムアウト付き）を使う。
#define FASTLED_RMT_BUILTIN_DRIVER 1
#include <FastLED.h>
#include "LedBar.h"

#ifndef LED_BAR_PIN
#define LED_BAR_PIN 25
#endif
#ifndef LED_BAR_NUM
#define LED_BAR_NUM 10
#endif
#ifndef LED_BAR_BRIGHTNESS
#define LED_BAR_BRIGHTNESS 40   // 0-255。側面 LED は明るいので控えめに
#endif

namespace {
  CRGB leds[LED_BAR_NUM];
  volatile LedBar::Mode currentMode = LedBar::Off;
  volatile LedBar::Mode modeBeforeWake = LedBar::Off;
  volatile int currentLevel = 0;
  volatile uint32_t wakeUntilMs = 0;
  volatile uint32_t frameCount = 0;     // 描画タスクの生存確認用
  TaskHandle_t taskHandle = nullptr;

  // 左右 5 個ずつの前提。index 0..4 を左、5..9 を右とみなし、各側 i=0 を中央寄りとして点灯する。
  // 実機の並びが逆に見える場合は -DLED_BAR_REVERSE で反転。
  inline int physIndex(int idx)
  {
#ifdef LED_BAR_REVERSE
    return LED_BAR_NUM - 1 - idx;
#else
    return idx;
#endif
  }

  void render()
  {
    const uint32_t now = millis();
    LedBar::Mode mode = currentMode;

    if(mode == LedBar::Wake){
      if((int32_t)(wakeUntilMs - now) <= 0){
        currentMode = modeBeforeWake;
        mode = currentMode;
      }else{
        fill_solid(leds, LED_BAR_NUM, CRGB(180, 180, 255));
        FastLED.show();
        return;
      }
    }

    switch(mode){
    case LedBar::Listening: {
      // 2 秒周期の呼吸（青）
      float phase = (now % 2000) / 2000.0f;
      float b = 0.35f + 0.65f * (0.5f - 0.5f * cosf(phase * 2.0f * PI));
      CRGB c = CRGB(0, (uint8_t)(90 * b), (uint8_t)(255 * b));
      fill_solid(leds, LED_BAR_NUM, c);
      break;
    }
    case LedBar::Speaking: {
      // 音量 0..15000 → 片側 0..5 個。立ち上がりは即、減衰はなめらかに
      static float smoothed = 0;
      int lv = currentLevel;
      if(lv < 0) lv = 0;
      if(lv > 15000) lv = 15000;
      float target = lv / 15000.0f;
      smoothed = (target > smoothed) ? target : smoothed * 0.80f + target * 0.20f;
      const int half = LED_BAR_NUM / 2;
      int count = (int)roundf(smoothed * half);
      for(int i = 0; i < half; i++){
        CRGB color = (i < count) ? CRGB(255, (uint8_t)(90 + 30 * i), (uint8_t)(40 + 40 * i)) : CRGB::Black;
        leds[physIndex(half - 1 - i)] = color;   // 左側: 中央寄りから外へ
        leds[physIndex(half + i)] = color;       // 右側
      }
      break;
    }
    case LedBar::Off:
    default:
      // ゆっくり消灯
      for(int i = 0; i < LED_BAR_NUM; i++) leds[i].nscale8(200);
      break;
    }
    FastLED.show();
  }

  void ledTask(void*)
  {
    for(;;){
      render();
      frameCount = frameCount + 1;
      vTaskDelay(pdMS_TO_TICKS(40));
    }
  }
}

namespace LedBar {

void begin()
{
  FastLED.addLeds<SK6812, LED_BAR_PIN, GRB>(leds, LED_BAR_NUM);
  FastLED.setBrightness(LED_BAR_BRIGHTNESS);
  fill_solid(leds, LED_BAR_NUM, CRGB::Black);
  FastLED.show();
  // スピーカー(APP_CPU, 優先度5)の邪魔をしないよう PRO_CPU・低優先度で回す
  xTaskCreatePinnedToCore(ledTask, "ledBar", 3 * 1024, nullptr, 1, &taskHandle, PRO_CPU_NUM);
  Serial.printf("LedBar: %d LEDs on GPIO%d\n", LED_BAR_NUM, LED_BAR_PIN);
}

void setMode(Mode mode)
{
  if(currentMode == Wake){ modeBeforeWake = mode; return; }
  currentMode = mode;
}

void setLevel(int level) { currentLevel = level; }

uint32_t frames() { return frameCount; }

void flashWake()
{
  if(currentMode != Wake) modeBeforeWake = currentMode;
  wakeUntilMs = millis() + 250;
  currentMode = Wake;
}

} // namespace LedBar

#endif // USE_LED_BAR
