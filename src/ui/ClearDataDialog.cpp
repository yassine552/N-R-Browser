#include "ClearDataDialog.h"

#include "AdBlockClient.h"
#include "BrowserApplication.h"
#include "FaviconStore.h"
#include "HistoryStore.h"
#include "Paths.h"
#include "ProfileManager.h"
#include "TopSitesStore.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace std::chrono_literals;

ClearDataDialog::ClearDataDialog(BrowserApplication *app, QWidget *parent)
    : QDialog(parent)
    , m_app(app)
{
    setModal(true);
    setWindowTitle(tr("Clear browsing data"));

    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(tr("Select what to delete. Clearing affects this device only."), this);
    info->setWordWrap(true);

    m_historyCheck = new QCheckBox(tr("Browsing history"), this);
    m_historyCheck->setChecked(true);
    m_cookiesCheck = new QCheckBox(tr("Cookies and site sign-ins"), this);
    m_cookiesCheck->setChecked(true);
    m_cacheCheck = new QCheckBox(tr("Cached files and images"), this);
    m_cacheCheck->setChecked(true);
    m_storageCheck = new QCheckBox(
        tr("All persistent site storage (applied on next restart)"), this);
    m_storageCheck->setChecked(false);

    auto *storageNote = new QLabel(
        tr("Persistent storage (local storage, databases, service workers) is removed "
           "before the browser starts again, because the engine must be closed for a "
           "safe wipe."), this);
    storageNote->setWordWrap(true);
    storageNote->setObjectName(QStringLiteral("noteLabel"));

    m_resultLabel = new QLabel(this);
    m_resultLabel->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(Qt::Horizontal, this);
    QPushButton *clear = buttons->addButton(tr("Clear now"), QDialogButtonBox::ActionRole);
    QPushButton *close = buttons->addButton(QDialogButtonBox::Close);
    connect(clear, &QPushButton::clicked, this, &ClearDataDialog::clearNow);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    layout->addWidget(info);
    layout->addWidget(m_historyCheck);
    layout->addWidget(m_cookiesCheck);
    layout->addWidget(m_cacheCheck);
    layout->addWidget(m_storageCheck);
    layout->addWidget(storageNote);
    layout->addWidget(m_resultLabel);
    layout->addWidget(buttons);
}

void ClearDataDialog::clearNow()
{
    QStringList cleared;

    if (m_historyCheck->isChecked()) {
        if (m_app->history()->clearAll()) {
            // Derived-from-history data must go with it: the favicon cache
            // (its host set IS browsing data) and the Top-Sites dismissal
            // list. Explicitly pinned sites survive, like bookmarks.
            if (m_app->favicons())
                m_app->favicons()->clearAll();
            cleared << tr("history");
        }
    }
    if (m_cookiesCheck->isChecked()) {
        m_app->profiles()->clearCookiesNow();
        m_app->adBlock()->resetBlockedTotal();
        cleared << tr("cookies");
    }
    if (m_cacheCheck->isChecked()) {
        m_app->profiles()->clearHttpCacheNow();
        m_app->profiles()->clearVisitedLinksNow();
        cleared << tr("cache");
    }
    if (m_storageCheck->isChecked()) {
        Paths::setPendingClearSiteStorage(true);
        cleared << tr("site storage (on next restart)");
    }

    m_resultLabel->setText(cleared.isEmpty()
                               ? tr("Nothing was selected.")
                               : tr("Cleared: %1.").arg(cleared.join(QStringLiteral(", "))));
}
