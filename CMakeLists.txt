cmake_minimum_required(VERSION 3.21)

project(nor-browser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(APP_TARGET  nor-browser)
set(APP_NAME    "NOR Browser")

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets WebEngineWidgets WebEngineCore Sql Network Test Svg LinguistTools)
qt_standard_project_setup()

# --------------------------------------------- Rust ad-block backend (optional)
# NOR_ADBLOCK_BACKEND: Auto (default) | Rust | BuiltIn
#   Rust   — brave/adblock-rust 0.13.5 (MPL-2.0) via the adblock-sys C ABI.
#            Requires a Rust toolchain: point NOR_CARGO at the cargo binary
#            (or have cargo on PATH). See docs/ADBLOCK_ENGINE.md.
#   BuiltIn — the zero-dependency C++ engine (subset of the Adblock syntax).
set(NOR_ADBLOCK_BACKEND "Auto" CACHE STRING "Ad-block engine backend: Auto, Rust or BuiltIn")

if(DEFINED ENV{NOR_CARGO})
  if(EXISTS "$ENV{NOR_CARGO}" AND NOT IS_DIRECTORY "$ENV{NOR_CARGO}")
    set(NOR_CARGO_EXECUTABLE "$ENV{NOR_CARGO}")
  else()
    find_program(NOR_CARGO_EXECUTABLE NAMES cargo HINTS "$ENV{NOR_CARGO}")
  endif()
else()
  find_program(NOR_CARGO_EXECUTABLE NAMES cargo)
endif()

