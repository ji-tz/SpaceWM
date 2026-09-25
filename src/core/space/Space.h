#pragma once

#include <QImage>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVector>
#include <Windows.h>

// Per-monitor independent spaces.
//
// State model (kept entirely in this process — Windows has no native API for it):
//   monitor -> currentSpaceIndex
//   monitor -> spaces[i] -> set of HWND + last screenshot
//
// Display model: windows not in the current space of their monitor are cloaked.
// Switching a space on monitor M only cloaks/uncloaks windows on M.
struct Space {
    QString name;
    QSet<HWND> windows;
    // Top → bottom HWND order captured when this space was last visible.
    QVector<HWND> zOrder;
    // Last rendered preview: wallpaper full-bleed + windows in Z-order (not a BitBlt).
    QImage screenshot;
    // Exclusive space (issue #1): only ever holds its single bound window.
    // Opt-in via overview right-click; only honored when the global switch is on.
    bool exclusive = false;
};

struct MonitorSpaces {
    HMONITOR hmon = nullptr;
    RECT physRect{};
    QRect geometry;       // Qt logical
    QString deviceName;
    int currentIndex = 0;
    QVector<Space> spaces;
};
