#pragma once

#include "lyric_domain.h"
#include "music_domain.h"
#include <QColor>
#include <QPixmap>
#include <QWidget>
#include <functional>

class GlassRecord;
class JellyButton;
class QLabel;
class QListWidget;
class QStackedWidget;
class QVariantAnimation;

class SongPage final : public QWidget {
public:
    explicit SongPage(QWidget *parent = nullptr);
    ~SongPage() override;
    void setAccentColor(const QColor &color);
    void setTrack(const Track &track, MusicPlatform platform = MusicPlatform::Kugou);
    // Keep the downloaded original image. The central label is up to 302 logical
    // pixels across; supply artwork at 600 to 1200px for high-DPI displays.
    void setCover(const QPixmap &original);
    void setPlaying(bool playing);
    void setPosition(qint64 position);
    void setSeekable(bool seekable);
    void setLyrics(const QVector<TimedLyricLine> &lines, const QString &emptyMessage = {});
    void focusLyrics();
    QString currentLyric() const;
    int currentLyricIndex() const;
    qreal recordAngle() const;
    bool rotationActive() const;
    QSize coverSourceSize() const;
    QListWidget *lyricsList() const;

    std::function<void()> onBackRequested;
    std::function<void(qint64)> onSeekRequested;
    std::function<void(const QString &)> onCurrentLyricChanged;

private:
    void updateActiveLyric();
    void centerActiveLyric(bool animate);
    GlassRecord *m_record;
    QLabel *m_title;
    QLabel *m_artist;
    QLabel *m_album;
    QLabel *m_platform;
    QLabel *m_lyricsMessage;
    QStackedWidget *m_lyricsContent;
    QListWidget *m_lyrics;
    JellyButton *m_back;
    QVariantAnimation *m_scroll;
    QWidget *m_recordCard;
    QWidget *m_lyricsCard;
    QVector<TimedLyricLine> m_lines;
    QColor m_accent;
    qint64 m_position = 0;
    int m_activeLine = -1;
    bool m_seekable = false;
};
