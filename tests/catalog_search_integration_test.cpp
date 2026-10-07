#include <QtWidgets>
#include <QtMultimedia>
#include <QtNetwork>
#include <functional>
#include <memory>
#include <vector>
#define private public
#define main originalPlayerMain
#include "../main.cpp"
#undef main
#undef private

#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}

void waitForEvents(int duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitUntil(const std::function<bool()> &condition, int timeout = 5000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout)
        waitForEvents(10);
    return condition();
}

struct Request {
    QString query;
    int page = 0;
    CatalogSearchCallback callback;
};

struct Requests {
    QVector<Request> calls;
};

class DeferredProvider final : public MusicProvider {
public:
    DeferredProvider(MusicPlatform platform, QVector<Playlist> playlists,
                     std::shared_ptr<Requests> requests)
        : m_platform(platform), m_playlists(std::move(playlists)), m_requests(std::move(requests)) {}
    MusicPlatform platform() const override { return m_platform; }
    QVector<Playlist> playlists() const override { return m_playlists; }
    QVector<Track> likedTracks() const override { return allPlaylistTracks(m_playlists); }
    QVector<Track> search(const QString &query) const override
    {
        return MusicSearchIndex(allPlaylistTracks(m_playlists)).search(query);
    }
    void searchCatalog(const QString &query, int page, CatalogSearchCallback callback) const override
    {
        m_requests->calls.append({query, page, std::move(callback)});
    }
private:
    MusicPlatform m_platform;
    QVector<Playlist> m_playlists;
    std::shared_ptr<Requests> m_requests;
};

QUrl silentWave(const QString &path)
{
    constexpr quint32 sampleRate = 16000;
    constexpr quint32 size = sampleRate * 8 * sizeof(qint16);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "The local silent WAV fixture must be writable.");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + size);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << sampleRate
           << quint32(sampleRate * sizeof(qint16)) << quint16(sizeof(qint16)) << quint16(16);
    stream.writeRawData("data", 4);
    stream << size;
    const QByteArray silence(size, '\0');
    stream.writeRawData(silence.constData(), silence.size());
    check(stream.status() == QDataStream::Ok, "The silent WAV fixture must contain valid PCM data.");
    file.close();
    return QUrl::fromLocalFile(path);
}

struct Fixtures {
    QTemporaryDir directory;
    QUrl cover;
    QUrl firstAudio;
    QUrl secondAudio;
    Fixtures()
    {
        check(directory.isValid(), "The fixture directory must be available.");
        firstAudio = silentWave(directory.filePath(QStringLiteral("first.wav")));
        secondAudio = silentWave(directory.filePath(QStringLiteral("second.wav")));
        QPixmap image(56, 56);
        image.fill(QColor(15, 25, 40));
        const QString imagePath = directory.filePath(QStringLiteral("cover.png"));
        check(image.save(imagePath), "Cover fixtures must remain local files.");
        cover = QUrl::fromLocalFile(imagePath);
    }
    Track track(const QString &id, const QString &title, bool second = false) const
    {
        Track result;
        result.id = id;
        result.hash = id.toUpper();
        result.albumAudioId = QStringLiteral("audio-") + id;
        result.title = title;
        result.artist = QStringLiteral("Fixture artist");
        result.album = QStringLiteral("Fixture album");
        result.coverUrl = cover;
        result.audioUrl = second ? secondAudio : firstAudio;
        return result;
    }
    Playlist playlist() const
    {
        Playlist result;
        result.id = QStringLiteral("fixture-library");
        result.name = QStringLiteral("Account playlist");
        result.coverUrl = cover;
        result.tracks = {track(QStringLiteral("library-song"), QStringLiteral("Existing library song"))};
        return result;
    }
};

CatalogSearchPage page(int number, const QVector<Track> &tracks, bool more = false,
                       int total = -1, MusicPlatform platform = MusicPlatform::Kugou)
{
    CatalogSearchPage result;
    result.page = number;
    result.pageSize = 2;
    result.total = total;
    result.hasMore = more;
    for (const Track &track : tracks)
        result.tracks.append({track, platform});
    return result;
}

