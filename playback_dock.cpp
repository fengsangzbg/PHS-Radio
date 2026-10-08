#include "playback_dock.h"
#include "meteor_effect.h"
#include "aero_widgets.h"
#include "liquid_backdrop.h"

#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

constexpr int SliderResolution = 1000000;

QIcon speakerIcon(bool silent, qreal dpr)
{
    const int pixels = qRound(24 * dpr);
    QPixmap icon(pixels, pixels);
    icon.setDevicePixelRatio(dpr);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(235, 248, 255), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(235, 248, 255, 28));
    QPainterPath speaker;
    speaker.moveTo(3, 9); speaker.lineTo(7, 9); speaker.lineTo(11, 5);
    speaker.lineTo(11, 19); speaker.lineTo(7, 15); speaker.lineTo(3, 15); speaker.closeSubpath();
    painter.drawPath(speaker);
    painter.setBrush(Qt::NoBrush);
    if (silent) {
        painter.drawLine(QPointF(16, 8), QPointF(22, 16));
        painter.drawLine(QPointF(22, 8), QPointF(16, 16));
    } else {
        painter.drawArc(QRectF(11, 8, 6, 8), -60 * 16, 120 * 16);
        painter.drawArc(QRectF(9, 4, 13, 16), -55 * 16, 110 * 16);
    }
    return QIcon(icon);
}

QString timeText(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(0, milliseconds) / 1000;
    const qint64 hours = seconds / 3600;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3").arg(hours)
            .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

class TrackLabel final : public QLabel {
public:
    explicit TrackLabel(QWidget *parent) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

class SeekSlider final : public QSlider {
public:
    explicit SeekSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {}

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !isEnabled()) {
            QSlider::mousePressEvent(event);
            return;
        }
        m_pointerDrag = true;
        setFocus(Qt::MouseFocusReason);
        setSliderDown(true);
        setSliderPosition(valueAt(event->position().x()));
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_pointerDrag) {
            QSlider::mouseMoveEvent(event);
            return;
        }
        if (isEnabled())
            setSliderPosition(valueAt(event->position().x()));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_pointerDrag || event->button() != Qt::LeftButton) {
            QSlider::mouseReleaseEvent(event);
            return;
        }
        if (isEnabled())
            setSliderPosition(valueAt(event->position().x()));
        m_pointerDrag = false;
        setSliderDown(false);
        event->accept();
    }

private:
    int valueAt(qreal x) const
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const int span = std::max(1, groove.width() - handle.width());
        const int offset = std::clamp(qRound(x) - groove.left() - handle.width() / 2, 0, span);
        return QStyle::sliderValueFromPosition(minimum(), maximum(), offset, span, option.upsideDown);
    }

    bool m_pointerDrag = false;
};

} // namespace

