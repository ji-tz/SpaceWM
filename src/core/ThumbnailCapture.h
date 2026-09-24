#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QVector>
#include <Windows.h>

// Captures a static thumbnail of a window for overview cards.
// Uses PrintWindow(PW_RENDERFULLCONTENT) with a GDI DIB — no DirectComposition
// dependency, fast enough for opening the overview.
namespace thumbs {

// Returns null image on failure.
QImage capture(HWND hwnd, const QSize &maxSize = QSize(480, 270));

} // namespace thumbs