void reply(const std::shared_ptr<Requests> &requests, int index,
           const CatalogSearchPage &result, const QString &error = {})
{
    check(index >= 0 && index < requests->calls.size(), "A deferred request must exist before delivery.");
    CatalogSearchCallback callback = std::move(requests->calls[index].callback);
    check(bool(callback), "A catalog response must be delivered exactly once.");
    callback(result, error);
    QCoreApplication::processEvents();
}

void installProviders(PlayerWindow &window, const Fixtures &fixtures,
                      const std::shared_ptr<Requests> &kugou,
                      const std::shared_ptr<Requests> &qq)
{
    const Playlist library = fixtures.playlist();
    {
        const QSignalBlocker platformSignals(window.m_platform);
        window.m_providers.clear();
        window.m_providers.push_back(std::make_unique<DeferredProvider>(MusicPlatform::Kugou,
            QVector<Playlist>{library}, kugou));
        window.m_providers.push_back(std::make_unique<DeferredProvider>(MusicPlatform::QQMusic,
            QVector<Playlist>{library}, qq));
        window.m_platform->clear();
        window.m_platform->addItems({platformName(MusicPlatform::Kugou), platformName(MusicPlatform::QQMusic)});
        window.m_platform->setCurrentIndex(0);
    }
    window.m_enabledPlatforms = {MusicPlatform::Kugou, MusicPlatform::QQMusic};
    window.m_kugouAccountConnected = true;
    window.m_kugouPlaylists = {library};
    window.m_kugouTrackCache.insert(library.id, library.tracks);
    window.m_completedPlaylists.insert(library.id);
    window.m_drawerSignature.clear();
    // An unexpected HTTP request can only reach a closed loopback port; every
    // expected catalog callback, audio source and image is supplied locally.
    const QNetworkProxy localOnly(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 1);
    window.m_kugouApi.m_network.setProxy(localOnly);
    window.m_kugouApi.m_libraryNetwork.setProxy(localOnly);
    window.m_kugouApi.m_playbackNetwork.setProxy(localOnly);
    window.m_coverNetwork->setProxy(localOnly);
    window.m_coverCache.insert(fixtures.cover.toString() + QLatin1Char('|')
        + QString::number(qCeil(80 * window.m_surface->devicePixelRatioF())),
        new QIcon(fixtures.cover.toLocalFile()));
    window.m_audioOutput->setMuted(true);
    window.m_audioOutput->setVolume(0);
    window.m_playbackQueue.setShuffle(false);
    window.m_dock->setPinned(false);
    window.m_drawer->setPinned(false);
    window.show();
    window.refresh();
    QCoreApplication::processEvents();
    check(window.m_visibleTracks.size() == 1
              && window.m_visibleTracks.first().id == library.tracks.first().id,
          "Catalog integration must begin with a distinct account-playlist view.");
}

int beginSearch(PlayerWindow &window, const std::shared_ptr<Requests> &requests,
                const QString &query, bool force = false)
{
    const int index = requests->calls.size();
    window.m_search->setText(query);
    window.m_searchTimer.stop();
    window.startCatalogSearch(force);
    check(requests->calls.size() == index + 1 && requests->calls[index].query == query
              && requests->calls[index].page == 1,
          "A top search must call the Kugou catalog provider with its first page.");
    check(window.m_catalogBusy, "A pending catalog request must prevent duplicate page requests.");
    return index;
}

