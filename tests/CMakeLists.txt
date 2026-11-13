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

# Divisão de ganho da rota APO (matemática pura: roda em todas as plataformas).
add_executable(ApoRouteMathTest ApoRouteMathTest.cpp)
target_include_directories(ApoRouteMathTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ApoRouteMathTest PRIVATE GTest::gtest GTest::gtest_main)
loudmixer_set_warnings(ApoRouteMathTest)
gtest_discover_tests(ApoRouteMathTest)

# APO do Windows: carrega a DLL real e a exercita como o mecanismo de áudio.
if(TARGET loudmixer_apo)
    add_executable(ApoTest ApoTest.cpp)
    target_include_directories(ApoTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(ApoTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        "LOUDMIXER_APO_DLL=\"$<TARGET_FILE:loudmixer_apo>\"")
    target_link_libraries(ApoTest PRIVATE GTest::gtest GTest::gtest_main ole32 uuid)
    add_dependencies(ApoTest loudmixer_apo)
    loudmixer_set_warnings(ApoTest)
    gtest_discover_tests(ApoTest)

    # Rota APO ponta a ponta contra o áudio real (pula sem LOUDMIXER_LIVE_TESTS=1).
    add_executable(WasapiApoLiveTest WasapiApoLiveTest.cpp)
    target_link_libraries(WasapiApoLiveTest PRIVATE loudmixer_core GTest::gtest GTest::gtest_main)
    target_compile_definitions(WasapiApoLiveTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    loudmixer_set_warnings(WasapiApoLiveTest)
    gtest_discover_tests(WasapiApoLiveTest)
endif()

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
