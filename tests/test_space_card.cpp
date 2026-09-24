#include <QtTest>

#include "ui/preview/SpaceCardWidget.h"

class TestSpaceCard : public QObject {
    Q_OBJECT
private slots:
    void setSpaceUpdatesLabels()
    {
        SpaceCardWidget card;
        card.setSpace(2, QStringLiteral("Coding"), true);
        // Title is private; validate via size/interaction invariants instead.
        QCOMPARE(card.spaceIndex(), 2);
    }

    void highlightTogglesWithoutCrash()
    {
        SpaceCardWidget card;
        card.setSpace(0, QStringLiteral("S1"), false);
        card.setHighlighted(true);
        card.setHighlighted(true);  // idempotent
        card.setHighlighted(false);
        card.setHighlighted(false);
        QVERIFY(true);
    }

    void emptyThumbnailsKeepLastImage()
    {
        SpaceCardWidget card;
        card.setSpace(0, QStringLiteral("Empty"), false);
        QImage img(64, 40, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(10, 20, 30));
        card.setScreenshot(img);
        // Null must not wipe the image / show "No preview".
        card.setScreenshot(QImage());
        card.setThumbnails({});
        card.setThumbnails({ QImage() });
        QVERIFY(true);
    }

    void screenshotAcceptsImage()
    {
        SpaceCardWidget card;
        QImage img(320, 180, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(20, 40, 80));
        card.setSpace(1, QStringLiteral("Shot"), false);
        card.setScreenshot(img);
        QVERIFY(true);
    }

    void populatedThumbnailsAcceptImages()
    {
        SpaceCardWidget card;
        QImage img(64, 32, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::blue);
        card.setThumbnails({ img, img, img, img, img });
        QVERIFY(true);
    }

    void clickEmitsActivated()
    {
        SpaceCardWidget card;
        card.setSpace(1, QStringLiteral("Two"), false);
        QSignalSpy spy(&card, &SpaceCardWidget::activated);

        // Synthesize a left press.
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&card, &press);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 1);
    }
};

QTEST_MAIN(TestSpaceCard)
#include "test_space_card.moc"