void verifyOutsideTracksStaleResultsAndPagination(PlayerWindow &window,
        const Fixtures &fixtures, const std::shared_ptr<Requests> &requests)
{
    const Track outside = fixtures.track(QStringLiteral("outside-a"), QStringLiteral("Song absent from the account"));
    const Track sameName = fixtures.track(QStringLiteral("outside-b"), outside.title, true);
    const Track third = fixtures.track(QStringLiteral("outside-c"), QStringLiteral("Third catalog song"));
    const int oldRequest = beginSearch(window, requests, QStringLiteral("old query"));
    const int newRequest = beginSearch(window, requests, QStringLiteral("new query"));
    reply(requests, newRequest, page(1, {outside, sameName}, true, 4));
    check(window.m_catalogTracks.size() == 2
              && window.m_catalogPanel->resultsTable()->rowCount() == 2,
          "Catalog songs outside every account playlist must be visible in the floating table.");
    check(window.m_catalogTracks[0].track.hash != window.m_catalogTracks[1].track.hash,
          "Different recordings sharing a title must remain separate catalog results.");
    check(window.m_visibleTracks.size() == 1
              && window.m_visibleTracks.first().id == fixtures.playlist().tracks.first().id,
          "Catalog results must preserve the account-playlist table underneath the panel.");
    reply(requests, oldRequest, page(1, {third}));
    check(window.m_catalogQuery == QStringLiteral("new query") && window.m_catalogTracks.size() == 2
              && window.m_catalogTracks.first().track.id == outside.id,
          "A delayed response for an older query must not replace newer catalog results.");

    const int moreRequest = requests->calls.size();
    window.loadMoreCatalogResults();
    window.loadMoreCatalogResults();
    check(requests->calls.size() == moreRequest + 1 && requests->calls.last().page == 2,
          "Two load-more clicks must produce one request for the next page.");
    reply(requests, moreRequest, page(2, {outside, third}, false, 4));
    check(window.m_catalogTracks.size() == 3 && window.m_catalogPage == 2 && !window.m_catalogHasMore,
          "Pagination must append in order and suppress repeated audio identities across pages.");
    check(window.m_catalogTracks[0].track.id == outside.id && window.m_catalogTracks[2].track.id == third.id,
          "Appending a page must preserve existing catalog order.");

    const int failingQuery = beginSearch(window, requests, QStringLiteral("page retry"));
    reply(requests, failingQuery, page(1, {outside, sameName}, true, 4));
    const int failingPage = requests->calls.size();
    window.loadMoreCatalogResults();
    reply(requests, failingPage, page(2, {}), QStringLiteral("Temporary fixture failure"));
    check(!window.m_catalogBusy && window.m_catalogPage == 1 && window.m_catalogTracks.size() == 2,
          "A failed page must preserve loaded results and the last successful page number.");
    const int retryPage = requests->calls.size();
    window.loadMoreCatalogResults();
    check(requests->calls.size() == retryPage + 1 && requests->calls.last().page == 2,
          "Retrying a failed page must request the same page rather than skip songs.");
    reply(requests, retryPage, page(2, {third}, false, 3));

    const int failingFirst = beginSearch(window, requests, QStringLiteral("first retry"));
    reply(requests, failingFirst, page(1, {}), QStringLiteral("Temporary fixture failure"));
    check(!window.m_catalogBusy && window.m_catalogTracks.isEmpty(),
          "A failed new search must not present the previous query's songs as its results.");
    const int retryFirst = beginSearch(window, requests, QStringLiteral("first retry"), true);
    reply(requests, retryFirst, page(1, {outside}));
    check(window.m_catalogTracks.size() == 1,
          "An explicit retry must recover from a failed first catalog page.");
}

