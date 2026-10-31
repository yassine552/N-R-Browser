#include "TopSitesStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <cmath>

using namespace std::chrono_literals;

namespace {
// One half-life: visits older than this count for half their weight.
constexpr qreal kHalfLifeDays = 14.0;

// Common two-level public suffixes (a practical subset of the PSL — no
// dependency, covers the regions users actually hit). "www" is never a
// suffix. When the last two labels form one of these, the base domain is
// the last THREE labels (news.bbc.co.uk -> bbc.co.uk).
const char *kTwoLevelSuffixes[] = {
    "co.uk", "org.uk", "gov.uk", "ac.uk", "me.uk", "net.uk", "sch.uk",
    "co.jp", "or.jp", "ne.jp", "ac.jp", "go.jp", "gov.jp",
    "com.au", "net.au", "org.au", "gov.au", "edu.au", "id.au",
    "com.br", "net.br", "org.br", "gov.br",
    "com.cn", "net.cn", "org.cn", "gov.cn", "edu.cn",
    "com.mx", "org.mx", "net.mx", "gob.mx",
    "com.ar", "net.ar", "org.ar", "gob.ar",
    "com.tr", "net.tr", "org.tr", "gov.tr", "edu.tr",
    "com.eg", "net.eg", "org.eg", "gov.eg", "edu.eg", "eun.eg",
    "com.sa", "net.sa", "org.sa", "edu.sa", "gov.sa",
    "com.dz", "net.dz", "org.dz", "gov.dz", "edu.dz",
    "com.ma", "net.ma", "org.ma", "gov.ma", "ac.ma",
    "com.tn", "net.tn", "org.tn", "gov.tn", "edu.tn",
    "co.za", "org.za", "net.za", "gov.za", "ac.za",
    "co.nz", "net.nz", "org.nz", "govt.nz", "ac.nz",
    "co.in", "net.in", "org.in", "firm.in", "gen.in", "ac.in", "edu.in",
    "com.pl", "net.pl", "org.pl", "edu.pl", "gov.pl",
    "com.sg", "net.sg", "org.sg", "gov.sg", "edu.sg",
    "com.my", "net.my", "org.my", "edu.my", "gov.my",
    "co.id", "or.id", "web.id", "ac.id", "go.id",
    "com.vn", "net.vn", "org.vn", "edu.vn", "gov.vn",
    "com.ph", "net.ph", "org.ph", "gov.ph", "edu.ph",
    "com.pk", "net.pk", "org.pk", "gov.pk", "edu.pk",
    "com.bd", "net.bd", "org.bd", "gov.bd", "edu.bd",
    "co.il", "org.il", "net.il", "ac.il", "gov.il", "muni.il",
    "co.kr", "or.kr", "ne.kr", "re.kr", "pe.kr", "go.kr",
    "com.tw", "org.tw", "net.tw", "edu.tw", "gov.tw",
    "com.hk", "org.hk", "net.hk", "edu.hk", "gov.hk",
    "co.th", "or.th", "ac.th", "go.th", "in.th", "net.th",
    "com.ng", "net.ng", "org.ng", "gov.ng", "edu.ng",
    "co.ke", "or.ke", "ne.ke", "go.ke", "ac.ke",
    "com.gh", "org.gh", "gov.gh", "edu.gh",
    "com.cy", "net.cy", "org.cy", "gov.cy", "ac.cy",
    "com.mt", "net.mt", "org.mt", "gov.mt", "edu.mt",
    "com.jo", "net.jo", "org.jo", "gov.jo", "edu.jo",
    "com.lb", "net.lb", "org.lb", "gov.lb", "edu.lb",
    "com.kw", "net.kw", "org.kw", "gov.kw", "edu.kw",
    "com.qa", "net.qa", "org.qa", "gov.qa", "edu.qa",
    "com.bh", "net.bh", "org.bh", "gov.bh", "edu.bh",
    "com.om", "net.om", "org.om", "gov.om", "edu.om",
    "com.ae", "net.ae", "org.ae", "gov.ae", "ac.ae",
    "co.ye", "com.ye", "net.ye", "org.ye", "gov.ye",
    "com.ly", "net.ly", "org.ly", "gov.ly", "edu.ly",
    "com.sd", "net.sd", "org.sd", "gov.sd",
    "com.sy", "net.sy", "org.sy", "gov.sy",
    "com.iq", "net.iq", "org.iq", "gov.iq", "edu.iq",
    "co.uz", "com.uz", "net.uz", "org.uz",
    "com.kz", "net.kz", "org.kz", "edu.kz", "gov.kz",
    "com.ge", "net.ge", "org.ge", "gov.ge", "edu.ge",
    "com.am", "net.am", "org.am",
    "com.az", "net.az", "org.az",
    "co.md", "com.md", "net.md", "org.md",
    "co.rs", "com.rs", "net.rs", "org.rs", "edu.rs",
    "co.hr", "com.hr", "net.hr", "org.hr",
    "com.ua", "net.ua", "org.ua", "gov.ua", "edu.ua",
    "com.by", "net.by", "org.by", "gov.by",
    "com.ru", "net.ru", "org.ru", "edu.ru", "gov.ru",
    "com.pt", "net.pt", "org.pt", "edu.pt", "gov.pt",
    "com.es", "nom.es", "org.es", "gob.es", "edu.es",
    "com.it", "net.it", "org.it", "edu.it", "gov.it",
    "com.fr", "net.fr", "org.fr", "gouv.fr", "asso.fr",
    "com.de", "net.de", "org.de",
    "com.nl", "net.nl", "org.nl",
    "com.be", "net.be", "ac.be",
    "com.ch", "net.ch", "org.ch", "gov.ch",
    "co.at", "or.at", "ac.at", "gv.at",
    "co.cz", "com.cz", "net.cz", "org.cz",
    "com.gr", "net.gr", "org.gr", "gov.gr", "edu.gr",
    "com.ro", "net.ro", "org.ro", "gov.ro", "edu.ro",
    "com.bg", "net.bg", "org.bg", "gov.bg", "edu.bg",
    "com.hu", "net.hu", "org.hu", "gov.hu", "edu.hu",
    "com.fi", "net.fi", "org.fi",
    "com.dk", "net.dk", "org.dk",
    "com.no", "net.no", "org.no",
    "com.se", "net.se", "org.se",
    "com.is", "net.is", "org.is",
    "com.ie", "net.ie", "org.ie", "gov.ie",
    "co.tt", "com.tt", "net.tt", "org.tt",
    "com.ve", "net.ve", "org.ve", "gov.ve",
    "com.co", "net.co", "org.co", "gov.co", "edu.co",
    "com.pe", "net.pe", "org.pe", "gob.pe",
    "com.ec", "net.ec", "org.ec", "gob.ec",
    "com.uy", "net.uy", "org.uy", "gub.uy",
    "com.py", "net.py", "org.py", "gov.py",
    "com.bo", "net.bo", "org.bo", "gob.bo",
    "com.do", "net.do", "org.do", "gob.do",
    "com.gt", "net.gt", "org.gt", "gob.gt",
    "com.sv", "edu.sv", "gob.sv", "org.sv",
    "com.hn", "net.hn", "org.hn", "gob.hn",
    "com.ni", "net.ni", "org.ni", "gob.ni",
    "com.cr", "net.cr", "or.cr", "go.cr",
    "com.pa", "net.pa", "org.pa", "gob.pa",
    "com.pa", "gob.pa",
    "com.cu", "net.cu", "org.cu", "gov.cu",
};
} // namespace