set(NOR_RUST_ADBLOCK_SUPPORTED OFF)
if(NOR_CARGO_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/adblock-sys/Cargo.toml")
  set(NOR_RUST_ADBLOCK_SUPPORTED ON)
endif()

if(NOR_ADBLOCK_BACKEND STREQUAL "Auto")
  if(NOR_RUST_ADBLOCK_SUPPORTED)
    set(NOR_ADBLOCK_EFFECTIVE "Rust")
  else()
    set(NOR_ADBLOCK_EFFECTIVE "BuiltIn")
  endif()
else()
  set(NOR_ADBLOCK_EFFECTIVE "${NOR_ADBLOCK_BACKEND}")
endif()

set(NOR_HAVE_RUST_ADBLOCK OFF)
if(NOR_ADBLOCK_EFFECTIVE STREQUAL "Rust")
  if(NOT NOR_RUST_ADBLOCK_SUPPORTED)
    message(FATAL_ERROR
      "NOR_ADBLOCK_BACKEND=Rust was requested but no cargo toolchain was found. "
      "Set the NOR_CARGO environment variable to your cargo binary, or build "
      "with -DNOR_ADBLOCK_BACKEND=BuiltIn.")
  endif()
  set(NOR_HAVE_RUST_ADBLOCK ON)
endif()

if(NOR_HAVE_RUST_ADBLOCK)
  set(NOR_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/adblock-rust")
  set(NOR_RUST_STATIC_LIB "${NOR_RUST_TARGET_DIR}/release/libnor_adblock_sys.a")
  add_custom_command(
    OUTPUT "${NOR_RUST_STATIC_LIB}"
    COMMAND "${NOR_CARGO_EXECUTABLE}" build --release --target-dir "${NOR_RUST_TARGET_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/adblock-sys"
    DEPENDS adblock-sys/src/lib.rs
            adblock-sys/Cargo.toml
            adblock-sys/rust-toolchain.toml
            adblock-sys/include/nor_adblock.h
    COMMENT "[nor] building adblock-rust engine shim (cargo)"
    VERBATIM)
  add_custom_target(nor_adblock_sys DEPENDS "${NOR_RUST_STATIC_LIB}")
  message(STATUS "Ad-block backend: adblock-rust (cargo: ${NOR_CARGO_EXECUTABLE})")
else()
  message(STATUS "Ad-block backend: built-in C++ engine (Rust toolchain not found or disabled)")
endif()

# ------------------------------------------------------------------ sources
# Absolute paths so subdirectories (tests/) can reuse the lists.
set(NOR_SRC ${CMAKE_CURRENT_SOURCE_DIR})

set(NOR_CORE_SOURCES
  ${NOR_SRC}/src/core/Paths.h             ${NOR_SRC}/src/core/Paths.cpp
  ${NOR_SRC}/src/core/AppConfig.h         ${NOR_SRC}/src/core/AppConfig.cpp
  ${NOR_SRC}/src/core/BackgroundStore.h   ${NOR_SRC}/src/core/BackgroundStore.cpp
  ${NOR_SRC}/src/core/FilterListManager.h ${NOR_SRC}/src/core/FilterListManager.cpp
  ${NOR_SRC}/src/core/LanguageManager.h   ${NOR_SRC}/src/core/LanguageManager.cpp
  ${NOR_SRC}/src/core/SearchEngines.h     ${NOR_SRC}/src/core/SearchEngines.cpp
  ${NOR_SRC}/src/core/BookmarkStore.h     ${NOR_SRC}/src/core/BookmarkStore.cpp
  ${NOR_SRC}/src/core/HistoryStore.h      ${NOR_SRC}/src/core/HistoryStore.cpp
  ${NOR_SRC}/src/core/TopSitesStore.h     ${NOR_SRC}/src/core/TopSitesStore.cpp
  ${NOR_SRC}/src/core/FaviconStore.h      ${NOR_SRC}/src/core/FaviconStore.cpp
  ${NOR_SRC}/src/core/DownloadManager.h   ${NOR_SRC}/src/core/DownloadManager.cpp
)

set(NOR_PRIVACY_SOURCES
  ${NOR_SRC}/src/privacy/AdblockBackend.h
  ${NOR_SRC}/src/privacy/BuiltInAdblockBackend.h  ${NOR_SRC}/src/privacy/BuiltInAdblockBackend.cpp
  ${NOR_SRC}/src/privacy/FilterRule.h     ${NOR_SRC}/src/privacy/FilterRule.cpp
  ${NOR_SRC}/src/privacy/AdBlockClient.h  ${NOR_SRC}/src/privacy/AdBlockClient.cpp
  ${NOR_SRC}/src/privacy/RequestInterceptor.h ${NOR_SRC}/src/privacy/RequestInterceptor.cpp
)
if(NOR_HAVE_RUST_ADBLOCK)
  list(APPEND NOR_PRIVACY_SOURCES
    ${NOR_SRC}/src/privacy/RustAdblockBackend.h
    ${NOR_SRC}/src/privacy/RustAdblockBackend.cpp)
endif()

set(NOR_BROWSER_SOURCES
  ${NOR_SRC}/src/browser/ProfileManager.h     ${NOR_SRC}/src/browser/ProfileManager.cpp
  ${NOR_SRC}/src/browser/WebPage.h            ${NOR_SRC}/src/browser/WebPage.cpp
  ${NOR_SRC}/src/browser/WebView.h            ${NOR_SRC}/src/browser/WebView.cpp
  ${NOR_SRC}/src/browser/TabWidget.h          ${NOR_SRC}/src/browser/TabWidget.cpp
  ${NOR_SRC}/src/browser/AddressBar.h         ${NOR_SRC}/src/browser/AddressBar.cpp
  ${NOR_SRC}/src/browser/AddressBarSuggestions.h ${NOR_SRC}/src/browser/AddressBarSuggestions.cpp
  ${NOR_SRC}/src/browser/FindBar.h            ${NOR_SRC}/src/browser/FindBar.cpp
  ${NOR_SRC}/src/browser/NewTabSchemeHandler.h ${NOR_SRC}/src/browser/NewTabSchemeHandler.cpp
  ${NOR_SRC}/src/browser/BrowserWindow.h      ${NOR_SRC}/src/browser/BrowserWindow.cpp
  ${NOR_SRC}/src/browser/BrowserApplication.h ${NOR_SRC}/src/browser/BrowserApplication.cpp
)

set(NOR_UI_SOURCES
  ${NOR_SRC}/src/ui/Theme.h               ${NOR_SRC}/src/ui/Theme.cpp
  ${NOR_SRC}/src/ui/Translucency.h        ${NOR_SRC}/src/ui/Translucency.cpp
  ${NOR_SRC}/src/ui/IconProvider.h        ${NOR_SRC}/src/ui/IconProvider.cpp
  ${NOR_SRC}/src/ui/SettingsDialog.h      ${NOR_SRC}/src/ui/SettingsDialog.cpp
  ${NOR_SRC}/src/ui/HistoryDialog.h       ${NOR_SRC}/src/ui/HistoryDialog.cpp
  ${NOR_SRC}/src/ui/BookmarksDialog.h     ${NOR_SRC}/src/ui/BookmarksDialog.cpp
  ${NOR_SRC}/src/ui/DownloadsDialog.h     ${NOR_SRC}/src/ui/DownloadsDialog.cpp
  ${NOR_SRC}/src/ui/ClearDataDialog.h     ${NOR_SRC}/src/ui/ClearDataDialog.cpp
)

# Subset needed by the profile lifecycle test (no main window dependency).
set(NOR_PROFILE_SOURCES
  ${NOR_SRC}/src/browser/ProfileManager.h       ${NOR_SRC}/src/browser/ProfileManager.cpp
  ${NOR_SRC}/src/browser/NewTabSchemeHandler.h  ${NOR_SRC}/src/browser/NewTabSchemeHandler.cpp
  ${NOR_SRC}/src/core/TopSitesStore.h           ${NOR_SRC}/src/core/TopSitesStore.cpp
  ${NOR_SRC}/src/core/FaviconStore.h            ${NOR_SRC}/src/core/FaviconStore.cpp
)

qt_add_executable(${APP_TARGET}
  src/main.cpp
  resources/resources.qrc
  ${NOR_CORE_SOURCES}
  ${NOR_PRIVACY_SOURCES}
  ${NOR_BROWSER_SOURCES}
  ${NOR_UI_SOURCES}
)

target_include_directories(${APP_TARGET} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/src/browser
  ${CMAKE_CURRENT_SOURCE_DIR}/src/privacy
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

# ------------------------------------------------------------- translations
set(TS_FILES
  translations/nor-browser_en.ts
  translations/nor-browser_ar.ts
  translations/nor-browser_fr.ts
)
qt_add_translations(${APP_TARGET}
  TS_FILES ${TS_FILES}
  RESOURCE_PREFIX "/translations"
  LUPDATE_OPTIONS -no-obsolete
)
# Location of generated .qm files for tests / runtime probing.
target_compile_definitions(${APP_TARGET} PRIVATE
  NOR_BUILD_TRANSLATIONS_DIR="${CMAKE_CURRENT_BINARY_DIR}"
  NOR_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(${APP_TARGET} PRIVATE
  Qt6::Widgets
  Qt6::WebEngineWidgets
  Qt6::WebEngineCore
  Qt6::Sql
  Qt6::Network
  Qt6::Test
  Qt6::Svg
  ${CMAKE_DL_LIBS}
)
if(NOR_HAVE_RUST_ADBLOCK)
  target_include_directories(${APP_TARGET} PRIVATE ${NOR_SRC}/adblock-sys/include)
  target_compile_definitions(${APP_TARGET} PRIVATE NOR_HAVE_RUST_ADBLOCK=1)
  target_link_libraries(${APP_TARGET} PRIVATE ${NOR_RUST_STATIC_LIB} ${CMAKE_DL_LIBS} m)
  add_dependencies(${APP_TARGET} nor_adblock_sys)
endif()
set_target_properties(${APP_TARGET} PROPERTIES
  OUTPUT_NAME nor-browser
)

if(WIN32)
  target_sources(${APP_TARGET} PRIVATE packaging/windows/nor-browser.rc)
  set_target_properties(${APP_TARGET} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# -------------------------------------------------------------------- tests
enable_testing()
add_subdirectory(tests)

# ------------------------------------------------------------------- install
include(GNUInstallDirs)
install(TARGETS ${APP_TARGET}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES packaging/linux/nor-browser.desktop
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)
foreach(size 16 22 24 32 48 64 128 256)
  install(FILES packaging/linux/icons/hicolor/${size}x${size}/apps/nor-browser.png
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps)
endforeach()
