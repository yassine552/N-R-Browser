#include "SettingsDialog.h"
#include "Theme.h"
#include "IconProvider.h"

#include "AdBlockClient.h"
#include "AppConfig.h"
#include "BrowserApplication.h"
#include "ClearDataDialog.h"
#include "FilterListManager.h"
#include "LanguageManager.h"
#include "SearchEngines.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace std::chrono_literals;

namespace {
const char *kPermissionFeatures[10] = { "media-audio", "media-video", "media-audio-video",
                                        "desktop-video", "desktop-audio-video", "mouse-lock",
                                        "notifications", "geolocation", "clipboard",
                                        "local-fonts" };
const char *kPermissionNames[10] = { QT_TRANSLATE_NOOP("SettingsDialog", "Microphone"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Camera"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Camera and microphone"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Screen sharing"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Screen and audio sharing"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Mouse lock"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Notifications"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Location"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Clipboard"),
                                     QT_TRANSLATE_NOOP("SettingsDialog", "Local fonts") };
} // namespace

SettingsDialog::SettingsDialog(BrowserApplication *app, QWidget *parent)
    : QDialog(parent)
    , m_app(app)
{
    setModal(true);
    setMinimumSize(560, 480);
    buildUi();
    loadValues();
    m_loading = false;
    retranslateUi();
}

