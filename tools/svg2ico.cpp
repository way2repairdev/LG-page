#include <QGuiApplication>
#include <QImage>
#include <QIcon>
#include <QPainter>
#include <QSvgRenderer>
#include <QFileInfo>
#include <QBuffer>
#include <vector>

static QImage renderSvg(const QString &svgPath, const QSize &size)
{
    QSvgRenderer renderer(svgPath);
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    renderer.render(&p);
    return img;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        qWarning("Usage: svg2ico <input.svg> <output.ico> [maxSize]");
        return 1;
    }
    QString in = QString::fromLocal8Bit(argv[1]);
    QString out = QString::fromLocal8Bit(argv[2]);
    int maxSize = (argc >= 4) ? QByteArray(argv[3]).toInt() : 256;

    std::vector<int> sizes = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    // Filter sizes by maxSize
    std::vector<int> filtered;
    for (int s : sizes) if (s <= maxSize) filtered.push_back(s);
    if (filtered.empty()) filtered.push_back(maxSize);

    QIcon icon;
    for (int s : filtered) {
        QImage img = renderSvg(in, QSize(s, s));
        icon.addPixmap(QPixmap::fromImage(img));
    }

    // Save ICO
    QFile file(out);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("Failed to open output file: %s", qPrintable(out));
        return 2;
    }
    bool ok = icon.pixmap(maxSize, maxSize).toImage().save(&file, "ICO");
    file.close();
    if (!ok) {
        qWarning("Failed to save ICO: %s", qPrintable(out));
        return 3;
    }
    return 0;
}
