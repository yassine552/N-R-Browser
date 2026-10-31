#pragma once

#include <QIcon>
#include <QString>

// Loads and caches icons from the embedded resources (:/icons/<name>.svg).
namespace IconProvider
{
QIcon icon(const QString &name);
QIcon appIcon();
void clearCache();
}
