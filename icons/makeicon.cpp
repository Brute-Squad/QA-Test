// Draws the program's icon and writes it in the forms the program uses:
//
//   qatest.ico              the .exe's icon (16, 24, 32, 48, 64 and 256 pixels) -
//                           src/qatest.rc names it; the installer has it too
//   qatest-<size>.png       16, 32, 48 and 256 pixels: the window's icon
//                           (src/qatest.qrc), and the installer's window
//
// The icon is a clipboard with a checklist - two things done, one still to
// do - on a blue-to-green square with round corners. At 32 pixels and below
// the list would be mush, so the small sizes show the board with one big
// check mark instead.
//
// Not part of the program's build. To change the icon: change the drawing
// here, build this (the target MakeIcon: cmake --build build --target MakeIcon),
// run   MakeIcon <the icons folder>   and commit what it rewrites.
#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QtEndian>

namespace
{
    // The drawing, on a square of 100 x 100 that the painter scales.
    void drawCheck(QPainter &painter, const QPointF &at, qreal size, const QColor &color, qreal width)
    {
        QPainterPath check;
        check.moveTo(at.x(), at.y() + size * 0.55);
        check.lineTo(at.x() + size * 0.38, at.y() + size * 0.9);
        check.lineTo(at.x() + size, at.y() + size * 0.12);
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(check);
    }

    QImage draw(int pixels)
    {
        QImage image(pixels, pixels, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(pixels / 100.0, pixels / 100.0);
        const bool small = pixels <= 32;

        // The background.
        QLinearGradient background(0, 0, 100, 100);
        background.setColorAt(0, QColor(0x3F, 0x51, 0xB5));
        background.setColorAt(1, QColor(0x00, 0x96, 0x88));
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(QRectF(2, 2, 96, 96), 20, 20);

        // The board, and the clip that holds the paper.
        const QRectF board = small ? QRectF(18, 16, 64, 72) : QRectF(20, 17, 60, 70);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(board, 7, 7);
        painter.setBrush(QColor(0x26, 0x32, 0x38));
        painter.drawRoundedRect(small ? QRectF(34, 9, 32, 14) : QRectF(36, 10, 28, 13), 5, 5);

        const QColor done(0x2E, 0x7D, 0x32);
        if (small)
        {
            // One check mark that still reads at 16 pixels.
            drawCheck(painter, QPointF(29, 36), 42, done, 11);
            return image;
        }

        // Three lines of the list: two done, one still to do.
        const QColor line(0x90, 0xA4, 0xAE);
        for (int row = 0; row < 3; ++row)
        {
            const qreal y = 33 + row * 17;
            if (row < 2)
                drawCheck(painter, QPointF(27, y), 11, done, 3.6);
            else
            {
                painter.setPen(QPen(line, 2.4));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(QRectF(27, y + 0.5, 10, 10), 2, 2);
            }
            painter.setPen(QPen(line, 4.2, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(45, y + 6), QPointF(row == 1 ? 66 : 72, y + 6));
        }
        return image;
    }

    QByteArray png(const QImage &image)
    {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }

    // A picture as an .ico file holds it below 256 pixels: a bitmap header,
    // the pixels bottom row first as blue, green, red, alpha, and a mask of
    // one bit per pixel (all zero: the alpha says what shows).
    QByteArray bitmap(const QImage &source)
    {
        const QImage image = source.convertToFormat(QImage::Format_ARGB32);
        const int size = image.width();
        QByteArray bytes;
        const auto put32 = [&bytes](quint32 value) { char raw[4]; qToLittleEndian(value, raw); bytes.append(raw, 4); };
        const auto put16 = [&bytes](quint16 value) { char raw[2]; qToLittleEndian(value, raw); bytes.append(raw, 2); };
        put32(40);                  // the header's size
        put32(quint32(size));
        put32(quint32(size * 2));   // the picture and its mask
        put16(1);
        put16(32);
        put32(0);                   // not compressed
        put32(0);
        put32(0);
        put32(0);
        put32(0);
        put32(0);
        for (int y = size - 1; y >= 0; --y)
        {
            const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < size; ++x)
                put32(row[x]);      // little endian ARGB = blue, green, red, alpha
        }
        const int maskRow = ((size + 31) / 32) * 4;
        bytes.append(QByteArray(maskRow * size, '\0'));
        return bytes;
    }

    bool write(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    }
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 2)
    {
        out << "MakeIcon <the folder to write qatest.ico and qatest-<size>.png into>" << Qt::endl;
        return 2;
    }
    const QString folder = QString::fromLocal8Bit(argv[1]);

    // The window's icon, as pictures.
    for (const int size : { 16, 32, 48, 256 })
    {
        if (!write(QStringLiteral("%1/qatest-%2.png").arg(folder).arg(size), png(draw(size))))
        {
            out << "could not write qatest-" << size << ".png into " << folder << Qt::endl;
            return 1;
        }
    }

    // The .exe's icon: a directory of the sizes, then each picture.
    const QList<int> sizes { 16, 24, 32, 48, 64, 256 };
    QList<QByteArray> pictures;
    for (const int size : sizes)
        pictures << (size == 256 ? png(draw(size)) : bitmap(draw(size)));

    QByteArray ico;
    const auto put32 = [&ico](quint32 value) { char raw[4]; qToLittleEndian(value, raw); ico.append(raw, 4); };
    const auto put16 = [&ico](quint16 value) { char raw[2]; qToLittleEndian(value, raw); ico.append(raw, 2); };
    put16(0);
    put16(1);                       // icons
    put16(quint16(sizes.size()));
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (int i = 0; i < sizes.size(); ++i)
    {
        ico.append(char(sizes.at(i) == 256 ? 0 : sizes.at(i)));     // 0 stands for 256
        ico.append(char(sizes.at(i) == 256 ? 0 : sizes.at(i)));
        ico.append(char(0));        // no palette
        ico.append(char(0));
        put16(1);
        put16(32);
        put32(quint32(pictures.at(i).size()));
        put32(offset);
        offset += quint32(pictures.at(i).size());
    }
    for (const QByteArray &picture : std::as_const(pictures))
        ico.append(picture);
    if (!write(folder + QStringLiteral("/qatest.ico"), ico))
    {
        out << "could not write qatest.ico into " << folder << Qt::endl;
        return 1;
    }
    out << "wrote qatest.ico (" << sizes.size() << " sizes) and 4 pictures into " << folder << Qt::endl;
    return 0;
}
