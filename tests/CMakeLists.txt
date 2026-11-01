# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
# NØR Browser test suite (Qt Test + CTest).
#
# Unit tests run with the offscreen platform plugin; WebEngine-based tests
# additionally disable the Chromium sandbox because CI containers do not
# provide user namespaces (documented limitation of the test environment,
# not of the browser itself).

find_package(Qt6 REQUIRED COMPONENTS Test Widgets WebEngineWidgets WebEngineCore Sql Network)

# Rust ad-block backend linkage (see root CMakeLists.txt).
function(nor_link_adblock target)
  if(NOR_HAVE_RUST_ADBLOCK)
    target_include_directories(${target} PRIVATE ${NOR_SRC}/adblock-sys/include)
    target_compile_definitions(${target} PRIVATE NOR_HAVE_RUST_ADBLOCK=1)
    target_link_libraries(${target} PRIVATE ${NOR_RUST_STATIC_LIB} ${CMAKE_DL_LIBS} m)
    add_dependencies(${target} nor_adblock_sys)
  endif()
endfunction()

function(nor_unit_test name)
  qt_add_executable(${name} ${name}.cpp ${NOR_CORE_SOURCES} ${NOR_PRIVACY_SOURCES})
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/browser
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/privacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui)
  target_link_libraries(${name} PRIVATE Qt6::Widgets Qt6::WebEngineCore Qt6::Test Qt6::Sql Qt6::Network)
  nor_link_adblock(${name})
  target_compile_definitions(${name} PRIVATE
    NOR_VERSION="${PROJECT_VERSION}"
    NOR_BUILD_TRANSLATIONS_DIR="${CMAKE_BINARY_DIR}"
    NOR_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_DISABLE_SANDBOX=1;NOR_BUILD_DIR=${CMAKE_BINARY_DIR};XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test-data/data;XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-data/cache;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-data/config")
endfunction()

nor_unit_test(tst_adblock)
nor_unit_test(tst_stores)
nor_unit_test(tst_topsites)
nor_unit_test(tst_language)
nor_unit_test(tst_filterlists)
if(NOR_HAVE_RUST_ADBLOCK)
  nor_unit_test(tst_rustengine)
endif()

# Autocomplete logic test: core stores + the suggestion ranker only.
qt_add_executable(tst_autocomplete tst_autocomplete.cpp
  ${NOR_CORE_SOURCES} ${NOR_PRIVACY_SOURCES}
  ${NOR_SRC}/src/browser/AddressBarSuggestions.h
  ${NOR_SRC}/src/browser/AddressBarSuggestions.cpp)
target_include_directories(tst_autocomplete PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/browser
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/privacy
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui)
target_link_libraries(tst_autocomplete PRIVATE Qt6::Widgets Qt6::WebEngineCore Qt6::Test Qt6::Sql Qt6::Network)
nor_link_adblock(tst_autocomplete)
add_test(NAME tst_autocomplete COMMAND tst_autocomplete)
set_tests_properties(tst_autocomplete PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Profile lifecycle test: needs the real WebEngine (still headless).
# Theme + Translucency: NewTabSchemeHandler builds the page palette from
# the design tokens (same single source of truth as the app chrome).
qt_add_executable(tst_profile tst_profile.cpp
  ${NOR_CORE_SOURCES} ${NOR_PRIVACY_SOURCES} ${NOR_PROFILE_SOURCES}
  ${NOR_SRC}/src/ui/Theme.h        ${NOR_SRC}/src/ui/Theme.cpp
  ${NOR_SRC}/src/ui/Translucency.h ${NOR_SRC}/src/ui/Translucency.cpp)
target_include_directories(tst_profile PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/browser
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/privacy
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui)
target_link_libraries(tst_profile PRIVATE
  Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebEngineCore Qt6::Test Qt6::Sql Qt6::Network Qt6::Svg ${CMAKE_DL_LIBS})
nor_link_adblock(tst_profile)
target_compile_definitions(tst_profile PRIVATE NOR_VERSION="${PROJECT_VERSION}")
add_test(NAME tst_profile COMMAND tst_profile)
set_tests_properties(tst_profile PROPERTIES
  TIMEOUT 120
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_DISABLE_SANDBOX=1;NOR_BUILD_DIR=${CMAKE_BINARY_DIR};XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test-data/data;XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/test-data/cache;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-data/config")
