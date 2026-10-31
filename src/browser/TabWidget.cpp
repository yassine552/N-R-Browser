#include "TabWidget.h"

#include "BrowserApplication.h"
#include "BrowserWindow.h"
#include "FaviconStore.h"
#include "IconProvider.h"
#include "NewTabSchemeHandler.h" // isNewTabUrl
#include "Theme.h"
#include "WebPage.h"
#include "WebView.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QMenu>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineProfile>

using namespace std::chrono_literals;

TabWidget::TabWidget(BrowserWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    // Pages stack: this widget fills the viewport below the chrome header.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_stack = new QStackedWidget(this);
    m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_stack);

    // The standalone tab strip the window places above the nav row.
    m_rowHost = new QWidget(nullptr);
    m_rowHost->setObjectName(QStringLiteral("tabRow"));
    auto *rowLayout = new QHBoxLayout(m_rowHost);
    rowLayout->setContentsMargins(4, 0, 4, 0);
    rowLayout->setSpacing(0);

    m_bar = new QTabBar(m_rowHost);
    // THE objectName the chrome stylesheet targets: without it every
    // QTabBar#tabBar rule silently matched nothing and the tab strip fell
    // back to the platform style (wrong height/padding, no accent
    // underline, a stray 2px base line under the strip).
    m_bar->setObjectName(QStringLiteral("tabBar"));
    m_bar->setDocumentMode(true);
    m_bar->setExpanding(false);
    m_bar->setTabsClosable(true);
    m_bar->setMovable(true);
    m_bar->setUsesScrollButtons(true);
    m_bar->setElideMode(Qt::ElideRight);
    m_bar->setIconSize(QSize(Theme::Metrics::TabIconSize, Theme::Metrics::TabIconSize));
    m_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_bar->setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
    m_bar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bar, &QTabBar::customContextMenuRequested,
            this, &TabWidget::showTabContextMenu);
    // The bar is the single source of truth for "current": both the page
    // stack and the window follow it.
    connect(m_bar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        emit currentChanged(index);
    });
    connect(m_bar, &QTabBar::tabCloseRequested, this, &TabWidget::closeTab);
    connect(m_bar, &QTabBar::tabMoved, this, [this](int from, int to) {
        QWidget *page = m_stack->widget(from);
        m_stack->removeWidget(page);
        m_stack->insertWidget(to, page);
        m_stack->setCurrentIndex(m_bar->currentIndex());
    });
    rowLayout->addWidget(m_bar);

    m_newTabButton = new QToolButton(m_rowHost);
    m_newTabButton->setObjectName(QStringLiteral("newTabButton"));
    m_newTabButton->setIcon(IconProvider::icon(QStringLiteral("tab-new")));
    m_newTabButton->setAutoRaise(true);
    m_newTabButton->setToolTip(tr("New tab (Ctrl+T)"));
    connect(m_newTabButton, &QToolButton::clicked, this, [this]() {
        if (m_window)
            m_window->openInNewForegroundTab(QUrl());
    });
    rowLayout->addWidget(m_newTabButton, 0, Qt::AlignVCenter);
}

WebPage *TabWidget::newTab(QWebEngineProfile *profile, const QUrl &url, bool background)
{
    WebView *view = new WebView(this);
    WebPage *page = new WebPage(profile, m_window, view);
    view->setPage(page);
    // Match the page background to the surrounding chrome: Chromium
    // paints the page background before the first composited frame, so a
    // hidden tab being shown would otherwise flash WHITE for a frame —
    // very visible in the dark theme. This is the standard fix and adds
    // no artificial delay; the page is never reloaded by switching.
    page->setBackgroundColor(Theme::webBackground());

    const int index = m_stack->addWidget(view);
    m_bar->addTab(QString());
    if (!background)
        setCurrentIndex(index);   // bar -> stack -> currentChanged

    connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl &url) {
        const int idx = m_stack->indexOf(view);
        if (idx >= 0) {
            m_bar->setTabToolTip(idx, url.toString());
            if (m_bar->tabIcon(idx).isNull())
                m_bar->setTabIcon(idx, iconForUrl(url));
        }
        if (m_window && view == currentWebView())
            m_window->onCurrentTabChanged();
    });
    connect(view, &QWebEngineView::titleChanged, this, [this, view](const QString &title) {
        const int idx = m_stack->indexOf(view);
        if (idx >= 0)
            m_bar->setTabText(idx, displayTitle(view->page()->url(), title));
        if (m_window && view == currentWebView())
            m_window->updateWindowTitle();
    });
    connect(view, &QWebEngineView::iconChanged, this, [this, view](const QIcon &icon) {
        const int idx = m_stack->indexOf(view);
        if (idx >= 0 && !icon.isNull())
            m_bar->setTabIcon(idx, icon);
        // Feed the local favicon cache used by the new-tab Top Sites —
        // but ONLY from the regular profile: a private session must
        // never leave traces of visited hosts on disk.
        if (m_window && view->page() && !view->page()->profile()->isOffTheRecord()) {
            if (FaviconStore *favicons = m_window->app()->favicons())
                favicons->capture(icon, view->page()->url());
        }
    });
    connect(view, &WebView::zoomChanged, m_window, &BrowserWindow::updateZoomIndicator);

    // Window-wide state driven by the CURRENT tab only.
    connect(view, &QWebEngineView::loadStarted, this, [this, view]() {
        if (m_window && view == currentWebView())
            m_window->onLoadingStarted();
    });
    connect(view, &QWebEngineView::loadProgress, this, [this, view](int p) {
        if (m_window && view == currentWebView())
            m_window->onLoadProgress(p);
    });
    connect(view, &QWebEngineView::loadFinished, this, [this, view](bool ok) {
        const int idx = m_stack->indexOf(view);
        if (!ok && idx >= 0) {
            // FAILED navigation: Chromium can leave the PREVIOUS page's
            // <title> behind (titleChanged never fires for the error
            // document). A stale title next to the old content is exactly
            // the "title says 7-Zip, viewport shows New Tab" mismatch:
            // drop it so the tab label always matches the visible
            // document (host / URL until a real title arrives).
            m_bar->setTabText(idx, displayTitle(view->page()->url(), QString()));
        }
        if (m_window && view == currentWebView()) {
            m_window->onLoadProgress(100);
            m_window->onCurrentTabChanged(); // full chrome re-sync (address bar included)
        }
    });
    connect(page, &QWebEnginePage::linkHovered, m_window, &BrowserWindow::setLinkPreview);
    connect(page, &WebPage::fullScreenToggled, m_window,
            &BrowserWindow::onFullScreenToggled);
    // Render-process crash handling: offer reload instead of a dead page.
    connect(page, &QWebEnginePage::renderProcessTerminated, view,
            [view](QWebEnginePage::RenderProcessTerminationStatus status, int code) {
                Q_UNUSED(code);
                if (status != QWebEnginePage::NormalTerminationStatus)
                    view->showCrashOverlay();
            });

    const QUrl target = url.isEmpty() ? QUrl(QStringLiteral("nor://newtab")) : url;
    view->load(target);
    m_bar->setTabText(index, displayTitle(target, QString()));
    m_bar->setTabIcon(index, iconForUrl(target));
    emit tabCountChanged();
    return page;
}

