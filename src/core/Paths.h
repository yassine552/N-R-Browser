// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QString>

// Centralized filesystem locations for NØR Browser.
// On Linux:   ~/.local/share/nor-browser  (honors XDG_DATA_HOME when set)
// On Windows: %LOCALAPPDATA%/nor-browser
class Paths
{
public:
    // Writable application data directory (created on demand).
    static QString appDataDir();
    // Qt WebEngine persistent storage root (regular profile only).
    static QString webEngineDir();
    // User-supplied filter lists directory.
    static QString filterListsDir();
    static QString bookmarksDbPath();
    static QString historyDbPath();
    // Top-Sites user state (pins/removals) and the local favicon cache.
    static QString topSitesStatePath();
    static QString faviconsDir();
    // User-imported New Tab backgrounds (bundled ones ship in the qrc).
    static QString backgroundsDir();
    // Directory that should be wiped when the user asked to clear
    // persistent site storage on next launch.
    static bool pendingClearSiteStorage();
    static void setPendingClearSiteStorage(bool pending);
    // Deferred site-storage wipe: performs and clears the pending request.
    // Must be called before any QWebEngineProfile exists. Returns true when
    // a wipe was pending and completed.
    static bool applyPendingSiteStorageWipe();
};