void SettingsDialog::buildUi()
{
    m_tabs = new QTabWidget(this);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // ------------------------- General -------------------------
    auto *general = new QWidget(this);
    auto *generalForm = new QFormLayout(general);
    m_languageCombo = new QComboBox(general);
    for (const LanguageManager::LanguageInfo &info : LanguageManager::availableLanguages())
        m_languageCombo->addItem(info.nativeName, info.code);
    connect(m_languageCombo, &QComboBox::currentIndexChanged, this,
            &SettingsDialog::applyLanguage);
    m_homeEdit = new QLineEdit(general);
    connect(m_homeEdit, &QLineEdit::editingFinished, this, &SettingsDialog::saveHomePage);
    auto *downloadRow = new QWidget(general);
    auto *downloadRowLayout = new QHBoxLayout(downloadRow);
    downloadRowLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadDirEdit = new QLineEdit(downloadRow);
    m_downloadDirEdit->setReadOnly(true);
    auto *browse = new QPushButton(tr("Browse…"), downloadRow);
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::pickDownloadDirectory);
    downloadRowLayout->addWidget(m_downloadDirEdit, 1);
    downloadRowLayout->addWidget(browse);
    m_downloadAskCheck = new QCheckBox(general);
    connect(m_downloadAskCheck, &QCheckBox::toggled, this, &SettingsDialog::saveDownloadsAsk);
    m_bookmarksBarCheck = new QCheckBox(general);
    connect(m_bookmarksBarCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading)
            m_app->config()->setBookmarksBarVisible(on);
    });
    m_suggestionsCheck = new QCheckBox(general);
    connect(m_suggestionsCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading)
            m_app->config()->setAddressBarSuggestions(on);
    });
    m_themeCombo = new QComboBox(general);
    m_themeCombo->addItem(QString(), QStringLiteral("system"));
    m_themeCombo->addItem(QString(), QStringLiteral("dark"));
    m_themeCombo->addItem(QString(), QStringLiteral("light"));
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_loading || !m_app)
            return;
        const QString key = m_themeCombo->itemData(index).toString();
        m_app->config()->setUiTheme(key);
        // Live re-style: tokens regenerate the stylesheet app-wide; the
        // icon cache is rebuilt because every SVG is recolored per theme.
        Theme::setSchemeKey(key);
        IconProvider::clearCache();
    });
    generalForm->addRow(new QLabel(tr("Appearance:"), general), m_themeCombo);
    generalForm->addRow(new QLabel(tr("Language:"), general), m_languageCombo);
    generalForm->addRow(new QLabel(tr("Home page:"), general), m_homeEdit);
    generalForm->addRow(new QLabel(tr("Download folder:"), general), downloadRow);
    generalForm->addRow(m_downloadAskCheck);
    generalForm->addRow(m_bookmarksBarCheck);
    generalForm->addRow(m_suggestionsCheck);
    m_tabs->addTab(general, QString());

    // ------------------------- Search -------------------------
    auto *search = new QWidget(this);
    auto *searchLayout = new QVBoxLayout(search);
    m_enginesTable = new QTableWidget(search);
    m_enginesTable->setColumnCount(2);
    m_enginesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_enginesTable->verticalHeader()->setVisible(false);
    connect(m_enginesTable, &QTableWidget::cellChanged, this,
            &SettingsDialog::onEngineItemChanged);
    auto *engineButtons = new QWidget(search);
    auto *engineButtonsLayout = new QHBoxLayout(engineButtons);
    engineButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto *addEngine = new QPushButton(tr("Add engine"), engineButtons);
    auto *removeEngine = new QPushButton(tr("Remove"), engineButtons);
    connect(addEngine, &QPushButton::clicked, this, &SettingsDialog::addSearchEngine);
    connect(removeEngine, &QPushButton::clicked, this, &SettingsDialog::removeSearchEngine);
    m_defaultEngineCombo = new QComboBox(engineButtons);
    connect(m_defaultEngineCombo, &QComboBox::currentIndexChanged, this,
            &SettingsDialog::onDefaultEngineChanged);
    engineButtonsLayout->addWidget(addEngine);
    engineButtonsLayout->addWidget(removeEngine);
    engineButtonsLayout->addStretch(1);
    engineButtonsLayout->addWidget(new QLabel(tr("Default:"), engineButtons));
    engineButtonsLayout->addWidget(m_defaultEngineCombo);
    searchLayout->addWidget(m_enginesTable, 1);
    searchLayout->addWidget(engineButtons);
    m_tabs->addTab(search, QString());

    // ------------------------- Privacy -------------------------
    auto *privacy = new QWidget(this);
    auto *privacyLayout = new QVBoxLayout(privacy);

    auto *blockGroup = new QGroupBox(privacy);
    auto *blockLayout = new QVBoxLayout(blockGroup);
    m_adBlockCheck = new QCheckBox(blockGroup);
    connect(m_adBlockCheck, &QCheckBox::toggled, this, &SettingsDialog::toggleAdBlock);
    m_statsLabel = new QLabel(blockGroup);
    m_statsLabel->setWordWrap(true);
    auto *listsRow = new QWidget(blockGroup);
    auto *listsLayout = new QHBoxLayout(listsRow);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    m_filterListsWidget = new QListWidget(listsRow);
    m_filterListsWidget->setMaximumHeight(100);
    auto *listButtons = new QWidget(listsRow);
    auto *listButtonsLayout = new QVBoxLayout(listButtons);
    listButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto *addList = new QPushButton(tr("Add list…"), listButtons);
    auto *removeList = new QPushButton(tr("Remove"), listButtons);
    connect(addList, &QPushButton::clicked, this, &SettingsDialog::addFilterListFile);
    connect(removeList, &QPushButton::clicked, this, &SettingsDialog::removeFilterListFile);
    listButtonsLayout->addWidget(addList);
    listButtonsLayout->addWidget(removeList);
    listButtonsLayout->addStretch(1);
    listsLayout->addWidget(m_filterListsWidget, 1);
    listsLayout->addWidget(listButtons);
    auto *allowRow = new QWidget(blockGroup);
    auto *allowLayout = new QHBoxLayout(allowRow);
    allowLayout->setContentsMargins(0, 0, 0, 0);
    m_allowlistWidget = new QListWidget(allowRow);
    m_allowlistWidget->setMaximumHeight(80);
    m_allowlistEdit = new QLineEdit(allowRow);
    m_allowlistEdit->setPlaceholderText(tr("example.com"));
    auto *allowAdd = new QPushButton(tr("Add"), allowRow);
    auto *allowRemove = new QPushButton(tr("Remove"), allowRow);
    connect(allowAdd, &QPushButton::clicked, this, &SettingsDialog::addAllowlistHost);
    connect(allowRemove, &QPushButton::clicked, this, &SettingsDialog::removeAllowlistHost);
    auto *allowEditRow = new QWidget(allowRow);
    auto *allowEditLayout = new QHBoxLayout(allowEditRow);
    allowEditLayout->setContentsMargins(0, 0, 0, 0);
    allowEditLayout->addWidget(m_allowlistEdit, 1);
    allowEditLayout->addWidget(allowAdd);
    allowEditLayout->addWidget(allowRemove);
    auto *allowCol = new QWidget(allowRow);
    auto *allowColLayout = new QVBoxLayout(allowCol);
    allowColLayout->setContentsMargins(0, 0, 0, 0);
    allowColLayout->addWidget(m_allowlistWidget, 1);
    allowColLayout->addWidget(allowEditRow);
    allowLayout->addWidget(allowCol, 1);

    // Remote filter-list sources (EasyList/EasyPrivacy by default) with
    // conditional auto-updates and offline caching.
    m_listUrlsWidget = new QListWidget(blockGroup);
    m_listUrlsWidget->setMaximumHeight(90);
    m_listUrlEdit = new QLineEdit(blockGroup);
    m_listUrlEdit->setPlaceholderText(tr("https://easylist.to/easylist/easylist.txt"));
    auto *urlAdd = new QPushButton(tr("Add"), blockGroup);
    auto *urlRemove = new QPushButton(tr("Remove"), blockGroup);
    connect(urlAdd, &QPushButton::clicked, this, &SettingsDialog::addListUrl);
    connect(urlRemove, &QPushButton::clicked, this, &SettingsDialog::removeListUrl);
    auto *urlEditRow = new QWidget(blockGroup);
    auto *urlEditLayout = new QHBoxLayout(urlEditRow);
    urlEditLayout->setContentsMargins(0, 0, 0, 0);
    urlEditLayout->addWidget(m_listUrlEdit, 1);
    urlEditLayout->addWidget(urlAdd);
    urlEditLayout->addWidget(urlRemove);
    m_autoUpdateCheck = new QCheckBox(blockGroup);
    connect(m_autoUpdateCheck, &QCheckBox::toggled, this,
            &SettingsDialog::toggleAutoUpdate);
    m_updateNowButton = new QPushButton(blockGroup);
    connect(m_updateNowButton, &QPushButton::clicked, this, [this]() {
        m_app->filterLists()->updateNow();
        m_updateNowButton->setEnabled(false);
        m_updateNowButton->setText(tr("Updating…"));
        QTimer::singleShot(3000, this, [this]() {
            refreshPrivacyStats();
            m_updateNowButton->setEnabled(true);
            retranslateUi();
        });
    });

    blockLayout->addWidget(m_adBlockCheck);
    blockLayout->addWidget(m_statsLabel);
    blockLayout->addWidget(new QLabel(tr("Filter list sources (downloaded, cached offline):"), blockGroup));
    blockLayout->addWidget(m_listUrlsWidget);
    blockLayout->addWidget(urlEditRow);
    blockLayout->addWidget(m_autoUpdateCheck);
    blockLayout->addWidget(m_updateNowButton);
    blockLayout->addWidget(new QLabel(tr("Additional filter lists (local files):"), blockGroup));
    blockLayout->addWidget(listsRow);
    blockLayout->addWidget(new QLabel(tr("Sites exempt from blocking:"), blockGroup));
    blockLayout->addWidget(allowRow);
    privacyLayout->addWidget(blockGroup);

    auto *dsGroup = new QGroupBox(privacy);
    auto *dsLayout = new QVBoxLayout(dsGroup);
    m_dataSaverCheck = new QCheckBox(dsGroup);
    connect(m_dataSaverCheck, &QCheckBox::toggled, this, &SettingsDialog::toggleDataSaver);
    m_thirdPartyImagesCheck = new QCheckBox(dsGroup);
    connect(m_thirdPartyImagesCheck, &QCheckBox::toggled, this,
            &SettingsDialog::toggleThirdPartyImages);
    m_blockImagesCheck = new QCheckBox(dsGroup);
    connect(m_blockImagesCheck, &QCheckBox::toggled, this,
            &SettingsDialog::toggleBlockImages);
    m_dsExceptionsWidget = new QListWidget(dsGroup);
    m_dsExceptionsWidget->setMaximumHeight(80);
    m_dsExceptionEdit = new QLineEdit(dsGroup);
    m_dsExceptionEdit->setPlaceholderText(tr("example.com"));
    auto *dsAdd = new QPushButton(tr("Add"), dsGroup);
    auto *dsRemove = new QPushButton(tr("Remove"), dsGroup);
    connect(dsAdd, &QPushButton::clicked, this, &SettingsDialog::addDataSaverException);
    connect(dsRemove, &QPushButton::clicked, this, &SettingsDialog::removeDataSaverException);
    auto *dsEditRow = new QWidget(dsGroup);
    auto *dsEditLayout = new QHBoxLayout(dsEditRow);
    dsEditLayout->setContentsMargins(0, 0, 0, 0);
    dsEditLayout->addWidget(m_dsExceptionEdit, 1);
    dsEditLayout->addWidget(dsAdd);
    dsEditLayout->addWidget(dsRemove);
    dsLayout->addWidget(m_dataSaverCheck);
    dsLayout->addWidget(m_blockImagesCheck);
    dsLayout->addWidget(m_thirdPartyImagesCheck);
    dsLayout->addWidget(new QLabel(tr("Sites exempt from Data Saver:"), dsGroup));
    dsLayout->addWidget(m_dsExceptionsWidget);
    dsLayout->addWidget(dsEditRow);
    privacyLayout->addWidget(dsGroup);

    // ------------------------- Privacy hardening -------------------------
    auto *hardGroup = new QGroupBox(privacy);
    auto *hardLayout = new QVBoxLayout(hardGroup);
    m_3pCookiesCheck = new QCheckBox(hardGroup);
    connect(m_3pCookiesCheck, &QCheckBox::toggled, this,
            &SettingsDialog::toggleBlock3pCookies);
    m_sessionCookiesCheck = new QCheckBox(hardGroup);
    connect(m_sessionCookiesCheck, &QCheckBox::toggled, this,
            &SettingsDialog::toggleSessionOnlyCookies);
    m_dntCheck = new QCheckBox(hardGroup);
    connect(m_dntCheck, &QCheckBox::toggled, this, &SettingsDialog::toggleDnt);
    m_gpcCheck = new QCheckBox(hardGroup);
    connect(m_gpcCheck, &QCheckBox::toggled, this, &SettingsDialog::toggleGpc);
    m_simplifiedUaCheck = new QCheckBox(hardGroup);
    connect(m_simplifiedUaCheck, &QCheckBox::toggled, this,
            &SettingsDialog::toggleSimplifiedUa);
    m_webrtcCheck = new QCheckBox(hardGroup);
    connect(m_webrtcCheck, &QCheckBox::toggled, this,
            &SettingsDialog::toggleWebrtcPublicIpOnly);
    m_hardeningHint = new QLabel(hardGroup);
    m_hardeningHint->setWordWrap(true);
    hardLayout->addWidget(m_3pCookiesCheck);
    hardLayout->addWidget(m_sessionCookiesCheck);
    hardLayout->addWidget(m_dntCheck);
    hardLayout->addWidget(m_gpcCheck);
    hardLayout->addWidget(m_simplifiedUaCheck);
    hardLayout->addWidget(m_webrtcCheck);
    hardLayout->addWidget(m_hardeningHint);
    privacyLayout->addWidget(hardGroup);

    m_clearDataButton = new QPushButton(privacy);
    connect(m_clearDataButton, &QPushButton::clicked, this, [this]() {
        ClearDataDialog dialog(m_app, this);
        dialog.exec();
    });
    privacyLayout->addWidget(m_clearDataButton);
    privacyLayout->addStretch(1);
    m_tabs->addTab(privacy, QString());

    // ------------------------- Permissions -------------------------
    auto *perm = new QWidget(this);
    auto *permLayout = new QHBoxLayout(perm);
    m_permOriginsWidget = new QListWidget(perm);
    connect(m_permOriginsWidget, &QListWidget::itemSelectionChanged, this,
            &SettingsDialog::onPermissionOriginSelected);
    m_permOriginsWidget->setMaximumWidth(220);
    auto *permRight = new QWidget(perm);
    auto *permRightLayout = new QVBoxLayout(permRight);
    m_permEmptyLabel = new QLabel(permRight);
    m_permEmptyLabel->setWordWrap(true);
    auto *permFormHost = new QWidget(permRight);
    auto *permForm = new QFormLayout(permFormHost);
    for (int i = 0; i < 10; ++i) {
        m_permCombos[i] = new QComboBox(permFormHost);
        m_permCombos[i]->addItem(QString(), QString());      // Ask
        m_permCombos[i]->addItem(QString(), QStringLiteral("allow"));
        m_permCombos[i]->addItem(QString(), QStringLiteral("deny"));
        connect(m_permCombos[i], &QComboBox::currentIndexChanged, this,
                &SettingsDialog::onPermissionComboChanged);
        permForm->addRow(new QLabel(tr(kPermissionNames[i]), permFormHost), m_permCombos[i]);
    }
    auto *permButtons = new QWidget(permRight);
    auto *permButtonsLayout = new QHBoxLayout(permButtons);
    permButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto *removeSite = new QPushButton(tr("Forget this site"), permButtons);
    auto *removeAll = new QPushButton(tr("Forget all sites"), permButtons);
    connect(removeSite, &QPushButton::clicked, this, &SettingsDialog::removePermissionOrigin);
    connect(removeAll, &QPushButton::clicked, this, &SettingsDialog::removeAllPermissions);
    permButtonsLayout->addWidget(removeSite);
    permButtonsLayout->addWidget(removeAll);
    permButtonsLayout->addStretch(1);
    permRightLayout->addWidget(m_permEmptyLabel);
    permRightLayout->addWidget(permFormHost, 1);
    permRightLayout->addWidget(permButtons);
    permLayout->addWidget(m_permOriginsWidget);
    permLayout->addWidget(permRight, 1);
    m_tabs->addTab(perm, QString());
}

