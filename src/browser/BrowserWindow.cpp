// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "BrowserWindow.h"

#include "AddressBar.h"
#include "AdBlockClient.h"
#include "AppConfig.h"
#include "BookmarkStore.h"
#include "BrowserApplication.h"
#include "ClearDataDialog.h"
#include "DownloadsDialog.h"
#include "FindBar.h"
#include "HistoryDialog.h"
#include "HistoryStore.h"
#include "BookmarksDialog.h"
#include "LanguageManager.h"
#include "NewTabSchemeHandler.h" // isNewTabUrl
#include "ProfileManager.h"
#include "SearchEngines.h"
#include "SettingsDialog.h"
#include "TabWidget.h"
#include "WebPage.h"
#include "WebView.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QScreen>
#include <QShortcut>
#include <QHBoxLayout>
#include <QSpacerItem>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineHistory>
#include <QWebEngineProfile>

#include "FaviconStore.h"
#include "IconProvider.h"
#include "Theme.h"
#include "Translucency.h"

using namespace std::chrono_literals;

BrowserWindow::BrowserWindow(BrowserApplication *app, bool privateMode, QWidget *parent)
    : QMainWindow(parent)
    , m_app(app)
    , m_private(privateMode)
{
    setProperty("private", m_private);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(IconProvider::appIcon());

    createActions();
    buildUi();
    buildNavRow();
    buildBookmarksRow();
    buildAppMenu();
    applyUiSettings();
    retranslateUi();

    if (!m_app->config()->windowGeometry().isEmpty())
        restoreGeometry(m_app->config()->windowGeometry());
    else
        resize(1200, 750);
    sanitizeRestoredGeometry();

    connect(m_app->config(), &AppConfig::changed, this, [this]() {
        applyUiSettings();
        // A theme switch re-tints open new-tab pages (their palette is
        // generated from the design tokens at request time).
        if (WebView *v = currentView()) {
            if (isNewTabUrl(v->page()->url()))
                v->reload();
        }
    });
    // New Tab customization made in ANOTHER window/tab (or a reset): the
    // issuing page applies changes live; every other open new-tab page
    // re-renders so all of them agree.
    connect(m_app->config(), &AppConfig::newTabCustomizationChanged, this, [this]() {
        TabWidget *tabs = m_tabs;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            WebView *v = tabs->viewAt(i);
            if (v && v != currentView() && isNewTabUrl(v->page()->url()))
                v->reload();
        }
    });

    // Session-wide ad/tracker counter -> address-bar shield.
    connect(m_app->adBlock(), &AdBlockClient::blockedCountChanged, this,
            [this](int total) { updateBlockIndicator(total); });
    updateBlockIndicator(m_app->adBlock()->blockedTotal());
}

BrowserWindow::~BrowserWindow()
{
    // Members are still valid here; BrowserApplication uses this to release
    // the off-the-record profile exactly when the last private window dies.
    emit windowGone(m_private);
}

QWebEngineProfile *BrowserWindow::profileForWindow() const
{
    return m_private ? m_app->profiles()->acquirePrivateProfile()
                     : m_app->profiles()->regularProfile();
}

// ------------------------------------------------------------------ ui ----