TopSitesStore::TopSitesStore(HistoryStore *history, const QString &statePath,
                             QObject *parent)
    : QObject(parent)
    , m_history(history)
    , m_statePath(statePath)
{
    load();

    // The ranking follows the history. addVisit() fires historyChanged on
    // every page load; the recompute is one capped SQL SELECT, but still
    // debounced so a burst of redirects produces one refresh.
    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(250);
    connect(&m_refreshDebounce, &QTimer::timeout, this,
            &TopSitesStore::topSitesChanged);

    if (m_history) {
        connect(m_history, &HistoryStore::historyChanged, this,
                &TopSitesStore::scheduleRefresh);
        connect(m_history, &HistoryStore::historyCleared, this, [this]() {
            // Derived data wipes itself; explicit pins survive like
            // bookmarks. "Removed" is derived data (it lists hosts found
            // in the history), so it goes too.
            if (!m_removedHosts.isEmpty() || !m_removedUrls.isEmpty()) {
                m_removedHosts.clear();
                m_removedUrls.clear();
                save();
            }
            scheduleRefresh();
        });
    }
}

qreal TopSitesStore::frecencyScore(int visitCount, qint64 lastVisit, qint64 nowSecs)
{
    if (visitCount <= 0 || lastVisit <= 0)
        return 0.0;
    const qint64 ageSecs = qMax<qint64>(0, nowSecs - lastVisit);
    const qreal ageDays = qreal(ageSecs) / 86400.0;
    return qreal(visitCount) * std::pow(0.5, ageDays / kHalfLifeDays);
}

