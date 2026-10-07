#include "../music_search.h"

#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>

namespace {

void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

Track song(const QString &id, const QString &title, const QString &artist,
           const QString &album = {}, const QStringList &aliases = {})
{
    Track track;
    track.id = id;
    track.albumAudioId = id;
    track.title = title;
    track.artist = artist;
    track.album = album;
    track.searchAliases = aliases;
    return track;
}

void expectIds(const MusicSearchIndex &index, const QString &query,
               QStringList expected, const char *message)
{
    QStringList actual;
    for (const Track &track : index.search(query))
        actual.append(track.id);
    actual.sort();
    expected.sort();
    if (actual != expected)
        qCritical() << "Query:" << query << "Expected:" << expected << "Actual:" << actual;
    check(actual == expected, message);
}

void verifyNamesAndQueries()
{
    const QVector<Track> tracks{
        song(QStringLiteral("miku-japanese"), QStringLiteral("ロミオとシンデレラ"),
             QStringLiteral("初音ミク"), QStringLiteral("Vocaloid Collection"),
             {QStringLiteral("罗密欧与灰姑娘"), QStringLiteral("Romeo and Cinderella")}),
        song(QStringLiteral("miku-english"), QStringLiteral("World Is Mine"),
             QStringLiteral("Hatsune Miku"), QStringLiteral("Vocaloid Collection"),
             {QStringLiteral("世界第一的公主殿下")}),
        song(QStringLiteral("miku-traditional"), QStringLiteral("千本櫻"),
             QStringLiteral("初音未來"), QStringLiteral("Vocaloid Collection")),
        song(QStringLiteral("other-miku"), QStringLiteral("Another Singer's Song"),
             QStringLiteral("Miku Ito")),
        song(QStringLiteral("jay"), QStringLiteral("晴天"), QStringLiteral("周杰伦"),
             QStringLiteral("叶惠美")),
        song(QStringLiteral("cover"), QStringLiteral("晴天"), QStringLiteral("另一位歌手")),
        song(QStringLiteral("accent"), QStringLiteral("Café Déjà-Vu (Live)"),
             QStringLiteral("Beyoncé"), QStringLiteral("ＲＡＤＩＯ．ＴＯＫＹＯ"))
    };
    const MusicSearchIndex index(tracks);
    const QStringList mikuIds{QStringLiteral("miku-japanese"), QStringLiteral("miku-english"),
                             QStringLiteral("miku-traditional")};
    for (const QString &query : {QStringLiteral("初音未来"), QStringLiteral("初音未來"),
                                QStringLiteral("初音ミク"), QStringLiteral("Hatsune Miku"),
                                QStringLiteral("Miku Hatsune")})
        expectIds(index, query, mikuIds, "Artist aliases must identify the same singer without Miku Ito.");

    expectIds(index, QStringLiteral("初音未来 罗密欧"), {QStringLiteral("miku-japanese")},
              "Multiple words must match artist and song aliases together.");
    expectIds(index, QStringLiteral("hatsune CINDERELLA"), {QStringLiteral("miku-japanese")},
              "Artist aliases and translated song aliases must combine case-insensitively.");
    expectIds(index, QStringLiteral("罗密欧与灰姑娘"), {QStringLiteral("miku-japanese")},
              "Explicit Chinese metadata aliases must be searchable.");
    expectIds(index, QStringLiteral("Romeo and Cinderella"), {QStringLiteral("miku-japanese")},
              "Explicit English metadata aliases must be searchable.");
    expectIds(index, QStringLiteral("初音未来 千本樱"), {QStringLiteral("miku-traditional")},
              "Simplified queries must find traditional artist and song names.");
    expectIds(index, QStringLiteral("初音未来 晴天"), {},
              "Every query word must match the same recording.");
    expectIds(index, QStringLiteral("chu yin wei lai"), mikuIds,
              "Spaced full pinyin must find the singer aliases.");
    expectIds(index, QStringLiteral("chuyinweilai"), mikuIds,
              "Unspaced full pinyin must find the singer aliases.");
    expectIds(index, QStringLiteral("cywl"), mikuIds,
              "Initials must preserve both characters in the word future.");
    expectIds(index, QStringLiteral("zjl"), {QStringLiteral("jay")},
              "Chinese singer initials must be searchable.");
    expectIds(index, QStringLiteral("zhoujielun"), {QStringLiteral("jay")},
              "Full Chinese singer pinyin must be searchable.");
    expectIds(index, QStringLiteral("周杰伦 晴天"), {QStringLiteral("jay")},
              "Song and artist words must exclude other recordings with the same title.");
    expectIds(index, QStringLiteral("cafe deja vu beyonce"), {QStringLiteral("accent")},
              "Accents and punctuation must not require exact spelling.");
    expectIds(index, QStringLiteral("ＣＡＦＥ́ DEJA—VU"), {QStringLiteral("accent")},
              "Full-width text and alternate punctuation must normalize with accents and case.");
    expectIds(index, QStringLiteral("radio tokyo"), {QStringLiteral("accent")},
              "Album queries must normalize full-width punctuation.");
    check(index.search(QString()).size() == tracks.size(), "An empty query must return the whole library.");
    check(index.search(QStringLiteral(" \t \n ")).size() == tracks.size(),
          "A whitespace-only query must return the whole library.");
    check(index.search(QStringLiteral("a singer absent from this library")).isEmpty(),
          "A query with no matching recording must return no results.");
}

void verifySimilarNamesDoNotGainAliases()
{
    const MusicSearchIndex index({
        song(QStringLiteral("miku"), QStringLiteral("Original Singer"), QStringLiteral("Hatsune Miku")),
        song(QStringLiteral("mikuo"), QStringLiteral("Another Character"), QStringLiteral("Hatsune Mikuo")),
        song(QStringLiteral("mikuru"), QStringLiteral("Another Character"), QStringLiteral("Hatsune Mikuru"))
    });
    // Similar English names can match English substrings, but must not acquire
    // the Chinese, Japanese, or pinyin aliases of a different singer.
    for (const QString &query : {QStringLiteral("初音未来"), QStringLiteral("初音ミク"), QStringLiteral("cywl")})
        expectIds(index, query, {QStringLiteral("miku")},
                  "Similar artist names must not inherit another artist's aliases.");
}

void verifyJapaneseMusicTranslations()
{
    const QVector<Track> tracks{
        song(QStringLiteral("dream-vocal"), QStringLiteral("ドリームレス・ドリームス"),
             QStringLiteral("はるまきごはん feat.初音ミク"), QStringLiteral("ネオドリームトラベラー")),
        song(QStringLiteral("dream-self"), QStringLiteral("ドリームレス・ドリームス (セルフカバー)"),
             QStringLiteral("HarumakiGohan")),
        song(QStringLiteral("dream-cover"), QStringLiteral("Dreamless Dreams"),
             QStringLiteral("Example Cover Singer")),
        song(QStringLiteral("same-album"), QStringLiteral("Another Track"),
             QStringLiteral("Harumaki Gohan"), QStringLiteral("ドリームレス・ドリームス")),
        song(QStringLiteral("synthesize-original"), QStringLiteral("合成するミライ (feat. 初音ミク)"),
             QStringLiteral("阿修")),
        song(QStringLiteral("synthesize-english"), QStringLiteral("Synthesize You"), QStringLiteral("阿修")),
        song(QStringLiteral("synthesize-other-title"), QStringLiteral("合成するミライの続編"),
             QStringLiteral("Another Composer")),
        song(QStringLiteral("dream-other-title"), QStringLiteral("ドリームレス・ドリームスの続編"),
             QStringLiteral("Another Composer")),
        song(QStringLiteral("similar-artist"), QStringLiteral("Another Song"),
             QStringLiteral("Harumaki Gohanette")),
        song(QStringLiteral("artist-mentioned-title"), QStringLiteral("はるまきごはんの物語"),
             QStringLiteral("Another Composer")),
        song(QStringLiteral("rin"), QStringLiteral("Rin Song"), QStringLiteral("鏡音リン")),
        song(QStringLiteral("len"), QStringLiteral("Len Song"), QStringLiteral("鏡音レン")),
        song(QStringLiteral("luka"), QStringLiteral("Luka Song"), QStringLiteral("巡音ルカ"))
    };
    const MusicSearchIndex index(tracks);
    const QStringList harumakiIds{QStringLiteral("dream-vocal"), QStringLiteral("dream-self"),
                                 QStringLiteral("same-album")};
    for (const QString &query : {QStringLiteral("春卷饭"), QStringLiteral("春卷飯")})
        expectIds(index, query, harumakiIds,
                  "The producer's verified Chinese and Japanese names must find all credited songs.");
    expectIds(index, QStringLiteral("はるまきごはん"),
              {QStringLiteral("dream-vocal"), QStringLiteral("dream-self"), QStringLiteral("same-album"),
               QStringLiteral("artist-mentioned-title")},
              "Original-name queries must retain ordinary title substring matching.");
    expectIds(index, QStringLiteral("春卷饭 无梦之梦"),
              {QStringLiteral("dream-vocal"), QStringLiteral("dream-self")},
              "Producer and translated song queries must combine without unrelated album tracks or covers.");
    for (const QString &query : {QStringLiteral("无梦之梦"), QStringLiteral("無夢之夢"),
                                QStringLiteral("Dreamless Dreams")})
        expectIds(index, query,
                  {QStringLiteral("dream-vocal"), QStringLiteral("dream-self"), QStringLiteral("dream-cover")},
                  "Verified song translations must find originals and covers while retaining separate recordings.");
    for (const QString &query : {QStringLiteral("合成的未来"), QStringLiteral("合成的未來"),
                                QStringLiteral("Synthesize You")})
        expectIds(index, query,
                  {QStringLiteral("synthesize-original"), QStringLiteral("synthesize-english")},
                  "The user's Chinese name and the composer's official English name must find the Japanese title.");
    expectIds(index, QStringLiteral("初音未来 合成的未来"), {QStringLiteral("synthesize-original")},
              "Featured singer metadata in a title must remain searchable with translated song names.");
    expectIds(index, QStringLiteral("镜音铃"), {QStringLiteral("rin")},
              "Official Chinese character names must find their Japanese names.");
    expectIds(index, QStringLiteral("Kagamine Rin"), {QStringLiteral("rin")},
              "Official English character names must find their Japanese names.");
    expectIds(index, QStringLiteral("镜音连"), {QStringLiteral("len")},
              "Official Chinese names must distinguish the two Kagamine singers.");
    expectIds(index, QStringLiteral("镜音莲"), {QStringLiteral("len")},
              "Published alternate Chinese character spelling must be searchable.");
    expectIds(index, QStringLiteral("巡音流歌"), {QStringLiteral("luka")},
              "Official Chinese Luka names must find the Japanese name.");
    expectIds(index, QStringLiteral("Megurine Luka"), {QStringLiteral("luka")},
              "Official English Luka names must find the Japanese name.");
    const auto dream = index.search(QStringLiteral("春卷饭 无梦之梦"));
    check(dream.first().title == tracks.first().title && dream.first().artist == tracks.first().artist
              && dream.first().albumAudioId == tracks.first().albumAudioId,
          "Search aliases must preserve displayed platform names and recording identifiers.");
}

void verifyAliasesSurvivePlaylistDeduplication()
{
    Track firstCopy = song(QStringLiteral("shared"), QStringLiteral("Moon Song"),
                           QStringLiteral("Example Singer"), {}, {QStringLiteral("Moon Song")});
    Track enrichedCopy = firstCopy;
    enrichedCopy.searchAliases = {QStringLiteral("月亮之歌"), QStringLiteral("Lunar Anthem")};
    const Track otherRecording = song(QStringLiteral("different-recording"), firstCopy.title,
                                      firstCopy.artist);
    Playlist first, second;
    first.tracks = {firstCopy};
    second.tracks = {enrichedCopy, otherRecording};
    const QVector<Track> combined = allPlaylistTracks({first, second});
    check(combined.size() == 2, "The same recording must merge while different recordings stay separate.");
    const MusicSearchIndex index(combined);
    expectIds(index, QStringLiteral("月亮之歌"), {QStringLiteral("shared")},
              "Later playlist metadata must contribute aliases to the merged recording.");
    expectIds(index, QStringLiteral("Lunar Anthem"), {QStringLiteral("shared")},
              "Later English aliases must survive playlist deduplication.");
    expectIds(index, QStringLiteral("Moon Song"),
              {QStringLiteral("shared"), QStringLiteral("different-recording")},
              "Searching cannot collapse separate recordings that share a displayed title.");
}

void verifyLargeLibraryRepeatedSearch()
{
    QVector<Track> tracks;
    tracks.reserve(12043);
    for (int row = 0; row < 12043; ++row)
        tracks.append(song(QStringLiteral("large-%1").arg(row), QStringLiteral("Archive Song %1").arg(row),
                           row % 17 == 0 ? QStringLiteral("初音ミク") : QStringLiteral("周杰伦"),
                           QStringLiteral("Performance Collection"),
                           {QStringLiteral("曲目 %1").arg(row), QStringLiteral("Archive Alias %1").arg(row)}));
    const MusicSearchIndex index(tracks);
    check(index.search(QString()).size() == 12043, "A large library must preserve every recording.");
    for (int pass = 0; pass < 32; ++pass) {
        expectIds(index, QStringLiteral("song 12042"), {QStringLiteral("large-12042")},
                  "Repeated searches must find the final recording in a large library.");
        expectIds(index, QStringLiteral("曲目 12042"), {QStringLiteral("large-12042")},
                  "Repeated alias searches must find the final recording in a large library.");
        check(!index.search(QStringLiteral("cywl")).isEmpty(),
              "Repeated pinyin searches must work across a large library.");
        check(index.search(QStringLiteral("never-a-matching-recording")).isEmpty(),
              "Repeated unsuccessful searches must return no results.");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Track known = song(QStringLiteral("known"), QStringLiteral("Original song"), QStringLiteral("Artist"));
    known.hash = QStringLiteral("AbCdEf");
    Track sameTitle = song(QStringLiteral("unrelated"), known.title, known.artist);
    Track platform;
    platform.hash = QStringLiteral("abcdef");
    platform.title = QStringLiteral("A translated name");
    Track unknown = song(QStringLiteral("not-in-library"), known.title, known.artist);
    const auto identified = mergeLibrarySearchMatches({known, known, sameTitle}, {}, {platform, unknown});
    check(identified.size() == 2 && identified.first().id == known.id && identified.last().id == known.id,
          "Platform lookup must match exact audio identity, preserve playlist repeats, and exclude outsiders/same-name recordings.");
    platform.hash.clear();
    platform.albumAudioId = sameTitle.albumAudioId;
    const auto combined = mergeLibrarySearchMatches({known, sameTitle}, {known}, {platform});
    check(combined.size() == 2 && combined.last().id == sameTitle.id,
          "Platform audio IDs must supplement local matches while keeping source order.");
    check(mergeLibrarySearchMatches({known}, {}, {platform}).isEmpty(),
          "A playlist-specific lookup cannot introduce tracks from other playlists.");
    verifyNamesAndQueries();
    verifySimilarNamesDoNotGainAliases();
    verifyJapaneseMusicTranslations();
    verifyAliasesSurvivePlaylistDeduplication();
    verifyLargeLibraryRepeatedSearch();
    return 0;
}