void verifyDismissalAndFixedCatalogRouting(PlayerWindow &window, const Fixtures &fixtures,
        const std::shared_ptr<Requests> &kugou, const std::shared_ptr<Requests> &qq)
{
    const Track outside = fixtures.track(QStringLiteral("cancelled-result"), QStringLiteral("Cancelled result"));
    const int cleared = beginSearch(window, kugou, QStringLiteral("cleared query"));
    window.m_search->clear();
    reply(kugou, cleared, page(1, {outside}));
    check(!window.m_catalogPanel->isVisible(),
          "A response arriving after the search input is cleared must not reopen the panel.");
    const int dismissed = beginSearch(window, kugou, QStringLiteral("dismissed query"));
    window.m_catalogPanel->dismiss();
    reply(kugou, dismissed, page(1, {outside}));
    check(!window.m_catalogPanel->isVisible(),
          "Closing a pending catalog panel must prevent its delayed response from reopening it.");

    const int requestsBeforeDismissal = kugou->calls.size();
    window.m_search->setText(QStringLiteral("dismiss during debounce"));
    window.m_catalogPanel->dismiss();
    waitForEvents(350);
    check(kugou->calls.size() == requestsBeforeDismissal && !window.m_catalogPanel->isVisible(),
          "Closing the panel while typing is debounced must cancel its scheduled search.");

    window.m_search->clear();
    window.m_platform->setCurrentIndex(1);
    const int qqRequests = qq->calls.size();
    const int pending = beginSearch(window, kugou, QStringLiteral("周杰伦"));
    check(window.provider()->platform() == MusicPlatform::QQMusic
              && window.m_catalogPlatform == MusicPlatform::Kugou
              && window.m_search->placeholderText().contains(QStringLiteral("酷狗")),
          "Browsing a QQ playlist must still use the explicitly labelled Kugou online search.");
    const int kugouRequests = kugou->calls.size();
    window.m_platform->setCurrentIndex(0);
    check(kugou->calls.size() == kugouRequests && qq->calls.size() == qqRequests && window.m_catalogBusy,
          "Changing the playlist browsing platform must not restart or redirect a pending catalog request.");
    reply(kugou, pending, page(1, {outside}, true, 2));
    check(window.m_catalogTracks.size() == 1
              && window.m_catalogTracks.first().platform == MusicPlatform::Kugou,
          "A pending Kugou catalog response must remain valid across a browsing-platform change.");
    const int nextPage = kugou->calls.size();
    window.loadMoreCatalogResults();
    check(kugou->calls.size() == nextPage + 1 && kugou->calls.last().page == 2,
          "Catalog pagination must continue to request Kugou independently of playlist browsing.");
    window.m_platform->setCurrentIndex(1);
    const Track additional = fixtures.track(QStringLiteral("fixed-catalog-next"), QStringLiteral("Additional Kugou song"));
    reply(kugou, nextPage, page(2, {additional}, false, 2));
    check(window.m_catalogTracks.size() == 2 && window.m_catalogPage == 2
              && window.m_catalogQuery == QStringLiteral("周杰伦") && qq->calls.size() == qqRequests,
          "Switching to QQ while a catalog page loads must preserve and append Kugou results without querying QQ.");
    const int completedRequests = kugou->calls.size();
    window.m_platform->setCurrentIndex(0);
    check(window.m_catalogTracks.size() == 2 && kugou->calls.size() == completedRequests,
          "Changing the browsing platform must retain completed online results without another request.");
    window.m_search->clear();

    const int oldAccount = beginSearch(window, kugou, QStringLiteral("old account"));
    ++window.m_kugouGeneration;
    window.resetCatalogSearch();
    reply(kugou, oldAccount, page(1, {outside}));
    check(window.m_catalogTracks.isEmpty() && !window.m_catalogPanel->isVisible(),
          "An account-generation change must discard earlier catalog responses.");
    window.m_search->clear();
}

void verifyMissingKugouAuthorization(PlayerWindow &window,
        const std::shared_ptr<Requests> &kugou, const std::shared_ptr<Requests> &qq)
{
    window.m_platform->setCurrentIndex(1);
    window.m_kugouAccountConnected = false;
    const int kugouRequests = kugou->calls.size();
    const int qqRequests = qq->calls.size();
    window.m_search->setText(QStringLiteral("周杰伦"));
    window.m_searchTimer.stop();
    window.startCatalogSearch();
    QString messages;
    for (QLabel *label : window.m_catalogPanel->findChildren<QLabel *>())
        messages += label->text() + QLatin1Char('\n');
    check(window.m_catalogPanel->isVisible() && messages.contains(QStringLiteral("登录"))
              && messages.contains(QStringLiteral("酷狗")) && !messages.contains(QStringLiteral("尚未接入")),
          "An unauthorized top search must explain the Kugou login requirement rather than claim QQ is unsupported.");
    check(kugou->calls.size() == kugouRequests && qq->calls.size() == qqRequests
              && window.m_catalogTracks.isEmpty() && !window.m_catalogBusy,
          "An unauthorized catalog search must not query either provider or present old songs.");
    window.m_kugouAccountConnected = true;
    window.m_search->clear();
    window.m_platform->setCurrentIndex(0);
}

