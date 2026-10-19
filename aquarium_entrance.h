#pragma once

#include <QElapsedTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector>
#include <QVector2D>
#include <functional>
#include <memory>

class QOpenGLShaderProgram;
class QOpenGLTexture;

class AquariumEntrance final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    enum class Stage { Welcome, Login, Zoom, Typing, Finished };
    explicit AquariumEntrance(QWidget *parent = nullptr);
    ~AquariumEntrance() override;
    void setReady(bool ready);
    bool isReady() const { return m_ready; }
    void showWelcome(bool ready);
    void showLogin();
    void enterPlayer();
    Stage stage() const { return m_stage; }
    QPointF logoCenter() const;
    bool rendererReady() const { return m_rendererReady; }
    QString rendererName() const { return m_rendererName; }
    QString rendererError() const { return m_error; }
    double measuredFps() const;
    double frameTimePercentile(double percentile) const;
    quint64 presentedFrames() const { return m_presented; }
    std::function<void()> onLoginRequested;
    std::function<void()> onEntryFinished;
    std::function<void(const QString &)> onRendererFailed;

protected:
    void initializeGL() override;
    void paintGL() override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void cleanup();
    void advance(double dt);
    void activateLogo();
    bool hitLogo(const QPointF &point) const;
    void updateScreenTexture();
    std::unique_ptr<QOpenGLTexture> texture(const QImage &image, bool nearest = false);
    QVector2D deviceCenter() const;
    float deviceAngle() const;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLTexture> m_background, m_whale, m_arms, m_letter, m_screen, m_type;
    unsigned int m_vao = 0, m_vbo = 0;
    QElapsedTimer m_clock, m_presentClock;
    qint64 m_previousTick = 0, m_previousPresent = 0;
    QVector<double> m_frameTimes;
    QVector<float> m_glyphEnds;
    float m_typeAspect = 1;
    quint64 m_presented = 0;
    Stage m_stage = Stage::Welcome;
    double m_time = 0, m_stageTime = 0;
    float m_spin = 0, m_spinVelocity = 0.13f;
    float m_hover = 0, m_press = 0, m_pressVelocity = 0, m_light = 0, m_login = 0;
    QPointF m_pointer;
    bool m_hovered = false, m_pressed = false, m_ready = false;
    bool m_rendererReady = false, m_screenDirty = true;
    QString m_rendererName, m_error;
};
