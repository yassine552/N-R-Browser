#include "RustAdblockBackend.h"

#include "FilterRule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "nor_adblock.h"

RustAdblockBackend::~RustAdblockBackend()
{
    destroyEngine();
    destroyFilterSet();
}

void RustAdblockBackend::destroyEngine()
{
    if (m_engine) {
        nor_adblock_engine_free(static_cast<NorAdblockEngine *>(m_engine));
        m_engine = nullptr;
    }
}

void RustAdblockBackend::destroyFilterSet()
{
    if (m_filterSet) {
        nor_adblock_filter_set_free(static_cast<NorAdblockFilterSet *>(m_filterSet));
        m_filterSet = nullptr;
    }
}

void RustAdblockBackend::clear()
{
    QWriteLocker locker(&m_lock);
    destroyEngine();
    destroyFilterSet();
    m_pendingLists.clear();
    m_offeredBlocking = 0;
    m_offeredExceptions = 0;
    m_debugInfo = false;
}

void RustAdblockBackend::addList(const QString &listText)
{
    QWriteLocker locker(&m_lock);
    // Buffer the text; the engine is created in build(), once the total
    // rule count is known (that decides whether per-rule debug info is
    // affordable).
    m_pendingLists.append(listText);
    // Line-based statistics in the same spirit as the built-in backend:
    // lines that are neither comments nor metadata are "offered"; "@@"
    // lines are exceptions; cosmetic ("##") lines belong to the engine
    // but are not network rules.
    const QStringList lines = listText.split(QLatin1Char('\n'));
    for (QString rawLine : lines) {
        rawLine = rawLine.trimmed();
        if (rawLine.isEmpty() || rawLine.startsWith(QLatin1Char('!'))
            || rawLine.startsWith(QLatin1Char('[')))
            continue;
        if (rawLine.contains(QLatin1String("##")) || rawLine.contains(QLatin1String("#@#")))
            continue;
        if (rawLine.startsWith(QLatin1String("@@")))
            ++m_offeredExceptions;
        else
            ++m_offeredBlocking;
    }
}

void RustAdblockBackend::build()
{
    QWriteLocker locker(&m_lock);
    destroyEngine();
    destroyFilterSet();
    if (m_pendingLists.isEmpty())
        return;
    // Small rule sets keep per-rule debug info so diagnostics and tests
    // can show which rule matched; real-world lists skip it for memory.
    m_debugInfo = (m_offeredBlocking + m_offeredExceptions) <= DebugInfoRuleThreshold;
    m_filterSet = nor_adblock_filter_set_new(m_debugInfo ? 1 : 0);
    for (const QString &text : m_pendingLists)
        nor_adblock_filter_set_add_list(static_cast<NorAdblockFilterSet *>(m_filterSet),
                                        text.toUtf8().constData());
    m_pendingLists.clear();
    // nor_adblock_engine_new consumes the filter set (takes ownership).
    m_engine = nor_adblock_engine_new(static_cast<NorAdblockFilterSet *>(m_filterSet));
    m_filterSet = nullptr;
}

int RustAdblockBackend::blockRuleCount() const
{
    QReadLocker locker(&m_lock);
    return m_offeredBlocking;
}

int RustAdblockBackend::exceptionRuleCount() const
{
    QReadLocker locker(&m_lock);
    return m_offeredExceptions;
}

int RustAdblockBackend::skippedRuleCount() const
{
    // The Rust engine accepts the full EasyList grammar; lines it cannot
    // parse are dropped internally and are not surfaced through the API.
    return 0;
}

QByteArray RustAdblockBackend::requestTypeForFlags(quint32 resourceType)
{
    using T = FilterRule::ContentType;
    if (resourceType & FilterRule::TypeDocument)
        return QByteArrayLiteral("document");
    if (resourceType & FilterRule::TypeSubdocument)
        return QByteArrayLiteral("subdocument");
    if (resourceType & FilterRule::TypeStylesheet)
        return QByteArrayLiteral("stylesheet");
    if (resourceType & FilterRule::TypeScript)
        return QByteArrayLiteral("script");
    if (resourceType & FilterRule::TypeImage)
        return QByteArrayLiteral("image");
    if (resourceType & FilterRule::TypeFont)
        return QByteArrayLiteral("font");
    if (resourceType & FilterRule::TypeMedia)
        return QByteArrayLiteral("media");
    if (resourceType & FilterRule::TypeXmlHttpRequest)
        return QByteArrayLiteral("xmlhttprequest");
    if (resourceType & FilterRule::TypePing)
        return QByteArrayLiteral("ping");
    if (resourceType & FilterRule::TypeWebSocket)
        return QByteArrayLiteral("websocket");
    if (resourceType & FilterRule::TypeObject)
        return QByteArrayLiteral("object");
    Q_UNUSED(T::TypeOther);
    return QByteArrayLiteral("other");
}

