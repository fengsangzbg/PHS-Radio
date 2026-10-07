#pragma once

#include <QColor>
#include <QPushButton>
#include <QPixmap>
#include <QRectF>
#include <array>

class QPainter;
class QVariantAnimation;

namespace Aero {

QColor defaultAccent();
void paintGlass(QPainter &painter, const QRectF &bounds, const QColor &accent, qreal radius = 24);
void paintGlassBase(QPainter &painter, const QRectF &bounds, const QColor &accent, qreal radius = 24);
void paintGlassHighlight(QPainter &painter, const QRectF &bounds, qreal radius = 24);
int animationInterval(const QWidget *widget);

} // namespace Aero

class JellyButton final : public QPushButton {
public:
    enum class Glyph { None, Play, Pause, Previous, Next, Shuffle, Sequential,
                       Pin, Palette, Close, Menu };

    explicit JellyButton(const QString &text, QWidget *parent = nullptr);
    void setAccentColor(const QColor &accent);
    void setGlyph(Glyph glyph);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void animatePressure(qreal endValue, bool releasing);
    void paintGlyph(QPainter &painter, const QRectF &bounds, const QColor &ink) const;
    const QPixmap &glassTexture(int state);

    QColor m_accent;
    Glyph m_glyph = Glyph::None;
    QVariantAnimation *m_pressureAnimation;
    qreal m_pressure = 0;
    bool m_hovered = false;
    QPointF m_pointer;
    std::array<QPixmap, 3> m_glassTextures;
    QSize m_cachedTextureSize;
    qreal m_cachedTextureDpr = 0;
};
