#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <functional>
#include "aero_animation.h"

class QAbstractItemModel;
class QAbstractItemView;
class QWheelEvent;
class QScrollBar;
class QWidget;

// Pixel scrolling for item views without changing their model or selection.
// Wheel notches retarget one time-driven spring; native touchpad pixel/momentum
// events track the finger directly and retain the platform's own inertia.
class SmoothScrollController final : public QObject {
public:
    static SmoothScrollController *attach(QAbstractItemView *view);
    ~SmoothScrollController() override;

    bool isAnimating() const;
    qreal targetValue() const;
    qreal velocity() const;
    void cancel();
    // Visual effects share the scroll tick instead of scheduling a second
    // viewport repaint between scroll frames. Destroyed receivers are skipped.
    void observeFrames(QObject *context, std::function<void(bool, bool)> frame);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit SmoothScrollController(QAbstractItemView *view);
    void observeModel();
    bool wheel(QWheelEvent *event);
    void advance();
    void retime();
    void applyPosition();
    void notifyFrame(bool continuing, bool cancelled = false);

    QPointer<QAbstractItemView> m_view;
    QPointer<QWidget> m_viewport;
    QPointer<QWidget> m_window;
    QPointer<QScrollBar> m_scrollbar;
    QPointer<QAbstractItemModel> m_model;
    QVector<QMetaObject::Connection> m_modelConnections;
    struct FrameObserver {
        QPointer<QObject> context;
        std::function<void(bool, bool)> frame;
    };
    QVector<FrameObserver> m_frameObservers;
    QObject *m_refreshObserver = nullptr;
    QTimer m_timer;
    Aero::FrameSchedule m_schedule;
    QElapsedTimer m_clock;
    qint64 m_previousTickNs = 0;
    qreal m_position = 0;
    qreal m_target = 0;
    qreal m_velocity = 0;
    bool m_applying = false;
    bool m_running = false;
    int m_lastAppliedValue = 0;
    int m_minimum = 0;
    int m_maximum = 0;
};
