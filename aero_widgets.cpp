#include "aero_widgets.h"

#include <QEasingCurve>
#include <QCache>
#include <QCursor>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QVariantAnimation>
#include <QtMath>

#include <algorithm>
#include <memory>

namespace Aero {

QColor defaultAccent()
{
    return QColor(QStringLiteral("#73d5e3"));
}

void paintGlassBase(QPainter &painter, const QRectF &bounds, const QColor &requestedAccent, qreal radius)
{
    if (bounds.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent = requestedAccent.isValid() ? requestedAccent : defaultAccent();
    radius = std::min({radius, bounds.width() / 2, bounds.height() / 2});
    painter.setPen(Qt::NoPen);
    for (int spread = 5; spread > 0; --spread) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0, 4 + (6 - spread) * 2), spread));
        const qreal outside = spread * .5;
        painter.drawRoundedRect(bounds.adjusted(-outside, -outside, outside, outside),
                                radius + outside, radius + outside);
    }
    QPainterPath silhouette;
    silhouette.addRoundedRect(bounds, radius, radius);
    QLinearGradient body(bounds.topLeft(), bounds.bottomLeft());
    body.setColorAt(0, QColor(5, 9, 13, 45));
    body.setColorAt(.48, QColor(4, 8, 12, 52));
    body.setColorAt(1, QColor(4, 7, 11, 62));
    painter.fillPath(silhouette, body);
    painter.save();
    painter.setClipPath(silhouette, Qt::IntersectClip);

    QColor tint = accent;
    tint.setAlpha(7);
    painter.fillRect(bounds, tint);
    QLinearGradient gloss(bounds.topLeft(), bounds.bottomLeft());
    gloss.setColorAt(0, QColor(255, 255, 255, 10));
    gloss.setColorAt(.44, QColor(255, 255, 255, 2));
    gloss.setColorAt(1, QColor(255, 255, 255, 0));
    painter.fillRect(bounds, gloss);

    painter.restore();

    QLinearGradient rim(bounds.topLeft(), bounds.bottomLeft());
    rim.setColorAt(0, QColor(255, 255, 255, 74));
    rim.setColorAt(.46, QColor(255, 255, 255, 12));
    QColor lowerRim = accent.lighter(115);
    lowerRim.setAlpha(31);
    rim.setColorAt(1, lowerRim);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QBrush(rim), .9));
    painter.drawPath(silhouette);
    // Short curved glints describe the edge thickness without a repeated reflection across the face.
    const qreal gleamLength = std::min(bounds.width() * .30, 104.0);
    QPainterPath cornerGleam;
    cornerGleam.moveTo(bounds.left() + 1.5, bounds.top() + radius);
    cornerGleam.quadTo(bounds.left() + 1.5, bounds.top() + 1.5,
                      bounds.left() + radius, bounds.top() + 1.5);
    cornerGleam.lineTo(bounds.left() + std::max(radius, gleamLength), bounds.top() + 1.5);
    QLinearGradient gleam(bounds.topLeft(), bounds.topLeft() + QPointF(std::max(radius, gleamLength), radius));
    gleam.setColorAt(0, QColor(255, 255, 255, 20));
    gleam.setColorAt(.30, QColor(255, 255, 255, 56));
    gleam.setColorAt(1, QColor(255, 255, 255, 0));
    painter.setPen(QPen(QBrush(gleam), .85, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(cornerGleam);
    painter.restore();
}

void paintGlassHighlight(QPainter &painter, const QRectF &bounds, qreal radius)
{
    if (bounds.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal lightRadius = std::clamp(std::min(bounds.width(), bounds.height()) * .70, 24.0, 110.0);
    QPointF light = bounds.topLeft() + QPointF(radius + lightRadius * .28, lightRadius * .20);
    if (auto *widget = dynamic_cast<QWidget *>(painter.device())) {
        bool invertible = false;
        const QTransform inverse = painter.worldTransform().inverted(&invertible);
        const QPointF pointer = inverse.map(widget->mapFromGlobal(QCursor::pos()));
        if (invertible && bounds.adjusted(-30, -30, 30, 30).contains(pointer))
            light = pointer;
    }
    QPainterPath silhouette;
    silhouette.addRoundedRect(bounds, radius, radius);
    painter.setClipPath(silhouette, Qt::IntersectClip);
    QRadialGradient hoverLight(light, lightRadius);
    hoverLight.setColorAt(0, QColor(255, 255, 255, 15));
    hoverLight.setColorAt(.42, QColor(255, 255, 255, 3));
    hoverLight.setColorAt(1, QColor(255, 255, 255, 0));
    const QRectF lightArea(light - QPointF(lightRadius, lightRadius), QSizeF(lightRadius * 2, lightRadius * 2));
    painter.fillRect(lightArea.intersected(bounds), hoverLight);
    QRadialGradient edgeLight(light, lightRadius);
    edgeLight.setColorAt(0, QColor(255, 255, 255, 37));
    edgeLight.setColorAt(1, QColor(255, 255, 255, 0));
    painter.setPen(QPen(QBrush(edgeLight), .7));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(1.7, 1.7, -1.7, -1.7),
                            std::max(1.0, radius - 1.7), std::max(1.0, radius - 1.7));
    painter.restore();
}

void paintGlass(QPainter &painter, const QRectF &bounds, const QColor &accent, qreal radius)
{
    if (bounds.isEmpty())
        return;
    // Overlay repaints and small effect damage reuse the same static glass, including its shadow.
    static thread_local QCache<QString, QPixmap> textures(16384); // Bounded to approximately 16 MiB.
    const qreal dpr = painter.device()->devicePixelRatioF();
    const QColor effectiveAccent = accent.isValid() ? accent : defaultAccent();
    const QString key = QStringLiteral("%1/%2/%3/%4/%5")
        .arg(bounds.width(), 0, 'f', 2).arg(bounds.height(), 0, 'f', 2)
        .arg(dpr, 0, 'f', 3).arg(effectiveAccent.rgba()).arg(radius, 0, 'f', 2);
    QPixmap *texture = textures.object(key);
    if (!texture) {
        const QSize pixels(qCeil((bounds.width() + 12) * dpr), qCeil((bounds.height() + 12) * dpr));
        auto created = std::make_unique<QPixmap>(pixels);
        created->setDevicePixelRatio(dpr);
        created->fill(Qt::transparent);
        {
            QPainter base(created.get());
            paintGlassBase(base, QRectF(QPointF(6, 6), bounds.size()), effectiveAccent, radius);
        }
        texture = created.get();
        const int cost = std::max(1, pixels.width() * pixels.height() / 256);
        if (cost <= textures.maxCost()) {
            textures.insert(key, created.release(), cost);
        } else {
            painter.drawPixmap(bounds.topLeft() - QPointF(6, 6), *texture);
            paintGlassHighlight(painter, bounds, radius);
            return;
        }
    }
    painter.drawPixmap(bounds.topLeft() - QPointF(6, 6), *texture);
    paintGlassHighlight(painter, bounds, radius);
}

} // namespace Aero

