#pragma once

#include "music_domain.h"
#include <QColor>
#include <QDateTime>
#include <QPixmap>
#include <QWidget>
#include <functional>

class HomeRecommendationSection;
class QLabel;
class QTimer;

// Daily recommendations are supplied by the controller; empty/loading states
// never substitute example tracks for the user's real platform recommendations.
class HomePage final : public QWidget {
public:
    explicit HomePage(QWidget *parent = nullptr);
    // Runtime gating keeps the shared widget library usable by development
    // tests while the public application exposes its released platforms only.
    void setKugouOnly(bool enabled);
    void setAccentColor(const QColor &color);
    void setRecommendations(MusicPlatform platform, const QVector<Track> &tracks);
    void setRecommendationState(MusicPlatform platform, const QString &message, bool loading = false);
    void setCover(MusicPlatform platform, int index, const QPixmap &cover);
    void refreshClock(const QDateTime &now = QDateTime::currentDateTime());
    static QString utcOffsetText(const QDateTime &now);
    QLabel *clockLabel() const;
    QLabel *dateLabel() const;
    QLabel *timeZoneLabel() const;

    std::function<void(MusicPlatform, int)> onRecommendationActivated;
    std::function<void(MusicPlatform)> onConnectRequested;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    HomeRecommendationSection *section(MusicPlatform platform) const;
    QLabel *m_clock;
    QLabel *m_date;
    QLabel *m_year;
    QLabel *m_timezone;
    QTimer *m_clockTimer;
    QWidget *m_clockCard;
    HomeRecommendationSection *m_kugou;
    HomeRecommendationSection *m_netease;
    QColor m_accent;
    bool m_kugouOnly = false;
};
