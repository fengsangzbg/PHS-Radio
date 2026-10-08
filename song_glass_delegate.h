#pragma once

#include <QColor>
#include <QCache>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QStaticText>
#include <QStyledItemDelegate>
#include <QTimer>
#include <array>
#include "aero_animation.h"

class QTableWidget;
class QVariantAnimation;
class SmoothScrollController;

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
    void stopScrollMotion();
    void advanceScrollMotion();
    void scheduleScrollMotion();
    void animatePress(qreal target, bool releasing);
    void updateRow(int row);
    const QPixmap &glassLayer(const QSizeF &size, qreal dpr, bool selected) const;
    const QPixmap &coverLayer(const QIcon &icon, int side, qreal dpr, bool enabled) const;
    const QStaticText &textLayout(const QModelIndex &index, const QFont &font, int width) const;
    const QPixmap &textLayer(const QModelIndex &index, const QFont &font, int width,
                             qreal dpr, const QColor &ink) const;

    struct TextLayout {
        struct DensityPixels {
            QPixmap pixels;
            qreal dpr = 0;
            quint64 lastUsed = 0;
        };
        QString source;
        QFont font;
        int width = 0;
        QStaticText text;
        QColor ink;
        // Content snapshots use DPR 1 while the visible viewport can be high
        // DPI. Keep both densities instead of rerasterizing on every capture.
        std::array<DensityPixels, 2> densityPixels;
        quint64 densityClock = 0;
    };

    struct GlassKey {
        int width = 0;
        int height = 0;
        int dpr = 0;
        bool selected = false;
        friend bool operator==(const GlassKey &a, const GlassKey &b)
        {
            return a.width == b.width && a.height == b.height
                   && a.dpr == b.dpr && a.selected == b.selected;
        }
        friend size_t qHash(const GlassKey &key, size_t seed = 0)
        {
            return qHashMulti(seed, key.width, key.height, key.dpr, key.selected);
        }
    };
    struct CoverKey {
        qint64 icon = 0;
        int side = 0;
        int dpr = 0;
        bool enabled = false;
        friend bool operator==(const CoverKey &a, const CoverKey &b)
        {
            return a.icon == b.icon && a.side == b.side
                   && a.dpr == b.dpr && a.enabled == b.enabled;
        }
        friend size_t qHash(const CoverKey &key, size_t seed = 0)
        {
            return qHashMulti(seed, key.icon, key.side, key.dpr, key.enabled);
        }
    };

    QTableWidget *m_table;
    QPointer<QWidget> m_viewport;
    QColor m_accent;
    QObject *m_refreshObserver = nullptr;
    QPointer<SmoothScrollController> m_scrollController;
    QTimer m_motionTimer;
    Aero::FrameSchedule m_motionSchedule;
    bool m_scrollAnimating = false;
    QElapsedTimer m_motionClock;
    int m_previousScroll = 0;
    qreal m_scrollImpulse = 0;
    qreal m_motionAge = 0;
    qreal m_motionEnvelope = 0;
    qreal m_scrollTarget = 0;
    qint64 m_previousMotionFrameNs = 0;
    qint64 m_previousScrollNs = 0;
    QVariantAnimation *m_pressAnimation;
    QPointF m_pointer;
    int m_hoveredRow = -1;
    int m_pressedRow = -1;
    qreal m_pressDepth = 0;
    bool m_pointerHeld = false;
    // These caches scale with visible styles and recent items, never the total
    // playlist size. Backdrop water is deliberately excluded from all textures.
    mutable QCache<GlassKey, QPixmap> m_glassLayers{8};
    mutable QCache<CoverKey, QPixmap> m_coverLayers{128};
    mutable QCache<QModelIndex, TextLayout> m_textLayouts{256};
};
