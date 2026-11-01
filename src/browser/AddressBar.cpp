// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "AddressBar.h"

#include "AddressBarSuggestions.h"
#include "AppConfig.h"
#include "BrowserApplication.h"
#include "BrowserWindow.h"
#include "HistoryStore.h"
#include "IconProvider.h"
#include "NewTabSchemeHandler.h" // isNewTabUrl

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QStringListModel>
#include <QTimer>
#include <QUrl>

using namespace std::chrono_literals;

AddressBar::AddressBar(BrowserWindow *window, QWidget *parent)
    : QLineEdit(parent)
    , m_window(window)
{
    setClearButtonEnabled(true);
    m_glyphAction = addAction(IconProvider::icon(QStringLiteral("globe")),
                              QLineEdit::LeadingPosition);
    m_glyphAction->setVisible(true);

    connect(this, &QLineEdit::returnPressed, this, [this]() {
        const QString text = this->text().trimmed();
        if (!text.isEmpty())
            emit requested(text);
    });

    // Local autocomplete from history and bookmarks (never a remote
    // service). Debounced so typing stays responsive; disabled entirely
    // through the settings (ui/addressBarSuggestions).
    if (m_window && m_window->app()) {
        m_shieldAction = addAction(IconProvider::icon(QStringLiteral("shield")),
                                   QLineEdit::TrailingPosition);
        m_shieldAction->setVisible(false);

        m_suggestions = new AddressBarSuggestions(
            *m_window->app()->config(), m_window->historyStore(),
            m_window->app()->bookmarks(), this);
        m_suggestionModel = new QStringListModel(this);
        m_completer = new QCompleter(m_suggestionModel, this);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setFilterMode(Qt::MatchContains);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setMaxVisibleItems(8);
        setCompleter(m_completer);
        connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
                this, &AddressBar::onSuggestionActivated);

        m_suggestionDebounce = new QTimer(this);
        m_suggestionDebounce->setSingleShot(true);
        m_suggestionDebounce->setInterval(120);
        connect(m_suggestionDebounce, &QTimer::timeout, this,
                &AddressBar::refreshSuggestions);
        connect(this, &QLineEdit::textEdited, this, [this]() {
            if (m_window->app()->config()->addressBarSuggestions())
                m_suggestionDebounce->start();
        });
    }
}

void AddressBar::setBlockStats(int siteBlocked, int totalBlocked)
{
    if (!m_shieldAction)
        return;
    m_shieldAction->setVisible(totalBlocked > 0);
    m_shieldAction->setText(siteBlocked > 0 ? QString::number(siteBlocked)
                                            : QString());
    m_shieldAction->setToolTip(
        tr("%1 blocked on this site · %2 blocked this session")
            .arg(siteBlocked)
            .arg(totalBlocked));
    m_shieldAction->setIcon(
        IconProvider::icon(QStringLiteral("shield")));
}

void AddressBar::displayUrl(const QUrl &url)
{
    // isNewTabUrl matches every canonicalization ("nor://newtab/") — a
    // string compare used to leak the raw internal URL into the field.
    if (isNewTabUrl(url)) {
        clear();
        setPlaceholderText(tr("Search or enter address"));
    } else {
        setText(url.toString());
    }
    setSecurityGlyph(url);
}

void AddressBar::focusAndSelect()
{
    setFocus(Qt::ShortcutFocusReason);
    selectAll();
}

void AddressBar::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    selectAll();
}

void AddressBar::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();
    const QString clip = QApplication::clipboard()->text().trimmed();
    if (!clip.isEmpty() && !isReadOnly()) {
        menu->addSeparator();
        QAction *pasteGo = menu->addAction(tr("Paste and Go"));
        connect(pasteGo, &QAction::triggered, this, [this, clip]() {
            setText(clip);
            emit requested(clip);
        });
    }
    menu->exec(event->globalPos());
    delete menu;
}

void AddressBar::resizeEvent(QResizeEvent *event)
{
    QLineEdit::resizeEvent(event);
    // Keep enough room for the security glyph (leading edge) and the
    // clear button (trailing edge) — mirrored for right-to-left layout.
    const int glyph = height() / 2;
    if (layoutDirection() == Qt::RightToLeft)
        setTextMargins(2, 0, glyph, 0);
    else
        setTextMargins(glyph, 0, 2, 0);
}

void AddressBar::setSecurityGlyph(const QUrl &url)
{
    QString name = QStringLiteral("globe");
    if (url.scheme() == QLatin1String("https"))
        name = QStringLiteral("lock");
    else if (url.scheme() == QLatin1String("nor"))
        name = QStringLiteral("home");
    m_glyphAction->setIcon(IconProvider::icon(name));
    m_glyphAction->setToolTip(url.scheme() == QLatin1String("https")
                                  ? tr("Secure HTTPS connection")
                                  : tr("Address"));
}

// ------------------------------------------------------- autocomplete -----

void AddressBar::refreshSuggestions()
{
    if (!m_suggestions || !m_suggestionModel)
        return;
    const QStringList suggestions = m_suggestions->suggestionsFor(text());
    m_suggestionModel->setStringList(suggestions);
    // Re-arm the completer so contains-matching runs against the new list.
    m_completer->setCompletionPrefix(QString());
    if (!suggestions.isEmpty())
        m_completer->setCompletionPrefix(text());
}

void AddressBar::onSuggestionActivated(const QString &display)
{
    const QString url = AddressBarSuggestions::urlFromDisplay(display);
    setText(url);
    emit requested(url);
}
