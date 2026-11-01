# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
# NorCore: pure-logic library shared by the browser UI and the unit tests.
# It must NOT depend on QtWebEngine so that the ad-block engine, settings and
# storage logic can be tested headlessly (QT_QPA_PLATFORM=offscreen).

qt_add_library(NorCore STATIC
    AppConfig.h
    AppConfig.cpp
    PrivacyFilter.h
    PrivacyFilter.cpp
    SearchEngines.h
    SearchEngines.cpp
)

target_link_libraries(NorCore
    PUBLIC Qt6::Core Qt6::Sql
)

target_include_directories(NorCore
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
