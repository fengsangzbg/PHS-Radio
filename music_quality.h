#pragma once

#include <QString>

enum class AudioQuality { Standard = 0, High = 1, Lossless = 2 };

inline AudioQuality audioQualityFromSetting(int value)
{
    return value >= 0 && value <= 2 ? static_cast<AudioQuality>(value) : AudioQuality::Standard;
}

inline QString audioQualityName(AudioQuality quality)
{
    switch (quality) {
    case AudioQuality::High: return QStringLiteral("高品质");
    case AudioQuality::Lossless: return QStringLiteral("无损");
    default: return QStringLiteral("标准");
    }
}

inline QString neteaseQualityLevel(AudioQuality quality)
{
    switch (quality) {
    case AudioQuality::High: return QStringLiteral("exhigh");
    case AudioQuality::Lossless: return QStringLiteral("lossless");
    default: return QStringLiteral("standard");
    }
}
