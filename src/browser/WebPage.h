// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QWebEngineCertificateError>
#include <QWebEngineFullScreenRequest>
#include <QWebEnginePage>
#include <QWebEnginePermission>

class AdBlockClient;
class BrowserWindow;
class HistoryStore;

// QWebEnginePage subclass owning all page-level policy:
//  - TLS certificate error handling (never silently accepted)
//  - site permission prompts backed by AppConfig
//  - window.open() / target=_blank routing into tabs
//  - HTTP authentication dialog
//  - history recording (never for off-the-record pages)
//  - cosmetic ad filtering (CSS selector hiding) when the active
//    ad-block engine supports it
class WebPage : public QWebEnginePage
{
    Q_OBJECT
public:
    WebPage(QWebEngineProfile *profile, BrowserWindow *window, QObject *parent = nullptr);
    ~WebPage() override;

    // Cached permission key names (also used by the permissions UI).
    static QString permissionKey(QWebEnginePermission::PermissionType type);
    static QString permissionDisplayName(QWebEnginePermission::PermissionType type);

    // Only http(s) visits from the regular profile are recorded.
    void recordHistoryIfAllowed();

    // True when the last load FAILED and the engine may still expose the
    // previous document's <title>: chrome (window title, tab label) must
    // not display it, or the UI would claim a page the viewport is not
    // showing (the "7-Zip title over New Tab content" mismatch).
    bool titleIsStale() const { return m_titleStale; }

signals:
    void pageCrashed(int exitCode);
    void fullScreenToggled(bool on);

protected:
    QWebEnginePage *createWindow(WebWindowType type) override;
    // Intercepts nor://newtab/action/... command URLs raised by the
    // new-tab page (pin/unpin/remove/edit/open) and blocks the navigation
    // itself: the action is executed natively and the page is reloaded.
    bool acceptNavigationRequest(const QUrl &url, NavigationType type,
                                 bool isMainFrame) override;

private slots:
    void onCertificateError(const QWebEngineCertificateError &error);
    void onPermissionRequested(QWebEnginePermission permission);
    void onFullScreenRequested(QWebEngineFullScreenRequest request);
    void onAuthenticationRequired(const QUrl &requestUrl, QAuthenticator *authenticator);
    void onWindowCloseRequested();
    void onLoadFinished(bool ok);
    void onTitleChanged(const QString &title);
    void onUrlChanged(const QUrl &url);

private:
    AdBlockClient *adBlockClient() const;
    // Executes one new-tab command URL; returns true when the page should
    // be reloaded afterwards (state changed).
    bool handleNewTabAction(const QUrl &commandUrl);
    // New Tab customization commands (background selection/import, glass
    // preferences, reset). Allowed in private windows: preferences are not
    // browsing data.
    bool handleNewTabCustomization(const QUrl &commandUrl);
    // Pushes the persisted New Tab preferences into the page's
    // norApplyPrefs() hook (keeps page state and config in lock-step).
    void pushNewTabPrefsToPage();
    // Replaces the per-URL cosmetic hiding script (specific "##" rules).
    void applyCosmeticFilters(const QUrl &url);
    // Second pass after DOM is ready: generic (domain-unrestricted)
    // selectors for the classes/ids actually present on the page.
    void applyGenericCosmeticFilters();
    void removeCosmeticScript(const QString &name);

    QPointer<BrowserWindow> m_window;
    QUrl m_lastRecordedUrl;
    bool m_titleStale = false;         // last load failed: title() untrustworthy
    QStringList m_cosmeticExceptions;   // selectors re-allowed on this page
    QStringList m_cosmeticHideSelectors; // URL-specific selectors to enforce
    bool m_genericHide = false;         // $generichide on this page
};
