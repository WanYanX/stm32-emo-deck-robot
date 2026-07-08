import os
import io
import sys
import wave
import time
import queue
import argparse
import threading
import base64

import numpy as np
import sounddevice as sd
from openai import OpenAI


def pcm16_to_wav_bytes(audio: np.ndarray, sample_rate: int) -> bytes:
    """
    audio: int16 numpy array, shape: [samples, channels]
    """
    buf = io.BytesIO()

    with wave.open(buf, "wb") as wf:
        wf.setnchannels(audio.shape[1])
        wf.setsampwidth(2)  # int16 = 2 bytes
        wf.setframerate(sample_rate)
        wf.writeframes(audio.tobytes())

    return buf.getvalue()


def extract_text_from_chunk(chunk) -> str:
    """
    兼容 OpenAI SDK 的 ChatCompletionChunk。
    一般文本会在 choices[0].delta.content。
    """
    try:
        if not chunk.choices:
            return ""

        delta = chunk.choices[0].delta

        text = getattr(delta, "content", None)
        if text:
            return text

        # 兜底：某些兼容接口可能返回 dict-like 内容
        if isinstance(delta, dict):
            return delta.get("content") or ""

    except Exception:
        pass

    return ""


def transcribe_wav_stream(client: OpenAI, wav_bytes: bytes, model: str, language: str, dump_chunks: bool = False):
    audio_base64 = base64.b64encode(wav_bytes).decode("utf-8")

    completion = client.chat.completions.create(
        model=model,
        messages=[
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_audio",
                        "input_audio": {
                            "data": f"data:audio/wav;base64,{audio_base64}"
                        },
                    }
                ],
            }
        ],
        extra_body={
            "asr_options": {
                "language": language
            }
        },
        stream=True,
    )

    for chunk in completion:
        if dump_chunks:
            print(chunk.model_dump_json(), flush=True)

        text = extract_text_from_chunk(chunk)
        if text:
            print(text, end="", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", default="mimo-v2.5-asr")
    parser.add_argument("--base-url", default="https://token-plan-cn.xiaomimimo.com/v1")
    parser.add_argument("--language", default="auto")
    parser.add_argument("--sample-rate", type=int, default=16000)
    parser.add_argument("--channels", type=int, default=1)
    parser.add_argument("--chunk-seconds", type=float, default=3.0)
    parser.add_argument("--overlap-seconds", type=float, default=0.0)
    parser.add_argument("--device", default=None, help="麦克风设备名或设备编号，可不填")
    parser.add_argument("--dump-chunks", action="store_true", help="打印原始流式返回，便于调试接口字段")
    args = parser.parse_args()

    api_key = "tp-c7b5nfu9dkn9domff7ihfaq806186vu2hxcvk45ejbiuf82t"
    if not api_key:
        raise RuntimeError("请先设置环境变量 MIMO_API_KEY")

    if args.overlap_seconds >= args.chunk_seconds:
        raise ValueError("--overlap-seconds 必须小于 --chunk-seconds")

    client = OpenAI(
        api_key=api_key,
        base_url=args.base_url,
    )

    audio_queue = queue.Queue()
    send_queue = queue.Queue(maxsize=8)
    stop_event = threading.Event()

    samples_per_chunk = int(args.sample_rate * args.chunk_seconds)
    overlap_samples = int(args.sample_rate * args.overlap_seconds)
    step_samples = samples_per_chunk - overlap_samples

    def audio_callback(indata, frames, time_info, status):
        if status:
            print(f"\n[录音警告] {status}", file=sys.stderr)

        audio_queue.put(indata.copy())

    def worker():
        while not stop_event.is_set():
            item = send_queue.get()

            if item is None:
                break

            seq, wav_bytes = item

            try:
                print(f"\n[片段 {seq}] ", end="", flush=True)
                transcribe_wav_stream(
                    client=client,
                    wav_bytes=wav_bytes,
                    model=args.model,
                    language=args.language,
                    dump_chunks=args.dump_chunks,
                )
                print("", flush=True)
            except Exception as e:
                print(f"\n[片段 {seq} 识别失败] {e}", file=sys.stderr)
            finally:
                send_queue.task_done()

    t = threading.Thread(target=worker, daemon=True)
    t.start()

    buffer = np.empty((0, args.channels), dtype=np.int16)
    seq = 0

    print("开始实时录音识别，按 Ctrl+C 停止。")
    print(f"采样率: {args.sample_rate}, 声道: {args.channels}, 分片: {args.chunk_seconds}s")

    try:
        with sd.InputStream(
            samplerate=args.sample_rate,
            channels=args.channels,
            dtype="int16",
            callback=audio_callback,
            device=args.device,
        ):
            while True:
                block = audio_queue.get()
                buffer = np.concatenate([buffer, block], axis=0)

                while len(buffer) >= samples_per_chunk:
                    segment = buffer[:samples_per_chunk]
                    buffer = buffer[step_samples:]

                    wav_bytes = pcm16_to_wav_bytes(segment, args.sample_rate)
                    seq += 1

                    try:
                        send_queue.put_nowait((seq, wav_bytes))
                    except queue.Full:
                        print("\n[警告] 识别请求积压，丢弃一个音频片段。", file=sys.stderr)

    except KeyboardInterrupt:
        print("\n正在停止...")

    finally:
        stop_event.set()
        send_queue.put(None)
        time.sleep(0.3)


if __name__ == "__main__":
    main()