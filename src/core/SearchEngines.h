// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

class AppConfig;

struct SearchEngine
{
    QString name;        // unique display name
    QString urlTemplate; // contains "%s" where the query is inserted
    bool isValid() const { return !name.isEmpty() && urlTemplate.contains(QLatin1String("%s")); }
};

// Search-engine configuration backed by AppConfig. DuckDuckGo is the
// default engine. New engines can be added and edited in Settings.
class SearchEngines : public QObject
{
    Q_OBJECT
public:
    explicit SearchEngines(AppConfig &config, QObject *parent = nullptr);

    static QVector<SearchEngine> defaultEngines();

    QVector<SearchEngine> engines() const;
    SearchEngine engine(const QString &name) const;
    SearchEngine defaultEngine() const;
    void setEngines(const QVector<SearchEngine> &engines, const QString &defaultName);

    // Decides whether user input in the address bar is a URL or a search
    // query, and produces the URL to navigate to.
    static bool looksLikeUrl(const QString &input);
    static QUrl urlForNavigation(const QString &input, const SearchEngine &fallbackEngine);
    static QUrl urlForQuery(const QString &query, const SearchEngine &engine);

signals:
    void enginesChanged();

private:
    void load();
    void save();

    AppConfig &m_config;
    QVector<SearchEngine> m_engines;
    QString m_defaultName;
};
