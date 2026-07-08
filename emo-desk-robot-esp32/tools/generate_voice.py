import os
import base64
from openai import OpenAI

client = OpenAI(
    api_key="tp-c7b5nfu9dkn9domff7ihfaq806186vu2hxcvk45ejbiuf82t",
    base_url="https://token-plan-cn.xiaomimimo.com/v1"
)

completion = client.chat.completions.create(
    model="mimo-v2.5-tts",
    messages=[
        {
            "role": "assistant",
            "content": "你好"
        }
    ],
    audio={
        "format": "wav",
        "voice": "冰糖"
    }
)

message = completion.choices[0].message
audio_bytes = base64.b64decode(message.audio.data)

output_file = "nihao_female.wav"
with open(output_file, "wb") as f:
    f.write(audio_bytes)

print(f"WAV 文件已生成：{output_file}")