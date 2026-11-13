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
# Valores passados literalmente ao gerador (os comandos NSIS abaixo têm
# barras invertidas e "$" que não devem ser reinterpretados pelo CMake).
set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
    # Copia as DLLs do Qt e os plugins necessários (windeployqt) na instalação.
    # FILENAME_VARIABLE foi trocado por OUTPUT_SCRIPT no Qt 6.5 e removido depois.
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
        qt_generate_deploy_app_script(
            TARGET loudmixer
            OUTPUT_SCRIPT loudmixer_deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR)
    else()
        qt_generate_deploy_app_script(
            TARGET loudmixer
            FILENAME_VARIABLE loudmixer_deploy_script
            NO_UNSUPPORTED_PLATFORM_ERROR)
    endif()
    install(SCRIPT ${loudmixer_deploy_script})

    if(MINGW)
        # O windeployqt copia o Qt e o runtime do GCC, mas não as outras DLLs de
        # que o Qt do MSYS2 depende (ICU, zstd, harfbuzz, freetype...). Sem isto
        # o instalador só funcionaria em máquinas com o MSYS2 no PATH.
        get_filename_component(LOUDMIXER_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        install(CODE "
            if(POLICY CMP0207)
                cmake_policy(SET CMP0207 NEW)
            endif()
            set(_root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
            file(GLOB_RECURSE _exes \"\${_root}/bin/*.exe\")
            file(GLOB_RECURSE _dlls \"\${_root}/*.dll\")
            # bin/ primeiro: os plugins devem enxergar as DLLs já copiadas pelo
            # windeployqt, e não as do MSYS2 (senão o CMake acusa conflito).
            file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES \${_exes}
                LIBRARIES \${_dlls}
                DIRECTORIES \"\${_root}/bin\" \"${LOUDMIXER_MINGW_BIN}\"
                RESOLVED_DEPENDENCIES_VAR _resolved
                UNRESOLVED_DEPENDENCIES_VAR _unresolved
                PRE_EXCLUDE_REGEXES \"^api-ms-\" \"^ext-ms-\"
                POST_EXCLUDE_REGEXES \"[Ss][Yy][Ss][Tt][Ee][Mm]32\")
            file(REAL_PATH \"\${_root}\" _root_real)
            foreach(_dep IN LISTS _resolved)
                file(REAL_PATH \"\${_dep}\" _dep_real)
                string(FIND \"\${_dep_real}\" \"\${_root_real}/\" _inside)
                if(NOT _inside EQUAL 0)
                    file(INSTALL \"\${_dep}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/bin\")
                endif()
            endforeach()
            if(_unresolved)
                message(STATUS \"DLLs não resolvidas (devem ser do sistema): \${_unresolved}\")
            endif()
        ")
    endif()

    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "LoudMixer")
    set(CPACK_NSIS_PACKAGE_NAME "LoudMixer ${PROJECT_VERSION}")
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/resources/icons/loudmixer.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/resources/icons/loudmixer.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\loudmixer.exe")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "loudmixer.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    # APO (boost do sistema). O audiodg mantém o loudmixer_apo.dll carregado
    # enquanto o boost está ativo em algum dispositivo: o arquivo não pode ser
    # sobrescrito nem apagado, mas pode ser renomeado. O renomeado some no
    # próximo boot.
    set(LOUDMIXER_NSIS_MOVE_APO_ASIDE [=[
  IfFileExists "$INSTDIR\bin\loudmixer_apo.dll" 0 +5
    GetTempFileName $R0 "$INSTDIR\bin"
    Delete "$R0"
    Rename "$INSTDIR\bin\loudmixer_apo.dll" "$R0"
    Delete /REBOOTOK "$R0"
]=])
    # Atualização: o DLL novo vai para o mesmo caminho registrado; o boost
    # dos dispositivos continua ativo e passa a usar o DLL novo quando o
    # áudio reiniciar.
    set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "${LOUDMIXER_NSIS_MOVE_APO_ASIDE}")
    # Nos comandos NSIS, comentários usam "#": o ";" separa itens de lista no CMake.
    # Desinstalação: tira o APO de todos os dispositivos e restaura o áudio
    # como era, antes de apagar os arquivos (a ferramenta ainda existe). Na
    # atualização, o instalador novo roda este desinstalador em modo silencioso
    # e no próprio lugar ($EXEDIR = $INSTDIR): aí o boost é mantido.
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
  IfSilent 0 lm_apo_remove
  StrCmp "$EXEDIR" "$INSTDIR" 0 lm_apo_remove
]=] "${LOUDMIXER_NSIS_MOVE_APO_ASIDE}" [=[
  Goto lm_apo_done
lm_apo_remove:
  IfFileExists "$INSTDIR\bin\loudmixer-apo-setup.exe" 0 +2
    ExecWait '"$INSTDIR\bin\loudmixer-apo-setup.exe" uninstall all'
  # Sobra de atualização com o boost ativo (DLL antiga renomeada para *.tmp):
  # apaga a pasta bin (sempre nossa) agora e agenda para o boot o que ainda
  # estiver em uso. A pasta de instalação nunca leva /r (pode ter sido
  # escolhida pelo usuário). MoveFileEx só a remove no boot se estiver vazia.
  IfFileExists "$INSTDIR\bin\*.tmp" 0 +3
    RMDir /r /REBOOTOK "$INSTDIR\bin"
    System::Call 'kernel32::MoveFileExW(w "$INSTDIR", p 0, i 4)'

  # A subchave do instalador (Software\<vendor>\<chave>) só seria apagada
  # depois: apaga antes, para a chave-mãe, compartilhada com o APO, sair vazia.
  DeleteRegKey HKLM "Software\LoudMixer\LoudMixer"
  DeleteRegKey /ifempty HKLM "Software\LoudMixer"
lm_apo_done:
  # Remove a entrada de auto-start.
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "LoudMixer"
]=])
    string(JOIN "" CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS ${CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS})
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "LoudMixer maintainers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire-pulse | pulseaudio")
endif()

include(CPack)
