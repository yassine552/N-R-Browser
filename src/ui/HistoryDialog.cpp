#include "HistoryDialog.h"

#include "BrowserApplication.h"
#include "HistoryStore.h"
#include "LanguageManager.h"

#include <QDateTime>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace std::chrono_literals;

HistoryDialog::HistoryDialog(BrowserApplication *app, QWidget *parent)
    : QDialog(parent)
    , m_app(app)
{
    setModal(true);
    setMinimumSize(720, 480);
    setWindowTitle(tr("Browsing history"));

    auto *layout = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search history"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &HistoryDialog::refresh);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("When"), tr("Title"), tr("Address") });
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &HistoryDialog::openSelected);

    auto *buttons = new QWidget(this);
    auto *buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    auto *open = new QPushButton(tr("Open"), buttons);
    auto *remove = new QPushButton(tr("Remove selected"), buttons);
    m_clearButton = new QPushButton(tr("Clear all history…"), buttons);
    connect(open, &QPushButton::clicked, this, &HistoryDialog::openSelected);
    connect(remove, &QPushButton::clicked, this, &HistoryDialog::removeSelected);
    connect(m_clearButton, &QPushButton::clicked, this, &HistoryDialog::clearAll);
    buttonsLayout->addWidget(open);
    buttonsLayout->addWidget(remove);
    buttonsLayout->addStretch(1);
    buttonsLayout->addWidget(m_clearButton);

    layout->addWidget(m_search);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);

    connect(m_app->history(), &HistoryStore::historyChanged, this, &HistoryDialog::refresh);
    refresh();
}

void HistoryDialog::refresh()
{
    const QString needle = m_search->text().trimmed();
    const QVector<HistoryEntry> entries =
        needle.isEmpty() ? m_app->history()->recent(500) : m_app->history()->search(needle);
    const QLocale locale = m_app->languages()->locale();

    m_table->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const HistoryEntry &e = entries.at(i);
        const QDateTime when = QDateTime::fromSecsSinceEpoch(e.lastVisit);
        auto *itemWhen = new QTableWidgetItem(locale.toString(when, QLocale::ShortFormat));
        auto *itemTitle = new QTableWidgetItem(e.title.isEmpty() ? tr("(no title)") : e.title);
        auto *itemUrl = new QTableWidgetItem(e.url);
        itemWhen->setData(Qt::UserRole, e.id);
        itemWhen->setData(Qt::UserRole + 1, e.url);
        m_table->setItem(i, 0, itemWhen);
        m_table->setItem(i, 1, itemTitle);
        m_table->setItem(i, 2, itemUrl);
    }
}

void HistoryDialog::openSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QTableWidgetItem *item = m_table->item(row, 0);
    const QUrl url(item->data(Qt::UserRole + 1).toString());
    if (url.isValid()) {
        emit openUrlRequested(url);
        accept();
    }
}

void HistoryDialog::removeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QTableWidgetItem *item = m_table->item(row, 0);
    const qint64 id = item->data(Qt::UserRole).toLongLong();
    m_app->history()->removeEntry(id);
}

void HistoryDialog::clearAll()
{
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Clear all history"),
        tr("Delete the entire browsing history stored on this device?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        m_app->history()->clearAll();
}

void HistoryDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete)
        removeSelected();
    else
        QDialog::keyPressEvent(event);
}
