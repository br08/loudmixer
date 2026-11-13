# ---------------------------------------------------------------------------
# loudmixer_dsp: DSP puro (sem Qt). Pode ser reutilizado em qualquer backend.
# ---------------------------------------------------------------------------
add_library(loudmixer_dsp STATIC
    core/GainProcessor.cpp
    core/GainProcessor.h
)
target_include_directories(loudmixer_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
loudmixer_set_warnings(loudmixer_dsp)

# ---------------------------------------------------------------------------
# loudmixer_core: núcleo audio-agnóstico (Qt Core) + backends.
# ---------------------------------------------------------------------------
add_library(loudmixer_core STATIC
    core/AudioTypes.h
    core/IAudioBackend.h
    core/AudioSessionManager.cpp
    core/AudioSessionManager.h
    core/PresetStore.cpp
    core/PresetStore.h
    core/AutoStartService.cpp
    core/AutoStartService.h
    backend/BackendFactory.cpp
    backend/BackendFactory.h
    backend/mock/MockBackend.cpp
    backend/mock/MockBackend.h
)
target_include_directories(loudmixer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(loudmixer_core PUBLIC loudmixer_dsp Qt6::Core)
set_target_properties(loudmixer_core PROPERTIES AUTOMOC ON)
loudmixer_set_warnings(loudmixer_core)

if(WIN32)
    target_sources(loudmixer_core PRIVATE
        backend/windows/ComUtil.h
        backend/windows/WasapiBackend.cpp
        backend/windows/WasapiBackend.h
        backend/windows/SessionEnumerator.cpp
        backend/windows/SessionEnumerator.h
        backend/windows/LoopbackCapture.cpp
        backend/windows/LoopbackCapture.h
        backend/windows/ApoRouteMath.h
        apo/ApoRegistry.h
        apo/ApoShared.h
    )
    target_compile_definitions(loudmixer_core PRIVATE LOUDMIXER_HAVE_WASAPI UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(loudmixer_core PUBLIC ole32 oleaut32 uuid avrt advapi32)
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    if(LOUDMIXER_WITH_PULSE)
        pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET GLOBAL libpulse)
        target_sources(loudmixer_core PRIVATE
            backend/linux/PulseBackend.cpp
            backend/linux/PulseBackend.h
            backend/linux/PulseBoostRoute.cpp
            backend/linux/PulseBoostRoute.h
        )
        target_compile_definitions(loudmixer_core PUBLIC LOUDMIXER_HAVE_PULSE)
        target_link_libraries(loudmixer_core PUBLIC PkgConfig::PULSE)
    endif()
    if(LOUDMIXER_WITH_PIPEWIRE)
        pkg_check_modules(PIPEWIRE IMPORTED_TARGET GLOBAL libpipewire-0.3)
        if(PIPEWIRE_FOUND)
            target_sources(loudmixer_core PRIVATE
                backend/linux/PipeWireBackend.cpp
                backend/linux/PipeWireBackend.h
            )
            target_compile_definitions(loudmixer_core PUBLIC LOUDMIXER_HAVE_PIPEWIRE)
            target_link_libraries(loudmixer_core PUBLIC PkgConfig::PIPEWIRE)
        else()
            message(STATUS "libpipewire-0.3 não encontrada: backend PipeWire nativo desativado")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Windows: APO de sistema (boost do dispositivo inteiro, sem cabo virtual)
# ---------------------------------------------------------------------------
if(WIN32)
    # A DLL roda dentro do audiodg.exe, que não acha as DLLs do MinGW: tudo
    # estático (só dependências do próprio Windows).
    add_library(loudmixer_apo SHARED
        apo/LoudMixerApo.cpp
        apo/ApoInterfaces.h
        apo/ApoShared.h
        apo/loudmixer_apo.def
    )
    set_target_properties(loudmixer_apo PROPERTIES PREFIX "" OUTPUT_NAME loudmixer_apo)
    target_include_directories(loudmixer_apo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(loudmixer_apo PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(loudmixer_apo PRIVATE loudmixer_dsp ole32 uuid advapi32)
    loudmixer_set_warnings(loudmixer_apo)

    add_executable(loudmixer-apo-setup apo/ApoSetup.cpp)
    target_include_directories(loudmixer-apo-setup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(loudmixer-apo-setup PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(loudmixer-apo-setup PRIVATE ole32 uuid advapi32 shell32)
    add_dependencies(loudmixer-apo-setup loudmixer_apo)
    loudmixer_set_warnings(loudmixer-apo-setup)

    if(MINGW)
        target_link_options(loudmixer_apo PRIVATE -static)
        target_link_options(loudmixer-apo-setup PRIVATE -static)
    endif()
endif()

# ---------------------------------------------------------------------------
# Aplicativo
# ---------------------------------------------------------------------------
if(NOT LOUDMIXER_BUILD_APP)
    return()
endif()

qt_add_executable(loudmixer WIN32
    main.cpp
    ui/AppIcons.cpp
    ui/AppIcons.h
    ui/BuildInfo.h
    ui/ChannelsPanel.cpp
    ui/ChannelsPanel.h
    ui/GainSlider.cpp
    ui/GainSlider.h
    ui/GlobalHotkeys.cpp
    ui/GlobalHotkeys.h
    ui/MixerWindow.cpp
    ui/MixerWindow.h
    ui/SettingsDialog.cpp
    ui/SettingsDialog.h
    ui/SingleInstance.cpp
    ui/SingleInstance.h
    ui/TrayController.cpp
    ui/TrayController.h
    ${PROJECT_SOURCE_DIR}/resources/loudmixer.qrc
)
set_target_properties(loudmixer PROPERTIES AUTOMOC ON AUTORCC ON OUTPUT_NAME loudmixer)
target_compile_definitions(loudmixer PRIVATE LOUDMIXER_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Debug>:LOUDMIXER_DEV_BUILD>) # "(dev)" no título e na bandeja
target_link_libraries(loudmixer PRIVATE loudmixer_core Qt6::Gui Qt6::Widgets Qt6::Network)
if(WIN32)
    target_sources(loudmixer PRIVATE ${PROJECT_SOURCE_DIR}/resources/loudmixer.rc)
    target_link_libraries(loudmixer PRIVATE user32)
endif()
loudmixer_set_warnings(loudmixer)

if(Qt6LinguistTools_FOUND)
    # Inclui também o núcleo e os backends (mensagens de erro marcadas com
    # LM_TRANSLATE_NOOP), não só os fontes do executável.
    file(GLOB_RECURSE LOUDMIXER_TR_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
    qt_add_translations(loudmixer
        TS_FILES ${PROJECT_SOURCE_DIR}/translations/loudmixer_pt_BR.ts
        SOURCES ${LOUDMIXER_TR_SOURCES}
        RESOURCE_PREFIX /i18n
        LUPDATE_OPTIONS -no-obsolete -locations none -tr-function-alias QT_TRANSLATE_NOOP+=LM_TRANSLATE_NOOP)
else()
    message(STATUS "Qt LinguistTools não encontrado: traduções não serão compiladas")
endif()

include(GNUInstallDirs)
install(TARGETS loudmixer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/loudmixer.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/loudmixer.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
