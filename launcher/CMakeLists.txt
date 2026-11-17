# Included by the patched ModernGekko project after it selects SDL3.
set(_refare_backends "${CMAKE_SOURCE_DIR}/vendor/dolphin/Externals/imgui/imgui/backends")
add_executable(refare-launcher WIN32 refare_launcher.cpp
  "${_refare_backends}/imgui_impl_sdl3.cpp"
  "${_refare_backends}/imgui_impl_sdlrenderer3.cpp")
target_compile_features(refare-launcher PRIVATE cxx_std_23)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ReFare.bmp" _refare_icon_hex HEX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ReFare.bmp")
string(REGEX REPLACE "(..)" "0x\\1," _refare_icon_bytes "${_refare_icon_hex}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../assets/branding/ReFareLogo.bmp" _refare_logo_hex HEX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/branding/ReFareLogo.bmp")
string(REGEX REPLACE "(..)" "0x\\1," _refare_logo_bytes "${_refare_logo_hex}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/refare_icon.h" "static const unsigned char refare_icon[] = {${_refare_icon_bytes}};\nstatic const unsigned char refare_logo[] = {${_refare_logo_bytes}};\n")
target_include_directories(refare-launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(WIN32)
  enable_language(RC)
  set(REFARE_WINDOWS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ReFare.ico")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ReFare.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/ReFare.rc" @ONLY)
  target_sources(refare-launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ReFare.rc")
  cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL target_sources moderngekko-run PRIVATE [[${CMAKE_CURRENT_BINARY_DIR}/ReFare.rc]])")
endif()
include(GNUInstallDirs)
install(TARGETS refare-launcher RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ReFare.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
  RENAME io.github.bittahbandit1.refare.desktop)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/icon-256.png"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps" RENAME io.github.bittahbandit1.refare.png)
target_include_directories(refare-launcher PRIVATE
  "${_refare_backends}" "${CMAKE_SOURCE_DIR}/vendor/picojson"
  "${CMAKE_SOURCE_DIR}/vendor/dolphin/Source/Core")
target_link_libraries(refare-launcher PRIVATE imgui common fmt::fmt ${_moderngekko_sdl_target})
set_target_properties(refare-launcher PROPERTIES OUTPUT_NAME ReFare
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MSVC)
  set(_refare_compiler msvc)
  target_compile_options(refare-launcher PRIVATE /Zc:preprocessor /utf-8)
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  set(_refare_compiler gcc)
else()
  set(_refare_compiler clang)
endif()
target_compile_definitions(refare-launcher PRIVATE
  REFARE_LAUNCHER_COMPILER="${_refare_compiler}" WIN32_LEAN_AND_MEAN NOMINMAX)

# Ship only explicit open-source inputs, never an extraction, module cache,
# or a generated game directory. The launcher is relocatable from the outset.
set(_refare_root "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(_refare_runtime "${CMAKE_SOURCE_DIR}/vendor/dolphin")
set(_refare_output "$<TARGET_FILE_DIR:refare-launcher>/refare-data")
add_custom_target(refare-launcher-resources
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "${_refare_output}/scripts" "${_refare_output}/config"
    "${_refare_output}/toolchain/vendor/dolphin/module-template"
    "${_refare_output}/toolchain/vendor/dolphin/GXRuntime/src"
    "${_refare_output}/toolchain/vendor/dolphin/Source/Core/Core/PowerPC"
    "${_refare_output}/toolchain/vendor/dolphin/Data/Sys/GameSettings"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/launcher_backend.py" "${_refare_output}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${_refare_root}/assets/branding/ReFareUI.ttf" "${_refare_output}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${_refare_root}/assets/branding/NOTICE.txt" "${_refare_output}/ReFareBranding.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${_refare_root}/scripts/ciso-to-iso.py" "${_refare_root}/scripts/prepare-game.py"
    "${_refare_output}/scripts"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${_refare_root}/config/GFX.ini" "${_refare_root}/config/startup-patches.json"
    "${_refare_output}/config"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${_refare_runtime}/module-template/CMakeLists.txt"
    "${_refare_runtime}/module-template/gen_module_tables.py"
    "${_refare_runtime}/module-template/module_export.c"
    "${_refare_runtime}/module-template/module.exports"
    "${_refare_output}/toolchain/vendor/dolphin/module-template"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${_refare_runtime}/GXRuntime/include"
    "${_refare_output}/toolchain/vendor/dolphin/GXRuntime/include"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${_refare_runtime}/GXRuntime/src/core"
    "${_refare_output}/toolchain/vendor/dolphin/GXRuntime/src/core"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${_refare_runtime}/Source/Core/Core/PowerPC/StaticRecomp"
    "${_refare_output}/toolchain/vendor/dolphin/Source/Core/Core/PowerPC/StaticRecomp"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${_refare_root}/LICENSE" "${_refare_output}"
  VERBATIM)
add_dependencies(refare-launcher refare-launcher-resources moderngekko-port)
