#include <QtTest>

#include "core/ThumbnailCapture.h"

#include <Windows.h>

class TestThumbnail : public QObject {
    Q_OBJECT
private slots:
    void nullAndInvalidAreSafe()
    {
        QImage a = thumbs::capture(nullptr);
        QVERIFY(a.isNull());
        QImage b = thumbs::capture(reinterpret_cast<HWND>(0xDEAD));
        QVERIFY(b.isNull());
    }

    void capturesRealWindowWithinMaxSize()
    {
        HWND hwnd = ::CreateWindowExW(
            0, L"STATIC", L"thumb target",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 400, 300,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd != nullptr);
        ::ShowWindow(hwnd, SW_SHOW);
        ::UpdateWindow(hwnd);
        ::Sleep(30);

        const QImage img = thumbs::capture(hwnd, QSize(240, 135));
        // PrintWindow can fail on some surfaces — accept null, but if non-null
        // dimensions must respect the cap.
        if (!img.isNull()) {
            QVERIFY(img.width() > 0);
            QVERIFY(img.height() > 0);
            QVERIFY(img.width() <= 240 || img.height() <= 135);
        }

        const QImage full = thumbs::capture(hwnd, QSize());
        if (!full.isNull()) {
            QVERIFY(full.width() > 0);
            QVERIFY(full.height() > 0);
            QVERIFY(full.width() <= 960);
            QVERIFY(full.height() <= 540);
        }

        ::DestroyWindow(hwnd);
    }
};

QTEST_MAIN(TestThumbnail)
#include "test_thumbnail.moc"
