#include "GainProcessor.h"

#include <algorithm>
#include <cmath>

namespace loudmixer {

GainProcessor::GainProcessor(float gain, ClipCurve curve) noexcept
    : gain_(clampParam(gain))
    , curve_(static_cast<int>(curve))
{
    for (auto& g : channelGains_)
        g.store(1.0f, std::memory_order_relaxed);
}

float GainProcessor::clampParam(float g) noexcept
{
    if (!(g >= 0.0f)) // NaN ou negativo
        return 0.0f;
    return std::min(g, kMaxGain);
}

void GainProcessor::setGain(float gain) noexcept { gain_.store(clampParam(gain), std::memory_order_relaxed); }
float GainProcessor::gain() const noexcept { return gain_.load(std::memory_order_relaxed); }

void GainProcessor::setChannelGain(int channel, float gain) noexcept
{
    if (channel >= 0 && channel < kMaxChannels)
        channelGains_[static_cast<std::size_t>(channel)].store(clampParam(gain), std::memory_order_relaxed);
}

float GainProcessor::channelGain(int channel) const noexcept
{
    if (channel < 0 || channel >= kMaxChannels)
        return 1.0f;
    return channelGains_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed);
}

void GainProcessor::setMuted(bool muted) noexcept { muted_.store(muted, std::memory_order_relaxed); }
bool GainProcessor::muted() const noexcept { return muted_.load(std::memory_order_relaxed); }

void GainProcessor::setCurve(ClipCurve curve) noexcept { curve_.store(static_cast<int>(curve), std::memory_order_relaxed); }
ClipCurve GainProcessor::curve() const noexcept { return static_cast<ClipCurve>(curve_.load(std::memory_order_relaxed)); }

void GainProcessor::resetRamp() noexcept { resetRequested_.store(true, std::memory_order_relaxed); }

float GainProcessor::clipSample(float x, ClipCurve curve) noexcept
{
    const float a = std::fabs(x);
    if (!(a == a)) // NaN: silencia em vez de propagar lixo para a placa
        return 0.0f;
    if (a <= kKnee)
        return x;

    constexpr float w = 1.0f - kKnee; // altura do joelho
    const float d = a - kKnee;
    float y = 1.0f;

    switch (curve) {
    case ClipCurve::Hard:
        y = std::min(a, 1.0f);
        break;
    case ClipCurve::Tanh:
        // Contínua e com derivada 1 em kKnee; tende assintoticamente a 1.0.
        y = kKnee + w * std::tanh(d / w);
        break;
    case ClipCurve::Cubic: {
        // v ∈ [0,1]: y = knee + 1.5w(v - v³/3) → derivada 1 no início, 0 no fim, y(1) = 1.
        const float v = d / (1.5f * w);
        y = v >= 1.0f ? 1.0f : kKnee + 1.5f * w * (v - v * v * v / 3.0f);
        break;
    }
    }
    return std::copysign(y, x);
}

void GainProcessor::atomicMax(std::atomic<float>& target, float value) noexcept
{
    float prev = target.load(std::memory_order_relaxed);
    while (prev < value && !target.compare_exchange_weak(prev, value, std::memory_order_relaxed)) {
    }
}

void GainProcessor::process(float* samples, std::size_t frames, int channels) noexcept
{
    process(samples, samples, frames, channels);
}

void GainProcessor::process(const float* in, float* out, std::size_t frames, int channels) noexcept
{
    if (frames == 0 || channels <= 0 || in == nullptr || out == nullptr)
        return;

    const int chCount = std::min(channels, kMaxChannels);
    const bool mute = muted_.load(std::memory_order_relaxed);
    const float master = gain_.load(std::memory_order_relaxed);
    const ClipCurve curve = static_cast<ClipCurve>(curve_.load(std::memory_order_relaxed));
    const bool reset = resetRequested_.exchange(false, std::memory_order_relaxed);

    std::array<float, kMaxChannels> start{};
    std::array<float, kMaxChannels> step{};
    std::array<bool, kMaxChannels> boosting{};
    for (int c = 0; c < chCount; ++c) {
        const auto ci = static_cast<std::size_t>(c);
        const float target = mute ? 0.0f : master * channelGains_[ci].load(std::memory_order_relaxed);
        if (reset)
            currentGain_[ci] = target;
        start[ci] = currentGain_[ci];
        step[ci] = (target - start[ci]) / static_cast<float>(frames);
        boosting[ci] = std::max(start[ci], target) > 1.0f && curve != ClipCurve::Hard;
        currentGain_[ci] = target;
    }

    float peakIn = 0.0f;
    float peakOut = 0.0f;
    std::uint64_t clipped = 0;

    for (std::size_t f = 0; f < frames; ++f) {
        const std::size_t base = f * static_cast<std::size_t>(channels);
        for (int c = 0; c < channels; ++c) {
            const std::size_t i = base + static_cast<std::size_t>(c);
            if (c >= chCount) { // canais além do suportado: só copia
                out[i] = in[i];
                continue;
            }
            const auto ci = static_cast<std::size_t>(c);
            // Rampa: na última amostra do bloco o ganho é exatamente o alvo.
            const float g = step[ci] == 0.0f ? start[ci] : start[ci] + step[ci] * static_cast<float>(f + 1);
            const float pre = in[i] * g;
            const float a = std::fabs(pre);
            peakIn = std::max(peakIn, a);
            if (a > 1.0f)
                ++clipped;

            float y;
            if (boosting[ci])
                y = clipSample(pre, curve);
            else
                y = a > 1.0f ? std::copysign(1.0f, pre) : (a == a ? pre : 0.0f);
            out[i] = y;
            peakOut = std::max(peakOut, std::fabs(y));
        }
    }

    atomicMax(peakIn_, peakIn);
    atomicMax(peakOut_, peakOut);
    if (clipped)
        clipped_.fetch_add(clipped, std::memory_order_relaxed);
    processed_.fetch_add(frames * static_cast<std::size_t>(channels), std::memory_order_relaxed);
}

ProcessorStats GainProcessor::takeStats() noexcept
{
    ProcessorStats s;
    s.peakIn = peakIn_.exchange(0.0f, std::memory_order_relaxed);
    s.peakOut = peakOut_.exchange(0.0f, std::memory_order_relaxed);
    s.clippedSamples = clipped_.exchange(0, std::memory_order_relaxed);
    s.processedSamples = processed_.exchange(0, std::memory_order_relaxed);
    return s;
}

} // namespace loudmixer
