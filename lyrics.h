#pragma once

#include "lyric_domain.h"

// Supports ordinary LRC (including multiple timestamps and offset metadata)
// and already-decoded KRC line timestamps. It never decompresses remote data.
TrackLyrics parseLyrics(const QString &source, const QString &translation = {});
