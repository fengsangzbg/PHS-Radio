#include "music_search.h"

#include <QCache>
#include <QHash>
#include <QRegularExpression>
#include <unicode/translit.h>
#include <unicode/uscript.h>
#include <algorithm>
#include <memory>

namespace {

QString transform(const QString &text, icu::Transliterator *transliterator)
{
    if (!transliterator)
        return text;
    icu::UnicodeString value(reinterpret_cast<const UChar *>(text.utf16()), text.size());
    transliterator->transliterate(value);
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(value.getBuffer()), value.length());
}

QString compact(const QString &text)
{
    const QString folded = text.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString result;
    result.reserve(folded.size());
    for (char32_t character : folded.toUcs4())
        if (QChar::isLetterOrNumber(character))
            result += QString::fromUcs4(&character, 1);
    return result;
}

class SearchText final {
public:
    SearchText() : m_cache(65536)
    {
        UErrorCode status = U_ZERO_ERROR;
        m_simplifier.reset(icu::Transliterator::createInstance("Traditional-Simplified", UTRANS_FORWARD, status));
        status = U_ZERO_ERROR;
        m_pinyin.reset(icu::Transliterator::createInstance(
            "Traditional-Simplified; Han-Latin; Latin-ASCII; Lower", UTRANS_FORWARD, status));
    }

    QString normalize(const QString &text) const
    {
        return compact(transform(text.normalized(QString::NormalizationForm_KC), m_simplifier.get()));
    }

    QString aliasSource(const QString &text) const
    {
        return transform(text.normalized(QString::NormalizationForm_KC), m_simplifier.get()).toCaseFolded();
    }

    QStringList forms(const QString &text)
    {
        if (const auto *cached = m_cache.object(text))
            return *cached;
        QStringList result;
        const QString normalized = normalize(text);
        if (!normalized.isEmpty())
            result.append(normalized);
        const auto characters = text.toUcs4();
        const bool hasHan = std::any_of(characters.cbegin(), characters.cend(), [](char32_t character) {
            return character >= 0x3400 && u_getIntPropertyValue(character, UCHAR_SCRIPT) == USCRIPT_HAN;
        });
        if (hasHan) {
            const QString pinyin = compact(transform(text, m_pinyin.get()));
            if (!pinyin.isEmpty())
                result.append(pinyin);
            QString initials;
            bool latinWord = false;
            for (char32_t character : characters) {
                if (u_getIntPropertyValue(character, UCHAR_SCRIPT) == USCRIPT_HAN) {
                    auto reading = m_readings.constFind(character);
                    if (reading == m_readings.cend())
                        reading = m_readings.insert(character,
                            compact(transform(QString::fromUcs4(&character, 1), m_pinyin.get())));
                    if (!reading.value().isEmpty())
                        initials += reading.value().front();
                    latinWord = false;
                } else if (character < 128 && QChar::isLetterOrNumber(character)) {
                    if (!latinWord)
                        initials += QChar(static_cast<char16_t>(character)).toLower();
                    latinWord = true;
                } else {
                    latinWord = false;
                }
            }
            if (!initials.isEmpty())
                result.append(initials);
        }
        result.removeDuplicates();
        m_cache.insert(text, new QStringList(result));
        return result;
    }

private:
    std::unique_ptr<icu::Transliterator> m_simplifier;
    std::unique_ptr<icu::Transliterator> m_pinyin;
    QCache<QString, QStringList> m_cache;
    QHash<char32_t, QString> m_readings;
};

SearchText &searchText()
{
    thread_local SearchText text;
    return text;
}

enum class AliasScope { Artist, Title };

struct AliasGroup {
    AliasScope scope;
    QStringList names;
};

// Verified name equivalences, used for searching without rewriting platform metadata.
// Keep artists and song titles separate: an album can share one song's title.
const QVector<AliasGroup> &aliasGroups()
{
    static const QVector<AliasGroup> groups{
        // Crypton's official Chinese and Japanese character pages:
        // https://piapro.net/intl/zh-hans_character.html
        // https://piapro.net/pages/character
        {AliasScope::Artist, {QStringLiteral("初音未来"), QStringLiteral("初音未來"), QStringLiteral("初音ミク"),
            QStringLiteral("Hatsune Miku"), QStringLiteral("Miku Hatsune")}},
        {AliasScope::Artist, {QStringLiteral("镜音铃"), QStringLiteral("鏡音鈴"),
            QStringLiteral("鏡音リン"), QStringLiteral("Kagamine Rin")}},
        // The published EULA also gives the Chinese spelling "鏡音蓮":
        // https://ec.crypton.co.jp/download/pdf/eula_virtualsingertry.pdf
        {AliasScope::Artist, {QStringLiteral("镜音连"), QStringLiteral("鏡音連"),
            QStringLiteral("镜音莲"), QStringLiteral("鏡音蓮"),
            QStringLiteral("鏡音レン"), QStringLiteral("Kagamine Len")}},
        {AliasScope::Artist, {QStringLiteral("巡音流歌"), QStringLiteral("巡音ルカ"),
            QStringLiteral("Megurine Luka")}},
        // The artist's own Chinese upload identifies both Chinese and Japanese names:
        // https://www.bilibili.com/video/BV1oW411Y7sm/
        // His label supplies the English artist and song names:
        // https://www.universal-music.co.jp/harumaki-gohan/products/um1as-00691/
        {AliasScope::Artist, {QStringLiteral("春卷饭"), QStringLiteral("春卷飯"),
            QStringLiteral("はるまきごはん"), QStringLiteral("Harumaki Gohan")}},
        {AliasScope::Title, {QStringLiteral("无梦之梦"), QStringLiteral("無夢之夢"),
            QStringLiteral("ドリームレス・ドリームス"), QStringLiteral("Dreamless Dreams")}},
        // User-supplied Chinese search name; original and English names verified
        // against the composer's own MV and the KARENT release catalogue:
        // https://www.youtube.com/watch?v=IGu9FL_fQnA
        // https://karent.jp/album/4158
        {AliasScope::Title, {QStringLiteral("合成的未来"), QStringLiteral("合成的未來"),
            QStringLiteral("合成するミライ"), QStringLiteral("Synthesize You")}}
    };
    return groups;
}

