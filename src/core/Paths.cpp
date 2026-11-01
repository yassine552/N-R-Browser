// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace {
void ensureWritable(const QString &path)
{
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(path))
        qWarning("Paths: cannot create directory %s", qPrintable(path));
}
} // namespace

QString Paths::appDataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    ensureWritable(base);
    return base;
}

QString Paths::webEngineDir()
{
    const QString dir = appDataDir() + QStringLiteral("/webengine");
    ensureWritable(dir);
    return dir;
}

QString Paths::filterListsDir()
{
    const QString dir = appDataDir() + QStringLiteral("/filterlists");
    ensureWritable(dir);
    return dir;
}

QString Paths::bookmarksDbPath()
{
    return appDataDir() + QStringLiteral("/bookmarks.db");
}

QString Paths::historyDbPath()
{
    return appDataDir() + QStringLiteral("/history.db");
}

QString Paths::topSitesStatePath()
{
    return appDataDir() + QStringLiteral("/topsites.json");
}

QString Paths::faviconsDir()
{
    const QString dir = appDataDir() + QStringLiteral("/favicons");
    ensureWritable(dir);
    return dir;
}

QString Paths::backgroundsDir()
{
    // User-imported New Tab backgrounds (bundled ones live in the qrc).
    const QString dir = appDataDir() + QStringLiteral("/backgrounds");
    ensureWritable(dir);
    return dir;
}

bool Paths::pendingClearSiteStorage()
{
    QSettings s;
    return s.value(QStringLiteral("privacy/clearStorageOnNextStart"), false).toBool();
}

void Paths::setPendingClearSiteStorage(bool pending)
{
    QSettings s;
    s.setValue(QStringLiteral("privacy/clearStorageOnNextStart"), pending);
    s.sync();
}

bool Paths::applyPendingSiteStorageWipe()
{
    // Performs the deferred site-storage wipe requested through
    // "Clear browsing data". Must run before any QWebEngineProfile is
    // created (the engine must be closed for a safe removal). Returns
    // true when a wipe was pending and completed.
    if (!pendingClearSiteStorage())
        return false;
    QDir webEngine(webEngineDir());
    const bool removed = !webEngine.exists() || webEngine.removeRecursively();
    setPendingClearSiteStorage(false);
    if (!removed)
        qWarning("Paths: site-storage wipe failed for %s",
                 qPrintable(webEngine.absolutePath()));
    return removed;
}
