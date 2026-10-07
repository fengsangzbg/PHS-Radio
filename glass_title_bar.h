#pragma once

#include <QColor>
#include <QPoint>
#include <QPointer>
#include <QWidget>
#include <functional>

class JellyButton;
class QLabel;
class QMainWindow;

// The owner supplies the frameless native resize hit-testing. Caption dragging
// delegates to the OS so snapping and moving between monitors remain native.
class GlassTitleBar final : public QWidget {
public:
    explicit GlassTitleBar(QMainWindow *owner, QWidget *parent = nullptr);
    void setAccentColor(const QColor &color);
    std::function<void()> onUpdateRequested;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateWindowState();
    void toggleMaximized();
    QPointer<QMainWindow> m_owner;
    QLabel *m_logo;
    QLabel *m_title;
    JellyButton *m_update;
    JellyButton *m_minimize;
    JellyButton *m_maximize;
    JellyButton *m_close;
    QColor m_accent;
    QPoint m_dragOffset;
    bool m_fallbackDragging = false;
};
