#pragma once

#include <QPoint>
#include <QColor>
#include <QRect>
#include <QWidget>

class QPropertyAnimation;
class QTimer;
class QVBoxLayout;

class PlaylistDrawer final : public QWidget {
public:
    explicit PlaylistDrawer(QWidget *host);

    QVBoxLayout *contentLayout() const;
    bool isExpanded() const;
    bool isPinned() const;
    void setExpanded(bool expanded);
    void setPinned(bool pinned);
    void toggle();
    void setTopInset(int pixels);
    void setAccentColor(const QColor &accent);
    QColor accentColor() const;

    // Positions are relative to the host; also used by pointer-behavior tests.
    void updatePointer(const QPoint &hostPosition, bool insideWindow);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QRect targetPanelRect() const;
    QRect hiddenPanelRect() const;
    bool hasPanelFocus() const;
    void adjustToHost();
    void animateToTarget();
    void pollPointer();
    void updateGlassStyles();

    QWidget *m_host;
    QVBoxLayout *m_contentLayout;
    QPropertyAnimation *m_slide;
    QTimer *m_pointerTimer;
    QTimer *m_leaveTimer;
    QColor m_accent;
    bool m_expanded = false;
    bool m_pinned = false;
    int m_topInset = 10;
};