QString TopSitesStore::hostOf(const QString &url)
{
    const QUrl u(url);
    const QString scheme = u.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return QString();
    return u.host().toLower();
}

QString TopSitesStore::baseDomainOf(const QString &host)
{
    // "www.youtube.com" -> "youtube.com"; "news.bbc.co.uk" -> "bbc.co.uk";
    // "youtube.com" -> "youtube.com"; IPs and single labels pass through.
    if (host.isEmpty())
        return QString();
    QString h = host;
    // Strip leading "www." (and "www2.", "m." stays: it may be a real
    // mobile site, but it still groups with the base domain below).
    while (h.startsWith(QLatin1String("www.")))
        h = h.mid(4);
    if (h.count(QLatin1Char('.')) == 0)
        return h;                       // single label (or a bare TLD)
    // Literal IP addresses (v4: 4 numeric labels; v6: colons) pass through.
    if (h.contains(QLatin1Char(':')))
        return h;
    {
        const QStringList maybeIp = h.split(QLatin1Char('.'));
        bool numeric = !maybeIp.isEmpty();
        for (const QString &part : maybeIp) {
            bool ok = false;
            part.toInt(&ok);
            if (!ok || part.size() > 3) {
                numeric = false;
                break;
            }
        }
        if (numeric && maybeIp.size() == 4)
            return h;
    }
    const QStringList labels = h.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (labels.size() < 2)
        return labels.isEmpty() ? h : labels.first();
    // Two-level public suffix ("co.uk")? Then the base is three labels.
    const QString lastTwo = labels.at(labels.size() - 2) + QLatin1Char('.')
                            + labels.last();
    for (const char *suffix : kTwoLevelSuffixes) {
        if (lastTwo == QLatin1String(suffix)) {
            if (labels.size() >= 3)
                return labels.at(labels.size() - 3) + QLatin1Char('.') + lastTwo;
            return lastTwo;
        }
    }
    return labels.at(labels.size() - 2) + QLatin1Char('.') + labels.last();
}

