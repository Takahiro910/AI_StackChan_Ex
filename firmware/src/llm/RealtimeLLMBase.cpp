#if defined(REALTIME_API)

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "share/Mutex.h"
//#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "rootCA/rootCAgoogleGemini.h"
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "RealtimeLLMBase.h"
//#include "FunctionCall.h"
//#include "MCPClient.h"
#include "Robot.h"

#include <base64.h>
#include <math.h>
#include "libb64/cdecode.h"
#include <WebSocketsClient.h>

using namespace m5avatar;
extern Avatar avatar;

int16_t rtRecBuf[RT_REC_LENGTH];    // リアルタイム録音用メモリ
                                    // Core2だとヒープが不足するので静的な配列とした

TaskHandle_t webSocketLoopTask_h = NULL;

// WebSocketのイベント処理(webSocket.loop())及び、録音データ（約0.1秒）を
// WebSocketで送信するためのループタスク
void webSocketLoopTask(void *arg) {
    Serial.println("WebSocket loop task created");
    RealtimeLLMBase* pThis = (RealtimeLLMBase*)arg;

    while(1){
        pThis->webSocketProcess();
        //delay(1);     //webSocketProcess()内で状態によってスリープ時間を変更
    }
}


RealtimeLLMBase::RealtimeLLMBase(llm_param_t param) : 
    LLMBase(param, 0),
    msgDoc(0),
    rtRecSamplerate(RT_REC_SAMPLE_RATE),
    rtRecLength(RT_REC_LENGTH),
    realtime_recording(false),
    response_done(false),
    startTime(0),
    nextBufIdx(0),
    playClockEndMs(0),
    idleText("Please touch"),
    outputText(String(""))
{
#ifdef REALTIME_API_RECORD_TEST
  // リアルタイム録音のチャンクデータを蓄積してテスト再生するためのバッファ（約4s）
  recTestLenMax = rtRecLength * 40;
  recTestLenCnt = 0;
  recTestBuf = (int16_t*)heap_caps_malloc(recTestLenMax * sizeof(*rtRecBuf), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif

#ifndef REALTIME_API_WITH_TTS
  // ストリーミング音声再生用のリングバッファを PSRAM に確保
  for(int i=0; i<audioBufNum; i++){
    audioBuf[i] = (uint8_t*)heap_caps_malloc(audioBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    memset(audioBuf[i], 0, audioBufSize);
  }
  memset(chunkLevel, 0, sizeof(chunkLevel));
#endif

}

void RealtimeLLMBase::webSocketProcess()
{
    webSocket.loop();

#ifdef REALTIME_API_WITH_TTS
    if(response_done && !speaking){
        startRealtimeRecord();
        response_done = false;
    }
#endif

    if(realtime_recording){
        enterMutexAudio();
        //M5.Mic.begin();
        if(!M5.Mic.record(rtRecBuf, rtRecLength, rtRecSamplerate)){
            Serial.println("Mic.record() returns false");
            delay(1000);
        }
        //M5.Mic.end();
        exitMutexAudio();
        String audio_base64;
        audio_base64 = base64::encode((u8*)rtRecBuf, rtRecLength * sizeof(int16_t));

#ifdef REALTIME_API_RECORD_TEST
        if((recTestLenCnt + rtRecLength) < recTestLenMax){
            memcpy((u8*)&recTestBuf[recTestLenCnt], (u8*)rtRecBuf, rtRecLength * sizeof(int16_t));
            recTestLenCnt += rtRecLength;
        }
#else
        String audioJsonBuf("");
        webSocket.sendTXT(buildInputAudioJson(audioJsonBuf, audio_base64));
#endif

        portTickType elapsedTime = checkRealtimeRecordTimeout();

#if 0   //Debug リスニング経過時間の表示
        static char speechTxt[64];
        sprintf(speechTxt, "Listening:%ds", int(elapsedTime / 1000));
        avatar.setSpeechText(speechTxt);
#else
        avatar.setSpeechText("Listening...");
#endif
        delay(1);
    }
    else{
        if(speaking){
            //発話中もしくはテキスト生成中
            avatar.setSpeechText("");
            resetRealtimeRecordStartTime(); //長いテキストを発話中にタイムアウトしてしまうのを防ぐ
            delay(1);
        }
        else{
            avatar.setSpeechText(idleText.c_str());
            delay(10);
        }
    }
}

int RealtimeLLMBase::getAudioLevel()
{
    if(!speaking) return 0;
    const uint32_t now = millis();
    for(int i = 0; i < audioBufNum; i++){
        const ChunkLevel& c = chunkLevel[i];
        if(c.nBlocks == 0) continue;
        if((int32_t)(now - c.startMs) < 0 || (int32_t)(c.endMs - now) <= 0) continue;
        int block = (now - c.startMs) / 20;
        if(block >= c.nBlocks) block = c.nBlocks - 1;
        int level = c.rms[block] * 3;      // 会話音声の RMS は 1000〜5000 程度。15000 で口全開
        return level > 15000 ? 15000 : level;
    }
    return 0;
}

void RealtimeLLMBase::startRealtimeRecord()
{
    if(!realtime_recording){
        Serial.println("Start realtime recording");
        realtime_recording = true;
        startTime = xTaskGetTickCount();
    }
}

void RealtimeLLMBase::stopRealtimeRecord()
{
    if(realtime_recording){
        Serial.println("Stop realtime recording");
        realtime_recording = false;
        startTime = 0;
    }
}

void RealtimeLLMBase::resetRealtimeRecordStartTime()
{
    startTime = xTaskGetTickCount();
}

portTickType RealtimeLLMBase::checkRealtimeRecordTimeout()
{
    portTickType elapsedTime;
    elapsedTime = (xTaskGetTickCount() - startTime) * portTICK_RATE_MS;
    if(elapsedTime > REALTIME_RECORD_TIMEOUT){
        Serial.println("Realtime recording timeout");
        stopRealtimeRecord();
#ifdef REALTIME_API_RECORD_TEST
        M5.Mic.end();
        if (M5.Speaker.begin())
        {
            M5.Speaker.playRaw(recTestBuf, recTestLenCnt, rtRecSamplerate);
            while (M5.Speaker.isPlaying()) { delay(10); }
            M5.Speaker.end();
            M5.Mic.begin();
        }
        recTestLenCnt = 0;
#endif
    }

    return elapsedTime;
}

int RealtimeLLMBase::base64_decode(const char* input, int size, char* output)
{
	/* keep track of our decoded position */
	char* c = output;
	/* store the number of bytes decoded by a single call */
	int cnt = 0;
	/* we need a decoder state */
	base64_decodestate s;
	
	/*---------- START DECODING ----------*/
	/* initialise the decoder state */
	base64_init_decodestate(&s);
	/* decode the input data */
	cnt = base64_decode_block(input, strlen(input), c, &s);
	c += cnt;
	/* note: there is no base64_decode_blockend! */
	/*---------- STOP DECODING  ----------*/
	
	/* we want to print the decoded data, so null-terminate it: */
	*c = 0;
	
	return cnt;
}


void RealtimeLLMBase::hexdump(const void *mem, uint32_t len, uint8_t cols) {
	const uint8_t* src = (const uint8_t*) mem;
	Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		Serial.printf("%02X ", *src);
		src++;
	}
	Serial.printf("\n");
}


void RealtimeLLMBase::streamAudioDelta(String& delta)
{
    streamAudioDelta(delta.c_str(), delta.length());
}

const char* RealtimeLLMBase::findSub(const char* hay, size_t n, const char* needle)
{
    size_t m = strlen(needle);
    if(m == 0 || n < m) return nullptr;
    for(size_t i = 0; i + m <= n; i++){
        if(hay[i] == needle[0] && memcmp(hay + i, needle, m) == 0) return hay + i;
    }
    return nullptr;
}

bool RealtimeLLMBase::streamBase64FieldInPlace(char* payload, size_t length, const char* afterToken, const char* key)
{
    const char* end = payload + length;
    const char* p = findSub(payload, length, afterToken);
    if(p == nullptr) return false;
    p = findSub(p, end - p, key);
    if(p == nullptr) return false;
    p += strlen(key);
    while(p < end && (*p == ' ' || *p == ':' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    if(p >= end || *p != '"') return false;
    char* start = (char*)p + 1;
    char* close = (char*)memchr(start, '"', end - start);
    if(close == nullptr) return false;
    *close = 0;                      // base64_decode が strlen を使うので一時的に終端する
    streamAudioDelta(start, close - start);
    *close = '"';
    return true;
}

void RealtimeLLMBase::streamAudioDelta(const char* base64, int base64Size)
{
    if((base64Size / 4) * 3 + 3 > audioBufSize){
        Serial.printf("audio chunk too large (%d byte base64), skipped\n", base64Size);
        return;
    }
    uint8_t* buf = audioBuf[nextBufIdx];
    int len = base64_decode(base64, base64Size, (char*)buf);

    // 20ms ブロックごとの RMS を先に計算しておく（再生位置の推定に使う）
    ChunkLevel& cl = chunkLevel[nextBufIdx];
    const int16_t* pcm = (const int16_t*)buf;
    const int nSamples = len / 2;
    int nBlocks = (nSamples + levelBlockSamples - 1) / levelBlockSamples;
    if(nBlocks > levelBlockMax) nBlocks = levelBlockMax;
    for(int b = 0; b < nBlocks; b++){
        const int s0 = b * levelBlockSamples;
        int s1 = s0 + levelBlockSamples; if(s1 > nSamples) s1 = nSamples;
        uint64_t acc = 0;
        for(int s = s0; s < s1; s += 4){ int32_t v = pcm[s]; acc += (uint64_t)(v * v); }   // 4 サンプル間引き
        int cnt = (s1 - s0 + 3) / 4;
        cl.rms[b] = (uint16_t)sqrtf((float)acc / (cnt > 0 ? cnt : 1));
    }
    //Serial.printf("audio pcm16 size: %d byte\n", len);

    // 旧実装は isPlaying() が false になるまで待ってから playRaw() していたため、チャンク境界ごとに数 ms の無音が入り
    // 「ブツ切れ」に聞こえた。playRaw() は次スロットが空くまで自分でブロックするので、待たずに積めば途切れなく連結される。
    M5.Speaker.playRaw((int16_t*)buf, len/2, 24000, false, 1, 0, false);   // channel は 0 固定。-1(自動) だと再生中は別チャンネルが選ばれて同時再生になりキューに積まれない

    // playRaw() から戻った時点で「前のチャンクの再生終了＝このチャンクの再生開始」なので、時計を進めて再生区間を記録
    const uint32_t now = millis();
    const uint32_t durationMs = (uint32_t)nSamples * 1000 / 24000;
    uint32_t start = ((int32_t)(playClockEndMs - now) > 0) ? playClockEndMs : now;
    cl.startMs = start;
    cl.endMs = start + durationMs;
    cl.nBlocks = nBlocks;
    playClockEndMs = cl.endMs;

    nextBufIdx = (nextBufIdx + 1) % audioBufNum;
}

void RealtimeLLMBase::invokeWebSocketLoopTask(void)
{
    // スピーカータスク(APP_CPU)と同じコアで重い JSON 解析を走らせると再生が途切れるため、PRO_CPU 側に固定する
    xTaskCreatePinnedToCore(webSocketLoopTask, /* Function to implement the task */
            "webSocketLoopTask", /* Name of the task */
            6*1024,               /* Stack size in words */
            this,                 /* Task input parameter */
            3,                    /* Priority of the task */
            &webSocketLoopTask_h, /* Task handle. */
            PRO_CPU_NUM);
}

void RealtimeLLMBase::suspendWebSocketLoopTask(void)
{
    if (eTaskGetState(webSocketLoopTask_h) != eSuspended) {
      Serial.println("webSocketLoopTask Suspend");
      vTaskSuspend(webSocketLoopTask_h);
    }
}

void RealtimeLLMBase::resumeWebSocketLoopTask(void)
{
    if (eTaskGetState(webSocketLoopTask_h) == eSuspended) {
      Serial.println("webSocketLoopTask Resume");
      vTaskResume(webSocketLoopTask_h);
    }
}

#endif  //REALTIME_API