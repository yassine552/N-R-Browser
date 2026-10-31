# Adding a Language (docs/ADD_LANGUAGE.md)

The UI architecture makes a new language a data-only change. To add,
for example, German (`de`), Spanish (`es`), or Italian (`it`):

## 1. Register the language

Edit `src/core/LanguageManager.cpp`, function `availableLanguages()`,
and add one row:

```cpp
{ QStringLiteral("de"), QStringLiteral("German"),
  QStringLiteral("Deutsch"), Qt::LeftToRight },
```

- `code` — ISO 639-1 code (also the .ts/.qm file suffix).
- `englishName` — used for internal listings.
- `nativeName` — shown in the Settings language picker.
- `direction` — `Qt::RightToLeft` for RTL languages (Arabic, Hebrew,
  Persian, Urdu…), otherwise `Qt::LeftToRight`.

Registering the language automatically enables RTL layout, locale-aware
date/number formatting (`LanguageManager::locale()`), and the picker
entry.

## 2. Create the translation file

```bash
# regenerate .ts sources (all languages) from the code
/path/to/Qt/6.8.x/gcc_64/bin/lupdate src/ -recursive \
    -ts translations/nor-browser_en.ts \
       translations/nor-browser_de.ts \
       translations/nor-browser_ar.ts \
       translations/nor-browser_fr.ts -no-obsolete
```

Add `translations/nor-browser_de.ts` to `TS_FILES` in `CMakeLists.txt`.

## 3. Fill in translations

Translate the `<source>` strings inside the generated `.ts` — either
in **Qt Linguist** (GUI, recommended) or by script (see
`scripts/fill_translations.py`, which is how the Arabic and French
catalogs were produced from dictionaries).

Rules that keep runtime behavior correct:

- Preserve `%1`/`%2` placeholders and HTML tags (`<b>`, `<br/>`).
- Keep `&` mnemonics where present (e.g. `&File` → `&Datei`).
- Multi-line sources must keep their line breaks.
- **Untranslated strings fall back to English automatically** — a
  partial translation never breaks the UI.

## 4. Build and verify

```bash
cmake --build build        # lrelease compiles .ts -> .qm, embedded via qrc
ctest --test-dir build --output-on-failure   # tst_language validates loading
tests/scripts/run_smoke_tests.sh build       # full UI switch + RTL check
```

`tst_language` verifies that your `.qm` loads and that missing strings
fall back to English; the `--smoke-language` mode switches the running
application between every registered language and screenshots the
new-tab page for visual verification.

## Notes

- Qt's own strings (standard buttons, context menus) load from Qt's
  `qtbase_<lang>.qm` when present next to the executable or in the Qt
  installation; deploying them is covered in the platform build guides.
- The Arabic UI works even on systems without Arabic fonts because Noto
  Sans Arabic and Noto Naskh Arabic are embedded in the executable
  (`resources/fonts/`) and registered at startup.
