# Included by the patched ModernGekko project after it selects SDL3.
set(_refare_backends "${CMAKE_SOURCE_DIR}/vendor/dolphin/Externals/imgui/imgui/backends")
add_executable(refare-launcher WIN32 refare_launcher.cpp
  "${_refare_backends}/imgui_impl_sdl3.cpp"
  "${_refare_backends}/imgui_impl_sdlrenderer3.cpp")
target_compile_features(refare-launcher PRIVATE cxx_std_23)
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
