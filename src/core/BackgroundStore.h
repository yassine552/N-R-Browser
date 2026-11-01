// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 The NØR Browser authors
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QImage;

// ---------------------------------------------------------------------------
// New Tab background catalog + user-import pipeline.
//
// Two sources, one interface:
//   * Bundled  — 8 procedurally generated JPEGs in the qrc
//                (resources/backgrounds, CC0 — see the README there).
//   * Custom   — images the user imports; downscaled to <=1920px, re-encoded
//                as JPEG and stored under Paths::backgroundsDir() with a
//                manifest recording the light/dark flag (decided from the
//                average luminance so page text keeps AA contrast).
//
// Everything is local: no downloads, no network, no tracking. The nor://
// scheme handler serves bytes straight from here (see NewTabSchemeHandler).
// ---------------------------------------------------------------------------
class BackgroundStore : public QObject
{
    Q_OBJECT
public:
    struct Entry
    {
        QString id;      // "nor-*" (bundled) | "custom-*" (imported)
        QString name;    // display name (already translated for bundled ids)
        bool dark = true; // light text (true) or dark text (false) over it
        bool bundled = true;
    };

    explicit BackgroundStore(const QString &customDir, QObject *parent = nullptr);

    // Catalog: bundled entries first, then the user's imports.
    QVector<Entry> entries() const;
    bool contains(const QString &id) const;
    Entry entry(const QString &id) const;   // invalid id -> bundled=false Entry

    // Full-size and 240x135 thumbnail JPEG bytes (empty on miss).
    QByteArray imageBytes(const QString &id) const;
    QByteArray thumbnailBytes(const QString &id) const;

    // Import pipeline: load -> orient-free decode -> cap the longest edge at
    // 1920 (cover-friendly for common displays) -> re-encode quality 78 ->
    // save -> register. Returns the new id, or an empty string with *error
    // set. Never throws away the original file.
    QString importFromFile(const QString &sourcePath, QString *error);

    bool removeCustom(const QString &id);

    // Deterministic random pick among ALL entries (used by the New Tab
    // "random background" action; excludes the currently selected id).
    QString randomId(const QString &exclude) const;

    static QStringList bundledIds();

private:
    void loadManifest();
    void saveManifest() const;
    QString manifestPath() const;
    static int averageLuminance(const QImage &image);

    QString m_customDir;
    // Custom entries are kept in a small JSON manifest next to the files:
    // [{ "id": "custom-<8hex>", "name": "<file base name>", "dark": bool }]
    QVector<Entry> m_custom;
};