void verifyBackgroundRefreshAndIndependentPlayback(PlayerWindow &window, const Fixtures &fixtures,
        const std::shared_ptr<Requests> &kugou)
{
    const Track first = fixtures.track(QStringLiteral("play-first"), QStringLiteral("First catalog playback"));
    const Track next = fixtures.track(QStringLiteral("play-next"), QStringLiteral("Next catalog playback"), true);
    const int search = beginSearch(window, kugou, QStringLiteral("playback catalog"));

    Playlist refreshed = fixtures.playlist();
    refreshed.tracks.append(fixtures.track(QStringLiteral("new-library-song"), QStringLiteral("Background loaded song")));
    window.m_kugouPlaylists = {refreshed};
    window.m_kugouTrackCache.insert(refreshed.id, refreshed.tracks);
    // A background load can replace a snapshot provider while a catalog is open.
    window.m_providers[0] = std::make_unique<DeferredProvider>(MusicPlatform::Kugou,
        QVector<Playlist>{refreshed}, kugou);
    reply(kugou, search, page(1, {first, next}, false, 2));
    check(window.m_catalogTracks.size() == 2,
          "Replacing a provider snapshot must not discard its same-platform pending response.");
    {
        const QSignalBlocker sectionSignals(window.m_navigation);
        window.m_navigation->setCurrentRow(3);
    }
    window.scheduleLibraryView();
    waitForEvents(1700);
    check(window.m_catalogTracks.size() == 2 && window.m_catalogTracks.first().track.id == first.id
              && window.m_catalogPanel->resultsTable()->rowCount() == 2,
          "Automatic account-library refresh must not replace or erase floating catalog results.");

    check(QMetaObject::invokeMethod(window.m_catalogPanel->resultsTable(), "cellClicked",
                                   Qt::DirectConnection, Q_ARG(int, 0), Q_ARG(int, 1)),
          "The floating table must expose its actual song activation signal.");
    check(waitUntil([&] {
        return window.m_player->source() == first.audioUrl
            && window.m_player->playbackState() == QMediaPlayer::PlayingState
            && window.m_player->duration() > 0;
    }), "Activating an outside catalog result must start the actual media backend with its local PCM source.");
    check(window.m_playbackQueue.size() == 2 && window.m_playbackQueue.current()->id == first.id
              && window.m_queueUsesKugou,
          "Catalog playback must copy its own result queue and preserve the source platform.");

    window.m_player->stop();
    window.m_player->setSource(QUrl());
    window.m_catalogPanel->resultsTable()->setCurrentCell(0, 1);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
    QApplication::sendEvent(window.m_catalogPanel->resultsTable(), &enter);
    check(waitUntil([&] {
        return window.m_player->source() == first.audioUrl
            && window.m_player->playbackState() == QMediaPlayer::PlayingState;
    }), "Pressing Enter on the selected catalog result must start actual local PCM playback.");

    const int replacement = beginSearch(window, kugou, QStringLiteral("different results"));
    reply(kugou, replacement, page(1, {fixtures.track(QStringLiteral("different-result"), QStringLiteral("Different result"))}));
    check(window.m_playbackQueue.size() == 2 && window.m_playbackQueue.current()->id == first.id
              && window.m_player->source() == first.audioUrl,
          "Replacing search results must preserve the currently playing catalog queue and source.");
    window.m_search->clear();
    window.m_platform->setCurrentIndex(1);
    window.m_dock->onNext();
    check(waitUntil([&] {
        return window.m_player->source() == next.audioUrl
            && window.m_player->playbackState() == QMediaPlayer::PlayingState;
    }), "Browsing another platform must leave next-song playback on the original catalog queue.");
    check(window.m_playbackQueue.current()->id == next.id && window.m_queueUsesKugou,
          "Changing the browsing platform must not reroute an active Kugou catalog queue.");
    window.m_player->stop();
    window.m_player->setSource(QUrl());
}

void verifyDestroyedWindowDropsResponse(const Fixtures &fixtures)
{
    const auto kugou = std::make_shared<Requests>();
    const auto qq = std::make_shared<Requests>();
    auto window = std::make_unique<PlayerWindow>(QVector<MusicPlatform>{MusicPlatform::QQMusic}, true);
    installProviders(*window, fixtures, kugou, qq);
    const int pending = beginSearch(*window, kugou, QStringLiteral("window lifetime"));
    QPointer<PlayerWindow> lifetime(window.get());
    window.reset();
    check(!lifetime, "Destroying the player window must release its QObject lifetime.");
    reply(kugou, pending, page(1, {fixtures.track(QStringLiteral("late-result"), QStringLiteral("Late result"))}));
}

