// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QDialog>

class BrowserApplication;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTableWidget;
class QTabWidget;

// Tabbed settings dialog: General / Search / Privacy / Permissions.
// Changes apply immediately (QSettings is written through AppConfig).
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(BrowserApplication *app, QWidget *parent = nullptr);

signals:
    // Emitted when the user picks a new UI language.
    void languageSwitchRequested(const QString &code);

private slots:
    void applyLanguage(int index);
    void saveHomePage();
    void saveDownloadsAsk(bool ask);
    void pickDownloadDirectory();

    void addSearchEngine();
    void removeSearchEngine();
    void onEngineItemChanged();
    void onDefaultEngineChanged(int index);

    void toggleAdBlock(bool on);
    void addFilterListFile();
    void removeFilterListFile();
    void addListUrl();
    void removeListUrl();
    void toggleAutoUpdate(bool on);
    void addAllowlistHost();
    void removeAllowlistHost();

    void toggleDataSaver(bool on);
    void toggleBlockImages(bool on);
    void toggleThirdPartyImages(bool on);
    void addDataSaverException();
    void removeDataSaverException();

    void toggleBlock3pCookies(bool on);
    void toggleSessionOnlyCookies(bool on);
    void toggleDnt(bool on);
    void toggleGpc(bool on);
    void toggleSimplifiedUa(bool on);
    void toggleWebrtcPublicIpOnly(bool on);

    void onPermissionOriginSelected();
    void onPermissionComboChanged(int index);
    void removePermissionOrigin();
    void removeAllPermissions();

private:
    void buildUi();
    void loadValues();
    void refreshPrivacyStats();
    void refreshPermissions();
    void retranslateUi();
    void saveEngines();

    BrowserApplication *m_app;
    QTabWidget *m_tabs = nullptr;

    // General
    QComboBox *m_languageCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QLineEdit *m_homeEdit = nullptr;
    QLineEdit *m_downloadDirEdit = nullptr;
    QCheckBox *m_downloadAskCheck = nullptr;
    QCheckBox *m_bookmarksBarCheck = nullptr;

    // Search
    QTableWidget *m_enginesTable = nullptr;
    QComboBox *m_defaultEngineCombo = nullptr;

    // Privacy
    QCheckBox *m_adBlockCheck = nullptr;
    QLabel *m_statsLabel = nullptr;
    QListWidget *m_filterListsWidget = nullptr;
    QListWidget *m_listUrlsWidget = nullptr;
    QLineEdit *m_listUrlEdit = nullptr;
    QCheckBox *m_autoUpdateCheck = nullptr;
    QPushButton *m_updateNowButton = nullptr;
    QListWidget *m_allowlistWidget = nullptr;
    QLineEdit *m_allowlistEdit = nullptr;
    QCheckBox *m_dataSaverCheck = nullptr;
    QCheckBox *m_blockImagesCheck = nullptr;
    QCheckBox *m_thirdPartyImagesCheck = nullptr;
    QCheckBox *m_suggestionsCheck = nullptr;
    QListWidget *m_dsExceptionsWidget = nullptr;
    QLineEdit *m_dsExceptionEdit = nullptr;
    QPushButton *m_clearDataButton = nullptr;

    // Privacy hardening (cookies, signals, fingerprint unification).
    QCheckBox *m_3pCookiesCheck = nullptr;
    QCheckBox *m_sessionCookiesCheck = nullptr;
    QCheckBox *m_dntCheck = nullptr;
    QCheckBox *m_gpcCheck = nullptr;
    QCheckBox *m_simplifiedUaCheck = nullptr;
    QCheckBox *m_webrtcCheck = nullptr;
    QLabel *m_hardeningHint = nullptr;

    // Permissions
    QListWidget *m_permOriginsWidget = nullptr;
    QComboBox *m_permCombos[10] = {};
    QLabel *m_permEmptyLabel = nullptr;

    bool m_loading = true;
};
