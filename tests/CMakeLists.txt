find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GoogleTest não encontrado no sistema: baixando via FetchContent")
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_library(GTest::gtest ALIAS gtest)
    add_library(GTest::gtest_main ALIAS gtest_main)
endif()

include(GoogleTest)

# DSP puro: não depende de Qt.
add_executable(GainProcessorTest GainProcessorTest.cpp)
target_link_libraries(GainProcessorTest PRIVATE loudmixer_dsp GTest::gtest GTest::gtest_main)
loudmixer_set_warnings(GainProcessorTest)
gtest_discover_tests(GainProcessorTest)

# Núcleo Qt + MockBackend.
add_executable(MockBackendTest MockBackendTest.cpp)
target_link_libraries(MockBackendTest PRIVATE loudmixer_core GTest::gtest)
loudmixer_set_warnings(MockBackendTest)
gtest_discover_tests(MockBackendTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Integração com o servidor real (pula sozinho sem LOUDMIXER_LIVE_TESTS=1).
if(TARGET PkgConfig::PULSE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSE_SIMPLE IMPORTED_TARGET libpulse-simple)
    if(PULSE_SIMPLE_FOUND)
        add_executable(PulseBackendLiveTest PulseBackendLiveTest.cpp)
        target_link_libraries(PulseBackendLiveTest PRIVATE loudmixer_core PkgConfig::PULSE_SIMPLE GTest::gtest GTest::gtest_main)
        loudmixer_set_warnings(PulseBackendLiveTest)
        gtest_discover_tests(PulseBackendLiveTest)
    endif()
endif()