void saveVisualPreview(PlayerWindow &window, const Fixtures &fixtures,
                       const std::shared_ptr<Requests> &kugou)
{
    check(!QPixmap(QStringLiteral(":/platforms/kugou.ico")).isNull(),
          "The catalog must bundle its official Kugou platform icon.");
    window.m_platform->setCurrentIndex(0);
    window.resize(1300, 850);
    const QStringList titles{QStringLiteral("世界第一的公主殿下"), QStringLiteral("无梦之梦"),
        QStringLiteral("合成的未来"), QStringLiteral("夜に駆ける"), QStringLiteral("ロキ"),
        QStringLiteral("メルト"), QStringLiteral("砂の惑星"), QStringLiteral("千本桜")};
    QVector<Track> results;
    for (int index = 0; index < titles.size(); ++index) {
        Track track = fixtures.track(QStringLiteral("preview-%1").arg(index), titles[index]);
        track.artist = index < 3 ? QStringLiteral("初音ミク / はるまきごはん") : QStringLiteral("试听歌手 · 离线测试");
        results.append(track);
    }
    const int request = beginSearch(window, kugou, QStringLiteral("初音未来"), true);
    reply(kugou, request, page(1, results, true, 42));
    window.m_search->setFocus();
    waitForEvents(240);
    const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/catalog-search-preview.png");
    check(window.grab().save(path), "The mock catalog preview must be saved for visual review.");
}

void verifyDrawerCategoriesAndSavePreviews(PlayerWindow &window)
{
    window.m_search->clear();
    window.m_drawer->setPinned(true);
    const QVector<QSize> sizes{QSize(1300, 850), QSize(760, 560)};
    for (const QSize &size : sizes) {
        window.resize(size);
        waitForEvents(240);
        QVector<QRect> bounds;
        for (JellyButton *button : window.m_navigationButtons) {
            check(button && button->isVisible()
                      && !button->visibleRegion().isEmpty(),
                  "Every playlist category must remain visible in the expanded drawer.");
            const QRect rectangle(button->mapTo(window.m_navigationControls, QPoint()), button->size());
            check(window.m_navigationControls->rect().contains(rectangle),
                  "Category buttons must fit inside the real drawer layout at both window sizes.");
            for (const QRect &previous : bounds)
                check(!rectangle.intersects(previous),
                      "Playlist category buttons must not overlap after resizing the actual window.");
            bounds.append(rectangle);
            const QFontMetrics metrics(button->font());
            check(metrics.horizontalAdvance(button->text()) + 24 <= button->width()
                      && metrics.height() + 8 <= button->height(),
                  "Category labels must fit with readable padding rather than truncate or run into adjacent buttons.");
        }
        for (int category = 0; category < 4; ++category) {
            window.m_navigationButtons[category]->click();
            check(window.m_navigation->currentRow() == category,
                  "Clicking a category must select its corresponding playlist section.");
            for (int other = 0; other < 4; ++other)
                check(window.m_navigationButtons[other]->isChecked() == (other == category),
                      "The visible category selection must remain exclusive and match the selected section.");
        }
        window.m_navigation->setCurrentRow(0);
        check(window.m_navigationButtons[0]->isChecked(),
              "Programmatic section changes must also update the visible category selection.");
        const QString suffix = size.width() > 760 ? QString() : QStringLiteral("-compact");
        const QString path = QCoreApplication::applicationDirPath()
            + QStringLiteral("/drawer-categories-preview") + suffix + QStringLiteral(".png");
        check(window.grab().save(path), "Drawer category previews must be saved for visual review.");
    }
    window.m_drawer->setPinned(false);
}
} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PHS Radio Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Catalog Search Integration Test"));
    Fixtures fixtures;
    const auto kugou = std::make_shared<Requests>();
    const auto qq = std::make_shared<Requests>();
    PlayerWindow window({MusicPlatform::QQMusic}, true);
    installProviders(window, fixtures, kugou, qq);
    verifyOutsideTracksStaleResultsAndPagination(window, fixtures, kugou);
    verifyDismissalAndFixedCatalogRouting(window, fixtures, kugou, qq);
    verifyMissingKugouAuthorization(window, kugou, qq);
    verifyBackgroundRefreshAndIndependentPlayback(window, fixtures, kugou);
    verifyDestroyedWindowDropsResponse(fixtures);
    saveVisualPreview(window, fixtures, kugou);
    verifyDrawerCategoriesAndSavePreviews(window);
    qInfo("Catalog search integration tests passed.");
    return 0;
}
