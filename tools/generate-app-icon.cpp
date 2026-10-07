// Build from the repository root (MSYS2 UCRT64 Qt6Gui development environment):
// g++ -std=c++17 -O2 tools/generate-app-icon.cpp $(pkg-config --cflags --libs Qt6Gui)
//     -lgdi32 -o build-release-bin/generate-app-icon.exe
// Run: generate-app-icon.exe resources
// This generator is the editable vector source for SVG, PNG and all ICO sizes.
// It uses paths instead of a system font, so the P remains identical everywhere.
#include <QGuiApplication>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QVector>
#include <QXmlStreamReader>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const QString arm = QStringLiteral(
    "M 94 438 C 39 384 28 308 43 234 C 64 126 160 60 267 63 "
    "C 339 65 403 101 443 159 C 371 121 294 117 238 145 "
    "C 175 177 145 238 158 299 C 171 354 213 390 271 392 "
    "C 213 438 143 462 94 438 Z");
const QString armGleam = QStringLiteral(
    "M 69 243 C 91 140 177 86 268 88 C 325 89 373 110 409 137");
const QString armRefraction = QStringLiteral(
    "M 109 420 C 161 435 213 418 252 399");
const QString letter = QStringLiteral(
    "M 189 158 L 263 158 C 313 158 343 186 343 230 "
    "C 343 274 312 300 264 300 L 238 300 L 238 360 L 189 360 Z "
    "M 238 204 L 238 255 L 259 255 C 281 255 294 246 294 230 "
    "C 294 214 281 204 259 204 Z");
const QString letterGleam = QStringLiteral(
    "M 195 179 L 195 165 L 260 165 C 286 165 306 173 319 188");

struct Stop { qreal at; QColor color; };
struct Gradient {
    QString name;
    QPointF from;
    QPointF to;
    QVector<Stop> stops;
    QLinearGradient paint() const
    {
        QLinearGradient gradient(from, to);
        for (const Stop &stop : stops)
            gradient.setColorAt(stop.at, stop.color);
        return gradient;
    }
    QString svg() const
    {
        QString result;
        QTextStream output(&result);
        output << "    <linearGradient id=\"" << name << "\" gradientUnits=\"userSpaceOnUse\" x1=\""
               << from.x() << "\" y1=\"" << from.y() << "\" x2=\"" << to.x()
               << "\" y2=\"" << to.y() << "\">\n";
        for (const Stop &stop : stops)
            output << "      <stop offset=\"" << stop.at << "\" stop-color=\"" << stop.color.name()
                   << "\" stop-opacity=\"" << QString::number(stop.color.alphaF(), 'f', 3) << "\"/>\n";
        output << "    </linearGradient>\n";
        return result;
    }
};

const Gradient armGlass{
    "arm-glass", {68, 64}, {400, 450},
    {{0, QColor(168, 249, 252, 238)}, {.26, QColor(75, 217, 236, 240)},
     {.62, QColor(30, 151, 195, 229)}, {1, QColor(20, 81, 114, 237)}}};
const Gradient rim{
    "rim", {70, 55}, {415, 405},
    {{0, QColor(245, 255, 255, 195)}, {.5, QColor(148, 234, 255, 70)},
     {1, QColor(138, 224, 245, 120)}}};
const Gradient gleam{
    "gleam", {74, 215}, {413, 133},
    {{0, QColor(245, 255, 255, 35)}, {.28, QColor(246, 255, 255, 173)},
     {1, QColor(246, 255, 255, 0)}}};
const Gradient eyeGlass{
    "eye-glass", {210, 132}, {282, 390},
    {{0, QColor(37, 79, 95, 210)}, {.44, QColor(12, 38, 51, 230)},
     {1, QColor(5, 15, 27, 240)}}};
const Gradient eyeRim{
    "eye-rim", {205, 130}, {292, 388},
    {{0, QColor(217, 251, 255, 168)}, {.46, QColor(171, 244, 255, 35)},
     {1, QColor(70, 166, 199, 94)}}};