JellyButton::JellyButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent), m_accent(Aero::defaultAccent()),
      m_pressureAnimation(new QVariantAnimation(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    connect(m_pressureAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_pressure = value.toReal();
        update();
    });
    connect(this, &QPushButton::pressed, this, [this] { animatePressure(1, false); });
    connect(this, &QPushButton::released, this, [this] { animatePressure(0, true); });
    connect(this, &QPushButton::toggled, this, [this] { update(); });
}

void JellyButton::setAccentColor(const QColor &accent)
{
    m_accent = accent.isValid() ? accent : Aero::defaultAccent();
    m_glassTextures = {};
    update();
}

void JellyButton::setGlyph(Glyph glyph)
{
    m_glyph = glyph;
    updateGeometry();
    update();
}

QSize JellyButton::sizeHint() const
{
    if (text().isEmpty())
        return QSize(44, 44);
    const bool hasGlyph = m_glyph != Glyph::None || !icon().isNull();
    const int labelWidth = fontMetrics().horizontalAdvance(text());
    return QSize(std::max(44, 44 + labelWidth + (hasGlyph ? 24 + (text().isEmpty() ? 0 : 8) : 0)), 44);
}

QSize JellyButton::minimumSizeHint() const
{
    return QSize(text().isEmpty() ? 36 : 64, 36);
}

void JellyButton::animatePressure(qreal endValue, bool releasing)
{
    m_pressureAnimation->stop();
    m_pressureAnimation->setStartValue(m_pressure);
    m_pressureAnimation->setEndValue(endValue);
    m_pressureAnimation->setDuration(releasing ? 440 : 90);
    m_pressureAnimation->setEasingCurve(releasing ? QEasingCurve::OutElastic : QEasingCurve::OutCubic);
    m_pressureAnimation->start();
}

void JellyButton::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QPushButton::enterEvent(event);
}

