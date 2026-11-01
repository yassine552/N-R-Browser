// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QIcon;

// Local favicon cache for the Top-Sites tiles.
//
// Icons are CAPTURED from pages the user actually visits (Qt WebEngine
// exposes the rendered page icon through QWebEnginePage::iconChanged) and
// stored as 64x64 PNGs in <appData>/favicons/<host>.png. No favicon is
// ever downloaded from a third-party favicon service — the browser never
// contacts any host the user did not visit, and no new network requests
// are introduced by this feature.
//
// Privacy:
//  - callers must skip off-the-record (private) pages, so private-session
//    hosts never reach the disk;
//  - "Clear browsing data" (history) wipes the cache with it: the set of
//    cached hosts is history-derived data.
class FaviconStore : public QObject
{
    Q_OBJECT
public:
    explicit FaviconStore(const QString &cacheDir, QObject *parent = nullptr);

    // Persist the icon for a visited page. Safe no-op for null icons,
    // non-http(s) URLs, private browsing (caller-side) or unusable hosts.
    void capture(const QIcon &icon, const QUrl &pageUrl);

    // True when a cached icon exists for the host.
    bool hasIcon(const QString &host) const;
    // Raw PNG bytes for the host; empty when missing.
    QByteArray iconPng(const QString &host) const;
    // nor://favicon/<host> — the URL the new-tab page embeds.
    static QString iconUrlFor(const QString &host);

    // Wipe every cached icon (history clear).
    bool clearAll();

    // [a-z0-9.-] only, lower-cased, length-capped; empty when unusable.
    // Prevents path traversal through hostile host names.
    static QString sanitizeHost(const QString &host);

private:
    QString pathFor(const QString &host) const;
    QString m_dir;
};