FilterDecision RustAdblockBackend::decide(const QUrl &requestUrl, const QUrl &firstPartyUrl,
                                          quint32 resourceType) const
{
    QReadLocker locker(&m_lock);
    FilterDecision d;
    if (!m_engine)
        return d;
    const QByteArray url = requestUrl.toString().toUtf8();
    const QByteArray firstParty = firstPartyUrl.isEmpty()
                                      ? QByteArrayLiteral("")
                                      : firstPartyUrl.toString().toUtf8();
    const QByteArray requestType = requestTypeForFlags(resourceType);
    char *matchedRule = nullptr;
    const int result = nor_adblock_engine_should_block(
        static_cast<NorAdblockEngine *>(m_engine),
        url.constData(), firstParty.constData(), requestType.constData(),
        "get", &matchedRule);
    if (matchedRule) {
        d.matchedRule = QString::fromUtf8(matchedRule);
        nor_adblock_string_free(matchedRule);
    }
    d.block = (result == 1);
    return d;
}

CosmeticResources RustAdblockBackend::cosmeticResources(const QUrl &pageUrl) const
{
    QReadLocker locker(&m_lock);
    CosmeticResources out;
    if (!m_engine)
        return out;
    const QByteArray url = pageUrl.toString().toUtf8();
    char *json = nor_adblock_engine_url_cosmetic_resources(
        static_cast<NorAdblockEngine *>(m_engine), url.constData());
    if (!json)
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    nor_adblock_string_free(json);
    if (!doc.isObject())
        return out;
    const QJsonObject obj = doc.object();
    const QJsonArray selectors = obj.value(QLatin1String("hide_selectors")).toArray();
    for (const QJsonValue &v : selectors)
        out.hideSelectors.append(v.toString());
    const QJsonArray exceptions = obj.value(QLatin1String("exceptions")).toArray();
    for (const QJsonValue &v : exceptions)
        out.exceptions.append(v.toString());
    out.injectedScript = obj.value(QLatin1String("injected_script")).toString();
    out.genericHide = obj.value(QLatin1String("generichide")).toBool(false);
    out.isValid = true;
    return out;
}

QStringList RustAdblockBackend::genericHideSelectors(const QStringList &classes,
                                                     const QStringList &ids,
                                                     const QStringList &exceptions) const
{
    QReadLocker locker(&m_lock);
    QStringList result;
    if (!m_engine)
        return result;
    const QByteArray classesCsv = classes.join(QLatin1Char(',')).toUtf8();
    const QByteArray idsCsv = ids.join(QLatin1Char(',')).toUtf8();
    const QByteArray exceptionsCsv = exceptions.join(QLatin1Char(',')).toUtf8();
    char *json = nor_adblock_engine_generic_hide_selectors(
        static_cast<NorAdblockEngine *>(m_engine),
        classesCsv.constData(), idsCsv.constData(), exceptionsCsv.constData());
    if (!json)
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    nor_adblock_string_free(json);
    if (!doc.isArray())
        return result;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr)
        result.append(v.toString());
    return result;
}

QByteArray RustAdblockBackend::serializeEngine() const
{
    QReadLocker locker(&m_lock);
    if (!m_engine)
        return {};
    const NorAdblockBytes bytes = nor_adblock_engine_serialize(
        static_cast<NorAdblockEngine *>(m_engine));
    if (!bytes.data || bytes.len == 0)
        return {};
    // The buffer is owned by the engine: copy immediately.
    return QByteArray(reinterpret_cast<const char *>(bytes.data), qsizetype(bytes.len));
}

bool RustAdblockBackend::deserializeEngine(const QByteArray &data)
{
    QWriteLocker locker(&m_lock);
    destroyEngine();
    destroyFilterSet();
    if (data.isEmpty())
        return false;
    NorAdblockEngine *engine = nor_adblock_engine_deserialize(
        reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()));
    if (!engine)
        return false;
    m_engine = engine;
    // Counts are not recoverable from the serialized engine; keep whatever
    // the caller accumulated (the client reloads stats from list texts).
    return true;
}