void SettingsDialog::loadValues()
{
    const int themeIdx = m_themeCombo->findData(m_app->config()->uiTheme());
    m_themeCombo->setCurrentIndex(themeIdx < 0 ? 0 : themeIdx);
    const int idx = m_languageCombo->findData(m_app->config()->language());
    m_languageCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    m_homeEdit->setText(m_app->config()->homePage());
    m_downloadDirEdit->setText(m_app->config()->downloadDirectory());
    m_downloadAskCheck->setChecked(m_app->config()->askWhereToSaveDownloads());
    m_bookmarksBarCheck->setChecked(m_app->config()->bookmarksBarVisible());

    // Search engines.
    const QVector<SearchEngine> engines = m_app->searchEngines()->engines();
    m_enginesTable->setRowCount(engines.size());
    for (int i = 0; i < engines.size(); ++i) {
        QTableWidgetItem *name = new QTableWidgetItem(engines.at(i).name);
        QTableWidgetItem *url = new QTableWidgetItem(engines.at(i).urlTemplate);
        m_enginesTable->setItem(i, 0, name);
        m_enginesTable->setItem(i, 1, url);
    }
    m_defaultEngineCombo->blockSignals(true);
    m_defaultEngineCombo->clear();
    for (const SearchEngine &e : engines)
        m_defaultEngineCombo->addItem(e.name);
    const int def = m_defaultEngineCombo->findText(m_app->searchEngines()->defaultEngine().name);
    m_defaultEngineCombo->setCurrentIndex(def < 0 ? 0 : def);
    m_defaultEngineCombo->blockSignals(false);

    // Privacy.
    m_adBlockCheck->setChecked(m_app->config()->adBlockEnabled());
    m_filterListsWidget->clear();
    m_filterListsWidget->addItems(m_app->config()->customFilterLists());
    m_listUrlsWidget->clear();
    m_listUrlsWidget->addItems(m_app->config()->adBlockListUrls());
    m_autoUpdateCheck->setChecked(m_app->config()->adBlockAutoUpdate());
    m_allowlistWidget->clear();
    m_allowlistWidget->addItems(m_app->config()->adBlockAllowlist());
    m_dataSaverCheck->setChecked(m_app->config()->dataSaverEnabled());
    m_blockImagesCheck->setChecked(m_app->config()->dataSaverBlockImages());
    m_thirdPartyImagesCheck->setChecked(m_app->config()->dataSaverBlockThirdPartyImages());
    m_suggestionsCheck->setChecked(m_app->config()->addressBarSuggestions());
    m_dsExceptionsWidget->clear();
    m_dsExceptionsWidget->addItems(m_app->config()->dataSaverExceptions());

    // Privacy hardening.
    m_3pCookiesCheck->setChecked(m_app->config()->blockThirdPartyCookies());
    m_sessionCookiesCheck->setChecked(m_app->config()->sessionOnlyCookies());
    m_dntCheck->setChecked(m_app->config()->sendDoNotTrack());
    m_gpcCheck->setChecked(m_app->config()->sendGlobalPrivacyControl());
    m_simplifiedUaCheck->setChecked(m_app->config()->simplifiedUserAgent());
    m_webrtcCheck->setChecked(m_app->config()->webrtcPublicIpOnly());
    refreshPrivacyStats();
    refreshPermissions();
}

