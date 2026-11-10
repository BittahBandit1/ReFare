cmake_minimum_required(VERSION 3.25)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum supported iOS version")
if(NOT TARGET ModernGekko::Runtime)
  project(ReFareImport LANGUAGES Swift)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "Configure with -DCMAKE_SYSTEM_NAME=iOS and the Xcode generator")
endif()
add_executable(ReFareImport MACOSX_BUNDLE ReFareImportApp.swift DiscPicker.swift DiscImporter.swift)
set_target_properties(ReFareImport PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.bittahbandit1.refare.import"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.bittahbandit1.refare.import"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
  MACOSX_BUNDLE_BUNDLE_NAME "ReFare Import"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.4.1"
  MACOSX_BUNDLE_BUNDLE_VERSION "12")

if(TARGET ModernGekko::Runtime)
  if(NOT REFARE_IOS_GENERATED_DIR)
    message(FATAL_ERROR "Gameplay requires personal generated game code")
  endif()
  set(GAME_ID GCTE51 CACHE STRING "" FORCE)
  set(GENERATED_DIR "${REFARE_IOS_GENERATED_DIR}" CACHE PATH "" FORCE)
  set(RECOMPCORE_MODULE_STATIC ON CACHE BOOL "" FORCE)
  set(RECOMPCORE_MODULE_ENABLE_IPO OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/vendor/dolphin/module-template"
                   "${CMAKE_CURRENT_BINARY_DIR}/game-module")
  set_source_files_properties(IOSRuntimeServices.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_sources(ReFareImport PRIVATE GameplayView.swift TouchControlModel.swift TouchControls.swift GameplayBridge.mm IOSRuntimeServices.mm
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/refare_options.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/refare_runtime_services.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/tools/dol_patch.cpp")
  target_include_directories(ReFareImport PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/tools"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/vendor/dolphin/Source/Core")
  target_compile_options(ReFareImport PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:-DREFARE_GAMEPLAY>"
    "$<$<COMPILE_LANGUAGE:Swift>:-import-objc-header>"
    "$<$<COMPILE_LANGUAGE:Swift>:${CMAKE_CURRENT_SOURCE_DIR}/GameplayBridge.h>")
  target_link_libraries(ReFareImport PRIVATE ModernGekko::Runtime core imgui gGCTE51_recomp
    "-framework UIKit" "-framework Metal" "-framework QuartzCore"
    "-framework AVFoundation" "-framework GameController")
  target_compile_features(ReFareImport PRIVATE cxx_std_23)
  execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path
    OUTPUT_VARIABLE _refare_ios_sdk OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  target_link_options(ReFareImport PRIVATE
    "SHELL:-Xclang-linker -isysroot -Xclang-linker ${_refare_ios_sdk}")
  include("${CMAKE_CURRENT_SOURCE_DIR}/StartupPatches.cmake")
  set(_startup_json "${CMAKE_CURRENT_SOURCE_DIR}/../../config/startup-patches.json")
  set(_startup_csv "${CMAKE_CURRENT_BINARY_DIR}/startup-patches.csv")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_startup_json}")
  refare_write_startup_patches("${_startup_json}" "${_startup_csv}")
  set_property(TARGET ReFareImport APPEND PROPERTY LINK_DEPENDS "${_startup_csv}")
  add_custom_command(TARGET ReFareImport POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/vendor/dolphin/Data/Sys"
      "$<TARGET_BUNDLE_DIR:ReFareImport>/Sys"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${_startup_csv}"
      "$<TARGET_BUNDLE_DIR:ReFareImport>/startup-patches.csv"
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_BUNDLE_DIR:ReFareImport>/startup-patches.json"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${CMAKE_CURRENT_SOURCE_DIR}/../../config/GFX.ini"
      "$<TARGET_BUNDLE_DIR:ReFareImport>/GFX.ini"
    VERBATIM)
endif()