QIcon TabWidget::iconForUrl(const QUrl &url) const
{
    // New-tab pages carry the NØR brand mark (like Firefox's logo on its
    // new-tab tabs); every other page shows its real favicon once known.
    if (isNewTabUrl(url) || url.isEmpty())
        return IconProvider::appIcon();
    return QIcon();
}

void TabWidget::closeTab(int index)
{
    if (index < 0 || index >= count())
        return;
    QWidget *w = m_stack->widget(index);
    m_bar->removeTab(index);
    m_stack->removeWidget(w);
    w->deleteLater();
    if (count() == 0) {
        if (m_window)
            m_window->closeWindow();
    } else {
        emit tabCountChanged();
    }
}

void TabWidget::closeCurrentTab()
{
    closeTab(currentIndex());
}

int TabWidget::count() const
{
    return m_stack->count();
}

int TabWidget::currentIndex() const
{
    return m_bar->currentIndex();
}

void TabWidget::setCurrentIndex(int index)
{
    m_bar->setCurrentIndex(index);
    m_stack->setCurrentIndex(index);
}

WebView *TabWidget::currentWebView() const
{
    return qobject_cast<WebView *>(m_stack->currentWidget());
}

WebView *TabWidget::viewAt(int index) const
{
    return qobject_cast<WebView *>(m_stack->widget(index));
}

QString TabWidget::displayTitle(const QUrl &url, const QString &title)
{
    if (isNewTabUrl(url) || (url.isEmpty() && title.isEmpty()))
        return QCoreApplication::translate("TabWidget", "New Tab");
    if (!title.isEmpty())
        return title;
    const QString host = url.host();
    return host.isEmpty() ? url.toString() : host;
}

void TabWidget::duplicateTab(int index)
{
    WebView *view = viewAt(index);
    if (!view || !m_window)
        return;
    m_window->openInNewForegroundTab(view->page()->url());
}

void TabWidget::showTabContextMenu(const QPoint &pos)
{
    const int index = m_bar->tabAt(pos);
    if (index < 0 || !m_window)
        return;
    WebView *view = viewAt(index);

    QMenu menu(this);
    QAction *duplicate = menu.addAction(tr("Duplicate tab"));
    QAction *copyUrl = menu.addAction(tr("Copy link address"));
    menu.addSeparator();
    QAction *closeOthers = menu.addAction(tr("Close other tabs"));
    QAction *closeRight = menu.addAction(tr("Close tabs to the right"));
    menu.addSeparator();
    QAction *close = menu.addAction(tr("Close tab"));

    QAction *chosen = menu.exec(m_bar->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == duplicate) {
        duplicateTab(index);
    } else if (chosen == copyUrl && view) {
        QApplication::clipboard()->setText(view->page()->url().toString());
    } else if (chosen == closeOthers) {
        QWidget *clicked = m_stack->widget(index);
        const int total = count();
        for (int i = total - 1; i >= 0; --i)
            if (m_stack->widget(i) != clicked)
                closeTab(i);
    } else if (chosen == closeRight) {
        for (int i = count() - 1; i > index; --i)
            closeTab(i);
    } else if (chosen == close) {
        closeTab(index);
    }
}

void TabWidget::retranslateUi()
{
    m_newTabButton->setToolTip(tr("New tab (Ctrl+T)"));
    for (int i = 0; i < count(); ++i) {
        WebView *view = viewAt(i);
        if (view)
            m_bar->setTabText(i, displayTitle(view->page()->url(), view->page()->title()));
    }
}