PlaybackDock::PlaybackDock(QWidget *host)
    : QWidget(host), m_host(host), m_title(new TrackLabel(this)), m_artist(new TrackLabel(this)),
      m_elapsed(new QLabel(this)), m_total(new QLabel(this)), m_progress(new SeekSlider(this)),
      m_volumeSlider(new SeekSlider(this)), m_volumeLabel(new QLabel(this)),
      m_mute(new JellyButton({}, this)), m_transportLeft(new QWidget(this)),
      m_transportRight(new QWidget(this)),
      m_playPause(new JellyButton({}, this)), m_previous(new JellyButton({}, this)),
      m_next(new JellyButton({}, this)), m_mode(new JellyButton({}, this)),
      m_pin(new JellyButton({}, this)), m_palette(new JellyButton({}, this)),
      m_songPage(new JellyButton(QStringLiteral("唱片"), this)),
      m_lyrics(new JellyButton(QStringLiteral("歌词"), this)),
      m_slide(new QPropertyAnimation(this, "geometry", this)), m_pointerTimer(new QTimer(this)),
      m_leaveTimer(new QTimer(this)), m_keyboardTimer(new QTimer(this)), m_meteor(new PlaybackMeteor(this)),
      m_accent(Aero::defaultAccent())
{
    Q_ASSERT(host);
    setObjectName(QStringLiteral("playbackDock"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 10, 22, 10);
    layout->setSpacing(8);
    layout->setSizeConstraint(QLayout::SetNoConstraint);

    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    auto *metadata = new QVBoxLayout;
    metadata->setSpacing(0);
    QFont titleFont = font();
    titleFont.setPointSize(13);
    titleFont.setWeight(QFont::DemiBold);
    m_title->setFont(titleFont);
    m_title->setObjectName(QStringLiteral("playbackTitle"));
    m_title->setStyleSheet(QStringLiteral("color: #edf4fa; background: transparent;"));
    m_artist->setStyleSheet(QStringLiteral("color: #96a4b3; background: transparent; font-size: 11px;"));
    metadata->addWidget(m_title);
    metadata->addWidget(m_artist);
    top->addLayout(metadata, 1);
    m_mode->setObjectName(QStringLiteral("playbackModeButton"));
    m_mode->setCheckable(true);
    m_mode->setFixedSize(132, 40);
    m_palette->setObjectName(QStringLiteral("playbackColorButton"));
    m_palette->setGlyph(JellyButton::Glyph::Palette);
    m_palette->setFixedSize(40, 40);
    m_palette->setToolTip(QStringLiteral("选择播放器的玻璃颜色"));
    m_palette->setAccessibleName(QStringLiteral("更换颜色"));
    m_pin->setObjectName(QStringLiteral("playbackPinButton"));
    m_pin->setGlyph(JellyButton::Glyph::Pin);
    m_pin->setCheckable(true);
    m_pin->setFixedSize(94, 40);
    top->addWidget(m_mode);
    top->addWidget(m_palette);
    top->addWidget(m_pin);
    layout->addLayout(top);

    auto *progressRow = new QHBoxLayout;
    progressRow->setSpacing(10);
    for (QLabel *label : {m_elapsed, m_total}) {
        label->setMinimumWidth(54);
        label->setStyleSheet(QStringLiteral("color: #b3c0cc; background: transparent; font-size: 11px;"));
    }
    m_total->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_progress->setObjectName(QStringLiteral("playbackProgress"));
    m_progress->setAccessibleName(QStringLiteral("播放进度"));
    m_progress->setFocusPolicy(Qt::StrongFocus);
    m_progress->setTracking(true);
    m_progress->setRange(0, 0);
    m_progress->setMinimumHeight(22);
    m_progress->setToolTip(QStringLiteral("点击或拖动调整播放进度，方向键前后移动"));
    m_progress->installEventFilter(this);
    progressRow->addWidget(m_elapsed);
    progressRow->addWidget(m_progress, 1);
    progressRow->addWidget(m_total);
    layout->addLayout(progressRow);

    auto *transport = new QHBoxLayout;
    transport->setSpacing(14);
    m_previous->setGlyph(JellyButton::Glyph::Previous);
    m_previous->setFixedSize(46, 46);
    m_previous->setAccessibleName(QStringLiteral("上一首"));
    m_previous->setToolTip(QStringLiteral("上一首"));
    m_playPause->setObjectName(QStringLiteral("playbackPlayPauseButton"));
    m_playPause->setFixedSize(54, 54);
    m_next->setGlyph(JellyButton::Glyph::Next);
    m_next->setFixedSize(46, 46);
    m_next->setAccessibleName(QStringLiteral("下一首"));
    m_next->setToolTip(QStringLiteral("下一首"));
    m_songPage->setObjectName(QStringLiteral("playbackSongPageButton"));
    m_songPage->setFixedSize(58, 38);
    m_songPage->setToolTip(QStringLiteral("打开唱片与高清封面"));
    m_lyrics->setObjectName(QStringLiteral("playbackLyricsButton"));
    m_lyrics->setFixedSize(58, 38);
    m_lyrics->setToolTip(QStringLiteral("打开滚动歌词"));
    auto *left = new QHBoxLayout(m_transportLeft);
    left->setContentsMargins(0, 0, 0, 0);
    left->addWidget(m_songPage);
    left->addStretch(1);
    auto *controls = new QWidget(this);
    auto *center = new QHBoxLayout(controls);
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(14);
    center->addWidget(m_previous);
    center->addWidget(m_playPause);
    center->addWidget(m_next);
    controls->setFixedSize(174, 54);
    auto *right = new QHBoxLayout(m_transportRight);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(8);
    right->addStretch(1);
    right->addWidget(m_lyrics);
    m_mute->setObjectName(QStringLiteral("playbackMuteButton"));
    m_mute->setCheckable(true);
    m_mute->setFixedSize(34, 34);
    m_mute->installEventFilter(this);
    right->addWidget(m_mute);
    m_volumeSlider->setObjectName(QStringLiteral("playbackVolume"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setSingleStep(1);
    m_volumeSlider->setPageStep(10);
    m_volumeSlider->setTracking(true);
    m_volumeSlider->setMinimumHeight(22);
    m_volumeSlider->setFocusPolicy(Qt::StrongFocus);
    m_volumeSlider->setAccessibleName(QStringLiteral("音乐音量"));
    m_volumeSlider->setAccessibleDescription(QStringLiteral("拖动或方向键调整音乐音量，不改变系统总音量"));
    m_volumeSlider->installEventFilter(this);
    right->addWidget(m_volumeSlider);
    m_volumeLabel->setObjectName(QStringLiteral("playbackVolumeLabel"));
    m_volumeLabel->setFixedWidth(36);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeLabel->setStyleSheet(QStringLiteral("color:#b3c0cc;background:transparent;font-size:11px;"));
    right->addWidget(m_volumeLabel);
    transport->addWidget(m_transportLeft);
    transport->addWidget(controls);
    transport->addWidget(m_transportRight);
    layout->addLayout(transport);

    connect(m_playPause, &QPushButton::clicked, this, [this] {
        if (!m_playing && !m_busy)
            triggerPlaybackMeteor();
        if (onPlayPause)
            onPlayPause();
    });
    connect(m_previous, &QPushButton::clicked, this, [this] { if (onPrevious) onPrevious(); });
    connect(m_next, &QPushButton::clicked, this, [this] { if (onNext) onNext(); });
    connect(m_songPage, &QPushButton::clicked, this, [this] {
        if (onSongPageRequested) onSongPageRequested();
    });
    connect(m_lyrics, &QPushButton::clicked, this, [this] {
        if (onLyricsRequested) onLyricsRequested();
    });
    connect(m_mode, &QPushButton::toggled, this, [this](bool shuffle) {
        setShuffle(shuffle);
        if (onShuffleChanged)
            onShuffleChanged(shuffle);
    });
    connect(m_pin, &QPushButton::toggled, this, [this](bool pinned) { setPinned(pinned); });
    connect(m_palette, &QPushButton::clicked, this, [this] {
        m_colorRequestActive = true;
        m_leaveTimer->stop();
        if (onColorRequested)
            onColorRequested();
        m_colorRequestActive = false;
    });
    connect(m_mute, &QPushButton::toggled, this, [this](bool muted) {
        if (!muted && m_volume <= 0) {
            setVolume(m_lastAudibleVolume > 0 ? m_lastAudibleVolume : 1);
            if (onVolumeChanged)
                onVolumeChanged(m_volume);
        }
        setMuted(muted);
        if (onMutedChanged)
            onMutedChanged(muted);
    });
    connect(m_volumeSlider, &QSlider::sliderPressed, this, [this] {
        m_leaveTimer->stop();
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        setVolume(value / 100.0);
        if (onVolumeChanged)
            onVolumeChanged(m_volume);
        if (m_muted && value > 0) {
            setMuted(false);
            if (onMutedChanged)
                onMutedChanged(false);
        }
    });
    connect(m_progress, &QSlider::sliderPressed, this, [this] {
        m_dragging = true;
        m_leaveTimer->stop();
    });
    connect(m_progress, &QSlider::sliderReleased, this, [this] {
        m_dragging = false;
        if (!m_syncingProgress)
            requestSeek(m_progress->value());
    });
    connect(m_progress, &QSlider::valueChanged, this, [this](int value) {
        if (m_syncingProgress)
            return;
        updateTimeLabels();
        if (!m_dragging)
            requestSeek(value);
    });

    m_slide->setDuration(180);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QPropertyAnimation::finished, this, [this] { if (!m_expanded) hide(); });
    m_leaveTimer->setSingleShot(true);
    m_leaveTimer->setInterval(500);
    connect(m_leaveTimer, &QTimer::timeout, this, [this] {
        if (m_expanded && !m_pinned && !hasInteraction())
            setExpanded(false);
    });
    m_keyboardTimer->setSingleShot(true);
    m_keyboardTimer->setInterval(700);
    m_pointerTimer->setInterval(75);
    connect(m_pointerTimer, &QTimer::timeout, this, [this] { pollPointer(); });
    host->installEventFilter(this);
    setTrack({}, {});
    setPlaying(false);
    setShuffle(false);
    setPinned(false);
    setVolume(1);
    setMuted(false);
    setAccentColor(m_accent);
    updateSeekEnabled();
    updateTimeLabels();
    setGeometry(hiddenPanelRect());
    updateTransportLayout();
    hide();
    m_pointerTimer->start();
}

void PlaybackDock::setPlaying(bool playing)
{
    m_playing = playing;
    updatePlayButton();
}

void PlaybackDock::triggerPlaybackMeteor()
{
    setExpanded(true);
    m_leaveTimer->stop();
    m_meteor->trigger();
}

void PlaybackDock::setBusy(bool busy)
{
    m_busy = busy;
    updatePlayButton();
}

void PlaybackDock::updatePlayButton()
{
    m_playPause->setGlyph(m_playing ? JellyButton::Glyph::Pause : JellyButton::Glyph::Play);
    m_playPause->setEnabled(!m_busy);
    const QString action = m_busy ? QStringLiteral("正在准备播放…")
                                 : m_playing ? QStringLiteral("暂停") : QStringLiteral("播放");
    m_playPause->setToolTip(action);
    m_playPause->setAccessibleName(action);
}

void PlaybackDock::setTrack(const QString &title, const QString &artist)
{
    m_title->setText(title.isEmpty() ? QStringLiteral("尚未播放") : title);
    m_title->setToolTip(title);
    m_trackArtist = artist;
    setCurrentLyric({});
}

void PlaybackDock::setCurrentLyric(const QString &text)
{
    m_currentLyric = text.simplified();
    m_artist->setText(m_currentLyric.isEmpty()
        ? (m_trackArtist.isEmpty() ? QStringLiteral("靠近窗口底边显示播放控制") : m_trackArtist)
        : m_currentLyric);
    m_artist->setToolTip(m_currentLyric.isEmpty() ? m_trackArtist
        : m_currentLyric + (m_trackArtist.isEmpty() ? QString() : QStringLiteral("\n") + m_trackArtist));
}

QString PlaybackDock::currentLyric() const { return m_currentLyric; }
JellyButton *PlaybackDock::songPageButton() const { return m_songPage; }
JellyButton *PlaybackDock::lyricsButton() const { return m_lyrics; }

void PlaybackDock::setPosition(qint64 position)
{
    m_position = std::max<qint64>(0, position);
    if (m_duration > 0)
        m_position = std::min(m_position, m_duration);
    if (m_dragging || m_progress->isSliderDown())
        return;
    const QSignalBlocker blockedSignals(m_progress);
    m_progress->setValue(valueForPosition(m_position));
    updateTimeLabels();
}

void PlaybackDock::setDuration(qint64 duration)
{
    m_duration = std::max<qint64>(0, duration);
    m_position = std::min(m_position, m_duration);
    {
        const QSignalBlocker blockedSignals(m_progress);
        m_syncingProgress = true;
        if (m_duration == 0) {
            m_progress->setSliderDown(false);
            m_dragging = false;
            m_position = 0;
        }
        m_progress->setRange(0, m_duration > 0 ? SliderResolution : 0);
        if (!m_dragging)
            m_progress->setValue(valueForPosition(m_position));
        m_progress->setSingleStep(m_duration > 0 ? std::max(1, valueForPosition(5000)) : 1);
        m_progress->setPageStep(m_duration > 0 ? std::max(1, valueForPosition(30000)) : 1);
        m_syncingProgress = false;
    }
    updateSeekEnabled();
    updateTimeLabels();
}

void PlaybackDock::setSeekable(bool seekable)
{
    m_seekable = seekable;
    if (!seekable) {
        const QSignalBlocker blockedSignals(m_progress);
        m_progress->setSliderDown(false);
        m_dragging = false;
    }
    updateSeekEnabled();
    updateTimeLabels();
}

void PlaybackDock::setVolume(qreal volume)
{
    if (!std::isfinite(volume))
        return;
    m_volume = std::clamp(volume, qreal(0), qreal(1));
    if (m_volume > 0)
        m_lastAudibleVolume = m_volume;
    const QSignalBlocker blockedSignals(m_volumeSlider);
    m_volumeSlider->setValue(qRound(m_volume * 100));
    updateVolumeDisplay();
}

void PlaybackDock::setMuted(bool muted)
{
    m_muted = muted;
    updateVolumeDisplay();
}

void PlaybackDock::updateVolumeDisplay()
{
    const int percent = qRound(m_volume * 100);
    const bool silent = m_muted || m_volume <= 0;
    {
        const QSignalBlocker blockedSignals(m_mute);
        m_mute->setChecked(silent);
    }
    const qreal dpr = devicePixelRatioF();
    if (m_mute->icon().isNull() || m_muteIconSilent != silent || !qFuzzyCompare(m_muteIconDpr, dpr)) {
        m_mute->setIcon(speakerIcon(silent, dpr));
        m_muteIconSilent = silent;
        m_muteIconDpr = dpr;
    }
    const QString action = silent ? QStringLiteral("恢复音乐声音") : QStringLiteral("静音音乐");
    m_mute->setToolTip(action);
    m_mute->setAccessibleName(action);
    m_volumeLabel->setText(QString::number(percent) + QLatin1Char('%'));
    m_volumeSlider->setToolTip(QStringLiteral("音乐音量：%1%%2\n拖动或方向键调整，不改变系统总音量")
        .arg(percent).arg(m_muted ? QStringLiteral("（已静音）") : QString()));
}

void PlaybackDock::updateTransportLayout()
{
    const int sideWidth = std::max(1, (width() - 44 - 174 - 28) / 2);
    m_transportLeft->setFixedWidth(sideWidth);
    m_transportRight->setFixedWidth(sideWidth);
    const bool compact = width() < 900;
    m_volumeLabel->setVisible(!compact);
    m_volumeSlider->setFixedWidth(compact ? std::clamp(sideWidth - 108, 64, 90) : 124);
}

void PlaybackDock::updateSeekEnabled()
{
    m_progress->setEnabled(m_seekable && m_duration > 0);
}

int PlaybackDock::valueForPosition(qint64 position) const
{
    if (m_duration <= 0)
        return 0;
    const long double fraction = static_cast<long double>(std::clamp<qint64>(position, 0, m_duration)) / m_duration;
    return static_cast<int>(std::llround(fraction * SliderResolution));
}

qint64 PlaybackDock::positionForValue(int value) const
{
    return m_duration > 0
        ? static_cast<qint64>(std::llround(static_cast<long double>(value) / SliderResolution * m_duration)) : 0;
}

void PlaybackDock::requestSeek(int value)
{
    if (!m_seekable || m_duration <= 0)
        return;
    m_position = positionForValue(value);
    updateTimeLabels();
    if (onSeekRequested)
        onSeekRequested(m_position);
}

void PlaybackDock::updateTimeLabels()
{
    m_elapsed->setText(timeText(m_dragging ? positionForValue(m_progress->value()) : m_position));
    m_total->setText(m_duration > 0 ? timeText(m_duration) : QStringLiteral("--:--"));
}

void PlaybackDock::setAccentColor(const QColor &color)
{
    if (!color.isValid())
        return;
    m_accent = color;
    for (JellyButton *button : {m_playPause, m_previous, m_next, m_mode, m_pin, m_palette, m_songPage, m_lyrics, m_mute})
        button->setAccentColor(color);
    m_progress->setStyleSheet(QStringLiteral(
        "QSlider { background: transparent; }"
        "QSlider::groove:horizontal { background: rgba(255,255,255,24); border: 1px solid rgba(255,255,255,18); height: 5px; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %2,stop:1 %1); border-radius: 3px; }"
        "QSlider::handle:horizontal { background: qradialgradient(cx:0.35,cy:0.25,radius:0.85,stop:0 #f8feff,stop:0.25 rgba(245,255,255,160),stop:0.65 rgba(200,235,245,70),stop:1 rgba(240,255,255,160));"
        " border: 1px solid rgba(255,255,255,210); width: 16px; margin: -6px 0; border-radius: 8px; }"
        "QSlider::handle:horizontal:hover { border: 1px solid %2; }"
        "QSlider::handle:horizontal:disabled { background: rgba(255,255,255,18); border-color: rgba(255,255,255,30); }"
        "QSlider::sub-page:horizontal:disabled { background: rgba(255,255,255,20); }")
        .arg(color.lighter(120).name(), color.darker(130).name()));
    m_volumeSlider->setStyleSheet(m_progress->styleSheet());
    update();
}

void PlaybackDock::setShuffle(bool shuffle)
{
    m_shuffle = shuffle;
    const QSignalBlocker blockedSignals(m_mode);
    m_mode->setChecked(shuffle);
    m_mode->setGlyph(shuffle ? JellyButton::Glyph::Shuffle : JellyButton::Glyph::Sequential);
    m_mode->setText(shuffle ? QStringLiteral("随机播放") : QStringLiteral("顺序播放"));
    m_mode->setToolTip(shuffle ? QStringLiteral("当前：随机播放；点击切换为顺序播放")
                              : QStringLiteral("当前：顺序播放；点击切换为随机播放"));
    m_mode->setAccessibleName(m_mode->text());
}

void PlaybackDock::setPinned(bool pinned)
{
    const bool changed = m_pinned != pinned;
    m_pinned = pinned;
    m_leaveTimer->stop();
    {
        const QSignalBlocker blockedSignals(m_pin);
        m_pin->setChecked(pinned);
        m_pin->setText(pinned ? QStringLiteral("已固定") : QStringLiteral("固定"));
        m_pin->setToolTip(pinned ? QStringLiteral("点击恢复移开后自动收起") : QStringLiteral("固定播放控制，移开鼠标也保持显示"));
    }
    if (pinned)
        setExpanded(true);
    if (changed && onPinnedChanged)
        onPinnedChanged(pinned);
}

void PlaybackDock::setInteractionHeld(bool held)
{
    m_interactionHeld = held;
    if (held) {
        m_leaveTimer->stop();
        setExpanded(true);
    }
}

bool PlaybackDock::isExpanded() const { return m_expanded; }
bool PlaybackDock::isPinned() const { return m_pinned; }
bool PlaybackDock::isShuffle() const { return m_shuffle; }
QSlider *PlaybackDock::progressSlider() const { return m_progress; }
QSlider *PlaybackDock::volumeSlider() const { return m_volumeSlider; }
JellyButton *PlaybackDock::muteButton() const { return m_mute; }
JellyButton *PlaybackDock::playPauseButton() const { return m_playPause; }
QLabel *PlaybackDock::titleLabel() const { return m_title; }

QRect PlaybackDock::targetPanelRect() const
{
    const int preferredWidth = std::clamp(m_host->width() - 96, 900, 1080);
    const int width = std::min(preferredWidth, std::max(1, m_host->width() - 32));
    const int height = std::min(156, std::max(1, m_host->height() - 24));
    return QRect((m_host->width() - width) / 2, m_host->height() - height - 14, width, height);
}

QRect PlaybackDock::hiddenPanelRect() const
{
    QRect panel = targetPanelRect();
    panel.moveTop(m_host->height() + 12);
    return panel;
}

bool PlaybackDock::hasInteraction() const
{
    if (m_interactionHeld || m_colorRequestActive || m_meteor->isRunning() || m_dragging || m_progress->isSliderDown()
        || m_volumeSlider->isSliderDown()
        || m_keyboardTimer->isActive() || QApplication::activePopupWidget())
        return true;
    for (const JellyButton *button : {m_playPause, m_previous, m_next, m_mode, m_pin, m_palette, m_mute, m_songPage, m_lyrics})
        if (button->isDown())
            return true;
    return false;
}

void PlaybackDock::setExpanded(bool expanded)
{
    m_leaveTimer->stop();
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    if (!expanded) {
        QWidget *focus = QApplication::focusWidget();
        if (focus && isAncestorOf(focus))
            focus->clearFocus();
    }
    animateToTarget();
}

void PlaybackDock::animateToTarget()
{
    m_slide->stop();
    if (m_expanded) {
        show();
        raise();
    }
    m_slide->setStartValue(geometry());
    m_slide->setEndValue(m_expanded ? targetPanelRect() : hiddenPanelRect());
    m_slide->start();
}

void PlaybackDock::adjustToHost()
{
    const bool moving = m_slide->state() == QAbstractAnimation::Running;
    m_slide->stop();
    const QRect destination = m_expanded ? targetPanelRect() : hiddenPanelRect();
    if (moving) {
        QRect current = geometry();
        current.setSize(destination.size());
        current.moveLeft(destination.left());
        current.moveTop(std::clamp(current.top(), targetPanelRect().top(), hiddenPanelRect().top()));
        setGeometry(current);
        animateToTarget();
    } else {
        setGeometry(destination);
        if (m_expanded)
            raise();
    }
}

void PlaybackDock::updatePointer(const QPoint &hostPosition, bool insideWindow)
{
    // The main window can have a status-bar footer below the player-page host.
    // insideWindow is checked by pollPointer against the complete window bounds.
    const bool atEdge = insideWindow && hostPosition.x() >= 0 && hostPosition.x() < m_host->width()
        && hostPosition.y() >= m_host->height() - 16;
    const bool overPanel = insideWindow && targetPanelRect().contains(hostPosition);
    if (atEdge) {
        setExpanded(true);
        return;
    }
    if (!m_expanded || m_pinned || overPanel || hasInteraction()) {
        m_leaveTimer->stop();
        return;
    }
    if (!m_leaveTimer->isActive())
        m_leaveTimer->start();
}

void PlaybackDock::pollPointer()
{
    if (!m_host->isVisible())
        return;
    QWidget *hostWindow = m_host->window();
    if (!hostWindow->isActiveWindow()) {
        m_leaveTimer->stop();
        if (!m_pinned && !hasInteraction())
            setExpanded(false);
        return;
    }
    const QPoint position = QCursor::pos();
    updatePointer(m_host->mapFromGlobal(position), hostWindow->rect().contains(hostWindow->mapFromGlobal(position)));
}

bool PlaybackDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            adjustToHost();
        else if (event->type() == QEvent::DevicePixelRatioChange)
            updateVolumeDisplay();
        else if (event->type() == QEvent::Hide) {
            m_leaveTimer->stop();
            if (!m_pinned && !hasInteraction())
                setExpanded(false);
        }
    } else if ((watched == m_progress || watched == m_volumeSlider || watched == m_mute)
               && (event->type() == QEvent::KeyPress || event->type() == QEvent::Wheel)) {
        m_keyboardTimer->start();
        m_leaveTimer->stop();
    }
    return QWidget::eventFilter(watched, event);
}

void PlaybackDock::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Liquid::paintBackdrop(painter, this, 26);
    Aero::paintGlass(painter, QRectF(rect()).adjusted(2, 2, -2, -2), m_accent, 26);
}

void PlaybackDock::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTransportLayout();
}
