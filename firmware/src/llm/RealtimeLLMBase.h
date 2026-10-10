#if defined(REALTIME_API)

#ifndef _REALTIME_LLM_BASE_H
#define _REALTIME_LLM_BASE_H

#include <Arduino.h>
#include <M5Unified.h>
#include "StackchanExConfig.h"
#include "SpiRamJsonDocument.h"
#include "ChatHistory.h"
#include "LLMBase.h"
#include <WebSocketsClient.h>

//#define REALTIME_API_RECORD_TEST

#define GEMINI_PROMPT_MAX_SIZE   (1024*50)

#define RT_REC_LENGTH       (2000)      //0.125s 
#define RT_REC_SAMPLE_RATE  (16000)

#ifdef REALTIME_API_RECORD_TEST
#define REALTIME_RECORD_TIMEOUT     (4 * 1000)      //ms  ※録音テスト再生用バッファのサイズに合わせる
#else
#define REALTIME_RECORD_TIMEOUT     (30 * 1000)      //ms
#endif

extern String InitBuffer;
extern const String json_ChatString;

class RealtimeLLMBase: public LLMBase{
//private:
public:   //本当はprivateにしたいところだがコールバック関数にthisポインタを渡して使うためにpublicとした
    WebSocketsClient webSocket;
    SpiRamJsonDocument msgDoc;

    // for record
    //
    //int16_t* rtRecBuf;
    int rtRecSamplerate;
    int rtRecLength;
    bool realtime_recording;
    bool response_done;
    portTickType startTime;

#ifdef REALTIME_API_RECORD_TEST
    int16_t* recTestBuf;
    int recTestLenMax;
    int recTestLenCnt;
#endif

    // for play
    //
    // Base64をデコードして得た音声データを格納するリングバッファ（PSRAM）。
    // M5Unified の Speaker は 1 チャンネルあたり「再生中＋次」の 2 スロットを持ち、playRaw() は次スロットが埋まっていると空くまでブロックする。
    // そのため待ち合わせなしで playRaw() を呼び続けても、同時に参照されるバッファは最大 2 面。4 面あれば書き込み面が再生中になることはない。
    static const int audioBufNum  = 4;
    static const int audioBufSize = 120 * 1024;
    uint8_t* audioBuf[audioBufNum];
    int nextBufIdx;          // 次回データを格納するバッファ面

public:
    RealtimeLLMBase(llm_param_t param);

    virtual void chat(String text, const char *base64_buf = NULL) {};   //dummy
    virtual String& buildInputAudioJson(String& jsonBuf, String& base64) = 0;

    void invokeWebSocketLoopTask(void);
    void suspendWebSocketLoopTask(void);
    void resumeWebSocketLoopTask(void);
    void webSocketProcess();
    int getAudioLevel();
    void startRealtimeRecord();
    void stopRealtimeRecord();
    void resetRealtimeRecordStartTime();
    portTickType checkRealtimeRecordTimeout();
    bool isRealtimeRecording() {return realtime_recording;};

    int base64_decode(const char* input, int size, char* output);
    void hexdump(const void *mem, uint32_t len, uint8_t cols = 16);
    void streamAudioDelta(String& delta);

    // for TTS
    //
    String outputText;

};


#endif  //_REALTIME_LLM_BASE_H

#endif  //REALTIME_API