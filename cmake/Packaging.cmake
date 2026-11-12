# Empacotamento com CPack.
#   Linux:   cpack -G DEB   (ou TGZ). AppImage: packaging/linux/build-appimage.sh
#   Windows: cpack -G NSIS  (instalador) ou ZIP

if(NOT TARGET loudmixer)
    return()
endif()

set(CPACK_PACKAGE_NAME "loudmixer")
set(CPACK_PACKAGE_VENDOR "LoudMixer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Volume booster per app, device and channel (up to 200% with soft-clipping)")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "LoudMixer")
set(CPACK_PACKAGE_EXECUTABLES "loudmixer;LoudMixer")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    # Copia as DLLs do Qt e os plugins necessários (windeployqt) na instalação.
    qt_generate_deploy_app_script(
        TARGET loudmixer
        FILENAME_VARIABLE loudmixer_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${loudmixer_deploy_script})

    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "LoudMixer")
    set(CPACK_NSIS_PACKAGE_NAME "LoudMixer ${PROJECT_VERSION}")
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/resources/icons/loudmixer.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/resources/icons/loudmixer.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\loudmixer.exe")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "loudmixer.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    # Remove a entrada de auto-start ao desinstalar.
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "DeleteRegValue HKCU 'Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run' 'LoudMixer'")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "LoudMixer maintainers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire-pulse | pulseaudio")
endif()

include(CPack)
