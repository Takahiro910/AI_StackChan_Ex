#if defined(REALTIME_API)

#ifndef _REALTIME_AI_MOD_H
#define _REALTIME_AI_MOD_H

#include <Arduino.h>
#include "mod/ModBase.h"
#include "llm/RealtimeLLMBase.h"

class RealtimeAiMod: public ModBase{
private:
    box_t box_servo;
    box_t box_stt;
    box_t box_BtnA;
    box_t box_BtnC;

    String avatarText;
    bool isOffline;

    RealtimeLLMBase* pRtLLM;

    // for TTS
    String ttsText;

    // for alarm (Function Calling)
    void alarmEventHandler();

    // for official CoreS3 head touch sensor
    unsigned long headTouchHappyUntilMs = 0;
    bool headTouchHappyActive = false;
    void updateHeadTouchExpression(void);

    // ウェイクワード（SimpleVox）。BtnA で有効/無効、BtnB 長押しで登録
    bool wakewordEnabled = false;
    bool wakewordAvailable = false;
    uint32_t wakeListenStartMs = 0;   // ウェイクワードで Listening に入った時刻（0 なら該当なし）

    // 声の切り替え（BtnB 短押しで次の声、2.5 秒操作がなければ確定→セッション再接続→NVS に保存）
    int voicePendingIdx = -1;
    uint32_t voicePendingSinceMs = 0;
    bool voiceApplyWaiting = false;
    const char* const* voiceList(int& count);
    int currentVoiceIndex();
    void updateVoicePicker(void);
    void btnB_pressed(void);
    void updateWakeword(void);
    void updateIdleText(void);
    void updateLedBar(void);

public:
    RealtimeAiMod(bool _isOffline);

    void init(void);
    void pause(void);
    void update(int page_no);
    void btnA_pressed(void);
    void btnB_longPressed(void);
    void btnC_pressed(void);
    void display_touched(int16_t x, int16_t y);
    void doubleTapped(float ax, float ay, float az);   // 加速度センサによるダブルタップ検出のコールバック。platformio.iniで-DENABLE_TAP_DETECTを有効にしてください
    void idle(void);
    bool isBusy(void);

    void toggleRealtimeRecord(void);
};


#endif  //_REALTIME_AI_MOD_H

#endif  //REALTIME_API
