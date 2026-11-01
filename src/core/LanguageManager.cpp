// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#include "LanguageManager.h"

#include "AppConfig.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QLibraryInfo>
#include <QTranslator>
#include <QWidget>

using namespace std::chrono_literals;

namespace {
// Ordered fallback locations probed for "<prefix>nor-browser_<lang>.qm":
//   1. embedded Qt resources (":/translations", ":/i18n")
//   2. <executable dir>/translations
//   3. Qt installation translations path
//   4. build-time output directory (developer runs)
QStringList qmSearchDirs()
{
    QStringList dirs;
    dirs << QStringLiteral(":/translations")
         << QStringLiteral(":/i18n")
         << QCoreApplication::applicationDirPath() + QStringLiteral("/translations")
         << QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#ifdef NOR_BUILD_TRANSLATIONS_DIR
    if (QDir(QStringLiteral(NOR_BUILD_TRANSLATIONS_DIR)).exists())
        dirs << QStringLiteral(NOR_BUILD_TRANSLATIONS_DIR);
#endif
    return dirs;
}
} // namespace

LanguageManager::LanguageManager(AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
}

const QVector<LanguageManager::LanguageInfo> &LanguageManager::availableLanguages()
{
    static const QVector<LanguageInfo> languages = {
        { QStringLiteral("en"), QStringLiteral("English"),  QStringLiteral("English"),  Qt::LeftToRight },
        { QStringLiteral("ar"), QStringLiteral("Arabic"),   QStringLiteral("العربية"),  Qt::RightToLeft },
        { QStringLiteral("fr"), QStringLiteral("French"),   QStringLiteral("Français"), Qt::LeftToRight },
    };
    return languages;
}

bool LanguageManager::isKnownLanguage(const QString &code)
{
    for (const LanguageInfo &info : availableLanguages())
        if (info.code == code)
            return true;
    return false;
}

QString LanguageManager::nativeNameFor(const QString &code)
{
    for (const LanguageInfo &info : availableLanguages())
        if (info.code == code)
            return info.nativeName;
    return code;
}

Qt::LayoutDirection LanguageManager::directionFor(const QString &code)
{
    for (const LanguageInfo &info : availableLanguages())
        if (info.code == code)
            return info.direction;
    return Qt::LeftToRight;
}

void LanguageManager::initialize()
{
    QString code = m_config.language();
    if (!isKnownLanguage(code))
        code = QStringLiteral("en");
    if (!loadTranslators(code)) {
        qWarning("LanguageManager: translators for '%s' could not be loaded, using English",
                 qPrintable(code));
        code = QStringLiteral("en");
        loadTranslators(code);
    }
    m_current = code;
    applyLayoutDirection();
}

Qt::LayoutDirection LanguageManager::layoutDirection() const
{
    return directionFor(m_current);
}

QLocale LanguageManager::locale() const
{
    return QLocale(m_current == QStringLiteral("ar") ? QLocale::Arabic
                   : m_current == QStringLiteral("fr") ? QLocale::French
                                                       : QLocale::English);
}

void LanguageManager::setLanguage(const QString &code)
{
    if (code == m_current || !isKnownLanguage(code))
        return;

    clearTranslators();
    if (!loadTranslators(code)) {
        qWarning("LanguageManager: failed loading '%s', reverting to English", qPrintable(code));
        loadTranslators(QStringLiteral("en"));
        m_current = QStringLiteral("en");
    } else {
        m_current = code;
    }
    m_config.setLanguage(m_current);
    applyLayoutDirection();
    emit languageChanged(m_current);
}

bool LanguageManager::loadTranslators(const QString &code)
{
    // Application-owned strings.
    auto *appTranslator = new QTranslator(this);
    bool appLoaded = false;
    for (const QString &dir : qmSearchDirs()) {
        if (appTranslator->load(QStringLiteral("nor-browser_") + code, dir)) {
            appLoaded = true;
            break;
        }
    }
    if (!appLoaded) {
        if (code != QStringLiteral("en")) // English ships no .qm by design
            qWarning("LanguageManager: nor-browser_%s.qm not found", qPrintable(code));
        delete appTranslator;
        if (code == QStringLiteral("en"))
            return true; // English is the source language: nothing to load.
        return false;
    }
    QCoreApplication::installTranslator(appTranslator);
    m_translators.append(appTranslator);

    // Qt's own strings (standard buttons, dialogs) when available.
    auto *qtTranslator = new QTranslator(this);
    for (const QString &dir : qmSearchDirs()) {
        if (qtTranslator->load(QStringLiteral("qtbase_") + code, dir)) {
            QCoreApplication::installTranslator(qtTranslator);
            m_translators.append(qtTranslator);
            break;
        }
    }
    if (m_translators.size() == 1) // only the app translator was loaded
        delete qtTranslator;
    return true;
}

void LanguageManager::clearTranslators()
{
    for (QTranslator *t : m_translators) {
        QCoreApplication::removeTranslator(t);
        t->deleteLater();
    }
    m_translators.clear();
}

void LanguageManager::applyLayoutDirection()
{
    const Qt::LayoutDirection dir = layoutDirection();
    if (QApplication::instance()) {
        QApplication::setLayoutDirection(dir);
        const QWidgetList tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops)
            w->setLayoutDirection(dir);
    }
}
