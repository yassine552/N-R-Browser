// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QUrl>

#include <QWebEngineUrlRequestInterceptor>

class AdBlockClient;
class AppConfig;

// Installs privacy/Data-Saver request policy on a WebEngine profile.
// One instance per profile; the profile does not own the interceptor, so
// ProfileManager parents it to itself and deletes it strictly after the
// profile has been destroyed.
// Note: QWebEngineUrlRequestInterceptor already derives from QObject; no
// extra QObject base is added (avoids an ambiguous-base diamond).
//
// Threading: interceptRequest() runs on Qt WebEngine's IO thread while the
// GUI thread updates preferences. Reading AppConfig (QSettings) from the IO
// thread is a data race, so the policy is snapshotted on the GUI thread by
// refreshPolicy() and published behind a mutex; interceptRequest() copies
// the snapshot under the lock and then decides without touching settings.
class RequestInterceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    RequestInterceptor(AppConfig &config, AdBlockClient &client, QObject *parent = nullptr);

    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

    // GUI thread only: re-reads the privacy/Data-Saver preferences into a
    // thread-safe snapshot. Called at construction and whenever settings
    // change.
    void refreshPolicy();

    // Maps WebEngine resource types onto FilterRule content-type flags.
    static quint32 mapResourceType(QWebEngineUrlRequestInfo::ResourceType type);

    // Pure Data-Saver policy, unit-testable without WebEngine:
    // should an image request be blocked given the per-site exceptions?
    //   - blockAllImages: block every image on non-excepted sites;
    //   - otherwise blockThirdPartyImages: block cross-site images only;
    //   - excepted sites never have images blocked by Data Saver.
    static bool dataSaverBlocksImage(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                                      bool dataSaverEnabled, bool siteExcepted,
                                      bool blockAllImages, bool blockThirdPartyImages);

signals:
    void blockedRequest(const QString &host);

private:
    AppConfig &m_config;
    AdBlockClient &m_client;

    struct Policy
    {
        bool dnt = false;
        bool gpc = false;
        bool dataSaverEnabled = false;
        bool dsBlockImages = false;
        bool dsBlockThirdPartyImages = false;
        QStringList dsExceptions;
    };
    mutable QMutex m_policyMutex;
    Policy m_policy;
};
