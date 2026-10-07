#pragma once

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QWidget>
#include <functional>

class JellyButton;
class QLabel;
class QPropertyAnimation;
class QSlider;
class QTimer;
class PlaybackMeteor;

class PlaybackDock final : public QWidget {
public:
    explicit PlaybackDock(QWidget *host);

    void setPlaying(bool playing);
    void setBusy(bool busy);
    void setTrack(const QString &title, const QString &artist);
    void setCurrentLyric(const QString &text);
    void setPosition(qint64 position);
    void setDuration(qint64 duration);
    void setSeekable(bool seekable);
    void setAccentColor(const QColor &color);
    void setShuffle(bool shuffle);
    void setExpanded(bool expanded);
    void setPinned(bool pinned);
    void setInteractionHeld(bool held);
    bool isExpanded() const;
    bool isPinned() const;
    bool isShuffle() const;
    void updatePointer(const QPoint &hostPosition, bool insideWindow);
    void triggerPlaybackMeteor();

    QSlider *progressSlider() const;
    JellyButton *playPauseButton() const;
    QLabel *titleLabel() const;
    QString currentLyric() const;
    JellyButton *songPageButton() const;
    JellyButton *lyricsButton() const;

    std::function<void()> onPlayPause;
    std::function<void()> onPrevious;
    std::function<void()> onNext;
    std::function<void(bool)> onShuffleChanged;
    std::function<void(bool)> onPinnedChanged;
    std::function<void(qint64)> onSeekRequested;
    std::function<void()> onColorRequested;
    std::function<void()> onSongPageRequested;
    std::function<void()> onLyricsRequested;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QRect targetPanelRect() const;
    QRect hiddenPanelRect() const;
    bool hasInteraction() const;
    void adjustToHost();
    void animateToTarget();
    void pollPointer();
    void updateTimeLabels();
    void updatePlayButton();
    void updateSeekEnabled();
    void requestSeek(int value);
    qint64 positionForValue(int value) const;
    int valueForPosition(qint64 position) const;

    QWidget *m_host;
    QLabel *m_title;
    QLabel *m_artist;
    QLabel *m_elapsed;
    QLabel *m_total;
    QSlider *m_progress;
    JellyButton *m_playPause;
    JellyButton *m_previous;
    JellyButton *m_next;
    JellyButton *m_mode;
    JellyButton *m_pin;
    JellyButton *m_palette;
    JellyButton *m_songPage;
    JellyButton *m_lyrics;
    QPropertyAnimation *m_slide;
    QTimer *m_pointerTimer;
    QTimer *m_leaveTimer;
    QTimer *m_keyboardTimer;
    PlaybackMeteor *m_meteor;
    QColor m_accent;
    QString m_trackArtist;
    QString m_currentLyric;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_expanded = false;
    bool m_pinned = false;
    bool m_shuffle = false;
    bool m_playing = false;
    bool m_busy = false;
    bool m_seekable = false;
    bool m_dragging = false;
    bool m_syncingProgress = false;
    bool m_interactionHeld = false;
    bool m_colorRequestActive = false;
};
