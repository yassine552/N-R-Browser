#include "BackgroundStore.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
// Bundled catalog: ids map 1:1 onto resources/backgrounds/<id>.jpg. Names are
// translated here so the customization gallery is localized with the rest of
// the UI. "dark" decides the New Tab text scheme over that artwork.
struct BundledInfo
{
    const char *id;
    const char *name;   // QT_TRANSLATE_NOOP source
    bool dark;
};

const BundledInfo kBundled[] = {
    { "nor-aurora",   QT_TRANSLATE_NOOP("NewTabPage", "Aurora"),        true  },
    { "nor-nebula",   QT_TRANSLATE_NOOP("NewTabPage", "Deep Space"),    true  },
    { "nor-ridge",    QT_TRANSLATE_NOOP("NewTabPage", "Night Ridge"),   true  },
    { "nor-zenith",   QT_TRANSLATE_NOOP("NewTabPage", "Dawn"),          false },
    { "nor-mesh",     QT_TRANSLATE_NOOP("NewTabPage", "Minimal Mesh"),  false },
    { "nor-slate",    QT_TRANSLATE_NOOP("NewTabPage", "Slate"),         true  },
    { "nor-dune",     QT_TRANSLATE_NOOP("NewTabPage", "Dusk Dunes"),    true  },
    { "nor-graphite", QT_TRANSLATE_NOOP("NewTabPage", "Graphite"),      true  },
};
constexpr int kBundledCount = int(sizeof(kBundled) / sizeof(kBundled[0]));

QString NTP(const char *source)
{
    return QCoreApplication::translate("NewTabPage", source);
}

// Ids appear in URLs (nor://background/<id>) — keep them strictly lexical.
bool validId(const QString &id)
{
    if (id.isEmpty() || id.size() > 48)
        return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-';
        if (!ok)
            return false;
    }
    return true;
}
} // namespace

BackgroundStore::BackgroundStore(const QString &customDir, QObject *parent)
    : QObject(parent)
    , m_customDir(customDir)
{
    QDir().mkpath(m_customDir);
    loadManifest();
}

QStringList BackgroundStore::bundledIds()
{
    QStringList ids;
    ids.reserve(kBundledCount);
    for (int i = 0; i < kBundledCount; ++i)
        ids << QLatin1String(kBundled[i].id);
    return ids;
}

void BackgroundStore::loadManifest()
{
    m_custom.clear();
    QFile f(manifestPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return; // first run: no imports yet
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QLatin1String("id")).toString();
        e.name = o.value(QLatin1String("name")).toString();
        e.dark = o.value(QLatin1String("dark")).toBool(true);
        e.bundled = false;
        if (validId(e.id) && QFile::exists(m_customDir + QLatin1Char('/') + e.id + QStringLiteral(".jpg")))
            m_custom.append(e);
    }
}

