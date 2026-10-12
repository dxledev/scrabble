#pragma once

#include "theme.h"

namespace scrabble {

Theme importPalette(const QString &path, const QString &name = {});
Theme bindSource(const Theme &current, const QString &kind, const QString &path = {});
Theme refreshSource(const Theme &theme);
QStringList sourceWatchPaths(const Theme &theme);

}
