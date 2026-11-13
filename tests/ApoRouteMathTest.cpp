// Matemática da rota APO (sem Windows: roda em todas as plataformas).

#include "backend/windows/ApoRouteMath.h"

#include <gtest/gtest.h>

#include <limits>

using namespace loudmixer::apoRoute;

TEST(ApoRouteMath, SplitsBelowAndAboveUnity)
{
    EXPECT_DOUBLE_EQ(split(0.5).native, 0.5);
    EXPECT_DOUBLE_EQ(split(0.5).factor, 1.0);
    EXPECT_DOUBLE_EQ(split(1.5).native, 1.0);
    EXPECT_DOUBLE_EQ(split(1.5).factor, 1.5);
    EXPECT_DOUBLE_EQ(split(9.0).factor, 2.0);
    EXPECT_DOUBLE_EQ(split(-1.0).native, 0.0);
    EXPECT_DOUBLE_EQ(split(std::numeric_limits<double>::quiet_NaN()).native, 0.0);
    for (double g : {0.0, 0.3, 1.0, 1.25, 2.0})
        EXPECT_DOUBLE_EQ(combine(split(g).native, split(g).factor), g);
}

TEST(ApoRouteMath, ExternalNativeChangeShowsEffectiveGain)
{
    // Boost em 150% e o usuário baixa o volume do Windows para 50%.
    EXPECT_DOUBLE_EQ(combine(0.5, split(1.5).factor), 0.75);
}

TEST(ApoRouteMath, AppAboveUnityAttenuatesOthers)
{
    const std::map<std::string, double> desired{{"spotify", 1.8}, {"discord", 1.0}, {"game", 0.5}};
    const double b = appBoost(desired);
    EXPECT_DOUBLE_EQ(b, 1.8);
    // O que se ouve = nativo da sessão × B
    EXPECT_NEAR(sessionNative(1.8, b) * b, 1.8, 1e-12);
    EXPECT_NEAR(sessionNative(1.0, b) * b, 1.0, 1e-12);
    EXPECT_NEAR(sessionNative(0.5, b) * b, 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(apoMaster(1.0, b), 1.8);
}

TEST(ApoRouteMath, NoAppAboveUnityMeansNoBoost)
{
    EXPECT_DOUBLE_EQ(appBoost({}), 1.0);
    EXPECT_DOUBLE_EQ(appBoost({{"a", 0.7}, {"b", 1.0}}), 1.0);
    EXPECT_DOUBLE_EQ(sessionNative(0.7, 1.0), 0.7);
}

TEST(ApoRouteMath, MasterIsCappedAt200Percent)
{
    EXPECT_DOUBLE_EQ(apoMaster(1.5, 1.8), 2.0); // 270% pedido, APO limita
    EXPECT_DOUBLE_EQ(apoMaster(1.2, 1.5), 1.8);
}

TEST(ApoRouteMath, ObserveSessionKeepsOrAcceptsExternalChange)
{
    // Nativo igual ao que gravamos: mantém o pedido (acima de 100%).
    EXPECT_DOUBLE_EQ(observeSession(1.0, 1.8, 1.8), 1.8);
    EXPECT_DOUBLE_EQ(observeSession(0.5, 0.9, 1.8), 0.9);
    // Alguém mudou no mixer do Windows: efetivo novo = nativo × B.
    EXPECT_NEAR(observeSession(0.25, 0.9, 1.8), 0.45, 1e-12);
}

TEST(ApoRouteMath, ReconstructsDeviceFactorOnRestart)
{
    EXPECT_DOUBLE_EQ(deviceFactorFrom(1.8, 1.8), 1.0);  // só apps em boost
    EXPECT_DOUBLE_EQ(deviceFactorFrom(1.8, 1.2), 1.5);  // dispositivo 150% × B 1.2
    EXPECT_DOUBLE_EQ(deviceFactorFrom(1.5, 0.0), 1.5);  // B desconhecido
    EXPECT_DOUBLE_EQ(deviceFactorFrom(0.5, 1.0), 1.0);  // fator nunca abaixo de 1
}
