# ESP32 WebSocket 语音接口接入文档

## 服务地址

服务端启动后会同时开放两个端口：

- 普通文本 TCP 服务：见 [`config.json`](../config.json) 的 `port`
- ESP32 语音 WebSocket 服务：见 [`config.json`](../config.json) 的 `websocket.port`

默认 WebSocket 地址：

```text
ws://<server_ip>:8289/emorobot/v1/
```

当前服务端使用 [`libwebsockets`](../libwebsockets) 实现 WebSocket，协议按“JSON 文本帧 + 二进制音频帧”处理。

## 握手 Header

ESP32 建议携带以下 Header：

```http
GET /emorobot/v1/ HTTP/1.1
Host: <server_ip>:8289
Upgrade: websocket
Connection: Upgrade
Authorization: Bearer <token>
Protocol-Version: 1
Device-Id: <esp32_mac>
Client-Id: <uuid>
```

当前 [`WsVoiceServer`](../ws_voice_server.hpp) 已预留 `token` 配置项，默认不强制鉴权。

## Text Frame：JSON 控制消息

### 1. ESP32 连接后发送 hello

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

### 2. 服务端返回 hello ack

```json
{
  "type": "hello",
  "transport": "websocket",
  "session_id": "esp32-1",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

### 3. 唤醒词检测

```json
{
  "session_id": "esp32-1",
  "type": "listen",
  "state": "detect",
  "text": "你好露西"
}
```

### 4. 开始上传语音

```json
{
  "session_id": "esp32-1",
  "type": "listen",
  "state": "start",
  "mode": "auto"
}
```

之后 ESP32 连续发送 Binary Frame。

### 5. 停止上传语音

```json
{
  "session_id": "esp32-1",
  "type": "listen",
  "state": "stop"
}
```

服务端收到 `stop` 后，会调用 MIMO ASR，再调用现有 LLM，最后调用 MIMO TTS。ESP32 端不需要处理字幕、STT 文本或 TTS 状态文本；服务端最终只通过 WebSocket Binary Frame 返回 Opus TTS 音频帧。

> 当前实现是“流式接收音频帧，结束后整句 ASR”的半流式流程：ESP32 可以连续发送 Opus Binary Frame，服务端会边收边解码缓存 PCM；收到 `listen/stop` 后封装 WAV 并请求 MIMO ASR。MIMO `stream=True` 的 SSE 增量 ASR 尚未接入。

## Binary Frame：音频数据

### ESP32 → 服务端

协议设计为：

```text
Binary Frame = Opus 音频帧
```

推荐参数：

```text
format: opus
sample_rate: 16000
channels: 1
frame_duration: 60ms
```

当前 C++ 版本已引入 [`opus`](../opus) 和 [`dr_wav.h`](../dr_libs/dr_wav.h)，服务端会把 ESP32 上传的 raw Opus 帧解码为 16kHz/mono/16-bit PCM，再封装为 WAV 后请求 MIMO ASR。

### 服务端 → ESP32

服务端 TTS 会返回 Opus 二进制帧：

```text
Binary Frame = Opus TTS payload
```

服务端内部会先调用 MIMO TTS 生成 WAV，再用 [`opus`](../opus) 编码为 16kHz、mono、60ms 的 Opus 帧逐帧发给 ESP32 播放。

## 服务端返回消息

语音问答阶段服务端不再向 ESP32 推送字幕、STT 文本或 TTS 状态 JSON，只返回 Opus Binary Frame。ESP32 只需要做两件事：

1. 麦克风采集后编码 Opus，通过 WebSocket Binary Frame 上传。
2. 收到服务端 WebSocket Binary Frame 后按 Opus 解码播放。

以下 JSON 状态消息仅保留为协议说明，当前语音问答主流程不依赖它们。

### ASR 处理中

```json
{
  "session_id": "esp32-1",
  "type": "asr",
  "state": "processing"
}
```

### 识别结果

```json
{
  "session_id": "esp32-1",
  "type": "stt",
  "text": "帮我打开灯"
}
```

### TTS 开始

```json
{
  "session_id": "esp32-1",
  "type": "tts",
  "state": "start"
}
```

### TTS 字幕

```json
{
  "session_id": "esp32-1",
  "type": "tts",
  "state": "sentence_start",
  "text": "@happy#好的，我帮你打开灯。"
}
```

### TTS 结束

```json
{
  "session_id": "esp32-1",
  "type": "tts",
  "state": "stop"
}
```

## 控制消息转发到另一个 TCP 设备

DeepSeek 返回文本中如果包含 `@happy#`、`@idle#` 这类控制命令，服务端会把完整回复文本通过原有 TCP 服务端广播给已连接的控制设备；也就是表情命令和后面的文本会一起发给另一个已经连接到 `192.168.8.109:8288` 的设备。

为了避免 TTS 把控制指令读出来，服务端在语音合成前会移除 `@...#` 片段，只把剩余自然语言文本传给 MIMO TTS。

例如 DeepSeek 返回：

```text
@happy#好的，我帮你打开灯。
```

服务端会：

1. 通过 WebSocket 给 ESP32 只发送去掉控制指令后的 TTS Opus 音频流，不发送字幕。
2. 通过 TCP 端口 `8288` 给控制设备发送完整文本：

```text
@happy#好的，我帮你打开灯。
```

控制设备如果只需要执行表情/动作，可以自行解析 `@` 到 `#` 之间的命令；如果还需要显示文字，可以继续使用命令后面的文本。

## 最小交互流程

```text
1. ESP32 连接 ws://<server_ip>:8289/emorobot/v1/
2. ESP32 → Server Text: hello
3. Server → ESP32 Text: hello + session_id
4. ESP32 → Server Text: listen/detect
5. ESP32 → Server Text: listen/start
6. ESP32 → Server Binary: 音频数据
7. ESP32 → Server Text: listen/stop
8. Server → ESP32 Text: stt
9. Server → ESP32 Binary: TTS Opus 帧、TTS Opus 帧、TTS Opus 帧...
```

## 配置 MIMO

在 [`config.json`](../config.json) 中配置：

```json
"mimo": {
  "api_key": "your-mimo-api-key-here",
  "asr_url": "https://api.xiaomimimo.com/v1/chat/completions",
  "asr_model": "mimo-v2.5-asr",
  "tts_url": "https://token-plan-cn.xiaomimimo.com/v1/chat/completions",
  "tts_model": "mimo-v2.5-tts",
  "tts_voice": "冰糖"
}
```

不要把真实密钥提交到公开仓库。
