import os
from openai import OpenAI
import base64

client = OpenAI(
    api_key="tp-c7b5nfu9dkn9domff7ihfaq806186vu2hxcvk45ejbiuf82t",
    base_url="https://token-plan-cn.xiaomimimo.com/v1"
)

completion = client.chat.completions.create(
    model="mimo-v2.5-tts",
    messages=[
        {
            "role": "user",
            "content": "Bright, bouncy, slightly sing-song tone — like you're bursting with good news you can barely hold in. Fast pace, rising pitch at the end."
        },
        {
            "role": "assistant",
            "content": "你好你好！"
        }
    ],
    audio={
        "format": "wav",
        "voice": "Chloe"
    }
)

message = completion.choices[0].message
audio_bytes = base64.b64decode(message.audio.data)
with open("audio_file.wav", "wb") as f:
    f.write(audio_bytes)