void TopSitesStore::load()
{
    m_pinned.clear();
    m_removedHosts.clear();
    m_removedUrls.clear();
    QFile f(m_statePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return; // first run: empty state is the normal case
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("TopSitesStore: state file corrupt (%s), starting fresh",
                 qPrintable(err.errorString()));
        return;
    }
    const QJsonObject root = doc.object();
    for (const QJsonValue &v : root.value(QLatin1String("pinned")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString url = o.value(QLatin1String("url")).toString();
        if (url.isEmpty())
            continue;
        Pin p;
        p.url = url;
        p.title = o.value(QLatin1String("title")).toString();
        m_pinned.append(p);
    }
    for (const QJsonValue &v : root.value(QLatin1String("removedHosts")).toArray())
        m_removedHosts.append(v.toString());
    for (const QJsonValue &v : root.value(QLatin1String("removedUrls")).toArray())
        m_removedUrls.append(v.toString());
}

bool TopSitesStore::save()
{
    QJsonObject root;
    QJsonArray pins;
    for (const Pin &p : m_pinned) {
        QJsonObject o;
        o.insert(QLatin1String("url"), p.url);
        o.insert(QLatin1String("title"), p.title);
        pins.append(o);
    }
    root.insert(QLatin1String("pinned"), pins);
    root.insert(QLatin1String("removedHosts"),
                QJsonArray::fromStringList(m_removedHosts));
    root.insert(QLatin1String("removedUrls"),
                QJsonArray::fromStringList(m_removedUrls));
    // QSaveFile: atomic replace, no half-written file after a crash.
    QSaveFile f(m_statePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("TopSitesStore: cannot write %s", qPrintable(m_statePath));
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

void TopSitesStore::scheduleRefresh()
{
    m_refreshDebounce.start();
}

QVector<TopSite> TopSitesStore::topSites(int limit) const
{
    QVector<TopSite> result;
    if (limit <= 0)
        return result;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSet<QString> usedBases;    // base domains already represented by a tile
    QSet<QString> usedUrls;

    // ---------- pass 1: scan the history once ----------
    // Group by BASE DOMAIN (www.x.com, x.com and m.x.com are ONE site).
    // For every base we keep: the representative entry (highest frecency,
    // ties prefer the shorter path), the SUM of the per-URL scores (so
    // visiting a site from several subdomains still counts as one strong
    // site instead of several weak ones), and every host seen under that
    // base with its best score (favicon fallback chain).
    struct Group
    {
        HistoryEntry entry;             // representative
        qreal entryScore = 0.0;         // representative's own score
        qreal totalScore = 0.0;         // summed score of the whole base
        QHash<QString, qreal> hostBest; // host -> best score under this base
    };
    QHash<QString, Group> groups;
    if (m_history) {
        const QVector<HistoryEntry> entries = m_history->allEntries();
        for (const HistoryEntry &e : entries) {
            const QString host = hostOf(e.url);
            if (host.isEmpty())
                continue; // only http(s) pages
            const QString base = baseDomainOf(host);
            if (base.isEmpty())
                continue;
            // Dismissed? Old state files stored exact hosts, new state
            // stores base domains — both must keep hiding the site.
            if (m_removedHosts.contains(host) || m_removedHosts.contains(base))
                continue;
            if (m_removedUrls.contains(e.url))
                continue;

            const qreal score = frecencyScore(e.visitCount, e.lastVisit, now);
            Group &g = groups[base];
            g.totalScore += score;
            if (!g.hostBest.contains(host) || score > g.hostBest.value(host))
                g.hostBest.insert(host, score);
            if (score > g.entryScore
                || (qFuzzyCompare(score, g.entryScore) && !g.entry.url.isEmpty()
                    && QUrl(e.url).path().size() < QUrl(g.entry.url).path().size())) {
                g.entry = e;
                g.entryScore = score;
            }
        }
    }

    // Hosts under one base domain, best-score first (favicon fallback).
    auto hostsUnder = [](const Group &g) {
        QStringList hosts = g.hostBest.keys();
        std::sort(hosts.begin(), hosts.end(),
                  [&g](const QString &a, const QString &b) {
                      return g.hostBest.value(a) > g.hostBest.value(b);
                  });
        return hosts;
    };

    // ---------- pass 2: pins, in the order the user pinned them ----------
    for (const Pin &p : m_pinned) {
        if (result.size() >= limit)
            break;
        const QUrl u(p.url);
        if (!u.isValid() || u.host().isEmpty())
            continue;
        TopSite s;
        s.url = p.url;
        s.host = u.host().toLower();
        s.baseDomain = baseDomainOf(s.host);
        if (s.baseDomain.isEmpty() || usedBases.contains(s.baseDomain))
            continue;
        s.title = p.title.isEmpty() ? s.host : p.title;
        s.pinned = true;
        s.score = -1.0; // pins sit above ranking
        // Favicon fallback chain: the pin's own host first, then the
        // hosts the history actually saw under the same base domain.
        s.faviconHosts.append(s.host);
        if (m_history) {
            const auto it = groups.constFind(s.baseDomain);
            if (it != groups.constEnd() && !it.value().hostBest.contains(s.host))
                s.faviconHosts.append(hostsUnder(it.value()));
        }
        usedBases.insert(s.baseDomain);
        usedUrls.insert(p.url);
        result.append(s);
    }

    // ---------- pass 3: derived entries, ranked by the base-domain total --
    QVector<QPair<QString, Group>> ranked;
    ranked.reserve(groups.size());
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (usedBases.contains(it.key()))
            continue; // already represented by a pin
        if (usedUrls.contains(it.value().entry.url))
            continue;
        ranked.append(qMakePair(it.key(), it.value()));
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const QPair<QString, Group> &a, const QPair<QString, Group> &b) {
                  if (!qFuzzyCompare(a.second.totalScore, b.second.totalScore))
                      return a.second.totalScore > b.second.totalScore;
                  // Stable, human-friendly tie-break: newest first.
                  return a.second.entry.lastVisit > b.second.entry.lastVisit;
              });
    for (const auto &keyed : ranked) {
        if (result.size() >= limit)
            break;
        const Group &g = keyed.second;
        TopSite s;
        s.url = g.entry.url;
        s.host = hostOf(g.entry.url);
        s.baseDomain = keyed.first;
        s.title = g.entry.title.isEmpty() ? s.host : g.entry.title;
        s.pinned = false;
        s.score = g.totalScore;
        s.faviconHosts = hostsUnder(g); // representative host first by score
        result.append(s);
    }
    return result;
}

bool TopSitesStore::pinSite(const QUrl &url, const QString &title)
{
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString canonical = url.toString();
    for (Pin &p : m_pinned) {
        if (p.url == canonical) {
            if (p.title == title)
                return true; // idempotent
            p.title = title;
            save();
            scheduleRefresh();
            return true;
        }
    }
    Pin p;
    p.url = canonical;
    p.title = title;
    m_pinned.append(p);
    const bool ok = save();
    scheduleRefresh();
    return ok;
}

bool TopSitesStore::unpinSite(const QUrl &url)
{
    const QString canonical = url.toString();
    for (int i = 0; i < m_pinned.size(); ++i) {
        if (m_pinned.at(i).url == canonical) {
            m_pinned.removeAt(i);
            const bool ok = save();
            scheduleRefresh();
            return ok;
        }
    }
    return false;
}

bool TopSitesStore::isPinned(const QUrl &url) const
{
    const QString canonical = url.toString();
    for (const Pin &p : m_pinned) {
        if (p.url == canonical)
            return true;
    }
    return false;
}

bool TopSitesStore::removeSite(const QUrl &url, const QString &title)
{
    Q_UNUSED(title);
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString canonical = url.toString();
    const QString host = url.host().toLower();
    const QString base = baseDomainOf(host);
    bool changed = false;
    for (int i = 0; i < m_pinned.size(); ++i) {
        if (m_pinned.at(i).url == canonical) {
            m_pinned.removeAt(i);
            // A dismissed pin must not reappear from the history either:
            // hide its exact URL AND its whole base domain.
            if (!m_removedUrls.contains(canonical))
                m_removedUrls.append(canonical);
            if (!base.isEmpty() && !m_removedHosts.contains(base))
                m_removedHosts.append(base);
            changed = true;
            break;
        }
    }
    if (!changed) {
        // Derived entry: hide the whole site. Both the exact host (legacy
        // entries) and the base domain (www/m/mobile variants) are hidden.
        if (!m_removedHosts.contains(host)) {
            m_removedHosts.append(host);
            changed = true;
        }
        if (!base.isEmpty() && base != host && !m_removedHosts.contains(base)) {
            m_removedHosts.append(base);
            changed = true;
        }
    }
    if (!changed)
        return false;
    const bool ok = save();
    scheduleRefresh();
    return ok;
}

bool TopSitesStore::editSite(const QUrl &oldUrl, const QUrl &newUrl,
                             const QString &newTitle)
{
    if (!oldUrl.isValid() || !newUrl.isValid() || newUrl.host().isEmpty())
        return false;
    const QString oldCanonical = oldUrl.toString();
    const QString newCanonical = newUrl.toString();

    // Drop any pin on the old URL; the edit replaces it.
    for (int i = 0; i < m_pinned.size(); ++i) {
        if (m_pinned.at(i).url == oldCanonical) {
            m_pinned.removeAt(i);
            break;
        }
    }
    // The old URL must not resurface as a derived tile next to the edit;
    // neither may the old site's other subdomains (base-domain grouping).
    if (!m_removedUrls.contains(oldCanonical))
        m_removedUrls.append(oldCanonical);
    const QString oldBase = baseDomainOf(oldUrl.host().toLower());
    if (!oldBase.isEmpty() && !m_removedHosts.contains(oldBase)
        && baseDomainOf(newUrl.host().toLower()) != oldBase) {
        m_removedHosts.append(oldBase);
    }
    // If some other pin already holds the new URL, update its title.
    bool updated = false;
    for (Pin &p : m_pinned) {
        if (p.url == newCanonical) {
            p.title = newTitle;
            updated = true;
            break;
        }
    }
    if (!updated) {
        Pin p;
        p.url = newCanonical;
        p.title = newTitle;
        m_pinned.append(p);
    }
    const bool ok = save();
    scheduleRefresh();
    return ok;
}

bool TopSitesStore::clearAllState()
{
    m_pinned.clear();
    m_removedHosts.clear();
    m_removedUrls.clear();
    const bool ok = save();
    scheduleRefresh();
    return ok;
}
