#pragma once

#include <QLocale>
#include <QObject>
#include <QString>
#include <QVector>

class AppConfig;
class QTranslator;

// Owns the QTranslator instances and the UI language state.
//
// English ("en") is the default and the fallback when a translation is
// missing. Arabic ("ar") switches the whole application layout to
// right-to-left. Adding a language means adding a .ts file, filling it,
// and registering the metadata below (see docs/ADD_LANGUAGE.md).
class LanguageManager : public QObject
{
    Q_OBJECT
public:
    struct LanguageInfo
    {
        QString code;          // ISO 639-1
        QString englishName;
        QString nativeName;    // shown in the language picker
        Qt::LayoutDirection direction;
    };

    explicit LanguageManager(AppConfig &config, QObject *parent = nullptr);

    static const QVector<LanguageInfo> &availableLanguages();
    static bool isKnownLanguage(const QString &code);
    static QString nativeNameFor(const QString &code);
    static Qt::LayoutDirection directionFor(const QString &code);

    void initialize();                 // applies saved (or default) language
    QString currentLanguage() const { return m_current; }
    Qt::LayoutDirection layoutDirection() const;
    QLocale locale() const;

public slots:
    void setLanguage(const QString &code);   // installs translators, persists

signals:
    void languageChanged(const QString &code);

private:
    bool loadTranslators(const QString &code);
    void applyLayoutDirection();
    void clearTranslators();

    AppConfig &m_config;
    QString m_current = QStringLiteral("en");
    QVector<QTranslator *> m_translators;   // app translator + qt translator
};
