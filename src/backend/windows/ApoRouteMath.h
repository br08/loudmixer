#pragma once
// Divisão de ganho entre o volume nativo do Windows e o APO do LoudMixer.
// Sem dependência de Windows: testável em qualquer plataforma.
//
// Dispositivo e canais: ganho efetivo = nativo × fator do APO.
//   O LoudMixer grava nativo = mín(g, 1) e fator = máx(g, 1). Assim, até 100%
//   o volume do Windows (teclas de volume, painel) segue o slider; acima disso
//   o APO completa. Se o volume nativo mudar por fora, o efetivo mostrado é
//   nativo × fator.
//
// Apps: o APO aplica um ganho único na mistura. Para um app pedir mais de
//   100%, o APO sobe pelo maior pedido (B) e cada app é atenuado no volume
//   nativo da sessão para pedido / B. O ganho mestre do APO é fator × B,
//   limitado a 200% (o máximo do GainProcessor).

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace loudmixer::apoRoute {

constexpr double kMax = 2.0;

struct Split {
    double native = 1.0; // 0..1, vai para o volume do Windows
    double factor = 1.0; // 1..2, vai para o APO
};

inline double clampGain(double g)
{
    if (!(g >= 0.0)) // NaN ou negativo
        return 0.0;
    return std::min(g, kMax);
}

inline Split split(double gain)
{
    gain = clampGain(gain);
    return {std::min(gain, 1.0), std::max(gain, 1.0)};
}

inline double combine(double native, double factor) { return clampGain(native * factor); }

// B: o maior ganho pedido entre os apps (no mínimo 1).
inline double appBoost(const std::map<std::string, double>& desired)
{
    double b = 1.0;
    for (const auto& [id, g] : desired)
        b = std::max(b, clampGain(g));
    return b;
}

inline double sessionNative(double desired, double boost) { return std::min(1.0, clampGain(desired) / boost); }

inline double apoMaster(double deviceFactor, double boost) { return std::min(kMax, deviceFactor * boost); }

// Ganho pedido de uma sessão a partir do volume nativo observado. Se o nativo
// bate com o que o LoudMixer gravou, mantém o pedido; se não, alguém mexeu no
// mixer do Windows e o novo valor é respeitado.
inline double observeSession(double native, double desired, double boost)
{
    if (std::fabs(native - sessionNative(desired, boost)) < 0.01)
        return clampGain(desired);
    return clampGain(native * boost);
}

// Fator de dispositivo a partir do mestre do APO e de B (ao reabrir o app).
inline double deviceFactorFrom(double master, double boost)
{
    if (!(boost >= 1.0))
        boost = 1.0;
    return std::clamp(clampGain(master) / boost, 1.0, kMax);
}

} // namespace loudmixer::apoRoute
