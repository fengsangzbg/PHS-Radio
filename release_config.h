#pragma once

#include "music_domain.h"

// This macro is defined on the public application target only. Internal linkage
// keeps development test/library translation units independent of that target.
namespace ReleaseConfig {

#if defined(PHSRADIO_KUGOU_ONLY) && PHSRADIO_KUGOU_ONLY
static constexpr bool KugouOnly = true;
#else
static constexpr bool KugouOnly = false;
#endif

#if defined(PHSRADIO_KUGOU_NETEASE) && PHSRADIO_KUGOU_NETEASE
static constexpr bool DualPlatformRelease = true;
#else
static constexpr bool DualPlatformRelease = false;
#endif

static constexpr bool platformAvailable(MusicPlatform platform)
{
    return KugouOnly ? platform == MusicPlatform::Kugou
        : !DualPlatformRelease || platform == MusicPlatform::Kugou || platform == MusicPlatform::NetEaseCloud;
}

static inline QVector<MusicPlatform> availablePlatforms()
{
    if (KugouOnly) return {MusicPlatform::Kugou};
    if (DualPlatformRelease) return {MusicPlatform::Kugou, MusicPlatform::NetEaseCloud};
    return {MusicPlatform::QQMusic, MusicPlatform::NetEaseCloud, MusicPlatform::Kugou};
}

static inline QVector<MusicPlatform> filterPlatforms(const QVector<MusicPlatform> &platforms)
{
    QVector<MusicPlatform> enabled;
    for (MusicPlatform platform : platforms)
        if (platformAvailable(platform) && !enabled.contains(platform)) enabled.append(platform);
    return enabled;
}

} // namespace ReleaseConfig
