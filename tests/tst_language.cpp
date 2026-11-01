// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
// Unit tests for localization: translator loading, language switching,
// RTL direction, and persistence of the language preference.
#include <QtTest>

#include "AppConfig.h"
#include "LanguageManager.h"

class TestLanguage : public QObject
{
    Q_OBJECT

private slots:
    void registryContainsEnArFr();
    void directions();
    void englishNeedsNoQm();
    void qmFilesExist();
    void arabicTranslationLoads();
    void frenchTranslationLoads();
    void unknownFallsBackToEnglish();
    void switchingUpdatesConfig();
    void runtimeSwitchSwapsTranslators();
    void arabicAppliesRtlLayout();

private:
    QString findQm(const QString &name) const;
    QTemporaryDir m_dir;
};

QString TestLanguage::findQm(const QString &name) const
{
    // Search the build tree (NOR_BUILD_DIR is set by CTest).
    const QString buildDir = qEnvironmentVariable("NOR_BUILD_DIR");
    if (!buildDir.isEmpty()) {
        QDir dir(buildDir);
        QDirIterator it(buildDir, { name }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            return it.next();
        }
        Q_UNUSED(dir);
    }
    // Fall back to the source tree next to the sources.
    QDirIterator it2(QCoreApplication::applicationDirPath(), { name }, QDir::Files,
                     QDirIterator::Subdirectories);
    while (it2.hasNext())
        return it2.next();
    return QString();
}

void TestLanguage::registryContainsEnArFr()
{
    const QVector<LanguageManager::LanguageInfo> langs = LanguageManager::availableLanguages();
    QCOMPARE(langs.size(), 3);
    QCOMPARE(langs.at(0).code, QStringLiteral("en"));
    QCOMPARE(langs.at(1).code, QStringLiteral("ar"));
    QCOMPARE(langs.at(2).code, QStringLiteral("fr"));
    // Native names are shown to users in their own script.
    QVERIFY(LanguageManager::nativeNameFor("ar").contains(
        QString::fromUtf8("\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9")));
}

void TestLanguage::directions()
{
    QCOMPARE(LanguageManager::directionFor("en"), Qt::LeftToRight);
    QCOMPARE(LanguageManager::directionFor("fr"), Qt::LeftToRight);
    QCOMPARE(LanguageManager::directionFor("ar"), Qt::RightToLeft);
    QCOMPARE(LanguageManager::directionFor("xx"), Qt::LeftToRight); // unknown -> LTR
}

void TestLanguage::englishNeedsNoQm()
{
    AppConfig config(m_dir.filePath("l1.ini"));
    LanguageManager lm(config);
    lm.initialize();
    // English is the source language; "loading" it must succeed without .qm.
    QCOMPARE(lm.currentLanguage(), QStringLiteral("en"));
    lm.setLanguage("en"); // no-op: same language
    QCOMPARE(lm.currentLanguage(), QStringLiteral("en"));
}

void TestLanguage::qmFilesExist()
{
    QVERIFY2(!findQm(QStringLiteral("nor-browser_ar.qm")).isEmpty(),
             "nor-browser_ar.qm not found in build tree");
    QVERIFY2(!findQm(QStringLiteral("nor-browser_fr.qm")).isEmpty(),
             "nor-browser_fr.qm not found in build tree");
}

void TestLanguage::arabicTranslationLoads()
{
    const QString qm = findQm(QStringLiteral("nor-browser_ar.qm"));
    QVERIFY2(!qm.isEmpty(), "ar .qm missing");
    QTranslator t;
    QVERIFY2(t.load(qm), "failed to load ar translator");
    QVERIFY(QCoreApplication::installTranslator(&t));
    const QString translated = QApplication::translate("TabWidget", "New Tab");
    QCOMPARE(translated, QStringLiteral("تبويب جديد"));
    QCoreApplication::removeTranslator(&t);
}

void TestLanguage::frenchTranslationLoads()
{
    const QString qm = findQm(QStringLiteral("nor-browser_fr.qm"));
    QVERIFY2(!qm.isEmpty(), "fr .qm missing");
    QTranslator t;
    QVERIFY2(t.load(qm), "failed to load fr translator");
    QVERIFY(QCoreApplication::installTranslator(&t));
    QCOMPARE(QApplication::translate("TabWidget", "New Tab"), QStringLiteral("Nouvel onglet"));
    QCoreApplication::removeTranslator(&t);
}

void TestLanguage::unknownFallsBackToEnglish()
{
    QTranslator t;
    const QString qm = findQm(QStringLiteral("nor-browser_ar.qm"));
    QVERIFY(!qm.isEmpty());
    QVERIFY(t.load(qm));
    QCoreApplication::installTranslator(&t);
    // A string missing from the .tm returns the English source.
    QCOMPARE(QApplication::translate("NoSuchContext", "Untranslated"),
             QStringLiteral("Untranslated"));
    QCoreApplication::removeTranslator(&t);
}

void TestLanguage::switchingUpdatesConfig()
{
    AppConfig config(m_dir.filePath("l2.ini"));
    LanguageManager lm(config);
    lm.initialize();
    lm.setLanguage("ar");
    QCOMPARE(config.language(), QStringLiteral("ar"));
    lm.setLanguage("fr");
    QCOMPARE(config.language(), QStringLiteral("fr"));
    // Unknown codes are rejected and keep the current language.
    lm.setLanguage("zz");
    QCOMPARE(config.language(), QStringLiteral("fr"));
}

void TestLanguage::runtimeSwitchSwapsTranslators()
{
    AppConfig config(m_dir.filePath("l3.ini"));
    LanguageManager lm(config);
    lm.initialize();
    QSignalSpy spy(&lm, &LanguageManager::languageChanged);
    lm.setLanguage("ar");
    QCOMPARE(spy.size(), 1);
    QCOMPARE(qvariant_cast<QString>(spy.at(0).at(0)), QStringLiteral("ar"));
    lm.setLanguage("en");
    QCOMPARE(spy.size(), 2);
    // Setting the same language again emits nothing.
    lm.setLanguage("en");
    QCOMPARE(spy.size(), 2);
}

void TestLanguage::arabicAppliesRtlLayout()
{
    AppConfig config(m_dir.filePath("l4.ini"));
    LanguageManager lm(config);
    lm.initialize();
    lm.setLanguage("ar");
    QCOMPARE(QGuiApplication::layoutDirection(), Qt::RightToLeft);
    lm.setLanguage("fr");
    QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
}

QTEST_MAIN(TestLanguage)
#include "tst_language.moc"
