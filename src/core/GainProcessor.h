#pragma once

// GainProcessor — o coração DSP do LoudMixer.
//
// C++ puro (sem Qt), seguro para rodar na thread de áudio em tempo real:
// não aloca, não trava mutex, não faz I/O. Parâmetros são atômicos e podem
// ser alterados de qualquer thread enquanto o áudio é processado.
//
// Pipeline por amostra:   y = clip(x * ganhoMestre * ganhoCanal)
//
//  - Mudanças de ganho são suavizadas por bloco (rampa linear) para evitar
//    "zipper noise".
//  - Com ganho efetivo <= 1.0 o sinal passa intocado (apenas um clamp de
//    segurança em ±1.0). Acima disso, aplica-se o soft-clip escolhido.
//  - O soft-clip é linear até kKnee (0.8 ≈ -1.9 dBFS) e satura suavemente
//    até 1.0, de modo que sinais baixos não ganham distorção alguma.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace loudmixer {

enum class ClipCurve : int {
    Hard = 0,  // clamp em ±1.0 (sem soft-clip)
    Tanh = 1,  // joelho com tangente hiperbólica (mais "analógico")
    Cubic = 2, // joelho polinomial cúbico (mais barato)
};

struct ProcessorStats {
    float peakIn = 0.0f;              // pico após o ganho e antes do clip (pode passar de 1.0)
    float peakOut = 0.0f;             // pico na saída (sempre <= 1.0)
    std::uint64_t clippedSamples = 0; // amostras que teriam passado de ±1.0
    std::uint64_t processedSamples = 0;
};

class GainProcessor {
public:
    static constexpr int kMaxChannels = 32;
    static constexpr float kKnee = 0.8f;
    static constexpr float kMaxGain = 2.0f;

    explicit GainProcessor(float gain = 1.0f, ClipCurve curve = ClipCurve::Tanh) noexcept;

    GainProcessor(const GainProcessor&) = delete;
    GainProcessor& operator=(const GainProcessor&) = delete;

    // --- Parâmetros (qualquer thread) --------------------------------------
    void setGain(float gain) noexcept; // limitado a [0, kMaxGain]
    float gain() const noexcept;

    void setChannelGain(int channel, float gain) noexcept; // [0, kMaxGain]
    float channelGain(int channel) const noexcept;

    void setMuted(bool muted) noexcept;
    bool muted() const noexcept;

    void setCurve(ClipCurve curve) noexcept;
    ClipCurve curve() const noexcept;

    // --- Processamento (thread de áudio) -----------------------------------
    // Buffers float32 intercalados (L R L R ...). `frames` = amostras por canal.
    void process(float* samples, std::size_t frames, int channels) noexcept;
    void process(const float* in, float* out, std::size_t frames, int channels) noexcept;

    // Descarta o estado da rampa (o próximo bloco começa direto no ganho alvo).
    void resetRamp() noexcept;

    // --- Medição (qualquer thread) -----------------------------------------
    // Devolve as estatísticas acumuladas desde a última chamada e as zera.
    ProcessorStats takeStats() noexcept;

    // Função de transferência do clipper, exposta para testes e para a UI.
    static float clipSample(float x, ClipCurve curve) noexcept;

private:
    static float clampParam(float g) noexcept;
    static void atomicMax(std::atomic<float>& target, float value) noexcept;

    std::atomic<float> gain_;
    std::array<std::atomic<float>, kMaxChannels> channelGains_;
    std::atomic<bool> muted_{false};
    std::atomic<int> curve_;
    std::atomic<bool> resetRequested_{true};

    // Estado privado da thread de áudio.
    std::array<float, kMaxChannels> currentGain_{};

    std::atomic<float> peakIn_{0.0f};
    std::atomic<float> peakOut_{0.0f};
    std::atomic<std::uint64_t> clipped_{0};
    std::atomic<std::uint64_t> processed_{0};
};

} // namespace loudmixer