void SettingsDialog::refreshPrivacyStats()
{
    const AdBlockClient *client = m_app->adBlock();
    m_statsLabel->setText(
        tr("%1 blocking rules, %2 exceptions, %3 unsupported lines skipped.")
            .arg(m_app->languages()->locale().toString(client->blockRuleCount()))
            .arg(m_app->languages()->locale().toString(client->exceptionRuleCount()))
            .arg(m_app->languages()->locale().toString(client->skippedRuleCount())));
}

// ------------------------------------------------------------- general ----

void SettingsDialog::applyLanguage(int index)
{
    if (m_loading || index < 0)
        return;
    const QString code = m_languageCombo->itemData(index).toString();
    m_app->config()->setLanguage(code);
    emit languageSwitchRequested(code);
    retranslateUi();
}

void SettingsDialog::saveHomePage()
{
    const QString text = m_homeEdit->text().trimmed();
    if (!text.isEmpty())
        m_app->config()->setHomePage(text);
}

void SettingsDialog::saveDownloadsAsk(bool ask)
{
    if (!m_loading)
        m_app->config()->setAskWhereToSaveDownloads(ask);
}

void SettingsDialog::pickDownloadDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose download folder"), m_app->config()->downloadDirectory());
    if (!dir.isEmpty()) {
        m_app->config()->setDownloadDirectory(dir);
        m_downloadDirEdit->setText(dir);
    }
}

