cmake_minimum_required(VERSION 3.24)

project(LoudMixer
    VERSION 0.1.0
    DESCRIPTION "Volume booster per app, device and channel"
    HOMEPAGE_URL "https://github.com/loudmixer/loudmixer"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(LOUDMIXER_BUILD_TESTS "Compilar os testes unitários" ON)
option(LOUDMIXER_BUILD_APP "Compilar o aplicativo Qt (desligue para só testar o DSP)" ON)
option(LOUDMIXER_WITH_PULSE "Linux: backend libpulse (PulseAudio e pipewire-pulse)" ON)
option(LOUDMIXER_WITH_PIPEWIRE "Linux: backend PipeWire nativo (experimental)" ON)
option(LOUDMIXER_WARNINGS_AS_ERRORS "Tratar avisos como erros" OFF)

include(cmake/CompilerWarnings.cmake)

# Qt 6.4 é o mínimo testado (Debian 12 / Ubuntu 22.04+). O plano previa 6.5+,
# mas nenhuma API usada exige 6.5.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
if(LOUDMIXER_BUILD_APP)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Widgets Network)
    find_package(Qt6 6.4 QUIET COMPONENTS LinguistTools)
endif()

qt_standard_project_setup()

add_subdirectory(src)

if(LOUDMIXER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
