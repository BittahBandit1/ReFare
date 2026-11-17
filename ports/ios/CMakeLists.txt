cmake_minimum_required(VERSION 3.25)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum supported iOS version")
if(NOT TARGET ModernGekko::Runtime)
  project(ReFareImport LANGUAGES Swift C CXX)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "Configure with -DCMAKE_SYSTEM_NAME=iOS and the Xcode generator")
endif()
add_executable(ReFareImport MACOSX_BUNDLE ReFareImportApp.swift DiscPicker.swift DiscImporter.swift)
target_sources(ReFareImport PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../launcher/ReFareBranding.swift")
set(_refare_brand_font "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/branding/ReFareUI.ttf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../assets/branding/ReFareLogo.png" "${CMAKE_CURRENT_BINARY_DIR}/ReFareLogo.png" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../assets/branding/NOTICE.txt" "${CMAKE_CURRENT_BINARY_DIR}/ReFareBranding.txt" COPYONLY)
set_source_files_properties("${_refare_brand_font}" "${CMAKE_CURRENT_BINARY_DIR}/ReFareLogo.png" "${CMAKE_CURRENT_BINARY_DIR}/ReFareBranding.txt" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(ReFareImport PRIVATE "${_refare_brand_font}" "${CMAKE_CURRENT_BINARY_DIR}/ReFareLogo.png" "${CMAKE_CURRENT_BINARY_DIR}/ReFareBranding.txt")
set(_refare_icon_files)
foreach(size 40 58 60 76 80 87 120 152 167 180 1024)
  list(APPEND _refare_icon_files "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/icons/icon-${size}.png")
endforeach()
set_source_files_properties(${_refare_icon_files} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(ReFareImport PRIVATE ${_refare_icon_files})
set_target_properties(ReFareImport PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.bittahbandit1.refare.import"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.bittahbandit1.refare.import"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
  MACOSX_BUNDLE_BUNDLE_NAME "ReFare Import"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2.5"
  MACOSX_BUNDLE_BUNDLE_VERSION "15")

if(REFARE_IOS_BUILDER)
  if(NOT REFARE_IOS_BUILDER_ASSETS OR NOT REFARE_IOS_LLVM_BUILD)
    message(FATAL_ERROR "Use scripts/ios_builder.py to prepare the embedded compiler and export template")
  endif()
  set(_llvm_source "${REFARE_IOS_LLVM_SOURCE}")
  # These are already cross-compiled libraries, outside the Apple SDK root.
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
  find_package(LLVM 20.1.8 EXACT CONFIG REQUIRED PATHS "${REFARE_IOS_LLVM_BUILD}/lib/cmake/llvm" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  find_package(Clang CONFIG REQUIRED PATHS "${REFARE_IOS_LLVM_BUILD}/lib/cmake/clang" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  find_package(LLD CONFIG REQUIRED PATHS "${REFARE_IOS_LLVM_BUILD}/lib/cmake/lld" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB TRUE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/vendor/dolphin/DolRecomp" dolrecomp EXCLUDE_FROM_ALL)
  get_target_property(_platform_sources dr_platform SOURCES)
  list(FILTER _platform_sources EXCLUDE REGEX "process\\.c$")
  set_property(TARGET dr_platform PROPERTY SOURCES "${_platform_sources}")
  target_sources(dr_platform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/BuilderPlatform.c")
  get_target_property(_frontend_sources dr_frontend SOURCES)
  list(FILTER _frontend_sources EXCLUDE REGEX "disc_extract\\.c$")
  set_property(TARGET dr_frontend PROPERTY SOURCES "${_frontend_sources}")
  get_target_property(_app_sources dr_app SOURCES)
  list(FILTER _app_sources EXCLUDE REGEX "database\\.c$")
  set_property(TARGET dr_app PROPERTY SOURCES "${_app_sources}")
  add_library(refare_builder_entry STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/vendor/dolphin/DolRecomp/src/app/main.c")
  target_compile_definitions(refare_builder_entry PRIVATE main=refare_dolrecomp_main)
  target_link_libraries(refare_builder_entry PUBLIC dr_app)
  target_sources(ReFareImport PRIVATE BuilderBridge.cpp GameBuilder.swift ModuleTables.swift IPAArchive.swift BuilderView.swift
    "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/tools/dol_patch.cpp")
  target_include_directories(ReFareImport PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS}
    "${_llvm_source}/lld/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/tools")
  target_compile_features(ReFareImport PRIVATE cxx_std_20)
  target_link_libraries(ReFareImport PRIVATE refare_builder_entry clangCodeGen clangFrontend clangDriver
    LLVMAArch64CodeGen LLVMAArch64AsmParser lldMachO "-framework UIKit")
  target_compile_options(ReFareImport PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:-DREFARE_BUILDER>"
    "$<$<COMPILE_LANGUAGE:Swift>:-import-objc-header>"
    "$<$<COMPILE_LANGUAGE:Swift>:${CMAKE_CURRENT_SOURCE_DIR}/BuilderBridge.h>")
  set_target_properties(ReFareImport PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.bittahbandit1.refare.builder"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.bittahbandit1.refare.builder")
  add_custom_command(TARGET ReFareImport POST_BUILD
    COMMAND ${CMAKE_COMMAND} "-DREFARE_BUILDER_ASSETS=${REFARE_IOS_BUILDER_ASSETS}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/EmbedBuilderAssets.cmake" VERBATIM)
endif()

if(TARGET ModernGekko::Runtime)
  if(REFARE_IOS_EXPORT_TEMPLATE)
    target_sources(ReFareImport PRIVATE ExportModule.mm)
  elseif(NOT REFARE_IOS_GENERATED_DIR)
    message(FATAL_ERROR "Gameplay requires personal generated game code")
  else()
  set(GAME_ID GCTE51 CACHE STRING "" FORCE)
  set(GENERATED_DIR "${REFARE_IOS_GENERATED_DIR}" CACHE PATH "" FORCE)
  set(RECOMPCORE_MODULE_STATIC ON CACHE BOOL "" FORCE)
  set(RECOMPCORE_MODULE_ENABLE_IPO OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ModernGekko/vendor/dolphin/module-template"
                   "${CMAKE_CURRENT_BINARY_DIR}/game-module")
    target_link_libraries(ReFareImport PRIVATE gGCTE51_recomp)
  endif()
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
  target_link_libraries(ReFareImport PRIVATE ModernGekko::Runtime core imgui
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