void BackgroundStore::saveManifest() const
{
    QJsonArray arr;
    for (const Entry &e : m_custom) {
        QJsonObject o;
        o.insert(QLatin1String("id"), e.id);
        o.insert(QLatin1String("name"), e.name);
        o.insert(QLatin1String("dark"), e.dark);
        arr.append(o);
    }
    QSaveFile f(manifestPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}

QString BackgroundStore::manifestPath() const
{
    return m_customDir + QStringLiteral("/manifest.json");
}

QVector<BackgroundStore::Entry> BackgroundStore::entries() const
{
    QVector<Entry> out;
    out.reserve(kBundledCount + m_custom.size());
    for (int i = 0; i < kBundledCount; ++i) {
        Entry e;
        e.id = QLatin1String(kBundled[i].id);
        e.name = NTP(kBundled[i].name);
        e.dark = kBundled[i].dark;
        e.bundled = true;
        out.append(e);
    }
    out += m_custom;
    return out;
}

bool BackgroundStore::contains(const QString &id) const
{
    if (!validId(id))
        return false;
    for (int i = 0; i < kBundledCount; ++i) {
        if (id == QLatin1String(kBundled[i].id))
            return true;
    }
    for (const Entry &e : m_custom) {
        if (e.id == id)
            return true;
    }
    return false;
}

BackgroundStore::Entry BackgroundStore::entry(const QString &id) const
{
    for (const Entry &e : entries()) {
        if (e.id == id)
            return e;
    }
    Entry none;
    none.bundled = false;
    return none;
}

QByteArray BackgroundStore::imageBytes(const QString &id) const
{
    if (!validId(id))
        return QByteArray();
    if (id.startsWith(QLatin1String("nor-"))) {
        QFile f(QStringLiteral(":/backgrounds/%1.jpg").arg(id));
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    }
    QFile f(m_customDir + QLatin1Char('/') + id + QStringLiteral(".jpg"));
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

QByteArray BackgroundStore::thumbnailBytes(const QString &id) const
{
    if (!validId(id))
        return QByteArray();
    if (id.startsWith(QLatin1String("nor-"))) {
        QFile f(QStringLiteral(":/backgrounds/thumbs/%1.jpg").arg(id));
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    }
    // Customs: generate once and cache beside the full image.
    const QString cache = m_customDir + QLatin1Char('/') + id + QStringLiteral("-thumb.jpg");
    {
        QFile f(cache);
        if (f.open(QIODevice::ReadOnly))
            return f.readAll();
    }
    const QImage full(m_customDir + QLatin1Char('/') + id + QStringLiteral(".jpg"));
    if (full.isNull())
        return QByteArray();
    // Center-crop to 16:9 then downscale to 240x135.
    const qreal ratio = qMax(qreal(240) / full.width(), qreal(135) / full.height());
    const int nw = qRound(full.width() * ratio), nh = qRound(full.height() * ratio);
    QImage scaled = full.scaled(nw, nh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int x = (nw - 240) / 2, y = (nh - 135) / 2;
    QImage thumb = scaled.copy(x, y, 240, 135);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    thumb.save(&buffer, "JPEG", 70);
    if (!bytes.isEmpty()) {
        QSaveFile f(cache);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(bytes);
            f.commit();
        }
    }
    return bytes;
}

int BackgroundStore::averageLuminance(const QImage &image)
{
    if (image.isNull())
        return 0;
    // Sample grid (fast even for huge photos) of perceived luminance.
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const int stepX = qMax(1, gray.width() / 64);
    const int stepY = qMax(1, gray.height() / 64);
    qint64 sum = 0;
    int n = 0;
    for (int y = 0; y < gray.height(); y += stepY) {
        for (int x = 0; x < gray.width(); x += stepX) {
            sum += qRed(gray.pixel(x, y)); // grayscale: all channels equal
            ++n;
        }
    }
    return n ? int(sum / n) : 0;
}

QString BackgroundStore::importFromFile(const QString &sourcePath, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QString();
    };

    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile())
        return fail(tr("File does not exist."));

    QImage image(sourcePath);
    if (image.isNull())
        return fail(tr("Unsupported or corrupted image."));

    // Cap the longest edge: backgrounds render with "cover", so anything
    // beyond 1920px only costs RAM (a 4000px photo decodes to ~60 MB).
    if (qMax(image.width(), image.height()) > 1920) {
        const qreal scale = 1920.0 / qMax(image.width(), image.height());
        image = image.scaled(qRound(image.width() * scale),
                             qRound(image.height() * scale),
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const bool dark = averageLuminance(image) < 110;
    const QByteArray hash = QCryptographicHash::hash(
        sourcePath.toUtf8() + QByteArray::number(image.size().width())
        + QByteArray::number(image.size().height()),
        QCryptographicHash::Md5).toHex().left(8);
    const QString id = QStringLiteral("custom-") + QLatin1String(hash);

    // Same image imported twice: keep one copy, refresh the name.
    for (const Entry &e : m_custom) {
        if (e.id == id) {
            saveManifest();
            return id;
        }
    }

    const QString target = m_customDir + QLatin1Char('/') + id + QStringLiteral(".jpg");
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 78))
        return fail(tr("Could not process the image."));
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly)) {
        return fail(tr("Could not save the background."));
    }
    f.write(bytes);
    if (!f.commit())
        return fail(tr("Could not save the background."));

    Entry e;
    e.id = id;
    e.name = info.completeBaseName();
    e.dark = dark;
    e.bundled = false;
    m_custom.append(e);
    saveManifest();
    return id;
}

bool BackgroundStore::removeCustom(const QString &id)
{
    if (!validId(id) || id.startsWith(QLatin1String("nor-")))
        return false;
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom.at(i).id == id) {
            m_custom.removeAt(i);
            QFile::remove(m_customDir + QLatin1Char('/') + id + QStringLiteral(".jpg"));
            QFile::remove(m_customDir + QLatin1Char('/') + id + QStringLiteral("-thumb.jpg"));
            saveManifest();
            return true;
        }
    }
    return false;
}

QString BackgroundStore::randomId(const QString &exclude) const
{
    const QVector<Entry> all = entries();
    if (all.isEmpty())
        return exclude;
    // Deterministic-per-second pick: pleasant in tests, non-repetitive in use.
    const qsizetype seed = QDateTime::currentSecsSinceEpoch() ^ exclude.size();
    const qsizetype pick = seed % all.size();
    if (all.at(pick).id != exclude)
        return all.at(pick).id;
    return all.at((pick + 1) % all.size()).id;
}
