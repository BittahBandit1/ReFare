# Xcode's effective platform suffix must be expanded at build time. A bundle
# generator expression in this script phase can retain it as a literal string.
set(app "$ENV{TARGET_BUILD_DIR}/$ENV{FULL_PRODUCT_NAME}")
if(NOT EXISTS "${app}/Info.plist" OR NOT EXISTS "${REFARE_BUILDER_ASSETS}/GameplayTemplate.rfarchive")
  message(FATAL_ERROR "Missing built app or builder resources")
endif()
file(REMOVE_RECURSE "${app}/BuilderAssets")
file(MAKE_DIRECTORY "${app}/BuilderAssets")
file(COPY "${REFARE_BUILDER_ASSETS}/" DESTINATION "${app}/BuilderAssets")