bool aliasMatches(const QString &alias, const QStringList &source, const SearchText &text)
{
    thread_local QHash<QString, QRegularExpression> patterns;
    const QString canonical = text.aliasSource(alias);
    auto pattern = patterns.constFind(canonical);
    if (pattern == patterns.cend()) {
        static const QRegularExpression separators(QStringLiteral("[\\s\\p{P}\\p{S}]+"));
        QStringList words = canonical.split(separators, Qt::SkipEmptyParts);
        for (QString &word : words)
            word = QRegularExpression::escape(word);
        const QString expression = QStringLiteral("(?<![\\p{L}\\p{N}\\p{M}])(?:%1)(?![\\p{L}\\p{N}\\p{M}])")
            .arg(words.join(QStringLiteral("[\\s\\p{P}\\p{S}]*")));
        pattern = patterns.insert(canonical, QRegularExpression(expression));
    }
    return std::any_of(source.cbegin(), source.cend(), [&](const QString &name) {
        return pattern.value().match(text.aliasSource(name)).hasMatch();
    });
}

} // namespace

QVector<Track> mergeLibrarySearchMatches(const QVector<Track> &source, const QVector<Track> &localMatches,
                                        const QVector<Track> &platformMatches)
{
    QSet<QString> localKeys, audioIds, hashes;
    for (const Track &track : localMatches)
        localKeys.insert(trackKey(track));
    for (const Track &track : platformMatches) {
        if (!track.albumAudioId.isEmpty() && track.albumAudioId != QStringLiteral("0"))
            audioIds.insert(track.albumAudioId);
        if (!track.hash.isEmpty() && track.hash != QStringLiteral("0"))
            hashes.insert(track.hash.toLower());
    }
    QVector<Track> results;
    for (const Track &track : source) {
        if (localKeys.contains(trackKey(track)) || audioIds.contains(track.albumAudioId)
            || (!track.hash.isEmpty() && hashes.contains(track.hash.toLower())))
            results.append(track);
    }
    return results;
}

MusicSearchIndex::MusicSearchIndex(QVector<Track> tracks)
{
    m_entries.reserve(tracks.size());
    auto &text = searchText();
    for (Track &track : tracks) {
        QStringList source{track.title, track.artist, track.album};
        source += track.searchAliases;
        source.removeDuplicates();
        QStringList names;
        for (const QString &name : source) {
            const auto forms = text.forms(name);
            names += forms;
        }
        QStringList artistSource{track.artist};
        // Some release titles carry an explicit featured singer while artist
        // contains only the composer. Read that credit without treating arbitrary
        // mentions of a singer in a title as performer metadata.
        static const QRegularExpression featuredCredit(
            QStringLiteral("(?:^|[\\s\\p{P}])(?:feat(?:uring)?\\.?|ft\\.?)[\\s:：]*([^\\)\\]】）]+)"),
            QRegularExpression::CaseInsensitiveOption);
        auto credits = featuredCredit.globalMatch(track.title);
        while (credits.hasNext())
            artistSource.append(credits.next().captured(1).trimmed());
        for (const auto &group : aliasGroups()) {
            QStringList scopedSource = group.scope == AliasScope::Artist ? artistSource : QStringList{track.title};
            scopedSource += track.searchAliases;
            QStringList originalNames;
            for (const QString &name : scopedSource) {
                const auto forms = text.forms(name);
                if (!forms.isEmpty())
                    originalNames.append(forms.first());
            }
            const bool named = std::any_of(group.names.cbegin(), group.names.cend(), [&](const QString &alias) {
                const QString normalized = text.forms(alias).first();
                const bool present = std::any_of(originalNames.cbegin(), originalNames.cend(), [&](const QString &name) {
                    return name.contains(normalized);
                });
                return present && aliasMatches(alias, scopedSource, text);
            });
            if (named)
                for (const QString &alias : group.names)
                    names += text.forms(alias);
        }
        names.removeDuplicates();
        m_entries.append({std::move(track), std::move(names)});
    }
}

QVector<Track> MusicSearchIndex::search(const QString &query) const
{
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    const auto words = query.split(spaces, Qt::SkipEmptyParts);
    QStringList terms;
    for (const QString &word : words) {
        const QString term = searchText().normalize(word);
        if (!term.isEmpty())
            terms.append(term);
    }
    // A query consisting only of punctuation should not match the entire library.
    if (terms.isEmpty() && !query.trimmed().isEmpty())
        return {};
    QVector<Track> matches;
    for (const Entry &entry : m_entries) {
        const bool matched = std::all_of(terms.cbegin(), terms.cend(), [&](const QString &term) {
            return std::any_of(entry.names.cbegin(), entry.names.cend(), [&](const QString &name) {
                return name.contains(term);
            });
        });
        if (matched)
            matches.append(entry.track);
    }
    return matches;
}
