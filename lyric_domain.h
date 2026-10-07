#pragma once

#include <QString>
#include <QVector>

struct TimedLyricLine {
    qint64 timeMs = 0;
    QString text;
    QString translation;
};

struct TrackLyrics {
    QVector<TimedLyricLine> lines;
    QString plainText;
    bool synchronized = false;
};