// ------------------------------------------------------------- search -----

void SettingsDialog::saveEngines()
{
    QVector<SearchEngine> engines;
    for (int i = 0; i < m_enginesTable->rowCount(); ++i) {
        const QTableWidgetItem *name = m_enginesTable->item(i, 0);
        const QTableWidgetItem *url = m_enginesTable->item(i, 1);
        if (!name || !url)
            continue;
        SearchEngine e{ name->text().trimmed(), url->text().trimmed() };
        if (e.isValid())
            engines.append(e);
    }
    if (engines.isEmpty())
        return; // never leave the user without a search engine
    m_app->searchEngines()->setEngines(engines, m_defaultEngineCombo->currentText());
}

void SettingsDialog::onEngineItemChanged()
{
    if (m_loading)
        return;
    saveEngines();
    // Refresh the default combo names.
    m_defaultEngineCombo->blockSignals(true);
    m_defaultEngineCombo->clear();
    for (int i = 0; i < m_enginesTable->rowCount(); ++i) {
        const QTableWidgetItem *name = m_enginesTable->item(i, 0);
        if (name && !name->text().trimmed().isEmpty())
            m_defaultEngineCombo->addItem(name->text().trimmed());
    }
    m_defaultEngineCombo->blockSignals(false);
}

void SettingsDialog::addSearchEngine()
{
    const int row = m_enginesTable->rowCount();
    m_enginesTable->insertRow(row);
    m_enginesTable->setItem(row, 0, new QTableWidgetItem(tr("New engine")));
    m_enginesTable->setItem(row, 1, new QTableWidgetItem(
                                     QStringLiteral("https://www.example.com/search?q=%s")));
    saveEngines();
}