void BrowserWindow::buildUi()
{
    // One chrome header, every modern-browser pattern:
    //   [ tab strip ]
    //   [ navigation row ]
    //   [ bookmarks row (only when bookmarks exist) ]
    // Below it the page stack owns 100% of the remaining space; no toolbar
    // areas, no status bar — nothing else ever steals page height.
    auto *shell = new QWidget(this);
    shell->setObjectName(QStringLiteral("shell"));
    auto *layout = new QVBoxLayout(shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_chromeHeader = new QWidget(shell);
    m_chromeHeader->setObjectName(QStringLiteral("chromeHeader"));
    m_chromeHeader->setProperty("private", m_private);
    auto *headerLayout = new QVBoxLayout(m_chromeHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    m_tabs = new TabWidget(this, shell);
    headerLayout->addWidget(m_tabs->tabRow());

    m_findBar = new FindBar(shell);

    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(3);
    m_progress->hide();

    layout->addWidget(m_chromeHeader);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(m_findBar);
    layout->addWidget(m_progress);
    setCentralWidget(shell);

    // Floating link-hoover preview (Firefox-style): a tiny label above
    // the page's bottom-start corner, visible only while a link is
    // hovered. It never occupies layout space, so the web page keeps the
    // full viewport — the fix behind the YouTube Shorts description being
    // covered by the old permanent status bar.
    m_linkPreview = new QLabel(shell);
    m_linkPreview->setObjectName(QStringLiteral("linkPreviewOverlay"));
    m_linkPreview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_linkPreview->setAttribute(Qt::WA_ShowWithoutActivating, true);
    m_linkPreview->hide();
    shell->installEventFilter(this);

    // Real desktop translucency when the platform supports it (KWin
    // blurs behind the header strip); opaque otherwise.
    Translucency::enableTranslucency(this);
    if (Translucency::isSupported())
        m_chromeHeader->setAttribute(Qt::WA_TranslucentBackground, false);
    updateChromeBlurRegion();

    connect(m_tabs, &TabWidget::currentChanged, this, [this](int) { onCurrentTabChanged(); });
    connect(m_tabs, &TabWidget::tabCountChanged, this, [this]() { refreshBookmarksRowVisibility(); });
}

void BrowserWindow::updateChromeBlurRegion()
{
    // Ask the WM to blur behind the header strip only — never the page
    // area, never the whole window (performance first).
    if (!Translucency::isSupported() || !m_chromeHeader)
        return;
    QRegion region;
    if (m_chromeHeader->isVisible())
        region += QRect(0, 0, width(), m_chromeHeader->height());
    Translucency::applyBlurBehind(this, region);
}

void BrowserWindow::createActions()
{
    auto mk = [this](const QString &icon) {
        QAction *a = new QAction(this);
        if (!icon.isEmpty())
            a->setIcon(IconProvider::icon(icon));
        return a;
    };
    m_back = mk(QStringLiteral("back"));
    m_back->setShortcut(QKeySequence::Back);
    m_forward = mk(QStringLiteral("forward"));
    m_forward->setShortcut(QKeySequence::Forward);
    m_reload = mk(QStringLiteral("reload"));
    m_reload->setShortcuts({ QKeySequence::Refresh, QKeySequence("Ctrl+R") });
    m_stop = mk(QStringLiteral("stop"));
    m_home = mk(QStringLiteral("home"));
    m_home->setShortcut(QKeySequence("Alt+Home"));
    m_star = mk(QStringLiteral("star"));

    m_newTab = mk(QStringLiteral("tab-new"));
    m_newTab->setShortcut(QKeySequence::AddTab);
    m_closeTab = mk(QStringLiteral("close"));
    m_closeTab->setShortcut(QKeySequence::Close);
    m_duplicateTab = mk(QString());
    m_duplicateTab->setShortcut(QKeySequence("Ctrl+Shift+K"));
    m_newWindow = mk(QString());
    m_newWindow->setShortcut(QKeySequence::New);
    m_newPrivateWindow = mk(QStringLiteral("private"));
    m_newPrivateWindow->setShortcut(QKeySequence("Ctrl+Shift+P"));
    m_quit = mk(QString());
    m_quit->setShortcut(QKeySequence::Quit);

    m_find = mk(QStringLiteral("search"));
    m_find->setShortcut(QKeySequence::Find);
    m_zoomIn = mk(QString());
    m_zoomIn->setShortcuts({ QKeySequence::ZoomIn, QKeySequence("Ctrl+=") });
    m_zoomOut = mk(QString());
    m_zoomOut->setShortcuts({ QKeySequence::ZoomOut });
    m_zoomReset = mk(QString());
    m_zoomReset->setShortcut(QKeySequence("Ctrl+0"));
    m_fullScreen = mk(QString());
    m_fullScreen->setShortcut(QKeySequence("F11"));
    m_bookmarksBarToggle = mk(QString());
    m_bookmarksBarToggle->setCheckable(true);

    m_showHistory = mk(QStringLiteral("history"));
    m_showHistory->setShortcut(QKeySequence("Ctrl+H"));
    m_showBookmarks = mk(QStringLiteral("bookmarks"));
    m_showDownloads = mk(QStringLiteral("downloads"));
    m_showDownloads->setShortcut(QKeySequence("Ctrl+J"));
    m_clearData = mk(QString());
    m_settings = mk(QStringLiteral("settings"));
    m_settings->setShortcut(QKeySequence("Ctrl+,"));
    m_about = mk(QString());
    m_blockingToggle = mk(QStringLiteral("shield"));
    m_blockingToggle->setCheckable(true);
    m_dataSaverToggle = mk(QString());
    m_dataSaverToggle->setCheckable(true);

    // Behavior.
    connect(m_back, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            v->back();
    });
    connect(m_forward, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            v->forward();
    });
    connect(m_reload, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            v->reload();
    });
    connect(m_stop, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            v->stop();
    });
    connect(m_home, &QAction::triggered, this, [this]() {
        openUrlInCurrentTab(QUrl(m_app->config()->homePage()));
    });
    connect(m_star, &QAction::triggered, this, &BrowserWindow::toggleBookmark);
    connect(m_newTab, &QAction::triggered, this, [this]() { openInNewForegroundTab(QUrl()); });
    connect(m_closeTab, &QAction::triggered, this, &BrowserWindow::closeCurrentTab);
    connect(m_duplicateTab, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            openInNewForegroundTab(v->page()->url());
    });
    connect(m_newWindow, &QAction::triggered, this, [this]() { m_app->createWindow(false); });
    connect(m_newPrivateWindow, &QAction::triggered, this, [this]() { m_app->createWindow(true); });
    connect(m_quit, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_find, &QAction::triggered, this, [this]() { m_findBar->activate(); });
    connect(m_zoomIn, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            v->zoomIn();
    });
    connect(m_zoomOut, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            v->zoomOut();
    });
    connect(m_zoomReset, &QAction::triggered, this, [this]() {
        if (WebView *v = currentView())
            v->zoomReset();
    });
    connect(m_fullScreen, &QAction::triggered, this, [this]() {
        onFullScreenToggled(!m_fullScreenActive);
    });
    connect(m_bookmarksBarToggle, &QAction::toggled, this, [this](bool on) {
        m_app->config()->setBookmarksBarVisible(on);
    });
    connect(m_showHistory, &QAction::triggered, this, &BrowserWindow::showHistoryDialog);
    connect(m_showBookmarks, &QAction::triggered, this, &BrowserWindow::showBookmarksDialog);
    connect(m_showDownloads, &QAction::triggered, this, &BrowserWindow::showDownloadsDialog);
    connect(m_clearData, &QAction::triggered, this, &BrowserWindow::showClearDataDialog);
    connect(m_settings, &QAction::triggered, this, &BrowserWindow::showSettingsDialog);
    connect(m_about, &QAction::triggered, this, &BrowserWindow::showAboutDialog);
    connect(m_blockingToggle, &QAction::toggled, this, [this](bool on) {
        m_app->config()->setAdBlockEnabled(on);
    });
    connect(m_dataSaverToggle, &QAction::toggled, this, [this](bool on) {
        m_app->config()->setDataSaverEnabled(on);
    });

    // Tab selection shortcuts.
    for (int i = 1; i <= 8; ++i) {
        auto *sc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i)), this);
        connect(sc, &QShortcut::activated, this, [this, i]() {
            if (i - 1 < m_tabs->count())
                m_tabs->setCurrentIndex(i - 1);
        });
    }
    auto *scLast = new QShortcut(QKeySequence(QStringLiteral("Ctrl+9")), this);
    connect(scLast, &QShortcut::activated, this, [this]() {
        if (m_tabs->count() > 0)
            m_tabs->setCurrentIndex(m_tabs->count() - 1);
    });
    auto *scFocusAddress = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(scFocusAddress, &QShortcut::activated, this, [this]() { m_address->focusAndSelect(); });
}

