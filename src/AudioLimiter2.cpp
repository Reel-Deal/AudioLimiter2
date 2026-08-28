// -----------------------------------------------------------------------------
// AudioLimiter2: a reimplementation of AudioLimiter by dimzon (requires C++17)
// -----------------------------------------------------------------------------

#include "avisynth.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

enum class LimiterMode {
    Exponential,
    Linear,
    WavGain,
    SoftClip
};

class AudioLimiter2 final : public GenericVideoFilter {
public:

    AudioLimiter2(PClip _child, float _param, LimiterMode _mode, IScriptEnvironment* env) :
        GenericVideoFilter(_child), filter_mode(_mode), factor(_param)
    {
        const char* const name = filter_name(filter_mode);

        if (!vi.HasAudio())
            env->ThrowError("%s: input clip has no audio.", name);

        if (vi.SampleType() != SAMPLE_FLOAT)
            env->ThrowError("%s: audio must be 32-bit float, use ConvertAudioToFloat().", name);

        switch (filter_mode) {
        case LimiterMode::Exponential:
        case LimiterMode::Linear:
            bypass = (factor == 0.0f);
            normalization = static_cast<float>(std::tanh(static_cast<double>(factor)));
            break;

        case LimiterMode::WavGain:
            normalization = static_cast<float>(1.0 / wavgain_curve(factor));
            break;

        case LimiterMode::SoftClip:
            curve = factor;
            curve_limit = static_cast<float>((1.0 - curve) * HALF_PI + curve);
            curve_headroom = 1.0 - curve;
            break;
        }
    }

    int __stdcall SetCacheHints(int cachehints, int) override {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }

    // -------------------------------------------------------------------------
    // Audio Processing
    // -------------------------------------------------------------------------
    void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) override {
        if (count <= 0)
            return;

        uint8_t* dst = static_cast<uint8_t*>(buf);
        const int64_t stride = vi.BytesPerAudioSample();
        const int64_t end = start + count;

        if (start >= vi.num_audio_samples || end <= 0) {
            std::memset(dst, 0, static_cast<size_t>(count * stride));
            return;
        }

        if (start < 0 || end > vi.num_audio_samples) {
            std::memset(dst, 0, static_cast<size_t>(count * stride));
            const int64_t first = std::max<int64_t>(start, 0);
            const int64_t last = std::min<int64_t>(end, vi.num_audio_samples);
            dst += (first - start) * stride;
            start = first;
            count = last - first;
        }

        child->GetAudio(dst, start, count, env);

        if (bypass)
            return;

        float* const samples = reinterpret_cast<float*>(dst);
        const int64_t n = count * vi.AudioChannels();

        switch (filter_mode) {

        // Mode 0: ExpotencialLimiter
        // Log-domain hybrid: converts to a logarithmic scale, applies normalized tanh
        // compression, and transforms back to linear space.
        // out = sign * (10^(tanh(factor*log10(1+9|in|)) / tanh(factor)) - 1) / 9
        case LimiterMode::Exponential:
            for (int64_t i = 0; i < n; i++)
                samples[i] = limit(expotencial(samples[i]));
            break;

        // Mode 1: LinearLimiter
        // Continuous saturation: applies a normalized tanh curve across the entire
        // dynamic range.
        // out = tanh(factor * in) / tanh(factor)
        case LimiterMode::Linear:
            for (int64_t i = 0; i < n; i++)
                samples[i] = limit(linear(samples[i]));
            break;

        // Mode 2: WavGainLimiter
        // Soft-knee: linear in [-0.5, 0.5]*factor, tanh-compressed outside it.
        case LimiterMode::WavGain:
            for (int64_t i = 0; i < n; i++)
                samples[i] = limit(wavgain(samples[i]));
            break;

        // Mode 3: SoftClipperFromAudX
        // Piecewise: linear within [-curve, curve], sine-rounded up to curve_limit,
        // hard-clipped beyond.
        case LimiterMode::SoftClip:
            for (int64_t i = 0; i < n; i++)
                samples[i] = limit(softclip(samples[i]));
            break;
        }
    }

private:

    static constexpr double SOFTCLIP_CEILING = 32767.0 / 32768.0;
    static constexpr double HALF_PI = 1.5707963267948966;
    static constexpr float EXP_SCALE = 1.0f / 9.0f;

    const LimiterMode filter_mode;
    const float factor;
    bool bypass = false;
    float normalization = 0.0f;
    float curve = 0.0f;
    float curve_limit = 0.0f;
    double curve_headroom = 0.0;

    static float limit(double out) {
        return static_cast<float>(std::clamp(out, -1.0, 1.0));
    }

    double expotencial(double in) const {
        const double sign = (in < 0.0) ? -1.0 : 1.0;
        const double exponent = std::tanh(factor * std::log10(1.0 + std::abs(in) * 9.0)) / normalization;
        return sign * (std::pow(10.0, exponent) - 1.0) * EXP_SCALE;
    }

    double linear(double in) const {
        return std::tanh(factor * in) / normalization;
    }

    double wavgain(double in) const {
        const double x = in * factor;
        if (x < -0.5)
            return -wavgain_curve(-x) * normalization;
        if (x <= 0.5)
            return x * normalization;
        return wavgain_curve(x) * normalization;
    }

    double softclip(double in) const {
        if (!(in < curve_limit) || in <= -curve_limit)
            return (in > 0.0) ? SOFTCLIP_CEILING : -1.0;
        if (in > curve)
            return std::sin((in - curve) / curve_headroom) * curve_headroom + curve;
        if (in < -curve)
            return std::sin((in + curve) / curve_headroom) * curve_headroom - curve;
        return in;
    }

    static double wavgain_curve(double v) {
        return (std::tanh(2.0 * (v - 0.5)) + 1.0) * 0.5;
    }

    static const char* filter_name(LimiterMode mode) {
        switch (mode) {
        case LimiterMode::Exponential: return "ExpotencialLimiter2";
        case LimiterMode::Linear:      return "LinearLimiter2";
        case LimiterMode::WavGain:     return "WavGainLimiter2";
        default:                       return "SoftClipperFromAudX2";
        }
    }
};

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------
template <LimiterMode Mode>
static AVSValue __cdecl CreateLimiter(AVSValue args, void*, IScriptEnvironment* env) {
    constexpr float def = (Mode == LimiterMode::SoftClip) ? 0.7f : 1.0f;
    return new AudioLimiter2(args[0].AsClip(), static_cast<float>(args[1].AsFloat(def)), Mode, env);
}

const AVS_Linkage* AVS_linkage = nullptr;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;

    env->AddFunction("WavGainLimiter2", "c[factor]f", CreateLimiter<LimiterMode::WavGain>, nullptr);
    env->AddFunction("SoftClipperFromAudX2", "c[curve]f", CreateLimiter<LimiterMode::SoftClip>, nullptr);
    env->AddFunction("LinearLimiter2", "c[factor]f", CreateLimiter<LimiterMode::Linear>, nullptr);
    env->AddFunction("ExpotencialLimiter2", "c[factor]f", CreateLimiter<LimiterMode::Exponential>, nullptr);

    return "AudioLimiter2 1.0";
}