void SettingsDialog::removeSearchEngine()
{
    const int row = m_enginesTable->currentRow();
    if (row < 0)
        return;
    if (m_enginesTable->rowCount() <= 1) {
        QMessageBox::information(this, tr("Search engines"),
                                 tr("At least one search engine is required."));
        return;
    }
    m_enginesTable->removeRow(row);
    saveEngines();
}

void SettingsDialog::onDefaultEngineChanged(int index)
{
    if (m_loading || index < 0)
        return;
    saveEngines();
}

// ------------------------------------------------------------ privacy -----

void SettingsDialog::toggleAdBlock(bool on)
{
    if (!m_loading)
        m_app->config()->setAdBlockEnabled(on);
}

void SettingsDialog::addFilterListFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose a filter list"), QString(),
        tr("Filter lists (*.txt *.list);;All files (*)"));
    if (path.isEmpty())
        return;
    QStringList lists = m_app->config()->customFilterLists();
    if (!lists.contains(path)) {
        lists.append(path);
        m_app->config()->setCustomFilterLists(lists);
        m_app->adBlock()->reload();
        m_filterListsWidget->clear();
        m_filterListsWidget->addItems(lists);
        refreshPrivacyStats();
    }
}

void SettingsDialog::removeFilterListFile()
{
    QListWidgetItem *item = m_filterListsWidget->currentItem();
    if (!item)
        return;
    QStringList lists = m_app->config()->customFilterLists();
    lists.removeAll(item->text());
    m_app->config()->setCustomFilterLists(lists);
    m_app->adBlock()->reload();
    m_filterListsWidget->clear();
    m_filterListsWidget->addItems(lists);
    refreshPrivacyStats();
}

void SettingsDialog::addListUrl()
{
    const QString url = m_listUrlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    const QUrl parsed(url);
    if (!parsed.isValid()
        || (parsed.scheme() != QLatin1String("https")
            && parsed.host() != QLatin1String("127.0.0.1")
            && parsed.host() != QLatin1String("localhost"))) {
        QMessageBox::warning(this, tr("Invalid list URL"),
                             tr("Filter list URLs must use HTTPS."));
        return;
    }
    QStringList urls = m_app->config()->adBlockListUrls();
    if (!urls.contains(url))
        urls.append(url);
    m_app->config()->setAdBlockListUrls(urls);
    m_listUrlEdit->clear();
    m_listUrlsWidget->clear();
    m_listUrlsWidget->addItems(urls);
    m_app->filterLists()->updateNow();
}

