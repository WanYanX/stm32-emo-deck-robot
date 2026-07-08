import argparse
import asyncio
import json
import math
import struct
import sys
import wave
from pathlib import Path


DEFAULT_URL = "ws://192.168.8.109:8289/emorobot/v1/"


def make_test_wav(path: Path, seconds: float = 1.2, sample_rate: int = 16000) -> None:
    """Generate a simple 16kHz mono PCM WAV file for smoke testing."""
    amplitude = 0.25
    frequency = 440.0
    total_samples = int(seconds * sample_rate)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        for i in range(total_samples):
            sample = int(32767 * amplitude * math.sin(2 * math.pi * frequency * i / sample_rate))
            wav.writeframesraw(struct.pack("<h", sample))


def load_wav_pcm(path: Path) -> tuple[list[int], int, int]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        if sample_width != 2:
            raise ValueError(f"Only 16-bit PCM WAV is supported, got sample width {sample_width}")
        raw = wav.readframes(wav.getnframes())

    samples = list(struct.unpack("<" + "h" * (len(raw) // 2), raw))
    if channels == 2:
        mono = []
        for i in range(0, len(samples), 2):
            mono.append(int((samples[i] + samples[i + 1]) / 2))
        samples = mono
        channels = 1
    elif channels != 1:
        raise ValueError(f"Only mono/stereo WAV is supported, got {channels} channels")
    return samples, sample_rate, channels


def encode_wav_to_opus_frames(wav_path: Path, frame_duration_ms: int = 60) -> list[bytes]:
    """Encode WAV to raw Opus frames with opuslib if available; otherwise fail with guidance."""
    try:
        import opuslib
    except ImportError as exc:
        raise RuntimeError(
            "Missing Python dependency opuslib. Install it with: pip install opuslib websocket-client"
        ) from exc

    samples, sample_rate, channels = load_wav_pcm(wav_path)
    if sample_rate not in (8000, 12000, 16000, 24000, 48000):
        raise ValueError(f"Opus requires 8/12/16/24/48kHz input, got {sample_rate}")

    frame_size = sample_rate * frame_duration_ms // 1000
    encoder = opuslib.Encoder(sample_rate, channels, opuslib.APPLICATION_AUDIO)
    frames: list[bytes] = []

    for offset in range(0, len(samples), frame_size):
        chunk = samples[offset : offset + frame_size]
        if len(chunk) < frame_size:
            chunk += [0] * (frame_size - len(chunk))
        pcm = struct.pack("<" + "h" * len(chunk), *chunk)
        frames.append(encoder.encode(pcm, frame_size))

    return frames


async def run_async(args: argparse.Namespace) -> int:
    try:
        import websockets
    except ImportError:
        print("Missing Python dependency websockets. Install it with: pip install websockets", file=sys.stderr)
        return 2

    wav_path = Path(args.wav)
    if args.generate_wav:
        make_test_wav(wav_path)
        print(f"Generated test WAV: {wav_path}")

    opus_frames: list[bytes] = []
    if args.send_audio:
        opus_frames = encode_wav_to_opus_frames(wav_path, args.frame_duration)
        print(f"Prepared {len(opus_frames)} Opus frames from {wav_path}")

    headers = {
        "Authorization": f"Bearer {args.token}",
        "Protocol-Version": "1",
        "Device-Id": args.device_id,
        "Client-Id": args.client_id,
    }

    print(f"Connecting: {args.url}")
    async with websockets.connect(args.url, extra_headers=headers, max_size=None) as ws:
        hello = {
            "type": "hello",
            "version": 1,
            "transport": "websocket",
            "audio_params": {
                "format": "opus",
                "sample_rate": args.sample_rate,
                "channels": 1,
                "frame_duration": args.frame_duration,
            },
        }
        await ws.send(json.dumps(hello, ensure_ascii=False))
        print("-> hello")

        first = await asyncio.wait_for(ws.recv(), timeout=args.timeout)
        print(f"<- {first!r}")

        if not args.send_audio:
            print("Handshake test passed.")
            return 0

        await ws.send(json.dumps({"type": "listen", "state": "detect", "text": "测试"}, ensure_ascii=False))
        print("-> listen/detect")
        try:
            msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
            print(f"<- {msg!r}")
        except asyncio.TimeoutError:
            pass

        await ws.send(json.dumps({"type": "listen", "state": "start", "mode": "manual"}, ensure_ascii=False))
        print("-> listen/start")
        try:
            msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
            print(f"<- {msg!r}")
        except asyncio.TimeoutError:
            pass

        for i, frame in enumerate(opus_frames, 1):
            await ws.send(frame)
            if args.realtime:
                await asyncio.sleep(args.frame_duration / 1000.0)
            if i % 10 == 0:
                print(f"-> sent {i}/{len(opus_frames)} Opus frames")

        await ws.send(json.dumps({"type": "listen", "state": "stop"}, ensure_ascii=False))
        print("-> listen/stop")

        binary_count = 0
        binary_bytes = 0
        while True:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=args.timeout)
            except asyncio.TimeoutError:
                break

            if isinstance(msg, bytes):
                binary_count += 1
                binary_bytes += len(msg)
                print(f"<- binary Opus frame #{binary_count}: {len(msg)} bytes")
            else:
                print(f"<- text: {msg}")

            if binary_count >= args.expect_binary_frames:
                break

        if binary_count > 0:
            print(f"Voice test passed: received {binary_count} binary frames, {binary_bytes} bytes total.")
            return 0

        print("Voice test finished without binary TTS frames. Check MIMO API key / ASR / TTS config.")
        return 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Test ESP32 WebSocket voice endpoint.")
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--token", default="test-token")
    parser.add_argument("--device-id", default="python-test-device")
    parser.add_argument("--client-id", default="python-test-client")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--sample-rate", type=int, default=16000)
    parser.add_argument("--frame-duration", type=int, default=60)
    parser.add_argument("--wav", default="tests/ws_test_input.wav")
    parser.add_argument("--generate-wav", action="store_true", help="Generate a simple WAV before sending.")
    parser.add_argument("--send-audio", action="store_true", help="Send Opus audio frames and wait for TTS Opus frames.")
    parser.add_argument("--realtime", action="store_true", help="Send audio frames at realtime frame interval.")
    parser.add_argument("--expect-binary-frames", type=int, default=1)
    return parser.parse_args()


def main() -> int:
    return asyncio.run(run_async(parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
