// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QSettings;

// Typed wrapper around QSettings. All application preferences flow through
// this class so that persistence has a single, testable owner.
//
// The test constructor takes an explicit INI file path for isolation.
class AppConfig : public QObject
{
    Q_OBJECT
public:
    explicit AppConfig(QObject *parent = nullptr);
    explicit AppConfig(const QString &iniPath, QObject *parent = nullptr);
    ~AppConfig() override;

    // ---- language -------------------------------------------------------
    QString language() const;                    // "en" (default), "ar", "fr"
    void setLanguage(const QString &code);

    // ---- startup / general ----------------------------------------------
    QString homePage() const;                    // default: nor://newtab
    void setHomePage(const QString &url);

    // ---- downloads ------------------------------------------------------
    QString downloadDirectory() const;
    void setDownloadDirectory(const QString &dir);
    bool askWhereToSaveDownloads() const;        // default: true
    void setAskWhereToSaveDownloads(bool ask);

    // ---- privacy filtering ----------------------------------------------
    bool adBlockEnabled() const;                 // default: true
    void setAdBlockEnabled(bool enabled);
    QStringList customFilterLists() const;       // absolute file paths
    void setCustomFilterLists(const QStringList &paths);

    // Remote filter-list sources (EasyList/EasyPrivacy by default).
    // Downloads happen only when the user keeps auto-update enabled and
    // an active network connection is available; the engine always works
    // offline from the last cached copies.
    QStringList adBlockListUrls() const;
    void setAdBlockListUrls(const QStringList &urls);
    bool adBlockAutoUpdate() const;              // default: true
    void setAdBlockAutoUpdate(bool enabled);
    // JSON cache of per-URL download state (etag/lastModified/lastCheck).
    QString filterListState() const;
    void setFilterListState(const QString &json);

    // Address-bar autocomplete from local history and bookmarks.
    bool addressBarSuggestions() const;          // default: true
    void setAddressBarSuggestions(bool enabled);
    QStringList adBlockAllowlist() const;        // hosts never filtered
    void setAdBlockAllowlist(const QStringList &hosts);

    // ---- data saver ------------------------------------------------------
    bool dataSaverEnabled() const;
    void setDataSaverEnabled(bool enabled);
    bool dataSaverBlockThirdPartyImages() const;
    void setDataSaverBlockThirdPartyImages(bool block);
    // Block all images on non-excepted sites while Data Saver is on
    // (per-request, so exceptions work per site).
    bool dataSaverBlockImages() const;                 // default: true
    void setDataSaverBlockImages(bool block);
    QStringList dataSaverExceptions() const;     // hosts exempt from data saver
    void setDataSaverExceptions(const QStringList &hosts);

    // ---- privacy hardening -------------------------------------------------
    // Third-party cookies are rejected by a cookie-store filter (Qt 6.8
    // QWebEngineCookieStore::setCookieFilter). Default: on.
    bool blockThirdPartyCookies() const;
    void setBlockThirdPartyCookies(bool block);
    // Cookies become session-only (never written to disk). Default: off.
    bool sessionOnlyCookies() const;
    void setSessionOnlyCookies(bool sessionOnly);
    // Send DNT: 1 on http/https requests. Default: on.
    bool sendDoNotTrack() const;
    void setSendDoNotTrack(bool send);
    // Send Sec-GPC: 1 (Global Privacy Control signal). Default: on.
    bool sendGlobalPrivacyControl() const;
    void setSendGlobalPrivacyControl(bool send);
    // Present the plain-Chromium User-Agent (drops the "QtWebEngine/x.y"
    // token that makes every Qt-based browser trivially identifiable).
    // Default: on. This unifies — never randomizes — the fingerprint.
    bool simplifiedUserAgent() const;
    void setSimplifiedUserAgent(bool simplify);
    // WebRTC: expose only the default public interface to pages (no local
    // IP / interface enumeration). Applied at startup via Chromium flags.
    // Default: on.
    bool webrtcPublicIpOnly() const;
    void setWebrtcPublicIpOnly(bool restrict);

    // ---- search ----------------------------------------------------------
    QString searchEnginesJson() const;           // [{"name":..,"url":..}]
    void setSearchEnginesJson(const QString &json);
    QString defaultSearchEngine() const;         // engine name
    void setDefaultSearchEngine(const QString &name);

    // ---- permissions -----------------------------------------------------
    // value: "allow" / "deny"; missing key means "ask" (prompt).
    QString permission(const QString &origin, const QString &feature) const;
    void setPermission(const QString &origin, const QString &feature, const QString &value);
    void removePermission(const QString &origin);
    QStringList permissionOrigins() const;

    // ---- ui ---------------------------------------------------------------
    bool bookmarksBarVisible() const;            // default: true
    void setBookmarksBarVisible(bool visible);
    // Appearance: "system" (recommended, default), "dark", "light".
    // NOTE: Theme.cpp reads the SAME key directly at startup (before any
    // AppConfig instance exists) — keep the key in sync.
    QString uiTheme() const;                     // default: "system"
    void setUiTheme(const QString &theme);
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    // ---- New Tab customization --------------------------------------------
    // Background image (bundled "nor-*" id, imported "custom-*" id, or ""
    // for the flat theme color). Enabled=false also means flat color.
    bool newTabBackgroundEnabled() const;             // default: true
    void setNewTabBackgroundEnabled(bool enabled);
    QString newTabBackgroundId() const;               // default: "nor-graphite"
    void setNewTabBackgroundId(const QString &id);
    // Real blur behind the glass surfaces (off -> higher-alpha fallback).
    bool newTabBlurEnabled() const;                   // default: true
    void setNewTabBlurEnabled(bool enabled);
    // Search-bar glass opacity, 30..90 (percent). Default: 48.
    int newTabSearchOpacity() const;
    void setNewTabSearchOpacity(int percent);
    bool newTabShowTopSites() const;                  // default: true
    void setNewTabShowTopSites(bool show);
    // Reset every New Tab customization to the shipped defaults.
    void resetNewTabCustomization();

    void sync();
    QString filePath() const;

signals:
    // Emitted after any persisted change (emitted once per setter).
    void changed();
    // New Tab customization only (background/glass/Top-Sites visibility).
    // Deliberately separate from changed(): those changes are applied live
    // by the New Tab page itself, so the window must NOT reload it; only
    // OTHER new-tab tabs need a refresh.
    void newTabCustomizationChanged();

private:
    void initDefaults();

    QSettings *m_settings = nullptr;
    bool m_ownsSettings = true;
};