const Gradient pearl{
    "pearl", {209, 160}, {303, 363},
    {{0, QColor(254, 255, 255)}, {.47, QColor(232, 251, 255)},
     {1, QColor(176, 224, 241)}}};

QPainterPath path(const QString &source)
{
    // A deliberately small, validated parser for the explicit commands above.
    const QRegularExpression expression(QStringLiteral("[MLCQZ]|-?[0-9]+(?:\\.[0-9]+)?"));
    QStringList tokens;
    auto iterator = expression.globalMatch(source);
    while (iterator.hasNext())
        tokens.append(iterator.next().captured());
    QPainterPath result;
    result.setFillRule(Qt::OddEvenFill);
    int position = 0;
    const auto number = [&]() { return tokens.at(position++).toDouble(); };
    const auto point = [&]() { const qreal x = number(); const qreal y = number(); return QPointF(x, y); };
    while (position < tokens.size()) {
        const QString command = tokens.at(position++);
        if (command == "M") result.moveTo(point());
        else if (command == "L") result.lineTo(point());
        else if (command == "C") {
            const QPointF first = point(), second = point(), end = point();
            result.cubicTo(first, second, end);
        } else if (command == "Q") {
            const QPointF control = point(), end = point();
            result.quadTo(control, end);
        } else if (command == "Z") result.closeSubpath();
        else qFatal("Unsupported vector path command.");
    }
    return result;
}

