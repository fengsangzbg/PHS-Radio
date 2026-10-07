#include "../lyrics.h"

#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>

namespace {
void check(bool condition, const char *message)
{
    if (!condition) { qCritical("%s", message); std::abort(); }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto timed = parseLyrics(QStringLiteral(
        "[ar:metadata]\r\n[offset:100]\n[00:02.12]Second\n"
        "[00:01.1][00:03.123]Repeat\n[00:04.000]\n"
        "[00:02.12]Harmony\n[00:05]Text [00:42] belongs to the lyric"),
        QStringLiteral("[offset:100]\n[00:02.12]翻译\n[00:03.123]重复"));
    check(timed.synchronized && timed.lines.size() == 5,
          "LRC must sort multiple timestamps, merge simultaneous voices, and retain blank instrumental markers.");
    check(timed.lines[0].timeMs == 1000 && timed.lines[0].text == "Repeat"
              && timed.lines[1].timeMs == 2020 && timed.lines[1].text == "Second\nHarmony"
              && timed.lines[2].timeMs == 3023 && timed.lines[3].timeMs == 3900,
          "LRC decimal fractions and offset must retain millisecond timing.");
    check(timed.lines[1].translation == QStringLiteral("翻译")
              && timed.lines[2].translation == QStringLiteral("重复")
              && timed.lines[3].text.isEmpty(),
          "Translations must attach to matching timestamps and empty timed lines must clear the display.");
    check(timed.lines[4].text == "Text [00:42] belongs to the lyric",
          "Timestamp-like text inside a lyric must not erase preceding words.");
    check(!timed.plainText.contains("metadata") && !timed.plainText.contains("offset:"),
          "Metadata must not appear in displayed lyrics.");
    const auto krc = parseLyrics(QStringLiteral("[1000,2000]<0,500,0>你<500,500,0>好\n[3000,1500]<0,100,0>World"));
    check(krc.synchronized && krc.lines.size() == 2 && krc.lines[0].timeMs == 1000
              && krc.lines[0].text == QStringLiteral("你好") && krc.lines[1].text == "World",
          "Decoded KRC must remove word timing tags without losing line timestamps.");
    const auto plain = parseLyrics(QStringLiteral("[ti:Song]\nFirst line\nSecond line"));
    check(!plain.synchronized && plain.lines.isEmpty() && plain.plainText == "First line\nSecond line",
          "Untimed lyrics must stay available as readable plain text.");
    const auto negative = parseLyrics("[offset:-250]\n[00:00.05]Later");
    check(negative.lines.size() == 1 && negative.lines[0].timeMs == 300,
          "A negative LRC offset must delay the line.");
    const auto empty = parseLyrics({});
    check(empty.lines.isEmpty() && empty.plainText.isEmpty() && !empty.synchronized,
          "No lyrics must remain an honest empty result.");
    qInfo("Lyrics parsing checks passed.");
    return 0;
}
