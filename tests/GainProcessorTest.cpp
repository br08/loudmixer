#include "core/GainProcessor.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>
#include <thread>
#include <vector>

using loudmixer::ClipCurve;
using loudmixer::GainProcessor;

namespace {

std::vector<float> sine(std::size_t frames, int channels, float amplitude, float freq = 440.0f, float rate = 48000.0f)
{
    std::vector<float> buf(frames * static_cast<std::size_t>(channels));
    for (std::size_t f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
            buf[f * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]
                = amplitude * std::sin(2.0f * 3.14159265f * freq * static_cast<float>(f) / rate);
    return buf;
}

float maxAbs(const std::vector<float>& v)
{
    float m = 0.0f;
    for (float x : v)
        m = std::max(m, std::fabs(x));
    return m;
}

constexpr ClipCurve kAllCurves[] = {ClipCurve::Hard, ClipCurve::Tanh, ClipCurve::Cubic};

} // namespace

// --- Parâmetros --------------------------------------------------------------

TEST(GainProcessor, GainIsClampedToValidRange)
{
    GainProcessor p;
    p.setGain(5.0f);
    EXPECT_FLOAT_EQ(p.gain(), 2.0f);
    p.setGain(-1.0f);
    EXPECT_FLOAT_EQ(p.gain(), 0.0f);
    p.setGain(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(p.gain(), 0.0f);
    p.setGain(1.5f);
    EXPECT_FLOAT_EQ(p.gain(), 1.5f);
}

TEST(GainProcessor, ChannelGainOutOfRangeIndexIsIgnored)
{
    GainProcessor p;
    p.setChannelGain(-1, 0.5f);
    p.setChannelGain(GainProcessor::kMaxChannels, 0.5f);
    EXPECT_FLOAT_EQ(p.channelGain(-1), 1.0f);
    EXPECT_FLOAT_EQ(p.channelGain(GainProcessor::kMaxChannels), 1.0f);
    p.setChannelGain(1, 3.0f);
    EXPECT_FLOAT_EQ(p.channelGain(1), 2.0f);
}

// --- Transparência -----------------------------------------------------------

TEST(GainProcessor, UnityGainIsBitExactPassthrough)
{
    for (ClipCurve curve : kAllCurves) {
        GainProcessor p(1.0f, curve);
        auto in = sine(1024, 2, 0.999f);
        auto out = in;
        p.process(out.data(), 1024, 2);
        EXPECT_EQ(in, out) << "curve " << static_cast<int>(curve);
    }
}

TEST(GainProcessor, AttenuationIsLinear)
{
    GainProcessor p(0.5f);
    auto in = sine(512, 2, 0.9f);
    std::vector<float> out(in.size());
    p.process(in.data(), out.data(), 512, 2);
    for (std::size_t i = 0; i < in.size(); ++i)
        EXPECT_FLOAT_EQ(out[i], in[i] * 0.5f);
}

TEST(GainProcessor, BoostIsLinearBelowKnee)
{
    // 0.3 * 2.0 = 0.6 < kKnee (0.8): nenhuma distorção.
    for (ClipCurve curve : kAllCurves) {
        GainProcessor p(2.0f, curve);
        auto in = sine(512, 1, 0.3f);
        auto out = in;
        p.process(out.data(), 512, 1);
        for (std::size_t i = 0; i < in.size(); ++i)
            EXPECT_FLOAT_EQ(out[i], in[i] * 2.0f);
    }
}

TEST(GainProcessor, InPlaceAndOutOfPlaceAreEquivalent)
{
    GainProcessor a(1.7f), b(1.7f);
    auto in = sine(777, 3, 0.8f);
    auto inPlace = in;
    std::vector<float> outOfPlace(in.size());
    a.process(inPlace.data(), 777, 3);
    b.process(in.data(), outOfPlace.data(), 777, 3);
    EXPECT_EQ(inPlace, outOfPlace);
}

// --- Soft-clip --------------------------------------------------------------

TEST(GainProcessor, OutputNeverExceedsFullScale)
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (ClipCurve curve : kAllCurves) {
        GainProcessor p(2.0f, curve);
        std::vector<float> buf(4096 * 2);
        for (float& x : buf)
            x = dist(rng);
        p.process(buf.data(), 4096, 2);
        EXPECT_LE(maxAbs(buf), 1.0f) << "curve " << static_cast<int>(curve);
    }
}

TEST(GainProcessor, ClipCurvesAreMonotonicContinuousAndOdd)
{
    for (ClipCurve curve : kAllCurves) {
        float prev = GainProcessor::clipSample(-4.0f, curve);
        for (int i = -3999; i <= 4000; ++i) {
            const float x = static_cast<float>(i) / 1000.0f;
            const float y = GainProcessor::clipSample(x, curve);
            EXPECT_GE(y, prev) << "não monotônica em x=" << x;
            EXPECT_LE(y - prev, 0.0011f) << "salto em x=" << x; // passo 0.001, derivada <= 1
            EXPECT_FLOAT_EQ(GainProcessor::clipSample(-x, curve), -y);
            EXPECT_LE(std::fabs(y), 1.0f);
            prev = y;
        }
    }
}

TEST(GainProcessor, SoftCurvesApproachFullScaleSmoothly)
{
    // Logo acima do joelho a curva ainda é praticamente linear...
    for (ClipCurve curve : {ClipCurve::Tanh, ClipCurve::Cubic}) {
        const float k = GainProcessor::kKnee;
        EXPECT_NEAR(GainProcessor::clipSample(k + 0.001f, curve), k + 0.001f, 1e-5f);
        // ...e satura perto de 1.0 para entradas grandes.
        EXPECT_GT(GainProcessor::clipSample(2.0f, curve), 0.99f);
        EXPECT_LE(GainProcessor::clipSample(2.0f, curve), 1.0f);
    }
    EXPECT_FLOAT_EQ(GainProcessor::clipSample(1.5f, ClipCurve::Hard), 1.0f);
    EXPECT_FLOAT_EQ(GainProcessor::clipSample(-1.5f, ClipCurve::Hard), -1.0f);
}

TEST(GainProcessor, NaNInputBecomesSilence)
{
    GainProcessor p(2.0f);
    float buf[2] = {std::numeric_limits<float>::quiet_NaN(), 0.25f};
    p.process(buf, 1, 2);
    EXPECT_EQ(buf[0], 0.0f);
    EXPECT_FLOAT_EQ(buf[1], 0.5f);

    GainProcessor unity(1.0f);
    float buf2[1] = {std::numeric_limits<float>::quiet_NaN()};
    unity.process(buf2, 1, 1);
    EXPECT_EQ(buf2[0], 0.0f);
}

TEST(GainProcessor, OverRangeInputIsClampedEvenAtUnity)
{
    // Mixagens em float podem passar de 1.0; nunca devolvemos isso à placa.
    GainProcessor p(1.0f);
    float buf[2] = {1.7f, -3.0f};
    p.process(buf, 1, 2);
    EXPECT_FLOAT_EQ(buf[0], 1.0f);
    EXPECT_FLOAT_EQ(buf[1], -1.0f);
}

// --- Rampa ------------------------------------------------------------------

TEST(GainProcessor, GainChangesAreRampedAcrossTheBlock)
{
    GainProcessor p(1.0f);
    std::vector<float> warm(64, 0.1f);
    p.process(warm.data(), 64, 1); // fixa o ganho atual em 1.0

    p.setGain(0.0f);
    std::vector<float> buf(100, 0.5f);
    p.process(buf.data(), 100, 1);
    // Desce de forma monotônica e termina exatamente no alvo.
    for (std::size_t i = 1; i < buf.size(); ++i)
        EXPECT_LE(buf[i], buf[i - 1]);
    EXPECT_GT(buf.front(), 0.49f * 0.98f);
    EXPECT_FLOAT_EQ(buf.back(), 0.0f);

    // Próximo bloco já no ganho final, sem rampa.
    std::vector<float> after(10, 0.5f);
    p.process(after.data(), 10, 1);
    for (float x : after)
        EXPECT_FLOAT_EQ(x, 0.0f);
}

TEST(GainProcessor, ResetRampJumpsStraightToTarget)
{
    GainProcessor p(1.0f);
    std::vector<float> warm(8, 0.1f);
    p.process(warm.data(), 8, 1);
    p.setGain(0.5f);
    p.resetRamp();
    std::vector<float> buf(8, 0.4f);
    p.process(buf.data(), 8, 1);
    for (float x : buf)
        EXPECT_FLOAT_EQ(x, 0.2f);
}

// --- Canais e mute ----------------------------------------------------------

TEST(GainProcessor, PerChannelGainsAreApplied)
{
    GainProcessor p(1.0f);
    p.setChannelGain(0, 0.5f);
    p.setChannelGain(1, 1.5f);
    std::vector<float> buf = {0.2f, 0.2f, 0.4f, 0.4f};
    p.process(buf.data(), 2, 2);
    EXPECT_FLOAT_EQ(buf[0], 0.1f);
    EXPECT_FLOAT_EQ(buf[1], 0.3f);
    EXPECT_FLOAT_EQ(buf[2], 0.2f);
    EXPECT_FLOAT_EQ(buf[3], 0.6f);
}

TEST(GainProcessor, MuteProducesSilence)
{
    GainProcessor p(1.8f);
    p.setMuted(true);
    p.resetRamp();
    auto buf = sine(256, 2, 0.9f);
    p.process(buf.data(), 256, 2);
    EXPECT_EQ(maxAbs(buf), 0.0f);
    EXPECT_TRUE(p.muted());
}

TEST(GainProcessor, ChannelsBeyondMaxArePassedThrough)
{
    const int ch = GainProcessor::kMaxChannels + 2;
    GainProcessor p(0.5f);
    std::vector<float> buf(static_cast<std::size_t>(ch), 0.4f);
    p.process(buf.data(), 1, ch);
    EXPECT_FLOAT_EQ(buf[0], 0.2f);
    EXPECT_FLOAT_EQ(buf[static_cast<std::size_t>(ch - 1)], 0.4f);
}

TEST(GainProcessor, DegenerateInputsAreNoOps)
{
    GainProcessor p(2.0f);
    float x = 0.5f;
    p.process(&x, 0, 1);
    p.process(&x, 1, 0);
    p.process(nullptr, 1, 1);
    EXPECT_FLOAT_EQ(x, 0.5f);
}

// --- Medição ----------------------------------------------------------------

TEST(GainProcessor, StatsReportPeaksAndClipping)
{
    GainProcessor p(2.0f);
    std::vector<float> buf = {0.1f, 0.6f, -0.7f, 0.2f}; // 1.2 e -1.4 passam de 1.0
    p.process(buf.data(), 4, 1);
    auto s = p.takeStats();
    EXPECT_NEAR(s.peakIn, 1.4f, 1e-6f);
    EXPECT_LE(s.peakOut, 1.0f);
    EXPECT_GT(s.peakOut, 0.9f);
    EXPECT_EQ(s.clippedSamples, 2u);
    EXPECT_EQ(s.processedSamples, 4u);

    // takeStats zera os contadores.
    auto empty = p.takeStats();
    EXPECT_EQ(empty.clippedSamples, 0u);
    EXPECT_EQ(empty.processedSamples, 0u);
    EXPECT_EQ(empty.peakIn, 0.0f);
}

TEST(GainProcessor, NoClippingReportedForQuietSignal)
{
    GainProcessor p(2.0f);
    auto buf = sine(4800, 2, 0.4f);
    p.process(buf.data(), 4800, 2);
    EXPECT_EQ(p.takeStats().clippedSamples, 0u);
}

// --- Concorrência -----------------------------------------------------------

TEST(GainProcessor, ParametersCanChangeWhileProcessing)
{
    // Não verifica valores: garante ausência de travamento/UB detectável
    // (rode com -fsanitize=thread para checar data races).
    GainProcessor p(1.0f);
    std::atomic<bool> done{false};
    std::thread ui([&] {
        for (int i = 0; i < 20000; ++i) {
            p.setGain(static_cast<float>(i % 200) / 100.0f);
            p.setCurve(static_cast<ClipCurve>(i % 3));
            p.setMuted(i % 7 == 0);
            (void)p.takeStats();
        }
        done = true;
    });
    auto buf = sine(256, 2, 0.9f);
    while (!done) {
        p.process(buf.data(), 256, 2);
        ASSERT_LE(maxAbs(buf), 1.0f);
    }
    ui.join();
}