void JellyButton::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_pointer = QPointF();
    update();
    QPushButton::leaveEvent(event);
}

void JellyButton::mouseMoveEvent(QMouseEvent *event)
{
    m_pointer = QPointF((event->position().x() / std::max(1, width()) - .5) * 2,
                        (event->position().y() / std::max(1, height()) - .5) * 2);
    update();
    QPushButton::mouseMoveEvent(event);
}

const QPixmap &JellyButton::glassTexture(int state)
{
    const qreal dpr = devicePixelRatioF();
    if (m_cachedTextureSize != size() || !qFuzzyCompare(m_cachedTextureDpr, dpr)) {
        m_glassTextures = {};
        m_cachedTextureSize = size();
        m_cachedTextureDpr = dpr;
    }
    QPixmap &texture = m_glassTextures[static_cast<size_t>(state)];
    if (!texture.isNull())
        return texture;
    texture = QPixmap(QSize(qCeil(width() * dpr), qCeil(height() * dpr)));
    texture.setDevicePixelRatio(dpr);
    texture.fill(Qt::transparent);
    QPainter painter(&texture);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal margin = std::max(6.0, width() * .065);
    const QRectF glass = QRectF(rect()).adjusted(margin, 6, -margin, -6);
    const QColor accent = state == 1 ? m_accent.lighter(122) : state == 2 ? m_accent.lighter(112) : m_accent;
    Aero::paintGlassBase(painter, glass, accent, glass.height() * .46);
    if (state == 1) {
        QColor checkedRim = m_accent.lighter(130);
        checkedRim.setAlpha(154);
        painter.setPen(QPen(checkedRim, 1.1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(glass.adjusted(3, 3, -3, -3), glass.height() * .36, glass.height() * .36);
    }
    return texture;
}

void JellyButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center = QRectF(rect()).center();
    painter.translate(center + QPointF(m_pointer.x() * 1.1, m_pointer.y() * .7 + m_pressure * 1.5));
    painter.rotate(m_hovered ? m_pointer.x() * 1.6 : 0);
    painter.scale(1 + m_pressure * .11, 1 - m_pressure * .20);
    painter.translate(-center);
    const qreal horizontalMargin = std::max(6.0, width() * .065);
    const QRectF glass = QRectF(rect()).adjusted(horizontalMargin, 6, -horizontalMargin, -6);
    if (!isEnabled())
        painter.setOpacity(.48);
    painter.drawPixmap(QPointF(0, 0), glassTexture(isChecked() ? 1 : m_hovered ? 2 : 0));
    Aero::paintGlassHighlight(painter, glass, glass.height() * .46);
    const QColor ink(QStringLiteral("#eaf3fa"));
    const bool hasGlyph = m_glyph != Glyph::None || !icon().isNull();
    const qreal innerPadding = text().isEmpty() ? 2 : 6;
    const QRectF content = glass.adjusted(innerPadding, 4, -innerPadding, -4);
    const qreal glyphSize = hasGlyph ? std::max(0.0, std::min({28.0, content.width(), glass.height() * .56})) : 0;
    const qreal gap = hasGlyph && !text().isEmpty() ? 7 : 0;
    const qreal availableText = std::max(0.0, content.width() - glyphSize - gap);
    const QString label = fontMetrics().elidedText(text(), Qt::ElideRight, static_cast<int>(availableText));
    const qreal labelWidth = label.isEmpty() ? 0 : fontMetrics().horizontalAdvance(label);
    const qreal totalWidth = glyphSize + gap + labelWidth;
    qreal left = glass.center().x() - totalWidth / 2;
    if (hasGlyph) {
        const QRectF glyphBounds(left, glass.center().y() - glyphSize / 2, glyphSize, glyphSize);
        if (m_glyph != Glyph::None)
            paintGlyph(painter, glyphBounds, ink);
        else
            icon().paint(&painter, glyphBounds.toRect(), Qt::AlignCenter,
                         isEnabled() ? QIcon::Normal : QIcon::Disabled, isChecked() ? QIcon::On : QIcon::Off);
        left += glyphSize + gap;
    }
    painter.setPen(ink);
    painter.drawText(QRectF(left, content.top(), labelWidth + 1, content.height()),
                     Qt::AlignVCenter | Qt::AlignLeft | Qt::TextShowMnemonic, label);
    if (hasFocus()) {
        painter.setPen(QPen(QColor(234, 243, 250, 100), .9, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(glass.adjusted(4, 4, -4, -4), glass.height() * .35, glass.height() * .35);
    }
}

void JellyButton::paintGlyph(QPainter &painter, const QRectF &bounds, const QColor &ink) const
{
    painter.save();
    painter.translate(bounds.topLeft());
    painter.scale(bounds.width() / 24, bounds.height() / 24);
    painter.setPen(QPen(ink, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    switch (m_glyph) {
    case Glyph::Play: {
        QPainterPath triangle;
        triangle.moveTo(8, 5);
        triangle.lineTo(19, 11);
        triangle.quadTo(20, 12, 19, 13);
        triangle.lineTo(8, 19);
        triangle.quadTo(6.5, 19.5, 6.5, 17.5);
        triangle.lineTo(6.5, 6.5);
        triangle.quadTo(6.5, 4.5, 8, 5);
        painter.fillPath(triangle, ink);
        break;
    }
    case Glyph::Pause:
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawRoundedRect(QRectF(6, 5, 4.5, 14), 1.4, 1.4);
        painter.drawRoundedRect(QRectF(13.5, 5, 4.5, 14), 1.4, 1.4);
        break;
    case Glyph::Previous:
    case Glyph::Next: {
        const bool next = m_glyph == Glyph::Next;
        if (!next) {
            painter.translate(24, 0);
            painter.scale(-1, 1);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        QPolygonF triangle;
        triangle << QPointF(5, 5) << QPointF(16, 12) << QPointF(5, 19);
        painter.drawPolygon(triangle);
        painter.drawRoundedRect(QRectF(17, 5, 2.5, 14), 1, 1);
        break;
    }
    case Glyph::Shuffle: {
        QPainterPath tracks;
        tracks.moveTo(3, 6);
        tracks.cubicTo(11, 6, 12, 18, 21, 18);
        tracks.moveTo(3, 18);
        tracks.cubicTo(11, 18, 12, 6, 21, 6);
        painter.drawPath(tracks);
        painter.drawLine(QPointF(18, 3), QPointF(21, 6));
        painter.drawLine(QPointF(21, 6), QPointF(18, 9));
        painter.drawLine(QPointF(18, 15), QPointF(21, 18));
        painter.drawLine(QPointF(21, 18), QPointF(18, 21));
        break;
    }
    case Glyph::Sequential: {
        QPainterPath loop;
        loop.moveTo(20, 10);
        loop.lineTo(20, 8);
        loop.quadTo(20, 5, 17, 5);
        loop.lineTo(4, 5);
        loop.moveTo(4, 14);
        loop.lineTo(4, 16);
        loop.quadTo(4, 19, 7, 19);
        loop.lineTo(20, 19);
        painter.drawPath(loop);
        painter.drawLine(QPointF(7, 2), QPointF(4, 5));
        painter.drawLine(QPointF(4, 5), QPointF(7, 8));
        painter.drawLine(QPointF(17, 16), QPointF(20, 19));
        painter.drawLine(QPointF(20, 19), QPointF(17, 22));
        break;
    }
    case Glyph::Pin: {
        QPainterPath pin;
        pin.moveTo(8, 4);
        pin.lineTo(16, 4);
        pin.lineTo(15, 12);
        pin.lineTo(18, 16);
        pin.lineTo(6, 16);
        pin.lineTo(9, 12);
        pin.closeSubpath();
        painter.drawPath(pin);
        painter.drawLine(QPointF(12, 16), QPointF(12, 22));
        break;
    }
    case Glyph::Palette: {
        QPainterPath palette;
        palette.moveTo(20, 8);
        palette.cubicTo(17, 1, 6, 1, 3, 9);
        palette.cubicTo(0, 17, 9, 23, 13, 19);
        palette.cubicTo(15, 17, 11, 15, 15, 13);
        palette.cubicTo(18, 12, 23, 15, 20, 8);
        painter.drawPath(palette);
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        for (const QPointF &dot : {QPointF(7, 8), QPointF(11, 5), QPointF(16, 7), QPointF(6, 13)})
            painter.drawEllipse(dot, 1.3, 1.3);
        break;
    }
    case Glyph::Close:
        painter.drawLine(QPointF(6, 6), QPointF(18, 18));
        painter.drawLine(QPointF(6, 18), QPointF(18, 6));
        break;
    case Glyph::Menu:
        for (qreal y : {6.0, 12.0, 18.0})
            painter.drawLine(QPointF(5, y), QPointF(19, y));
        break;
    case Glyph::None:
        break;
    }
    painter.restore();
}