void BrowserWindow::buildNavRow()
{
    // The navigation row: [back][forward][reload][home]  ⇄  [ ADDRESS ]
    //  ⇄  [zoom][star][downloads][private?][menu]. The address bar is the
    // centerpiece — elastic spacers center it and let it take all spare
    // width, exactly like Firefox/Brave.
    m_navRow = new QWidget(m_chromeHeader);
    m_navRow->setObjectName(QStringLiteral("navRow"));
    auto *row = new QHBoxLayout(m_navRow);
    row->setContentsMargins(8, 6, 8, 6);
    row->setSpacing(3);

    auto navButton = [this](QAction *action) -> QToolButton * {
        auto *button = new QToolButton(m_navRow);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setContextMenuPolicy(Qt::NoContextMenu);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    };

    row->addWidget(navButton(m_back));
    row->addWidget(navButton(m_forward));
    row->addWidget(navButton(m_reload));
    row->addWidget(navButton(m_home));

    m_address = new AddressBar(this, m_navRow);
    m_address->setObjectName(QStringLiteral("addressField"));
    m_address->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_address, &AddressBar::requested, this, &BrowserWindow::navigateCurrent);

    // Zoom indicator (moved here from the removed status bar): visible
    // only when the current tab is zoomed; clicking resets it.
    m_zoomIndicator = new QToolButton(m_navRow);
    m_zoomIndicator->setObjectName(QStringLiteral("zoomIndicator"));
    m_zoomIndicator->setAutoRaise(true);
    m_zoomIndicator->setToolTip(tr("Reset zoom"));
    m_zoomIndicator->setFocusPolicy(Qt::NoFocus);
    m_zoomIndicator->hide();
    connect(m_zoomIndicator, &QToolButton::clicked, this, [this]() {
        if (WebView *v = currentView())
            v->zoomReset();
    });

    // Elastic spacers: the address bar centers when there is spare space
    // and grows into it when the window narrows.
    m_navSpacerStart = new QSpacerItem(20, 1, QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_navSpacerEnd = new QSpacerItem(20, 1, QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->addItem(m_navSpacerStart);
    row->addWidget(m_address, 1);
    row->addWidget(m_zoomIndicator);
    row->addItem(m_navSpacerEnd);

    row->addWidget(navButton(m_star));
    row->addWidget(navButton(m_showDownloads));

    if (m_private) {
        m_privateBadge = new QLabel(m_navRow);
        m_privateBadge->setObjectName(QStringLiteral("privateBadge"));
        row->addWidget(m_privateBadge);
    }

    // Hamburger: the whole classic menu bar lives in here now.
    m_appMenuButton = new QToolButton(m_navRow);
    m_appMenuButton->setObjectName(QStringLiteral("appMenuButton"));
    m_appMenuButton->setIcon(IconProvider::icon(QStringLiteral("menu")));
    m_appMenuButton->setAutoRaise(true);
    m_appMenuButton->setToolTip(tr("NØR menu"));
    m_appMenuButton->setFocusPolicy(Qt::NoFocus);
    m_appMenuButton->setPopupMode(QToolButton::InstantPopup);
    m_appMenu = new QMenu(m_appMenuButton);
    m_appMenuButton->setMenu(m_appMenu);
    row->addWidget(m_appMenuButton);

    m_chromeHeader->layout()->addWidget(m_navRow);
}

// The classic menu bar is GONE: every action moved into the hamburger
// menu (the standard desktop-browser pattern), which freed one full row
// of page space. Sections mirror the old menus so nothing is lost.
void BrowserWindow::buildAppMenu()
{
    m_menuSectionFile = m_appMenu->addMenu(QString());
    m_menuSectionView = m_appMenu->addMenu(QString());
    m_menuSectionHistory = m_appMenu->addMenu(QString());
    m_menuSectionBookmarks = m_appMenu->addMenu(QString());
    m_menuSectionTools = m_appMenu->addMenu(QString());
    m_menuSectionHelp = m_appMenu->addMenu(QString());

    // Recent-history entries are injected each time the menu opens.
    connect(m_menuSectionHistory, &QMenu::aboutToShow, this, [this]() {
        const QList<QAction *> acts = m_menuSectionHistory->actions();
        for (int i = acts.size() - 1; i >= 0; --i) {
            if (acts.at(i)->property("dynamic").toBool())
                m_menuSectionHistory->removeAction(acts.at(i));
        }
        const QVector<HistoryEntry> recent = m_app->history()->recent(8);
        if (!recent.isEmpty())
            m_menuSectionHistory->addSeparator();
        const QLocale locale = m_app->languages()->locale();
        for (const HistoryEntry &e : recent) {
            const QString label = e.title.isEmpty() ? e.url : e.title;
            QAction *a = m_menuSectionHistory->addAction(label);
            a->setProperty("dynamic", true);
            const QDateTime when = QDateTime::fromSecsSinceEpoch(e.lastVisit);
            a->setToolTip(locale.toString(when, QLocale::ShortFormat));
            connect(a, &QAction::triggered, this, [this, e]() {
                openUrlInCurrentTab(QUrl(e.url));
            });
        }
    });

    // Rebuild on every open: bookmarks and recent history stay live and
    // translations apply without re-creating actions. Blur behind the
    // popup too when the desktop supports it (Acrylic-ish menus without
    // touching rendering performance elsewhere).
    connect(m_appMenu, &QMenu::aboutToShow, this, [this]() {
        rebuildAppMenuSections();
        Translucency::applyBlurBehindWhole(m_appMenu);
    });
}

void BrowserWindow::rebuildAppMenuSections()
{
    // ---- File ----
    m_menuSectionFile->clear();
    m_menuSectionFile->setTitle(tr("File"));
    m_menuSectionFile->addAction(m_newTab);
    m_menuSectionFile->addAction(m_newWindow);
    m_menuSectionFile->addAction(m_newPrivateWindow);
    m_menuSectionFile->addSeparator();
    m_menuSectionFile->addAction(m_duplicateTab);
    m_menuSectionFile->addAction(m_closeTab);
    m_menuSectionFile->addSeparator();
    m_menuSectionFile->addAction(m_quit);

    // ---- View ----
    m_menuSectionView->clear();
    m_menuSectionView->setTitle(tr("View"));
    m_menuSectionView->addAction(m_reload);
    m_menuSectionView->addAction(m_stop);
    m_menuSectionView->addAction(m_home);
    m_menuSectionView->addSeparator();
    m_menuSectionView->addAction(m_zoomIn);
    m_menuSectionView->addAction(m_zoomOut);
    m_menuSectionView->addAction(m_zoomReset);
    m_menuSectionView->addSeparator();
    m_menuSectionView->addAction(m_bookmarksBarToggle);
    m_menuSectionView->addAction(m_fullScreen);
    m_menuSectionView->addSeparator();
    m_menuSectionView->addAction(m_find);

    // ---- History (recent entries appended on open) ----
    m_menuSectionHistory->clear();
    m_menuSectionHistory->setTitle(tr("History"));
    m_menuSectionHistory->addAction(m_back);
    m_menuSectionHistory->addAction(m_forward);
    m_menuSectionHistory->addSeparator();
    m_menuSectionHistory->addAction(m_showHistory);

    // ---- Bookmarks ----
    m_menuSectionBookmarks->clear();
    m_menuSectionBookmarks->setTitle(tr("Bookmarks"));
    m_menuSectionBookmarks->addAction(m_star);
    m_menuSectionBookmarks->addAction(m_showBookmarks);
    const QVector<Bookmark> marks = m_app->bookmarks()->bookmarks();
    if (marks.isEmpty()) {
        QAction *hint = m_menuSectionBookmarks->addAction(
            tr("No bookmarks yet — press Ctrl+D to add this page"));
        hint->setEnabled(false);
    } else {
        m_menuSectionBookmarks->addSeparator();
        for (const Bookmark &b : marks) {
            QAction *a = m_menuSectionBookmarks->addAction(
                bookmarkIcon(QUrl(b.url)), b.title.isEmpty() ? b.url : b.title);
            a->setProperty("dynamic", true);
            connect(a, &QAction::triggered, this, [this, b]() {
                openUrlInCurrentTab(QUrl(b.url));
            });
        }
    }

    // ---- Tools ----
    m_menuSectionTools->clear();
    m_menuSectionTools->setTitle(tr("Tools"));
    m_menuSectionTools->addAction(m_showDownloads);
    m_menuSectionTools->addAction(m_clearData);
    m_menuSectionTools->addSeparator();
    m_menuSectionTools->addAction(m_blockingToggle);
    m_menuSectionTools->addAction(m_dataSaverToggle);
    m_menuSectionTools->addSeparator();
    m_menuSectionTools->addAction(m_settings);

    // ---- Help ----
    m_menuSectionHelp->clear();
    m_menuSectionHelp->setTitle(tr("Help"));
    m_menuSectionHelp->addAction(m_about);
}

void BrowserWindow::buildBookmarksRow()
{
    // Bookmarks live on their own row below the navigation row. When the
    // user has NO bookmarks the row disappears entirely (the old inline
    // "No bookmarks yet" message wasted primary toolbar space); the hint
    // lives in the hamburger menu's bookmarks section instead.
    m_bookmarksRow = new QWidget(m_chromeHeader);
    m_bookmarksRow->setObjectName(QStringLiteral("bookmarksRow"));
    auto *row = new QHBoxLayout(m_bookmarksRow);
    row->setContentsMargins(10, 4, 10, 4);
    row->setSpacing(2);
    row->addStretch(1);

    m_chromeHeader->layout()->addWidget(m_bookmarksRow);
    rebuildBookmarksBar();
    connect(m_app->bookmarks(), &BookmarkStore::bookmarksChanged,
            this, [this]() { rebuildBookmarksBar(); });
}

void BrowserWindow::rebuildBookmarksBar()
{
    if (!m_bookmarksRow)
        return;
    // Clear the row (keep the trailing stretch item).
    QLayout *row = m_bookmarksRow->layout();
    while (row->count() > 1) {
        QLayoutItem *item = row->takeAt(0);
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    const QVector<Bookmark> marks = m_app->bookmarks()->bookmarks();
    for (const Bookmark &b : marks) {
        QAction *action = new QAction(bookmarkIcon(QUrl(b.url)),
                                      b.title.isEmpty() ? b.url : b.title, this);
        action->setToolTip(b.url);
        connect(action, &QAction::triggered, this, [this, b]() {
            openUrlInCurrentTab(QUrl(b.url));
        });
        auto *button = new QToolButton(m_bookmarksRow);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        button->installEventFilter(this); // middle click -> new background tab
        connect(button, &QToolButton::customContextMenuRequested, this,
                [this, b, button](const QPoint &pos) {
                    QMenu menu(button);
                    QAction *newTab = menu.addAction(tr("Open in new tab"));
                    QAction *remove = menu.addAction(tr("Remove bookmark"));
                    QAction *chosen = menu.exec(button->mapToGlobal(pos));
                    if (!chosen)
                        return;
                    if (chosen == newTab)
                        openInNewBackgroundTab(QUrl(b.url));
                    else if (chosen == remove)
                        m_app->bookmarks()->removeBookmark(b.id);
                });
        m_bookmarksRow->layout()->addWidget(button);
    }
    refreshBookmarksRowVisibility();
    updateChromeBlurRegion();
}

QIcon BrowserWindow::bookmarkIcon(const QUrl &url)
{
    // Real favicon from the local cache when we have one (never fetched
    // remotely), the star glyph otherwise.
    const QString host = url.host();
    if (!host.isEmpty() && m_app->favicons() && m_app->favicons()->hasIcon(host)) {
        const QByteArray png = m_app->favicons()->iconPng(host);
        QPixmap pm;
        if (!png.isEmpty() && pm.loadFromData(png, "PNG"))
            return QIcon(pm);
    }
    return IconProvider::icon(QStringLiteral("star"));
}

void BrowserWindow::refreshBookmarksRowVisibility()
{
    if (!m_bookmarksRow)
        return;
    const bool hasBookmarks = !m_app->bookmarks()->bookmarks().isEmpty();
    const bool visible = hasBookmarks && m_bookmarksBarToggle->isChecked()
                         && !m_fullScreenActive;
    m_bookmarksRow->setVisible(visible);
    updateChromeBlurRegion();
}

bool BrowserWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Keep the floating link-preview glued to the page area while the
    // window (or the find bar) changes the central widget's geometry.
    if (watched == centralWidget() && event->type() == QEvent::Resize)
        positionLinkPreview();

    // Middle-click on a bookmark button opens it in a background tab.
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::MiddleButton) {
            if (auto *button = qobject_cast<QToolButton *>(watched)) {
                if (QAction *action = button->defaultAction()) {
                    const QString url = action->toolTip();
                    if (!url.isEmpty()) {
                        openInNewBackgroundTab(QUrl(url));
                        return true;
                    }
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void BrowserWindow::applyUiSettings()
{
    const bool barVisible = m_app->config()->bookmarksBarVisible();
    if (m_bookmarksBarToggle->isChecked() != barVisible)
        m_bookmarksBarToggle->setChecked(barVisible);
    refreshBookmarksRowVisibility();
    m_blockingToggle->setChecked(m_app->config()->adBlockEnabled());
    m_dataSaverToggle->setChecked(m_app->config()->dataSaverEnabled());
}

// ------------------------------------------------------------- actions ----

void BrowserWindow::newInitialTab(const QUrl &url)
{
    m_tabs->newTab(profileForWindow(), url);
}

WebPage *BrowserWindow::openInNewForegroundTab(const QUrl &url)
{
    return m_tabs->newTab(profileForWindow(), url, false);
}

WebPage *BrowserWindow::openInNewBackgroundTab(const QUrl &url)
{
    return m_tabs->newTab(profileForWindow(), url, true);
}

WebPage *BrowserWindow::openInNewWindow(const QUrl &url)
{
    BrowserWindow *window = m_app->createWindow(m_private);
    if (!window)
        return nullptr;
    return window->openInNewForegroundTab(url);
}

void BrowserWindow::closeCurrentTab()
{
    m_tabs->closeCurrentTab();
}

void BrowserWindow::closeWindow()
{
    close();
}

int BrowserWindow::tabCount() const
{
    return m_tabs->count();
}

WebView *BrowserWindow::currentView() const
{
    return m_tabs->currentWebView();
}

void BrowserWindow::navigateCurrent(const QString &text)
{
    const SearchEngine engine = m_app->searchEngines()->defaultEngine();
    const QUrl url = SearchEngines::urlForNavigation(text, engine);
    if (url.isValid())
        openUrlInCurrentTab(url);
    m_address->clearFocus();
}

void BrowserWindow::openUrlInCurrentTab(const QUrl &url)
{
    WebView *view = currentView();
    if (!view) {
        openInNewForegroundTab(url);
        return;
    }
    view->load(url);
}

void BrowserWindow::toggleBookmark()
{
    WebView *view = currentView();
    if (!view)
        return;
    const QString url = view->page()->url().toString();
    if (url.isEmpty() || isNewTabUrl(view->page()->url()))
        return;
    if (m_app->bookmarks()->containsUrl(url)) {
        m_app->bookmarks()->removeBookmarkByUrl(url);
    } else {
        QString title = view->page()->title();
        if (title.isEmpty())
            title = url;
        m_app->bookmarks()->addBookmark(title, url);
    }
    updateStarState();
}

void BrowserWindow::updateStarState()
{
    WebView *view = currentView();
    const bool marked = view && m_app->bookmarks()->containsUrl(view->page()->url().toString());
    m_star->setIcon(IconProvider::icon(marked ? QStringLiteral("star-filled")
                                              : QStringLiteral("star")));
}

// ------------------------------------------------------------ dialogs ----

void BrowserWindow::showSettingsDialog()
{
    SettingsDialog dialog(m_app, this);
    connect(&dialog, &SettingsDialog::languageSwitchRequested, this,
            [this](const QString &code) { m_app->languages()->setLanguage(code); });
    dialog.exec();
}

void BrowserWindow::showHistoryDialog()
{
    HistoryDialog dialog(m_app, this);
    connect(&dialog, &HistoryDialog::openUrlRequested, this,
            [this](const QUrl &url) { openUrlInCurrentTab(url); });
    dialog.exec();
}

void BrowserWindow::showBookmarksDialog()
{
    BookmarksDialog dialog(m_app, this);
    connect(&dialog, &BookmarksDialog::openUrlRequested, this,
            [this](const QUrl &url) { openUrlInCurrentTab(url); });
    dialog.exec();
}

void BrowserWindow::showDownloadsDialog()
{
    DownloadsDialog dialog(m_app, this);
    dialog.exec();
}

void BrowserWindow::showClearDataDialog()
{
    ClearDataDialog dialog(m_app, this);
    dialog.exec();
}

void BrowserWindow::showAboutDialog()
{
    QMessageBox::about(this, tr("About NØR Browser"),
                       tr("<b>NØR Browser %1</b><br/>A private-by-design web browser."
                          "<br/><br/>Built with Qt %2 and Qt WebEngine."
                          "<br/>No accounts, no telemetry, no cloud."
                          "<br/><br/>Free software under the GNU GPL v3 or later "
                          "(GPL-3.0-or-later).<br/>"
                          "\xC2\xA9 2026 The N\xC3\x98R Browser authors \xE2\x80\x94 see "
                          "the LICENSE file.")
                           .arg(QStringLiteral(NOR_VERSION), QString::fromLatin1(qVersion())));
}

// ---------------------------------------------------------- tab state ----

void BrowserWindow::onCurrentTabChanged()
{
    WebView *view = currentView();
    if (!view) {
        m_address->displayUrl(QUrl());
        return;
    }
    WebPage *page = qobject_cast<WebPage *>(view->page());
    m_address->displayUrl(page ? page->url() : QUrl());
    m_findBar->attachPage(page);
    // Keep the per-site shield accurate as the user switches tabs: the
    // current page's URL drives the per-site counter.
    if (page) {
        connect(page, &QWebEnginePage::urlChanged, this,
                &BrowserWindow::updateAddressBarStats, Qt::UniqueConnection);
    }
    updateAddressBarStats();
    m_back->setEnabled(view->history()->canGoBack());
    m_forward->setEnabled(view->history()->canGoForward());
    m_loading = false;
    m_progress->hide();
    updateWindowTitle();
    updateStarState();
    updateZoomIndicator(view->zoomFactor());
}

void BrowserWindow::updateWindowTitle()
{
    // One source of truth: the CURRENT view's page state, with stale titles
    // filtered out (WebPage::titleIsStale — a failed load may leave the
    // previous document's <title> behind). The window title, the tab label
    // and the address bar all derive from the same snapshot, so they can
    // never disagree about which page is showing.
    WebView *view = currentView();
    QString title;
    if (view) {
        const QUrl url = view->page()->url();
        if (isNewTabUrl(url)) {
            title = tr("New Tab");
        } else {
            const auto *page = qobject_cast<WebPage *>(view->page());
            const QString pageTitle = (page && page->titleIsStale())
                                          ? QString() : view->page()->title();
            title = pageTitle.isEmpty() ? url.toString() : pageTitle;
        }
    }
    const QString product = QStringLiteral("NØR Browser");
    setWindowTitle(m_private
                       ? QStringLiteral("%1 — %2 — %3").arg(tr("Private"), title, product)
                       : QStringLiteral("%1 — %2").arg(title, product));
}

void BrowserWindow::updateZoomIndicator(qreal factor)
{
    if (!m_zoomIndicator)
        return;
    if (qFuzzyCompare(factor, 1.0)) {
        m_zoomIndicator->hide();
    } else {
        m_zoomIndicator->setText(QStringLiteral("%1%").arg(qRound(factor * 100)));
        m_zoomIndicator->setToolTip(tr("Zoom %1% — click to reset")
                                         .arg(qRound(factor * 100)));
        m_zoomIndicator->show();
    }
}

void BrowserWindow::onLoadingStarted()
{
    m_loading = true;
    m_progress->setValue(1);
    m_progress->show();
}

void BrowserWindow::onLoadProgress(int progress)
{
    if (progress >= 100) {
        m_progress->setValue(100);
        QTimer::singleShot(600, this, [this]() {
            if (!m_loading)
                m_progress->hide();
        });
    } else {
        m_progress->setValue(progress);
    }
}

void BrowserWindow::onFullScreenToggled(bool on)
{
    m_fullScreenActive = on;
    if (on) {
        showFullScreen();
        m_chromeHeader->hide();
        m_findBar->hide();
        hideLinkPreview();
    } else {
        showNormal();
        m_chromeHeader->show();
        applyUiSettings();
    }
    updateChromeBlurRegion();
}

void BrowserWindow::updateBlockIndicator(int total)
{
    // The counter lives in the address-bar shield (site count / session
    // count tooltip); the old status-bar button is gone with the bar.
    Q_UNUSED(total);
    updateAddressBarStats();
}

void BrowserWindow::updateAddressBarStats()
{
    if (!m_address)
        return;
    const int total = m_app->adBlock()->blockedTotal();
    int site = 0;
    if (const WebView *view = currentView()) {
        const QString host = view->page()->url().host().toLower();
        if (!host.isEmpty())
            site = m_app->adBlock()->blockedForSite(host);
    }
    m_address->setBlockStats(site, total);
}

void BrowserWindow::positionLinkPreview()
{
    if (!m_linkPreview || !m_linkPreview->isVisible())
        return;
    QWidget *central = centralWidget();
    if (!central)
        return;
    const int margin = 8;
    const int maxWidth = qMax(120, central->width() * 3 / 5);
    QString text = m_linkPreview->toolTip(); // full URL kept on the tooltip
    QFontMetrics fm(m_linkPreview->font());
    if (fm.horizontalAdvance(text) > maxWidth)
        text = fm.elidedText(text, Qt::ElideMiddle, maxWidth);
    m_linkPreview->setText(text);
    m_linkPreview->adjustSize();
    // Bottom-start corner, inside the page area, mirroring the layout
    // direction (RTL puts it bottom-right).
    if (layoutDirection() == Qt::RightToLeft)
        m_linkPreview->move(central->width() - m_linkPreview->width() - margin,
                             central->height() - m_linkPreview->height() - margin);
    else
        m_linkPreview->move(margin, central->height() - m_linkPreview->height() - margin);
    m_linkPreview->raise();
}

void BrowserWindow::hideLinkPreview()
{
    if (m_linkPreview)
        m_linkPreview->hide();
}

void BrowserWindow::setLinkPreview(const QString &url)
{
    if (!m_linkPreview)
        return;
    if (url.isEmpty()) {
        // Chromium sends the empty string when the hover ends.
        hideLinkPreview();
        return;
    }
    m_linkPreview->setToolTip(url);
    positionLinkPreview();
    m_linkPreview->show();
}

// ----------------------------------------------------------- permissions --

QString BrowserWindow::storedPermission(const QString &origin, const QString &feature) const
{
    return m_app->config()->permission(origin, feature);
}

void BrowserWindow::storePermission(const QString &origin, const QString &feature,
                                    const QString &value)
{
    m_app->config()->setPermission(origin, feature, value);
}

HistoryStore *BrowserWindow::historyStore() const
{
    return m_app->history();
}

// ---------------------------------------------------------------- events --

void BrowserWindow::closeEvent(QCloseEvent *event)
{
    // Only a fully laid-out regular window defines the remembered size.
    // Private windows are transient and a window closed before layout
    // settles (e.g. automated runs) would otherwise persist a degenerate
    // geometry that the next start restores as a broken 30px-tall window.
    if (!m_private && width() >= MinimumSaneWidth && height() >= MinimumSaneHeight)
        m_app->config()->setWindowGeometry(saveGeometry());
    QMainWindow::closeEvent(event);
}

void BrowserWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // QWidget::restoreGeometry() defers applying the saved size until the
    // widget is shown, so a constructor-time check sees only the default
    // size. Re-check one event-loop turn after the first show, when the
    // deferred (possibly degenerate) geometry has actually been applied.
    if (!m_geometrySanitized) {
        m_geometrySanitized = true;
        QTimer::singleShot(0, this, [this]() { sanitizeRestoredGeometry(); });
    }
}

void BrowserWindow::sanitizeRestoredGeometry()
{
    // Restored geometry can be stale or corrupt (e.g. saved before the
    // first layout pass, or left by an interrupted session): fall back to
    // a sane default instead of showing a sliver-sized window, and clamp
    // to the available screen area.
    if (width() < MinimumSaneWidth || height() < MinimumSaneHeight) {
        qWarning("BrowserWindow: ignoring corrupt restored geometry %dx%d",
                 width(), height());
        resize(1200, 750);
    }
    if (const QScreen *screen = window()->screen()) {
        const QRect avail = screen->availableGeometry();
        if (!avail.isEmpty()) {
            if (width() > avail.width() || height() > avail.height())
                resize(qMin(width(), avail.width()), qMin(height(), avail.height()));
            if (!avail.intersects(geometry()))
                move(avail.topLeft());
        }
    }
}

void BrowserWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        QCoreApplication::instance()->setProperty("norActiveWindow",
                                                  QVariant::fromValue<QWidget *>(this));
    QMainWindow::changeEvent(event);
}

// --------------------------------------------------------- retranslate ----

void BrowserWindow::retranslateUi()
{
    m_back->setText(tr("Back"));
    m_forward->setText(tr("Forward"));
    m_reload->setText(tr("Reload"));
    m_stop->setText(tr("Stop"));
    m_home->setText(tr("Home"));
    m_star->setText(tr("Bookmark this page"));
    m_star->setToolTip(tr("Bookmark this page (Ctrl+D)"));
    m_newTab->setText(tr("New tab"));
    m_closeTab->setText(tr("Close tab"));
    m_duplicateTab->setText(tr("Duplicate tab"));
    m_newWindow->setText(tr("New window"));
    m_newPrivateWindow->setText(tr("New private window"));
    m_quit->setText(tr("Quit"));
    m_find->setText(tr("Find in page…"));
    m_zoomIn->setText(tr("Zoom in"));
    m_zoomOut->setText(tr("Zoom out"));
    m_zoomReset->setText(tr("Reset zoom"));
    m_fullScreen->setText(tr("Full screen"));
    m_bookmarksBarToggle->setText(tr("Bookmarks bar"));
    m_showHistory->setText(tr("Browsing history…"));
    m_showBookmarks->setText(tr("Manage bookmarks…"));
    m_showDownloads->setText(tr("Downloads…"));
    m_showDownloads->setToolTip(tr("Downloads (Ctrl+J)"));
    m_clearData->setText(tr("Clear browsing data…"));
    m_settings->setText(tr("Settings…"));
    m_about->setText(tr("About NØR Browser"));
    m_blockingToggle->setText(tr("Block ads and trackers"));
    m_blockingToggle->setToolTip(tr("Block known ad and tracker requests (recommended)"));
    m_dataSaverToggle->setText(tr("Data Saver"));
    m_find->setToolTip(tr("Find in page (Ctrl+F)"));

    rebuildAppMenuSections();

    m_address->setPlaceholderText(tr("Search or enter address"));
    if (m_privateBadge)
        m_privateBadge->setText(tr("Private"));
    m_tabs->retranslateUi();
    m_findBar->retranslateUi();
    rebuildBookmarksBar();
    updateWindowTitle();
    if (const WebView *view = currentView())
        updateZoomIndicator(view->zoomFactor());
    updateBlockIndicator(m_app->adBlock()->blockedTotal());
}
