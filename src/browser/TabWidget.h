#pragma once

#include <QIcon>
#include <QWidget>

class BrowserWindow;
class QStackedWidget;
class QTabBar;
class QToolButton;
class WebPage;
class WebView;
class QWebEngineProfile;

// The tab container of a browser window.
//
// Layout model (matches every modern desktop browser): the TAB STRIP is a
// standalone row widget that the BrowserWindow places at the very top of
// the chrome header; the PAGES live in a QStackedWidget that fills the
// viewport below the header. Splitting the two is what allows the tab row
// to sit ABOVE the navigation row without detaching QTabBar internals.
//
// The public API intentionally mirrors the QTabWidget surface the rest of
// the code used (count/currentIndex/setCurrentIndex/viewAt/tabBar/...).
class TabWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TabWidget(BrowserWindow *window, QWidget *parent = nullptr);

    // The standalone tab strip (owns the QTabBar and the new-tab button).
    // Insert it into the window header; it stays owned by this TabWidget.
    QWidget *tabRow() const { return m_rowHost; }
    QTabBar *tabBar() const { return m_bar; }

    WebPage *newTab(QWebEngineProfile *profile, const QUrl &url,
                    bool background = false);
    void closeTab(int index);
    void closeCurrentTab();

    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

    WebView *currentWebView() const;
    WebView *viewAt(int index) const;

    void retranslateUi();

    static QString displayTitle(const QUrl &url, const QString &title);

signals:
    void currentChanged(int index);
    void tabCountChanged();

private:
    void showTabContextMenu(const QPoint &pos);
    void duplicateTab(int index);
    QIcon iconForUrl(const QUrl &url) const;
    void syncTabOptions(int index);

    BrowserWindow *m_window = nullptr;
    QWidget *m_rowHost = nullptr;        // the strip shown at the top
    QTabBar *m_bar = nullptr;
    QToolButton *m_newTabButton = nullptr;
    QStackedWidget *m_stack = nullptr;
};
