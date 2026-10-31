#include "SearchEngines.h"

#include "AppConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

using namespace std::chrono_literals;

SearchEngines::SearchEngines(AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    load();
}

QVector<SearchEngine> SearchEngines::defaultEngines()
{
    return {
        { QStringLiteral("DuckDuckGo"), QStringLiteral("https://duckduckgo.com/?q=%s") },
        { QStringLiteral("Google"),     QStringLiteral("https://www.google.com/search?q=%s") },
        { QStringLiteral("Bing"),       QStringLiteral("https://www.bing.com/search?q=%s") },
        { QStringLiteral("Wikipedia"),  QStringLiteral("https://en.wikipedia.org/w/index.php?search=%s") },
    };
}

void SearchEngines::load()
{
    m_engines = defaultEngines();
    m_defaultName = QStringLiteral("DuckDuckGo");

    const QString json = m_config.searchEnginesJson();
    if (!json.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (doc.isArray()) {
            QVector<SearchEngine> parsed;
            for (const QJsonValue &v : doc.array()) {
                const QJsonObject o = v.toObject();
                SearchEngine e{ o.value(QStringLiteral("name")).toString(),
                                o.value(QStringLiteral("url")).toString() };
                if (e.isValid())
                    parsed.append(e);
            }
            if (!parsed.isEmpty())
                m_engines = parsed;
        }
    }
    const QString def = m_config.defaultSearchEngine();
    if (!def.isEmpty() && engine(def).isValid())
        m_defaultName = def;
}

void SearchEngines::save()
{
    QJsonArray arr;
    for (const SearchEngine &e : m_engines) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), e.name);
        o.insert(QStringLiteral("url"), e.urlTemplate);
        arr.append(o);
    }
    m_config.setSearchEnginesJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    m_config.setDefaultSearchEngine(m_defaultName);
    m_config.sync();
}

QVector<SearchEngine> SearchEngines::engines() const { return m_engines; }

SearchEngine SearchEngines::engine(const QString &name) const
{
    for (const SearchEngine &e : m_engines)
        if (e.name.compare(name, Qt::CaseInsensitive) == 0)
            return e;
    return SearchEngine();
}

SearchEngine SearchEngines::defaultEngine() const
{
    const SearchEngine e = engine(m_defaultName);
    if (e.isValid())
        return e;
    return m_engines.isEmpty() ? SearchEngine() : m_engines.first();
}

void SearchEngines::setEngines(const QVector<SearchEngine> &engines, const QString &defaultName)
{
    m_engines = engines;
    if (!defaultName.isEmpty() && engine(defaultName).isValid())
        m_defaultName = defaultName;
    save();
    emit enginesChanged();
}

bool SearchEngines::looksLikeUrl(const QString &rawInput)
{
    QString input = rawInput.trimmed();
    input.remove(QRegularExpression(QStringLiteral("^['\"]|['\"]$")));
    if (input.isEmpty() || input.contains(QLatin1Char(' ')))
        return false;
    // Explicit schemes are URLs (http, https, file, ftp, nor, about, data...).
    if (input.contains(QStringLiteral("://")))
        return true;
    if (input.startsWith(QStringLiteral("about:"), Qt::CaseInsensitive))
        return true;
    if (input.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive))
        return true;
    // "localhost" with optional port/path.
    if (input.startsWith(QStringLiteral("localhost"), Qt::CaseInsensitive))
        return true;
    // Bare IPv4 address.
    static const QRegularExpression ipv4(
        QStringLiteral("^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}(:[0-9]+)?(/.*)?$"));
    if (ipv4.match(input).hasMatch())
        return true;
    // A dot-separated host with an alphabetic TLD, possibly with path/query.
    // International host names work too: QUrl::fromUserInput() handles them.
    static const QRegularExpression host(
        QStringLiteral("^[^/\\s]+\\.[^./\\s\\d]{2,}(:[0-9]+)?([/?#].*)?$"));
    return host.match(input).hasMatch();
}

QUrl SearchEngines::urlForQuery(const QString &query, const SearchEngine &engine)
{
    if (!engine.isValid())
        return QUrl();
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
    QString tpl = engine.urlTemplate;
    return QUrl(tpl.replace(QStringLiteral("%s"), encoded));
}

QUrl SearchEngines::urlForNavigation(const QString &input, const SearchEngine &fallbackEngine)
{
    if (looksLikeUrl(input)) {
        QUrl url = QUrl::fromUserInput(input.trimmed());
        if (url.isValid())
            return url;
    }
    return urlForQuery(input, fallbackEngine);
}
