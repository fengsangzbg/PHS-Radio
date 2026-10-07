#include "lyrics.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace {
TrackLyrics parseSource(const QString &source)
{
    TrackLyrics result;
    const QRegularExpression stamp(QStringLiteral("\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]"));
    const QRegularExpression krcStamp(QStringLiteral("^\\[(\\d+),(\\d+)\\]"));
    const QRegularExpression wordTiming(QStringLiteral("<\\d+,\\d+,\\d+>"));
    const QRegularExpression metadata(QStringLiteral("^\\[(?:ar|al|ti|au|by|re|ve|length|language|offset):"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression offsetTag(QStringLiteral("\\[offset:([+-]?\\d+)\\]"),
                                        QRegularExpression::CaseInsensitiveOption);
    qint64 offset = 0;
    const auto offsetMatch = offsetTag.match(source);
    if (offsetMatch.hasMatch())
        offset = offsetMatch.captured(1).toLongLong();
    QStringList plain;
    for (QString line : source.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || metadata.match(line).hasMatch())
            continue;
        QVector<qint64> timestamps;
        int textStart = 0;
        while (textStart < line.size()) {
            const auto time = stamp.match(line, textStart);
            if (!time.hasMatch() || time.capturedStart() != textStart)
                break;
            if (time.captured(2).toInt() >= 60)
                break;
            QString fraction = time.captured(3);
            while (fraction.size() < 3) fraction += QLatin1Char('0');
            const qint64 timeMs = (time.captured(1).toLongLong() * 60 + time.captured(2).toLongLong()) * 1000
                + fraction.left(3).toLongLong();
            timestamps.append(qMax<qint64>(0, timeMs - offset));
            textStart = time.capturedEnd();
        }
        if (timestamps.isEmpty()) {
            const auto krc = krcStamp.match(line);
            if (krc.hasMatch()) {
                timestamps.append(qMax<qint64>(0, krc.captured(1).toLongLong() - offset));
                textStart = krc.capturedEnd();
            }
        }
        QString text = line.mid(textStart).remove(wordTiming).trimmed();
        if (text.isEmpty() && timestamps.isEmpty())
            continue;
        if (!text.isEmpty()) plain.append(text);
        for (qint64 time : timestamps)
            result.lines.append({time, text, {}});
    }
    std::stable_sort(result.lines.begin(), result.lines.end(),
                     [](const TimedLyricLine &left, const TimedLyricLine &right) { return left.timeMs < right.timeMs; });
    QVector<TimedLyricLine> merged;
    for (const TimedLyricLine &line : result.lines) {
        if (!merged.isEmpty() && merged.last().timeMs == line.timeMs) {
            if (merged.last().text != line.text)
                merged.last().text += QLatin1Char('\n') + line.text;
        } else {
            merged.append(line);
        }
    }
    result.lines = std::move(merged);
    result.plainText = plain.join(QLatin1Char('\n'));
    result.synchronized = !result.lines.isEmpty();
    return result;
}
} // namespace

TrackLyrics parseLyrics(const QString &source, const QString &translation)
{
    TrackLyrics result = parseSource(source);
    if (!translation.isEmpty()) {
        const TrackLyrics translated = parseSource(translation);
        QHash<qint64, QString> translatedLines;
        for (const TimedLyricLine &line : translated.lines)
            translatedLines.insert(line.timeMs, line.text);
        for (TimedLyricLine &line : result.lines)
            line.translation = translatedLines.value(line.timeMs);
    }
    return result;
}
