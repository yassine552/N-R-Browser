#pragma once

#include <QIcon>
#include <QMainWindow>
#include <QPointer>
#include <QTabWidget>
#include <QWebEngineProfile>

class AddressBar;
class BrowserApplication;
class DownloadManager;
class FindBar;
class HistoryStore;
class QLabel;
class QMenu;
class QProgressBar;
class QScreen;
class QToolButton;
class QSpacerItem;
class TabWidget;
class WebPage;
class WebView;
class QWidget;

// Below these sizes a saved/restored window geometry is treated as corrupt
// (e.g. saved before the first layout pass) and replaced by the default.
constexpr int MinimumSaneWidth = 600;
constexpr int MinimumSaneHeight = 400;

// The main browser window: menus, navigation toolbar, address bar,
// bookmarks bar, tab container, find bar, status indicators.
class BrowserWindow : public QMainWindow
{
    Q_OBJECT
public:
    BrowserWindow(BrowserApplication *app, bool privateMode, QWidget *parent = nullptr);
    ~BrowserWindow() override;

    bool isPrivate() const { return m_private; }
    BrowserApplication *app() const { return m_app; }

signals:
    // Emitted from ~BrowserWindow() while members are still valid, so the
    // application can release the private profile at the right moment
    // (QObject::destroyed fires too late for member access).
    void windowGone(bool wasPrivate);

public:
    void newInitialTab(const QUrl &url = QUrl());
    WebPage *openInNewForegroundTab(const QUrl &url);
    WebPage *openInNewBackgroundTab(const QUrl &url);
    WebPage *openInNewWindow(const QUrl &url = QUrl());

    void closeCurrentTab();
    void closeWindow();
    int tabCount() const;
    WebView *currentView() const;
    TabWidget *tabWidget() const { return m_tabs; }
    AddressBar *addressBar() const { return m_address; }   // smoke probes

    // Used by WebPage (permissions + history recording).
    QString storedPermission(const QString &origin, const QString &feature) const;
    void storePermission(const QString &origin, const QString &feature, const QString &value);
    HistoryStore *historyStore() const;

    // Called by TabWidget / pages.
    void onCurrentTabChanged();
    void updateWindowTitle();
    void updateZoomIndicator(qreal factor);
    void onLoadProgress(int progress);
    void onLoadingStarted();
    void onFullScreenToggled(bool on);
    void setLinkPreview(const QString &url);

    void retranslateUi();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void sanitizeRestoredGeometry();
    void buildUi();
    void buildNavRow();
    void buildBookmarksRow();
    void buildAppMenu();
    void rebuildAppMenuSections();
    QIcon bookmarkIcon(const QUrl &url);
    void createActions();
    void applyUiSettings();
    void updateChromeBlurRegion();
    void refreshBookmarksRowVisibility();
    void navigateCurrent(const QString &text);
    void toggleBookmark();
    void updateStarState();
    void showSettingsDialog();
    void showHistoryDialog();
    void showBookmarksDialog();
    void showDownloadsDialog();
    void showClearDataDialog();
    void showAboutDialog();
    void openUrlInCurrentTab(const QUrl &url);
    QWebEngineProfile *profileForWindow() const;
    void rebuildBookmarksBar();
    void updateBlockIndicator(int total);
    void updateAddressBarStats();
    void positionLinkPreview();
    void hideLinkPreview();

    BrowserApplication *m_app;
    bool m_private;
    TabWidget *m_tabs = nullptr;
    AddressBar *m_address = nullptr;
    FindBar *m_findBar = nullptr;
    QProgressBar *m_progress = nullptr;

    // Unified chrome header: [tab strip] [navigation row] [bookmarks row].
    QWidget *m_chromeHeader = nullptr;
    QWidget *m_navRow = nullptr;
    QWidget *m_bookmarksRow = nullptr;
    QToolButton *m_appMenuButton = nullptr;
    QMenu *m_appMenu = nullptr;
    QSpacerItem *m_navSpacerStart = nullptr;
    QSpacerItem *m_navSpacerEnd = nullptr;
    QLabel *m_privateBadge = nullptr;

    // There is deliberately NO QStatusBar: it permanently stole ~26px of
    // page height (the YouTube Shorts description bug). Link hover
    // previews moved into a floating overlay that occupies no layout
    // space; the block indicator lives in the address bar; the zoom
    // indicator is a toolbar button.
    QToolButton *m_zoomIndicator = nullptr;  // toolbar: "110%", hidden at 100%
    QLabel *m_linkPreview = nullptr;        // hover overlay, bottom-start

    // actions (the classic menubar is gone: every action lives in the
    // hamburger menu instead, freeing a full row for page content)
    QAction *m_back = nullptr;
    QAction *m_forward = nullptr;
    QAction *m_reload = nullptr;
    QAction *m_stop = nullptr;
    QAction *m_home = nullptr;
    QAction *m_star = nullptr;
    QAction *m_newTab = nullptr;
    QAction *m_closeTab = nullptr;
    QAction *m_newWindow = nullptr;
    QAction *m_newPrivateWindow = nullptr;
    QAction *m_quit = nullptr;
    QAction *m_find = nullptr;
    QAction *m_zoomIn = nullptr;
    QAction *m_zoomOut = nullptr;
    QAction *m_zoomReset = nullptr;
    QAction *m_fullScreen = nullptr;
    QAction *m_bookmarksBarToggle = nullptr;
    QAction *m_showHistory = nullptr;
    QAction *m_showBookmarks = nullptr;
    QAction *m_showDownloads = nullptr;
    QAction *m_clearData = nullptr;
    QAction *m_settings = nullptr;
    QAction *m_about = nullptr;
    QAction *m_blockingToggle = nullptr;
    QAction *m_dataSaverToggle = nullptr;
    QAction *m_duplicateTab = nullptr;

    QMenu *m_menuSectionFile = nullptr;
    QMenu *m_menuSectionView = nullptr;
    QMenu *m_menuSectionHistory = nullptr;
    QMenu *m_menuSectionBookmarks = nullptr;
    QMenu *m_menuSectionTools = nullptr;
    QMenu *m_menuSectionHelp = nullptr;

    bool m_loading = false;
    bool m_fullScreenActive = false;
    bool m_geometrySanitized = false;
};