void paintLogo(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPainterPath spiral = path(arm);
    for (int side = 0; side < 2; ++side) {
        painter.save();
        if (side) {
            painter.translate(256, 256);
            painter.rotate(180);
            painter.translate(-256, -256);
        }
        painter.setBrush(armGlass.paint());
        painter.setPen(QPen(QBrush(rim.paint()), 3.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(spiral);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QBrush(gleam.paint()), 4.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(path(armGleam));
        painter.setPen(QPen(QColor(198, 249, 255, 80), 2.3, Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(path(armRefraction));
        painter.restore();
    }
    // A dark transparent eye keeps the pearl P legible on light or dark desktops.
    painter.setPen(QPen(QColor(0, 8, 18, 50), 9));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(133, 133, 246, 246));
    painter.setPen(QPen(QBrush(eyeRim.paint()), 3.4));
    painter.setBrush(eyeGlass.paint());
    painter.drawEllipse(QRectF(134, 134, 244, 244));
    const QPainterPath p = path(letter);
    painter.save();
    painter.translate(0, 3);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 5, 14, 92));
    painter.drawPath(p);
    painter.restore();
    painter.setPen(QPen(QColor(242, 255, 255, 130), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(pearl.paint());
    painter.drawPath(p);
    painter.setPen(QPen(QColor(255, 255, 255, 164), 2.3, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path(letterGleam));
}

QImage frame(int size)
{
    constexpr int supersampling = 4;
    QImage image(size * supersampling, size * supersampling, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.scale(image.width() / 512.0, image.height() / 512.0);
        paintLogo(painter);
    }
    return image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QString svgSource()
{
    QString source;
    QTextStream output(&source);
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<!-- Generated from tools/generate-app-icon.cpp. PHS Radio original artwork. -->\n"
           << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"512\" height=\"512\" viewBox=\"0 0 512 512\" role=\"img\" aria-labelledby=\"title desc\">\n"
           << "  <title id=\"title\">PHS Radio</title>\n"
           << "  <desc id=\"desc\">A pearl P inside two cyan glass typhoon arms on a transparent background.</desc>\n"
           << "  <defs>\n";
    for (const Gradient &gradient : {armGlass, rim, gleam, eyeGlass, eyeRim, pearl})
        output << gradient.svg();
    output << "    <g id=\"spiral-arm\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
           << "      <path d=\"" << arm << "\" fill=\"url(#arm-glass)\" stroke=\"url(#rim)\" stroke-width=\"3.3\"/>\n"
           << "      <path d=\"" << armGleam << "\" fill=\"none\" stroke=\"url(#gleam)\" stroke-width=\"4.5\"/>\n"
           << "      <path d=\"" << armRefraction << "\" fill=\"none\" stroke=\"#c6f9ff\" stroke-opacity=\"0.314\" stroke-width=\"2.3\"/>\n"
           << "    </g>\n"
           << "  </defs>\n"
           << "  <use href=\"#spiral-arm\"/>\n"
           << "  <use href=\"#spiral-arm\" transform=\"rotate(180 256 256)\"/>\n"
           << "  <circle cx=\"256\" cy=\"256\" r=\"123\" fill=\"none\" stroke=\"#000812\" stroke-opacity=\"0.196\" stroke-width=\"9\"/>\n"
           << "  <circle cx=\"256\" cy=\"256\" r=\"122\" fill=\"url(#eye-glass)\" stroke=\"url(#eye-rim)\" stroke-width=\"3.4\"/>\n"
           << "  <path d=\"" << letter << "\" transform=\"translate(0 3)\" fill=\"#00050e\" fill-opacity=\"0.361\" fill-rule=\"evenodd\"/>\n"
           << "  <path d=\"" << letter << "\" fill=\"url(#pearl)\" fill-rule=\"evenodd\" stroke=\"#f2ffff\" stroke-opacity=\"0.510\" stroke-width=\"1.2\" stroke-linejoin=\"round\"/>\n"
           << "  <path d=\"" << letterGleam << "\" fill=\"none\" stroke=\"white\" stroke-opacity=\"0.643\" stroke-width=\"2.3\" stroke-linecap=\"round\"/>\n"
           << "</svg>\n";
    return source;
}

void require(bool condition, const char *message)
{
    if (!condition) qFatal("%s", message);
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot open artwork output.");
    require(file.write(bytes) == bytes.size(), "Cannot save complete artwork output.");
}

void verifyIcon(const QString &filename, const QVector<int> &sizes)
{
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly), "Cannot reopen Windows icon.");
    const QByteArray contents = file.readAll();
    QDataStream directory(contents);
    directory.setByteOrder(QDataStream::LittleEndian);
    quint16 reserved, type, count;
    directory >> reserved >> type >> count;
    require(reserved == 0 && type == 1 && count == sizes.size(), "ICO directory header is invalid.");
    quint32 expectedOffset = 6 + count * 16;
    for (int size : sizes) {
        quint8 width, height, colors, unused;
        quint16 planes, bits;
        quint32 length, offset;
        directory >> width >> height >> colors >> unused >> planes >> bits >> length >> offset;
        require((width ? width : 256) == size && (height ? height : 256) == size
                    && colors == 0 && unused == 0 && planes == 1 && bits == 32 && offset == expectedOffset,
                "ICO entry size, bit depth or frame offset is invalid.");
        const QImage image = QImage::fromData(contents.mid(offset, length), "PNG");
        require(!image.isNull() && image.size() == QSize(size, size) && image.hasAlphaChannel()
                    && image.pixelColor(0, 0).alpha() == 0,
                "ICO frame must decode at its advertised size with transparency.");
        expectedOffset += length;
#ifdef Q_OS_WIN
        const QString nativeFilename = QDir::toNativeSeparators(QFileInfo(filename).absoluteFilePath());
        HICON nativeIcon = static_cast<HICON>(LoadImageW(nullptr,
            reinterpret_cast<LPCWSTR>(nativeFilename.utf16()), IMAGE_ICON, size, size, LR_LOADFROMFILE));
        require(nativeIcon != nullptr, "Windows cannot load an ICO frame.");
        ICONINFO info{};
        require(GetIconInfo(nativeIcon, &info), "Windows cannot read the icon bitmap.");
        BITMAP bitmap{};
        require(info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap)
                    && bitmap.bmWidth == size && bitmap.bmHeight == size,
                "Windows loaded an icon at the wrong size.");
        if (info.hbmColor) DeleteObject(info.hbmColor);
        if (info.hbmMask) DeleteObject(info.hbmMask);
        DestroyIcon(nativeIcon);
#endif
    }
    require(directory.status() == QDataStream::Ok && expectedOffset == contents.size(),
            "ICO frames must exactly occupy the declared file.");
}
} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QGuiApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const QString outputRoot = arguments.size() > 1 ? arguments.at(1) : QStringLiteral("resources");
    require(QDir().mkpath(outputRoot), "Cannot create artwork output directory.");
    const QDir output(outputRoot);
    const QString svg = svgSource();
    QXmlStreamReader xml(svg);
    while (!xml.atEnd()) xml.readNext();
    require(!xml.hasError(), "Generated SVG must be valid XML.");
    writeFile(output.filePath(QStringLiteral("app_logo.svg")), svg.toUtf8());
    require(frame(512).save(output.filePath(QStringLiteral("app-logo-preview.png")), "PNG"), "Cannot save logo preview.");

    const QVector<int> sizes{16, 20, 24, 32, 40, 48, 64, 128, 256};
    QVector<QByteArray> pngs;
    for (int size : sizes) {
        const QImage image = frame(size);
        require(image.pixelColor(0, 0).alpha() == 0 && image.pixelColor(size - 1, size - 1).alpha() == 0,
                "The icon's corner background must remain transparent.");
        QByteArray png;
        QBuffer buffer(&png);
        require(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"), "Cannot encode ICO frame.");
        pngs.append(png);
    }
    QByteArray icon;
    QDataStream bytes(&icon, QIODevice::WriteOnly);
    bytes.setByteOrder(QDataStream::LittleEndian);
    bytes << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + sizes.size() * 16;
    for (int index = 0; index < sizes.size(); ++index) {
        const int size = sizes.at(index);
        bytes << quint8(size == 256 ? 0 : size) << quint8(size == 256 ? 0 : size)
              << quint8(0) << quint8(0) << quint16(1) << quint16(32)
              << quint32(pngs.at(index).size()) << offset;
        offset += pngs.at(index).size();
    }
    for (const QByteArray &png : pngs)
        require(bytes.writeRawData(png.constData(), png.size()) == png.size(), "Cannot encode complete ICO frame.");
    require(bytes.status() == QDataStream::Ok, "Cannot serialize Windows icon directory.");
    writeFile(output.filePath(QStringLiteral("app.ico")), icon);
    verifyIcon(output.filePath(QStringLiteral("app.ico")), sizes);

    // Optional contact sheet is a review artifact, not a runtime asset.
    if (arguments.size() > 2) {
        QImage sheet(760, 292, QImage::Format_ARGB32_Premultiplied);
        sheet.fill(QColor(11, 17, 25));
        QPainter painter(&sheet);
        painter.drawImage(QRect(18, 18, 256, 256), frame(256));
        painter.fillRect(QRect(294, 18, 196, 256), QColor(238, 243, 247));
        painter.drawImage(QRect(306, 62, 172, 172), frame(172));
        int y = 32;
        for (int size : {16, 20, 24, 32, 40, 48}) {
            painter.drawImage(QPoint(514, y), frame(size));
            painter.setPen(QColor(224, 239, 246));
            painter.drawText(QRect(574, y, 164, size), Qt::AlignVCenter, QStringLiteral("%1 × %1 px").arg(size));
            y += size + 11;
        }
        painter.end();
        require(sheet.save(arguments.at(2), "PNG"), "Cannot save contact sheet.");
    }
    qInfo("Generated original PHS Radio SVG, transparent 512px preview and 9-size PNG-compressed ICO.");
    return 0;
}
