#pragma once

#include <QColor>
#include <QCache>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QStaticText>
#include <QStyledItemDelegate>
#include <QTimer>

class QTableWidget;
class QVariantAnimation;

// Draws one continuous glass capsule per song without changing the table's
// model, row geometry, selection, or hit targets. Animation repaints only the
// viewport and always stops shortly after scrolling.
class SongGlassDelegate final : public QStyledItemDelegate {
public:
    explicit SongGlassDelegate(QTableWidget *table);
    ~SongGlassDelegate() override;

    void setAccentColor(const QColor &accent);
    bool scrollMotionActive() const;
    bool pressMotionActive() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void beginScrollMotion(int value);
    void animatePress(qreal target, bool releasing);
    void updateRow(int row);
    const QPixmap &glassLayer(const QSizeF &size, qreal dpr, bool selected) const;
    const QPixmap &coverLayer(const QIcon &icon, int side, qreal dpr, bool enabled) const;
    const QStaticText &textLayout(const QModelIndex &index, const QFont &font, int width) const;

    struct TextLayout {
        QString source;
        QFont font;
        int width = 0;
        QStaticText text;
    };

    QTableWidget *m_table;
    QPointer<QWidget> m_viewport;
    QColor m_accent;
    QTimer m_motionTimer;
    QElapsedTimer m_motionClock;
    int m_previousScroll = 0;
    qreal m_scrollImpulse = 0;
    qreal m_motionAge = 0;
    qreal m_motionEnvelope = 0;
    QVariantAnimation *m_pressAnimation;
    QPointF m_pointer;
    int m_hoveredRow = -1;
    int m_pressedRow = -1;
    qreal m_pressDepth = 0;
    bool m_pointerHeld = false;
    // These caches scale with visible styles and recent items, never the total
    // playlist size. Backdrop water is deliberately excluded from all textures.
    mutable QCache<QString, QPixmap> m_glassLayers{8};
    mutable QCache<QString, QPixmap> m_coverLayers{128};
    mutable QCache<QModelIndex, TextLayout> m_textLayouts{256};
};