void SettingsDialog::removeListUrl()
{
    QListWidgetItem *item = m_listUrlsWidget->currentItem();
    if (!item)
        return;
    QStringList urls = m_app->config()->adBlockListUrls();
    urls.removeAll(item->text());
    m_app->config()->setAdBlockListUrls(urls);
    m_listUrlsWidget->clear();
    m_listUrlsWidget->addItems(urls);
    // Drop the cached file immediately and rebuild the engine.
    m_app->filterLists()->updateNow();
    m_app->adBlock()->reload();
    refreshPrivacyStats();
}

void SettingsDialog::toggleAutoUpdate(bool on)
{
    if (!m_loading)
        m_app->config()->setAdBlockAutoUpdate(on);
}

void SettingsDialog::addAllowlistHost()
{
    const QString host = m_allowlistEdit->text().trimmed().toLower();
    if (host.isEmpty() || host.contains(QLatin1Char(' ')))
        return;
    QStringList hosts = m_app->config()->adBlockAllowlist();
    if (!hosts.contains(host)) {
        hosts.append(host);
        m_app->config()->setAdBlockAllowlist(hosts);
        m_allowlistWidget->clear();
        m_allowlistWidget->addItems(hosts);
    }
    m_allowlistEdit->clear();
}

void SettingsDialog::removeAllowlistHost()
{
    QListWidgetItem *item = m_allowlistWidget->currentItem();
    if (!item)
        return;
    QStringList hosts = m_app->config()->adBlockAllowlist();
    hosts.removeAll(item->text());
    m_app->config()->setAdBlockAllowlist(hosts);
    m_allowlistWidget->clear();
    m_allowlistWidget->addItems(hosts);
}

void SettingsDialog::toggleDataSaver(bool on)
{
    if (!m_loading)
        m_app->config()->setDataSaverEnabled(on);
}

void SettingsDialog::toggleBlockImages(bool on)
{
    if (!m_loading)
        m_app->config()->setDataSaverBlockImages(on);
}

void SettingsDialog::toggleThirdPartyImages(bool on)
{
    if (!m_loading)
        m_app->config()->setDataSaverBlockThirdPartyImages(on);
}

void SettingsDialog::addDataSaverException()
{
    const QString host = m_dsExceptionEdit->text().trimmed().toLower();
    if (host.isEmpty() || host.contains(QLatin1Char(' ')))
        return;
    QStringList hosts = m_app->config()->dataSaverExceptions();
    if (!hosts.contains(host)) {
        hosts.append(host);
        m_app->config()->setDataSaverExceptions(hosts);
        m_dsExceptionsWidget->clear();
        m_dsExceptionsWidget->addItems(hosts);
    }
    m_dsExceptionEdit->clear();
}

void SettingsDialog::removeDataSaverException()
{
    QListWidgetItem *item = m_dsExceptionsWidget->currentItem();
    if (!item)
        return;
    QStringList hosts = m_app->config()->dataSaverExceptions();
    hosts.removeAll(item->text());
    m_app->config()->setDataSaverExceptions(hosts);
    m_dsExceptionsWidget->clear();
    m_dsExceptionsWidget->addItems(hosts);
}

// ---------------------------------------------------- privacy hardening ----

void SettingsDialog::toggleBlock3pCookies(bool on)
{
    if (!m_loading)
        m_app->config()->setBlockThirdPartyCookies(on);
}

void SettingsDialog::toggleSessionOnlyCookies(bool on)
{
    if (!m_loading)
        m_app->config()->setSessionOnlyCookies(on);
}

void SettingsDialog::toggleDnt(bool on)
{
    if (!m_loading)
        m_app->config()->setSendDoNotTrack(on);
}

void SettingsDialog::toggleGpc(bool on)
{
    if (!m_loading)
        m_app->config()->setSendGlobalPrivacyControl(on);
}

void SettingsDialog::toggleSimplifiedUa(bool on)
{
    if (!m_loading)
        m_app->config()->setSimplifiedUserAgent(on);
}

void SettingsDialog::toggleWebrtcPublicIpOnly(bool on)
{
    if (!m_loading)
        m_app->config()->setWebrtcPublicIpOnly(on);
}

// --------------------------------------------------------- permissions ----

void SettingsDialog::refreshPermissions()
{
    m_permOriginsWidget->clear();
    const QStringList origins = m_app->config()->permissionOrigins();
    m_permOriginsWidget->addItems(origins);
    m_permEmptyLabel->setVisible(origins.isEmpty());
}

