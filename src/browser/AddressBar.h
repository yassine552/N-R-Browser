// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QLineEdit>

class AddressBarSuggestions;
class BrowserWindow;
class QCompleter;
class QStringListModel;
class QTimer;

// The address/search field. Emits requested() on Enter; supports
// "Paste and Go"; shows a security glyph (lock/globe) for the current URL.
// Local autocomplete from history and bookmarks (disableable).
class AddressBar : public QLineEdit
{
    Q_OBJECT
public:
    explicit AddressBar(BrowserWindow *window, QWidget *parent = nullptr);

    // Shows the display URL (nor://newtab is shown as an empty field with
    // placeholder).
    void displayUrl(const QUrl &url);
    void focusAndSelect();

    // Per-site + session ad-block statistics shown as a trailing shield.
    void setBlockStats(int siteBlocked, int totalBlocked);

signals:
    void requested(const QString &text);

protected:
    void focusInEvent(QFocusEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setSecurityGlyph(const QUrl &url);
    // Rebuilds the local suggestion model for the current text.
    void refreshSuggestions();
    // A suggestion was picked: navigate to its URL part.
    void onSuggestionActivated(const QString &display);

    BrowserWindow *m_window;
    QAction *m_glyphAction = nullptr;
    QAction *m_shieldAction = nullptr;
    AddressBarSuggestions *m_suggestions = nullptr;
    QStringListModel *m_suggestionModel = nullptr;
    QCompleter *m_completer = nullptr;
    QTimer *m_suggestionDebounce = nullptr;
};
