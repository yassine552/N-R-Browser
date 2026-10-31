#include "FindBar.h"

#include "IconProvider.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWebEngineFindTextResult>
#include <QWebEnginePage>

using namespace std::chrono_literals;

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("findBar"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("Find in page"));
    m_edit->setClearButtonEnabled(true);
    setFocusProxy(m_edit);

    m_prev = new QPushButton(IconProvider::icon(QStringLiteral("find-previous")),
                             QString(), this);
    m_next = new QPushButton(IconProvider::icon(QStringLiteral("find-next")),
                             QString(), this);
    m_close = new QPushButton(IconProvider::icon(QStringLiteral("close")),
                              QString(), this);
    m_counter = new QLabel(this);
    m_counter->setObjectName(QStringLiteral("findCounter"));

    m_prev->setToolTip(tr("Previous match (Shift+Enter)"));
    m_next->setToolTip(tr("Next match (Enter)"));
    m_close->setToolTip(tr("Close find bar (Esc)"));
    for (QPushButton *b : { m_prev, m_next, m_close })
        b->setFixedWidth(b->sizeHint().height());

    layout->addWidget(m_edit, 1);
    layout->addWidget(m_counter);
    layout->addWidget(m_prev);
    layout->addWidget(m_next);
    layout->addWidget(m_close);

    connect(m_edit, &QLineEdit::textChanged, this, [this]() { runFind(false); });
    connect(m_edit, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(m_prev, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(m_next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(m_close, &QPushButton::clicked, this, &FindBar::hideBar);

    hide();
}

void FindBar::retranslateUi()
{
    m_edit->setPlaceholderText(tr("Find in page"));
    m_prev->setToolTip(tr("Previous match (Shift+Enter)"));
    m_next->setToolTip(tr("Next match (Enter)"));
    m_close->setToolTip(tr("Close find bar (Esc)"));
    if (!m_counter->text().isEmpty()) {
        // Re-run the find so the counter text uses the new language.
        if (!m_edit->text().isEmpty())
            runFind(false);
    }
}

void FindBar::attachPage(QWebEnginePage *page)
{
    m_page = page;
}

void FindBar::activate()
{
    show();
    raise();
    m_edit->setFocus();
    m_edit->selectAll();
    if (!m_edit->text().isEmpty())
        runFind(false);
}

void FindBar::findNext()
{
    runFind(false);
}

void FindBar::findPrevious()
{
    runFind(true);
}

void FindBar::hideBar()
{
    if (m_page)
        m_page->findText(QString()); // clears highlights
    m_counter->clear();
    hide();
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hideBar();
        return;
    }
    QWidget::keyPressEvent(event);
}

void FindBar::runFind(bool backward)
{
    if (!m_page) {
        m_counter->clear();
        return;
    }
    const QString text = m_edit->text();
    if (text.isEmpty()) {
        m_page->findText(QString());
        m_counter->clear();
        return;
    }
    QWebEnginePage::FindFlags flags;
    if (backward)
        flags |= QWebEnginePage::FindBackward;
    m_page->findText(text, flags, [this](const QWebEngineFindTextResult &result) {
        if (result.numberOfMatches() > 0)
            m_counter->setText(tr("%1/%2").arg(result.activeMatch())
                                          .arg(result.numberOfMatches()));
        else
            m_counter->setText(tr("No matches"));
    });
}