void SettingsDialog::onPermissionOriginSelected()
{
    QListWidgetItem *item = m_permOriginsWidget->currentItem();
    if (!item)
        return;
    const QString origin = item->text();
    m_loading = true; // avoid feedback while populating
    for (int i = 0; i < 10; ++i) {
        const QString value = m_app->config()->permission(origin, kPermissionFeatures[i]);
        const int idx = value == QLatin1String("allow") ? 1
                        : value == QLatin1String("deny") ? 2 : 0;
        m_permCombos[i]->setCurrentIndex(idx);
    }
    m_loading = false;
}

void SettingsDialog::onPermissionComboChanged(int index)
{
    if (m_loading || index < 0)
        return;
    QListWidgetItem *item = m_permOriginsWidget->currentItem();
    if (!item)
        return;
    const QString origin = item->text();
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo)
        return;
    int featureIndex = -1;
    for (int i = 0; i < 10; ++i)
        if (m_permCombos[i] == combo)
            featureIndex = i;
    if (featureIndex < 0)
        return;
    const QString value = combo->currentData().toString();
    if (value.isEmpty())
        m_app->config()->removePermission(origin);
    else
        m_app->config()->setPermission(origin, kPermissionFeatures[featureIndex], value);
}

void SettingsDialog::removePermissionOrigin()
{
    QListWidgetItem *item = m_permOriginsWidget->currentItem();
    if (!item)
        return;
    m_app->config()->removePermission(item->text());
    refreshPermissions();
}

void SettingsDialog::removeAllPermissions()
{
    const QStringList origins = m_app->config()->permissionOrigins();
    for (const QString &origin : origins)
        m_app->config()->removePermission(origin);
    refreshPermissions();
}

// --------------------------------------------------------- retranslate ----

void SettingsDialog::retranslateUi()
{
    setWindowTitle(tr("Settings"));
    m_tabs->setTabText(0, tr("General"));
    m_tabs->setTabText(1, tr("Search"));
    m_tabs->setTabText(2, tr("Privacy"));
    m_tabs->setTabText(3, tr("Permissions"));

    m_themeCombo->setItemText(0, tr("Match my desktop theme"));
    m_themeCombo->setItemText(1, tr("Dark"));
    m_themeCombo->setItemText(2, tr("Light"));
    m_enginesTable->setHorizontalHeaderLabels({ tr("Name"), tr("URL template (%s = query)") });
    m_downloadAskCheck->setText(tr("Always ask where to save downloads"));
    m_bookmarksBarCheck->setText(tr("Show bookmarks bar"));
    m_suggestionsCheck->setText(tr("Suggest pages from history and bookmarks while typing"));
    m_adBlockCheck->setText(tr("Block ads and trackers"));
    m_dataSaverCheck->setText(tr("Data Saver"));
    m_thirdPartyImagesCheck->setText(tr("Also block third-party images"));
    m_blockImagesCheck->setText(tr("Block all images on sites without an exception"));
    m_autoUpdateCheck->setText(tr("Update filter lists automatically (downloads when online)"));
    m_updateNowButton->setText(tr("Update lists now"));
    m_clearDataButton->setText(tr("Clear browsing data…"));
    // Privacy hardening.
    m_3pCookiesCheck->setText(tr("Block third-party cookies"));
    m_sessionCookiesCheck->setText(
        tr("Keep cookies only until NØR closes (session-only cookies)"));
    m_dntCheck->setText(tr("Send the Do Not Track signal (DNT: 1)"));
    m_gpcCheck->setText(tr("Send the Global Privacy Control signal (Sec-GPC: 1)"));
    m_simplifiedUaCheck->setText(
        tr("Use the standard Chromium User-Agent (removes the QtWebEngine tag)"));
    m_webrtcCheck->setText(
        tr("WebRTC: share only the default public network interface"));
    m_hardeningHint->setText(
        tr("These options reduce what websites can learn about you. They do "
           "not make you anonymous: the browser's canvas, fonts and screen "
           "can still be measured by determined trackers (a Chromium-level "
           "limitation). The WebRTC option applies after a restart."));
    m_permEmptyLabel->setText(tr("No site has stored permissions yet."));
    m_homeEdit->setPlaceholderText(tr("nor://newtab"));
    m_permEmptyLabel->setVisible(m_permOriginsWidget->count() == 0);

    for (int i = 0; i < 10; ++i) {
        m_permCombos[i]->blockSignals(true);
        m_permCombos[i]->setItemText(0, tr("Ask"));
        m_permCombos[i]->setItemText(1, tr("Allow"));
        m_permCombos[i]->setItemText(2, tr("Deny"));
        m_permCombos[i]->blockSignals(false);
    }
    refreshPrivacyStats();
}
