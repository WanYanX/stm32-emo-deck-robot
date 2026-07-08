#include "audio_codec.hpp"
#include <opus.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#define DR_WAV_IMPLEMENTATION
#include "dr_libs/dr_wav.h"

#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>

AudioCodec::AudioCodec(int sample_rate, int channels, int frame_duration_ms)
    : sample_rate_(sample_rate), channels_(channels), frame_size_(sample_rate * frame_duration_ms / 1000)
{
    int error = OPUS_OK;
    decoder_ = opus_decoder_create(sample_rate_, channels_, &error);
    if (error != OPUS_OK || !decoder_) {
        spdlog::error("Failed to create Opus decoder: {}", opus_strerror(error));
        decoder_ = nullptr;
    }
}

AudioCodec::~AudioCodec()
{
    if (decoder_) {
        opus_decoder_destroy(static_cast<OpusDecoder*>(decoder_));
        decoder_ = nullptr;
    }
}

bool AudioCodec::decode_opus_frame(const unsigned char* data, int len)
{
    if (!decoder_ || !data || len <= 0)
        return false;

    std::vector<opus_int16> frame(static_cast<size_t>(frame_size_ * channels_));
    int samples = opus_decode(static_cast<OpusDecoder*>(decoder_), data, len, frame.data(), frame_size_, 0);
    if (samples < 0) {
        spdlog::warn("Opus decode failed: {}", opus_strerror(samples));
        return false;
    }

    pcm_.insert(pcm_.end(), frame.begin(), frame.begin() + samples * channels_);
    return true;
}

std::vector<unsigned char> AudioCodec::build_wav() const
{
    if (pcm_.empty())
        return {};

    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = static_cast<drwav_uint32>(channels_);
    format.sampleRate = static_cast<drwav_uint32>(sample_rate_);
    format.bitsPerSample = 16;

    void* wav_data = nullptr;
    size_t wav_size = 0;
    drwav wav;
    drwav_bool32 ok = drwav_init_memory_write(
        &wav,
        &wav_data,
        &wav_size,
        &format,
        nullptr);

    if (!ok)
        return {};

    drwav_uint64 frames_to_write = static_cast<drwav_uint64>(pcm_.size() / channels_);
    drwav_uint64 frames_written = drwav_write_pcm_frames(&wav, frames_to_write, pcm_.data());
    drwav_uninit(&wav);

    if (frames_written != frames_to_write || !wav_data || wav_size == 0)
        return {};

    const auto* bytes = static_cast<const unsigned char*>(wav_data);
    std::vector<unsigned char> out(bytes, bytes + wav_size);
    drwav_free(wav_data, nullptr);
    return out;
}

static std::vector<short> convert_pcm_to_target(const short* input,
                                                drwav_uint64 input_frames,
                                                unsigned int input_sample_rate,
                                                unsigned int input_channels,
                                                int target_sample_rate,
                                                int target_channels)
{
    if (!input || input_frames == 0 || input_sample_rate == 0 || input_channels == 0)
        return {};

    drwav_uint64 target_frames = static_cast<drwav_uint64>(
        std::ceil(static_cast<double>(input_frames) * target_sample_rate / input_sample_rate));
    std::vector<short> output(static_cast<size_t>(target_frames * target_channels));

    for (drwav_uint64 i = 0; i < target_frames; ++i) {
        double src_pos = static_cast<double>(i) * input_sample_rate / target_sample_rate;
        drwav_uint64 idx0 = static_cast<drwav_uint64>(src_pos);
        drwav_uint64 idx1 = std::min<drwav_uint64>(idx0 + 1, input_frames - 1);
        double frac = src_pos - idx0;

        for (int ch = 0; ch < target_channels; ++ch) {
            double sample0 = 0.0;
            double sample1 = 0.0;

            if (target_channels == 1) {
                for (unsigned int in_ch = 0; in_ch < input_channels; ++in_ch) {
                    sample0 += input[idx0 * input_channels + in_ch];
                    sample1 += input[idx1 * input_channels + in_ch];
                }
                sample0 /= input_channels;
                sample1 /= input_channels;
            } else {
                unsigned int in_ch = std::min<unsigned int>(static_cast<unsigned int>(ch), input_channels - 1);
                sample0 = input[idx0 * input_channels + in_ch];
                sample1 = input[idx1 * input_channels + in_ch];
            }

            double mixed = sample0 + (sample1 - sample0) * frac;
            output[static_cast<size_t>(i * target_channels + ch)] = static_cast<short>(
                std::max(-32768.0, std::min(32767.0, mixed)));
        }
    }

    return output;
}

std::vector<std::vector<unsigned char>> AudioCodec::encode_wav_to_opus_frames(
    const std::vector<unsigned char>& wav_bytes,
    int target_sample_rate,
    int target_channels,
    int frame_duration_ms)
{
    if (wav_bytes.empty())
        return {};

    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drwav_uint64 frame_count = 0;
    drwav_int16* pcm = drwav_open_memory_and_read_pcm_frames_s16(
        wav_bytes.data(), wav_bytes.size(), &channels, &sample_rate, &frame_count, nullptr);
    if (!pcm) {
        spdlog::warn("Failed to decode TTS WAV for Opus encoding");
        return {};
    }

    std::vector<short> target_pcm = convert_pcm_to_target(
        pcm, frame_count, sample_rate, channels, target_sample_rate, target_channels);
    drwav_free(pcm, nullptr);

    if (target_pcm.empty())
        return {};

    int error = OPUS_OK;
    OpusEncoder* encoder = opus_encoder_create(target_sample_rate, target_channels, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK || !encoder) {
        spdlog::warn("Failed to create Opus encoder: {}", opus_strerror(error));
        return {};
    }

    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    const int frame_size = target_sample_rate * frame_duration_ms / 1000;
    const int max_packet_size = 4000;
    std::vector<std::vector<unsigned char>> frames;
    std::vector<short> frame_pcm(static_cast<size_t>(frame_size * target_channels), 0);
    std::vector<unsigned char> packet(static_cast<size_t>(max_packet_size));

    size_t total_frames = target_pcm.size() / target_channels;
    for (size_t offset = 0; offset < total_frames; offset += frame_size) {
        std::fill(frame_pcm.begin(), frame_pcm.end(), 0);
        size_t frames_to_copy = std::min<size_t>(frame_size, total_frames - offset);
        std::copy_n(target_pcm.begin() + static_cast<ptrdiff_t>(offset * target_channels),
                    frames_to_copy * target_channels,
                    frame_pcm.begin());

        int bytes = opus_encode(encoder, frame_pcm.data(), frame_size, packet.data(), max_packet_size);
        if (bytes > 0)
            frames.emplace_back(packet.begin(), packet.begin() + bytes);
        else
            spdlog::warn("Opus encode failed: {}", opus_strerror(bytes));
    }

    opus_encoder_destroy(encoder);
    return frames;
}

void AudioCodec::clear()
{
    pcm_.clear();
    if (decoder_)
        opus_decoder_ctl(static_cast<OpusDecoder*>(decoder_), OPUS_RESET_STATE);
}

bool AudioCodec::empty() const
{
    return pcm_.empty();
}

size_t AudioCodec::pcm_sample_count() const
{
    return pcm_.size();
}
