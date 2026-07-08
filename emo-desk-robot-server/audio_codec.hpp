#pragma once

#include <vector>

class AudioCodec {
public:
    AudioCodec(int sample_rate = 16000, int channels = 1, int frame_duration_ms = 60);
    ~AudioCodec();

    bool decode_opus_frame(const unsigned char* data, int len);
    std::vector<unsigned char> build_wav() const;
    static std::vector<std::vector<unsigned char>> encode_wav_to_opus_frames(
        const std::vector<unsigned char>& wav_bytes,
        int target_sample_rate = 16000,
        int target_channels = 1,
        int frame_duration_ms = 60);
    void clear();
    bool empty() const;
    size_t pcm_sample_count() const;

private:
    int sample_rate_;
    int channels_;
    int frame_size_;
    void* decoder_ = nullptr;
    std::vector<short> pcm_;
};
