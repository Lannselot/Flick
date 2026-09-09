// SPDX-License-Identifier: GPL-3.0-or-later

#include "image_loading.h"

#include <QColorSpace>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class ImageLoadingTest final : public QObject
{
    Q_OBJECT

private slots:
    void decodesStaticImage();
    void preservesAnimatedMetadata();
    void appliesOrientationAndDefaultsUntaggedFramesToSrgb();
    void reportsMalformedInput();
    void requestsConfirmationBeforeExceptionalDecode();

private:
    QString writeFixture(const QString &encodedName, const QString &imageName);

    QTemporaryDir fixtures_;
};

QString ImageLoadingTest::writeFixture(const QString &encodedName, const QString &imageName)
{
    QFile encoded(QStringLiteral(FLICK_FIXTURE_DIR "/") + encodedName);
    if (!encoded.open(QIODevice::ReadOnly)) {
        return {};
    }
    QFile image(fixtures_.filePath(imageName));
    const QByteArray contents = QByteArray::fromBase64(encoded.readAll());
    if (!image.open(QIODevice::WriteOnly) || image.write(contents) != contents.size()) {
        return {};
    }
    return image.fileName();
}

void ImageLoadingTest::decodesStaticImage()
{
    QVERIFY(fixtures_.isValid());
    const QString path = writeFixture(QStringLiteral("static.png.base64"),
                                      QStringLiteral("static.png"));
    QVERIFY(!path.isEmpty());

    const ImageLoading::DecodeOutcome outcome = ImageLoading::decode({path});

    QVERIFY(std::holds_alternative<ImageLoading::LoadedImage>(outcome));
    const auto &loaded = std::get<ImageLoading::LoadedImage>(outcome);
    QCOMPARE(loaded.path, path);
    QCOMPARE(loaded.frames.size(), 1);
    QCOMPARE(loaded.frames.first().size(), QSize(8, 6));
    QCOMPARE(loaded.frames.first().pixelColor(4, 3), QColor(220, 20, 60));
    QCOMPARE(loaded.frameDelays, QList<int>{100});
}

void ImageLoadingTest::preservesAnimatedMetadata()
{
    const QString gifPath = writeFixture(QStringLiteral("animated.gif.base64"),
                                         QStringLiteral("animated.gif"));
    const QString webpPath = writeFixture(QStringLiteral("animated.webp.base64"),
                                          QStringLiteral("animated.webp"));

    const auto gif = std::get<ImageLoading::LoadedImage>(ImageLoading::decode({gifPath}));
    QCOMPARE(gif.frames.size(), 2);
    QCOMPARE(gif.frames.at(0).pixelColor(4, 3), QColor(220, 20, 60));
    QCOMPARE(gif.frames.at(1).pixelColor(4, 3), QColor(50, 205, 50));
    QCOMPARE(gif.frameDelays, QList<int>({120, 280}));
    QCOMPARE(gif.loopCount, 0);

    const auto webp = std::get<ImageLoading::LoadedImage>(ImageLoading::decode({webpPath}));
    QCOMPARE(webp.frames.size(), 2);
    const QColor webpFirst = webp.frames.at(0).pixelColor(4, 3);
    const QColor webpSecond = webp.frames.at(1).pixelColor(4, 3);
    QVERIFY(qAbs(webpFirst.red() - 220) <= 5);
    QVERIFY(qAbs(webpFirst.green() - 20) <= 5);
    QVERIFY(qAbs(webpFirst.blue() - 60) <= 5);
    QVERIFY(qAbs(webpSecond.red() - 50) <= 5);
    QVERIFY(qAbs(webpSecond.green() - 205) <= 5);
    QVERIFY(qAbs(webpSecond.blue() - 50) <= 5);
    QCOMPARE(webp.frameDelays, QList<int>({120, 280}));
    QCOMPARE(webp.loopCount, -1);
}

void ImageLoadingTest::appliesOrientationAndDefaultsUntaggedFramesToSrgb()
{
    const QString orientedPath = writeFixture(QStringLiteral("oriented.jpg.base64"),
                                              QStringLiteral("oriented.jpg"));
    const auto oriented =
        std::get<ImageLoading::LoadedImage>(ImageLoading::decode({orientedPath}));
    QCOMPARE(oriented.frames.first().size(), QSize(6, 12));

    const QString untaggedPath = writeFixture(QStringLiteral("static.png.base64"),
                                              QStringLiteral("untagged.png"));
    const auto untagged =
        std::get<ImageLoading::LoadedImage>(ImageLoading::decode({untaggedPath}));
    QCOMPARE(untagged.frames.first().colorSpace(), QColorSpace(QColorSpace::SRgb));
}

void ImageLoadingTest::reportsMalformedInput()
{
    const QString path = fixtures_.filePath(QStringLiteral("broken.png"));
    QFile malformed(path);
    QVERIFY(malformed.open(QIODevice::WriteOnly));
    QCOMPARE(malformed.write("not an image"), 12);
    malformed.close();

    const ImageLoading::DecodeOutcome outcome = ImageLoading::decode({path});

    QVERIFY(std::holds_alternative<ImageLoading::DecodeFailure>(outcome));
    const auto &failure = std::get<ImageLoading::DecodeFailure>(outcome);
    QCOMPARE(failure.path, path);
    QVERIFY(!failure.details.isEmpty());
}

void ImageLoadingTest::requestsConfirmationBeforeExceptionalDecode()
{
    const QString path = writeFixture(QStringLiteral("static.png.base64"),
                                      QStringLiteral("exceptional.png"));

    const ImageLoading::DecodeOutcome outcome = ImageLoading::decode({path, false, 100});

    QVERIFY(std::holds_alternative<ImageLoading::ConfirmationRequired>(outcome));
    const auto &confirmation = std::get<ImageLoading::ConfirmationRequired>(outcome);
    QCOMPARE(confirmation.path, path);
    QCOMPARE(confirmation.declaredSize, QSize(8, 6));
    QCOMPARE(confirmation.estimatedAllocationBytes, 192);
}

QTEST_GUILESS_MAIN(ImageLoadingTest)

#include "image_loading_test.moc"
