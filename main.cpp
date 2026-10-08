#include "mock_music_provider.h"
#include "kugou_api_client.h"
#include "platform_login_page.h"
#include "playlist_snapshot_provider.h"
#include "playlist_drawer.h"
#include "aero_surface.h"
#include "aero_animation.h"
#include "aero_widgets.h"
#include "playback_dock.h"
#include "playback_queue.h"
#include "song_glass_delegate.h"
#include "smooth_scroll.h"
#include "liquid_backdrop.h"
#include "catalog_search_panel.h"
#include "home_page.h"
#include "song_page.h"
#include "netease_api_client.h"
#include "netease_music_provider.h"
#include "lyric_domain.h"
#include "background_theme.h"
#include "theme_panel.h"
#include "wallpaper_engine_capture.h"
#include "native_window_capture.h"
#include "glass_title_bar.h"
#include "release_config.h"
#include "update_panel.h"
#include "app_updater.h"
#include "cover_image_decoder.h"

#include <QApplication>
#include <QAudioOutput>
#include <QAudioDevice>
#include <QAbstractItemView>
#include <QColor>
#include <QColorDialog>
#include <QFontMetrics>
#include <QComboBox>
#include <QButtonGroup>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QSize>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QStatusBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStringList>
#include <QWidget>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QCache>
#include <QDate>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QFile>
#include <QtMath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <memory>
#include <algorithm>
#include <functional>
#include <vector>

#ifndef PHSRADIO_VERSION
#define PHSRADIO_VERSION "0.2.3-beta"
#endif

namespace {

QString platformKey(MusicPlatform platform)
{
    switch (platform) {
    case MusicPlatform::QQMusic: return QStringLiteral("qq-music");
    case MusicPlatform::NetEaseCloud: return QStringLiteral("netease-cloud");
    case MusicPlatform::Kugou: return QStringLiteral("kugou");
    }
    return {};
}

QString coverRequestKey(const Track &track)
{
    // Include the source URL: refreshed metadata for the same recording may
    // point to a different cover, while duplicate rows can share one request.
    return trackKey(track) + QChar(0x1f) + track.coverUrl.toString();
}

QSize wallpaperRenderPixels(const QWidget *surface)
{
    const qreal dpr = surface->devicePixelRatioF();
    return QSize(qCeil(surface->width() * dpr), qCeil(surface->height() * dpr));
}

QPixmap placeholderCover()
{
    QPixmap image(56, 56);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    Aero::paintGlass(painter, QRectF(1, 1, 54, 54), Aero::defaultAccent(), 12);
    painter.setPen(QPen(QColor(QStringLiteral("#d4f1fa")), 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(31, 15), QPointF(31, 36));
    painter.drawLine(QPointF(31, 15), QPointF(41, 19));
    painter.setBrush(QColor(QStringLiteral("#d4f1fa")));
    painter.drawEllipse(QRectF(20, 32, 11, 8));
    return image;
}

QVector<MusicPlatform> loadSelectedPlatforms(QSettings &settings)
{
    QVector<MusicPlatform> selected;
    const QStringList keys = settings.value(QStringLiteral("selectedPlatforms")).toStringList();
    for (const QString &key : keys) {
        MusicPlatform platform;
        if (key == QStringLiteral("qq-music"))
            platform = MusicPlatform::QQMusic;
        else if (key == QStringLiteral("netease-cloud"))
            platform = MusicPlatform::NetEaseCloud;
        else if (key == QStringLiteral("kugou"))
            platform = MusicPlatform::Kugou;
        else
            continue;
        if (!selected.contains(platform))
            selected.push_back(platform);
    }
    return ReleaseConfig::filterPlatforms(selected);
}

}

class PlayerWindow final : public QMainWindow {
public:
    PlayerWindow(const QVector<MusicPlatform> &enabledPlatforms, bool setupCompleted, bool smokeMode = false)
    : m_kugouApi(this), m_neteaseApi(this)
    {
        const QVector<MusicPlatform> allowedPlatforms = ReleaseConfig::filterPlatforms(enabledPlatforms);
        Q_INIT_RESOURCE(app);
        setWindowIcon(QIcon(QStringLiteral(":/app/app.ico")));
        setWindowFlag(Qt::FramelessWindowHint);
        setWindowTitle(QStringLiteral("PHS Radio · 新大地播放器"));
        if (!QCoreApplication::applicationVersion().isEmpty())
            setWindowTitle(windowTitle() + QStringLiteral(" v")
                + QString(QCoreApplication::applicationVersion()).replace(QStringLiteral("-beta"), QStringLiteral(" beta")));
        resize(1180, 780);
        setMinimumSize(760, 560);
        QFont uiFont = font();
        uiFont.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
        uiFont.setPointSize(10);
        setFont(uiFont);
        m_pages = new QStackedWidget(this);
        m_loginPage = new PlatformLoginPage(m_pages);
        m_loginTitleBar = new GlassTitleBar(this, m_loginPage);
        if (auto *loginLayout = qobject_cast<QVBoxLayout *>(m_loginPage->layout()))
            loginLayout->insertWidget(0, m_loginTitleBar);
        m_pages->addWidget(m_loginPage);
        setCentralWidget(m_pages);

        auto *central = new AeroSurface(m_pages);
        m_surface = central;
        central->setObjectName(QStringLiteral("playerSurface"));
        m_playerPage = central;
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(26, 16, 24, 26);
        root->setSpacing(16);
        m_titleBar = new GlassTitleBar(this, central);
        m_titleBar->onUpdateRequested = [this] { showUpdatePanel(); };
        m_loginTitleBar->onUpdateRequested = [this] { showUpdatePanel(); };
        root->addWidget(m_titleBar);
        m_toolbarPanel = new AeroPanel(central);
        auto *top = new QHBoxLayout(m_toolbarPanel);
        top->setContentsMargins(20, 12, 16, 12);
        auto *brandIcon = new QLabel(m_toolbarPanel);
        brandIcon->setPixmap(QIcon(QStringLiteral(":/app/logo.png")).pixmap(QSize(34, 34), devicePixelRatioF()));
        brandIcon->setFixedSize(34, 34);
        brandIcon->setAccessibleName(QStringLiteral("PHS Radio 玻璃台风标志"));
        top->addWidget(brandIcon);
        auto *brand = new QLabel(QStringLiteral("PHS Radio"), m_toolbarPanel);
        brand->setObjectName(QStringLiteral("toolbarBrand"));
        brand->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 600; color: #eef5fb;"));
        auto *drawerButton = new JellyButton(QStringLiteral("歌单"), m_toolbarPanel);
        auto *homeButton = new JellyButton(QStringLiteral("首页"), m_toolbarPanel);
        homeButton->setObjectName(QStringLiteral("homeButton"));
        auto *themeButton = new JellyButton(QStringLiteral("主题"), m_toolbarPanel);
        themeButton->setObjectName(QStringLiteral("themeButton"));
        themeButton->setToolTip(QStringLiteral("导入图片、动态视频或 Wallpaper Engine 壁纸"));
        drawerButton->setGlyph(JellyButton::Glyph::Menu);
        drawerButton->setToolTip(QStringLiteral("打开左侧歌单，也可将鼠标移到窗口左边缘"));
        m_platform = new QComboBox(m_toolbarPanel);
        m_platform->setMinimumWidth(90);
        m_platform->setMaximumWidth(154);
        m_search = new QLineEdit(m_toolbarPanel);
        m_search->setMinimumWidth(100);
        m_search->setAccessibleName(QStringLiteral("酷狗在线曲库搜索"));
        m_search->setPlaceholderText(QStringLiteral("搜索酷狗全平台歌曲、歌手"));
        m_search->setToolTip(QStringLiteral("搜索酷狗在线曲库，点击结果即可播放；不受歌单浏览平台影响。\n自己的歌单内搜索请使用左侧歌单面板。"));
        m_search->setClearButtonEnabled(true);
        auto *platformsButton = new JellyButton(QStringLiteral("平台连接"), m_toolbarPanel);
        platformsButton->setObjectName(QStringLiteral("platformsButton"));
        platformsButton->setMaximumWidth(116);
        top->addWidget(brand);
        top->addWidget(homeButton);
        top->addWidget(drawerButton);
        top->addWidget(m_platform);
        top->addWidget(m_search, 1);
        top->addWidget(themeButton);
        top->addWidget(platformsButton);
        root->addWidget(m_toolbarPanel);

        m_drawer = new PlaylistDrawer(central);
        m_drawer->setTopInset(102);
        auto *drawerContent = m_drawer->contentLayout();
        drawerContent->setContentsMargins(16, 16, 16, 16);
        drawerContent->setSpacing(10);
        auto *drawerHeader = new QHBoxLayout;
        auto *drawerTitle = new QLabel(QStringLiteral("我的歌单"), m_drawer);
        drawerTitle->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700;"));
        auto *pinDrawer = new JellyButton(QStringLiteral("固定"), m_drawer);
        pinDrawer->setGlyph(JellyButton::Glyph::Pin);
        pinDrawer->setFixedSize(100, 38);
        pinDrawer->setCheckable(true);
        pinDrawer->setToolTip(QStringLiteral("保持歌单面板展开"));
        auto *closeDrawer = new JellyButton(QString(), m_drawer);
        closeDrawer->setGlyph(JellyButton::Glyph::Close);
        closeDrawer->setFixedSize(34, 34);
        closeDrawer->setAccessibleName(QStringLiteral("收起歌单"));
        drawerHeader->addWidget(drawerTitle, 1);
        drawerHeader->addWidget(pinDrawer);
        drawerHeader->addWidget(closeDrawer);
        drawerContent->addLayout(drawerHeader);
        auto *drawerHint = new QLabel(QStringLiteral("靠近左边缘展开 · 移开后收起"), m_drawer);
        m_drawerHint = drawerHint;
        drawerHint->setStyleSheet(QStringLiteral("color: #94a3b4; font-size: 11px;"));
        drawerContent->addWidget(drawerHint);
        m_navigation = new QListWidget(m_drawer);
        m_navigation->addItems({QStringLiteral("全部歌单"), QStringLiteral("收藏歌单"), QStringLiteral("自建歌单"), QStringLiteral("全部音乐")});
        // Keep the section model separate from the visible controls. A fixed
        // list-view grid let style-sheet padding make adjacent cells overlap.
        m_navigation->hide();
        m_navigation->setCurrentRow(0);
        m_navigationControls = new QWidget(m_drawer);
        m_navigationControls->setObjectName(QStringLiteral("playlistCategories"));
        auto *categoryLayout = new QGridLayout(m_navigationControls);
        categoryLayout->setContentsMargins(0, 0, 0, 0);
        categoryLayout->setSpacing(8);
        auto *categoryGroup = new QButtonGroup(m_navigationControls);
        categoryGroup->setExclusive(true);
        for (int index = 0; index < 4; ++index) {
            auto *button = new JellyButton(m_navigation->item(index)->text(), m_navigationControls);
            button->setObjectName(QStringLiteral("playlistCategory%1").arg(index));
            button->setCheckable(true);
            button->setMinimumWidth(0);
            button->setFixedHeight(38);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            categoryGroup->addButton(button, index);
            categoryLayout->addWidget(button, index / 2, index % 2);
            m_navigationButtons[index] = button;
        }
        categoryLayout->setColumnStretch(0, 1);
        categoryLayout->setColumnStretch(1, 1);
        m_navigationButtons[0]->setChecked(true);
        m_navigationControls->setFixedHeight(84);
        connect(categoryGroup, &QButtonGroup::idClicked, this, [this](int index) {
            if (index != m_navigation->currentRow())
                m_navigation->setCurrentRow(index);
            else if (!m_search->text().isEmpty())
                m_search->clear();
            else if (!m_playlistSearch->text().isEmpty())
                m_playlistSearch->clear();
        });
        drawerContent->addWidget(m_navigationControls);
        m_drawerListSummary = new QLabel(m_drawer);
        m_drawerListSummary->setStyleSheet(QStringLiteral("color: #94a3b4; font-size: 11px;"));
        drawerContent->addWidget(m_drawerListSummary);

        m_contentPages = new QStackedWidget(central);
        // Hidden pages' larger preferred heights must not force the restored
        // window's active home page outside its available viewport.
        m_contentPages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        m_contentPages->setMinimumSize(0, 0);
        m_contentPages->setObjectName(QStringLiteral("contentPages"));
        m_contentPages->setStyleSheet(QStringLiteral("QStackedWidget#contentPages { background: transparent; }"));
        m_homePage = new HomePage(m_contentPages);
        m_homePage->setKugouOnly(ReleaseConfig::KugouOnly || smokeMode);
        m_libraryPage = new QWidget(m_contentPages);
        m_libraryPage->setObjectName(QStringLiteral("libraryPage"));
        m_songPage = new SongPage(m_contentPages);
        m_contentPages->addWidget(m_homePage);
        m_contentPages->addWidget(m_libraryPage);
        m_contentPages->addWidget(m_songPage);
        auto *content = new QVBoxLayout(m_libraryPage);
        content->setContentsMargins(0, 0, 0, 0);
        m_heading = new QLabel(central);
        m_librarySummary = new QLabel(central);
        m_librarySummary->setWordWrap(true);
        m_heading->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: 600;"));
        m_librarySummary->setStyleSheet(QStringLiteral("color: #8e9daa; font-size: 12px;"));
        m_playlistList = new QListWidget(m_drawer);
        m_playlistList->setIconSize(QSize(48, 48));
        m_playlistList->setSpacing(6);
        m_playlistList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_playlistList->setTextElideMode(Qt::ElideRight);
        SmoothScrollController::attach(m_playlistList);
        drawerContent->addWidget(m_playlistList, 1);
        m_playlistSearchScope = new QLabel(QStringLiteral("先选择一个歌单"), m_drawer);
        m_playlistSearchScope->setWordWrap(true);
        drawerContent->addWidget(m_playlistSearchScope);
        m_playlistSearch = new QLineEdit(m_drawer);
        m_playlistSearch->setPlaceholderText(QStringLiteral("搜索当前歌单里的歌曲"));
        m_playlistSearch->setClearButtonEnabled(true);
        m_playlistSearch->setFixedHeight(36);
        m_playlistSearch->setToolTip(QStringLiteral("只搜索选中的歌单，支持别名、拼音和多个关键词"));
        drawerContent->addWidget(m_playlistSearch);
        m_playlistSearchSummary = new QLabel(m_drawer);
        m_playlistSearchSummary->setStyleSheet(QStringLiteral("color: #94a3b4; font-size: 11px;"));
        drawerContent->addWidget(m_playlistSearchSummary);
        auto *edge = new JellyButton(QString(), central);
        edge->setGlyph(JellyButton::Glyph::Menu);
        m_edgeHandle = edge;
        m_edgeHandle->setObjectName(QStringLiteral("drawerEdgeHandle"));
        m_edgeHandle->setToolTip(QStringLiteral("靠近左边缘展开歌单"));
        m_edgeHandle->setAccessibleName(QStringLiteral("打开歌单"));
        m_edgeHandle->setCursor(Qt::PointingHandCursor);
        connect(drawerButton, &QPushButton::clicked, this, [this] { m_drawer->toggle(); });
        connect(m_edgeHandle, &QPushButton::clicked, this, [this] { m_drawer->setExpanded(true); });
        connect(pinDrawer, &QPushButton::toggled, this, [this, pinDrawer](bool pinned) {
            m_drawer->setPinned(pinned);
            pinDrawer->setText(pinned ? QStringLiteral("已固定") : QStringLiteral("固定"));
            QSettings().setValue(QStringLiteral("appearance/drawerPinned"), pinned);
        });
        connect(closeDrawer, &QPushButton::clicked, this, [this, pinDrawer] {
            pinDrawer->setChecked(false);
            m_drawer->setExpanded(false);
        });
        m_tracks = new QTableWidget(0, 4, central);
        m_tracks->setHorizontalHeaderLabels({QStringLiteral("封面"), QStringLiteral("歌曲"),
                             QStringLiteral("歌手"), QStringLiteral("专辑")});
        m_tracks->setIconSize(QSize(52, 52));
        m_tracks->horizontalHeader()->setObjectName(QStringLiteral("libraryHeader"));
        m_tracks->horizontalHeader()->setFixedHeight(46);
        m_tracks->horizontalHeader()->setStretchLastSection(true);
        m_tracks->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        m_tracks->setColumnWidth(0, 76);
        m_tracks->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_tracks->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
        m_tracks->setColumnWidth(2, 190);
        m_tracks->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_tracks->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tracks->setShowGrid(false);
        m_tracks->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_tracks->setAlternatingRowColors(false);
        m_tracks->verticalHeader()->setDefaultSectionSize(80);
        m_tracks->verticalHeader()->hide();
        m_tracks->setMouseTracking(true);
        m_trackDelegate = new SongGlassDelegate(m_tracks);
        m_tracks->setItemDelegate(m_trackDelegate);
        content->addWidget(m_heading);
        content->addWidget(m_librarySummary);
        content->addWidget(m_tracks, 1);

        auto *retryLibrary = new JellyButton(QStringLiteral("重试未加载歌单"), m_drawer);
        retryLibrary->setFixedHeight(36);
        drawerContent->addWidget(retryLibrary);
        root->addWidget(m_contentPages, 1);
        m_dock = new PlaybackDock(central);
        m_nowPlaying = m_dock->titleLabel();
        m_dock->setTrack(QStringLiteral("尚未播放"), QStringLiteral("双击歌曲开始播放"));
        m_catalogPanel = new CatalogSearchPanel(central);
        m_catalogPanel->onTrackActivated = [this](int row) { playCatalogTrack(row); };
        m_catalogPanel->onMoreRequested = [this] {
            if (m_catalogPage > 0)
                loadMoreCatalogResults();
            else
                startCatalogSearch(true);
        };
        m_catalogPanel->onDismissed = [this] {
            m_searchTimer.stop();
            ++m_catalogRequest;
            m_catalogBusy = false;
            m_catalogPanel->setLoadingMore(false);
        };
        connect(m_catalogPanel->resultsTable()->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this] { loadVisibleCatalogCovers(); });
        qApp->installEventFilter(this);
        m_pages->addWidget(m_playerPage);
        statusBar()->showMessage(QStringLiteral("演示模式：平台账号未授权，当前显示本地示例数据。"));

        m_audioOutput = new QAudioOutput(this);
        m_player = new QMediaPlayer(this);
        m_player->setAudioOutput(m_audioOutput);
        m_audioSettingsTimer.setSingleShot(true);
        m_audioSettingsTimer.setTimerType(Qt::PreciseTimer);
        m_audioSettingsTimer.setInterval(350);
        connect(&m_audioSettingsTimer, &QTimer::timeout, this, [this] { saveMusicAudioSettings(); });
        connect(m_audioOutput, &QAudioOutput::volumeChanged, m_dock, [this](float volume) {
            m_dock->setVolume(volume);
        });
        connect(m_audioOutput, &QAudioOutput::mutedChanged, m_dock, [this](bool muted) {
            m_dock->setMuted(muted);
        });
        m_dock->onVolumeChanged = [this](qreal volume) {
            m_audioOutput->setVolume(volume);
            m_audioSettingsTimer.start();
        };
        m_dock->onMutedChanged = [this](bool muted) {
            m_audioOutput->setMuted(muted);
            m_audioSettingsTimer.start();
        };
        m_mediaDevices = new QMediaDevices(this);
        m_audioRouteTimer.setSingleShot(true);
        m_audioRouteTimer.setInterval(120);
        connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged,
                this, [this] { scheduleAudioOutputSync(); });
        connect(&m_audioRouteTimer, &QTimer::timeout,
                this, [this] { syncDefaultAudioOutput(); });
        m_coverNetwork = new QNetworkAccessManager(this);
        m_coverApplyTimer.setSingleShot(true);
        connect(&m_coverApplyTimer, &QTimer::timeout, this, [this] { flushTrackCovers(); });
        m_wallpaperCapture = new WallpaperEngineCapture(this);
        m_wallpaperCapture->setOwnerWindow(static_cast<quintptr>(winId()));
        m_wallpaperResizeTimer.setSingleShot(true);
        m_wallpaperResizeTimer.setInterval(100);
        connect(&m_wallpaperResizeTimer, &QTimer::timeout, this, [this] {
            m_wallpaperCapture->setRenderSize(wallpaperRenderPixels(m_surface));
        });
        m_wallpaperCapture->onFrame = [this](const QImage &frame) { m_surface->setBackgroundFrame(frame); };
        m_wallpaperCapture->onStatusChanged = [this](const QString &status) {
            statusBar()->showMessage(status, 5000);
            if (m_activeThemePanel)
                m_activeThemePanel->setWallpaperStatus(status);
        };
        m_wallpaperCapture->onError = [this](const QString &error) {
            statusBar()->showMessage(error, 10000);
            if (m_activeThemePanel)
                m_activeThemePanel->setWallpaperStatus(error);
        };
        m_surface->onBackgroundError = [this](const QString &error) { statusBar()->showMessage(error, 10000); };
        m_surface->onBackgroundActivityChanged = [this](bool active) {
            m_wallpaperCapture->setPaused(!active);
        };
        connect(homeButton, &QPushButton::clicked, this, [this] { showHomePage(); });
        connect(themeButton, &QPushButton::clicked, this, [this] { chooseBackgroundTheme(); });
        m_homePage->onRecommendationActivated = [this](MusicPlatform platform, int row) {
            playRecommendation(platform, row);
        };
        m_homePage->onConnectRequested = [this](MusicPlatform platform) {
            showPlatformLogin(platform);
        };
        m_songPage->onBackRequested = [this] { m_contentPages->setCurrentWidget(m_previousContentPage); };
        m_songPage->onSeekRequested = [this](qint64 position) {
            if (!m_sourceRequestPending && m_player->isSeekable())
                m_player->setPosition(position);
        };
        m_songPage->onCurrentLyricChanged = [this](const QString &text) { m_dock->setCurrentLyric(text); };
        m_dock->onSongPageRequested = [this] { showSongPage(); };
        m_dock->onLyricsRequested = [this] { showSongPage(); m_songPage->focusLyrics(); };
        connect(m_player, &QMediaPlayer::positionChanged, m_dock, [this](qint64 position) {
            m_dock->setPosition(position);
            m_songPage->setPosition(position);
        });
        connect(m_player, &QMediaPlayer::durationChanged, m_dock, [this](qint64 duration) {
            m_dock->setDuration(duration);
        });
        connect(m_player, &QMediaPlayer::seekableChanged, m_dock, [this](bool seekable) {
            m_dock->setSeekable(seekable && !m_sourceRequestPending);
            m_songPage->setSeekable(seekable && !m_sourceRequestPending);
        });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            m_dock->setPlaying(state == QMediaPlayer::PlayingState);
            m_songPage->setPlaying(state == QMediaPlayer::PlayingState);
            if (state == QMediaPlayer::PlayingState) {
                m_pendingTitle.clear();
                m_dock->setBusy(false);
                statusBar()->showMessage(QStringLiteral("正在播放"));
            } else if (state == QMediaPlayer::PausedState) {
                statusBar()->showMessage(QStringLiteral("已暂停"));
            } else if (!m_switchingTrack && m_pendingTitle.isEmpty()
                       && m_player->error() == QMediaPlayer::NoError
                       && m_player->mediaStatus() != QMediaPlayer::EndOfMedia) {
                statusBar()->showMessage(QStringLiteral("播放已停止"));
            }
        });
        connect(m_player, &QMediaPlayer::errorOccurred, this,
                [this](QMediaPlayer::Error error, const QString &errorString) {
            if (error == QMediaPlayer::NoError)
                return;
            m_pendingTitle.clear();
            m_sourceRequestPending = false;
            m_dock->setBusy(false);
            m_dock->setSeekable(false);
            m_player->stop();
            const QString message = errorString.isEmpty() ? QStringLiteral("未知错误") : errorString;
            statusBar()->showMessage(QStringLiteral("播放失败：%1").arg(message), 8000);
        });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
                [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) {
                m_pendingTitle.clear();
                const int request = m_playRequest;
                // Finish the media signal before replacing its source, and reject stale endings.
                QTimer::singleShot(0, this, [this, request] {
                    if (request != m_playRequest || m_sourceRequestPending || m_switchingTrack
                        || m_player->mediaStatus() != QMediaPlayer::EndOfMedia)
                        return;
                    if (m_playbackQueue.next(true))
                        playQueueTrack();
                    else {
                        m_dock->setPlaying(false);
                        statusBar()->showMessage(QStringLiteral("列表播放完毕"), 5000);
                    }
                });
            }
        });
        connect(m_platform, &QComboBox::currentIndexChanged, this, [this] {
            const QString query = m_search->text();
            const QSignalBlocker searchSignals(m_search);
            m_search->clear();
            m_searchTimer.stop();
            updateCatalogSearchHint();
            refresh(true);
            m_search->setText(query);
            if (!query.trimmed().isEmpty())
                startCatalogSearch();
        });
        connect(m_navigation, &QListWidget::currentRowChanged, this, [this] {
            showLibraryPage();
            const int section = m_navigation->currentRow();
            if (section >= 0 && section < 4)
                m_navigationButtons[section]->setChecked(true);
            {
                const QSignalBlocker localSignals(m_playlistSearch);
                m_playlistSearch->clear();
            }
            m_playlistSearchTimer.stop();
            if (!m_search->text().isEmpty())
                m_search->clear();
            else
                refresh();
        });
        connect(m_navigation, &QListWidget::itemClicked, this, [this] {
            if (!m_search->text().isEmpty())
                m_search->clear();
            else if (!m_playlistSearch->text().isEmpty())
                m_playlistSearch->clear();
        });
        m_searchTimer.setSingleShot(true);
        m_searchTimer.setInterval(280);
        connect(&m_searchTimer, &QTimer::timeout, this, [this] { startCatalogSearch(); });
        connect(m_search, &QLineEdit::returnPressed, this, [this] {
            m_searchTimer.stop();
            startCatalogSearch(true);
        });
        connect(m_search, &QLineEdit::textChanged, this, [this] {
            m_nameLookupFailures.clear();
            resetCatalogSearch();
            if (m_search->text().trimmed().isEmpty()) {
                m_searchTimer.stop();
                refresh();
            } else {
                const bool hadPlaylistFilter = !m_playlistSearch->text().trimmed().isEmpty();
                const QSignalBlocker localSignals(m_playlistSearch);
                m_playlistSearch->clear();
                m_playlistSearchTimer.stop();
                m_playlistSearchSummary->setText(QStringLiteral("当前歌单 %1 首 · 输入以筛选此歌单").arg(m_selectedPlaylistTracks.size()));
                if (hadPlaylistFilter && !m_selectedPlaylistId.isEmpty()) {
                    setTracks(m_selectedPlaylistTracks);
                    m_heading->setText(m_selectedPlaylistName);
                }
                m_catalogPanel->setLoading(m_search->text().trimmed());
                showCatalogPanel();
                m_searchTimer.start();
            }
        });
        m_playlistSearchTimer.setSingleShot(true);
        m_playlistSearchTimer.setInterval(180);
        connect(&m_playlistSearchTimer, &QTimer::timeout, this, [this] { applyPlaylistSearch(); });
        connect(m_playlistSearch, &QLineEdit::textChanged, this, [this] {
            showLibraryPage();
            m_nameLookupFailures.clear();
            if (m_playlistSearch->text().trimmed().isEmpty()) {
                m_playlistSearchTimer.stop();
                applyPlaylistSearch();
            } else {
                m_playlistSearchTimer.start();
            }
        });
        connect(m_playlistList, &QListWidget::currentRowChanged, this, [this] { showLibraryPage(); showSelectedPlaylist(); });
        connect(m_playlistList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *) { showLibraryPage(); showSelectedPlaylist(); });
        connect(m_tracks->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { loadVisibleTrackCovers(); });
        connect(m_tracks, &QTableWidget::cellDoubleClicked, this, [this] { playSelected(); });
        connect(retryLibrary, &QPushButton::clicked, this, [this] {
            if (m_kugouAccountConnected)
                startLibrarySync(true);
        });
        m_dock->onPlayPause = [this] { togglePlayback(); };
        m_dock->onPrevious = [this] {
            if (m_playbackQueue.previous())
                playQueueTrack();
            else
                playSelected();
        };
        m_dock->onNext = [this] {
            if (m_playbackQueue.next())
                playQueueTrack();
            else
                playSelected();
        };
        m_dock->onShuffleChanged = [this](bool shuffle) {
            m_playbackQueue.setShuffle(shuffle);
            QSettings().setValue(QStringLiteral("playback/shuffle"), shuffle);
            statusBar()->showMessage(shuffle ? QStringLiteral("随机播放 · 一轮内不重复")
                                            : QStringLiteral("顺序播放 · 按当前播放列表顺序"), 3000);
        };
        m_dock->onSeekRequested = [this](qint64 position) {
            if (!m_sourceRequestPending && m_player->isSeekable() && m_player->duration() > 0)
                m_player->setPosition(qBound<qint64>(0, position, m_player->duration()));
        };
        m_dock->onColorRequested = [this] { chooseAeroColor(); };
        m_dock->onPinnedChanged = [](bool pinned) {
            QSettings().setValue(QStringLiteral("appearance/dockPinned"), pinned);
        };
        connect(m_loginPage->continueButton(), &QPushButton::clicked, this, [this] { completeSetup(); });
        connect(m_loginPage->kugouLoginButton(), &QPushButton::clicked, this, [this] {
            QVector<MusicPlatform> selected = m_loginPage->selectedPlatforms();
            if (!selected.contains(MusicPlatform::Kugou)) {
                selected.push_back(MusicPlatform::Kugou);
                m_loginPage->setSelectedPlatforms(selected);
            }
            m_kugouApi.startQrLogin();
        });
        if (m_loginPage->neteaseLoginButton()) {
            connect(m_loginPage->neteaseLoginButton(), &QPushButton::clicked, this, [this] {
                if (!ReleaseConfig::platformAvailable(MusicPlatform::NetEaseCloud)) return;
                QVector<MusicPlatform> selected = m_loginPage->selectedPlatforms();
                if (!selected.contains(MusicPlatform::NetEaseCloud)) {
                    selected.push_back(MusicPlatform::NetEaseCloud);
                    m_loginPage->setSelectedPlatforms(selected);
                }
                m_neteaseApi.startQrLogin();
            });
        }
        connect(platformsButton, &QPushButton::clicked, this, [this] {
            m_catalogPanel->dismiss();
            m_loginPage->setSelectedPlatforms(m_enabledPlatforms);
            m_pages->setCurrentWidget(m_loginPage);
        });

        m_kugouApi.onStatusChanged = [this](const QString &status) {
            m_loginPage->setKugouStatus(status);
            statusBar()->showMessage(status);
        };
        m_kugouApi.onError = [this](const QString &error) {
            m_loginPage->setKugouStatus(QStringLiteral("连接失败"));
            statusBar()->showMessage(error, 10000);
        };
        m_kugouApi.onQrCodeReady = [this](const QImage &image) {
            m_loginPage->setKugouQr(image);
        };
        m_kugouApi.onPlaylistsReady = [this](const QVector<Playlist> &playlists) {
            ++m_playRequest;
            m_pendingTitle.clear();
            m_sourceRequestPending = false;
            m_playbackQueue.setTracks({}, -1);
            m_player->stop();
            m_player->setSource(QUrl());
            m_dock->setBusy(false);
            m_dock->setTrack(QStringLiteral("尚未播放"), QStringLiteral("双击歌曲开始播放"));
            ++m_kugouGeneration;
            resetCatalogSearch();
            m_kugouTrackCache.clear();
            m_loadingPlaylists.clear();
            m_completedPlaylists.clear();
            m_libraryErrors.clear();
            m_librarySyncActive = false;
            m_libraryViewPending = false;
            m_drawerSignature.clear();
            m_nameMatches.clear();
            m_nameLookups.clear();
            m_nameLookupFailures.clear();
            m_kugouPlaylists = playlists;
            m_kugouAccountConnected = true;
            m_dailyTracks.remove(static_cast<int>(MusicPlatform::Kugou));
            m_recommendationLoaded.remove(static_cast<int>(MusicPlatform::Kugou));
            m_homePage->setRecommendations(MusicPlatform::Kugou, {});
            m_loginPage->clearKugouQr();
            requestRecommendations(MusicPlatform::Kugou, true);
            QVector<MusicPlatform> selected = m_loginPage->selectedPlatforms();
            if (!selected.contains(MusicPlatform::Kugou))
                selected.push_back(MusicPlatform::Kugou);
            m_loginPage->setSelectedPlatforms(selected);

            QStringList keys;
            for (MusicPlatform platform : selected)
                keys.push_back(platformKey(platform));
            QSettings settings;
            settings.setValue(QStringLiteral("selectedPlatforms"), keys);

            if (m_pages->currentWidget() == m_playerPage) {
                configurePlatforms(selected);
                refresh();
            }
            startLibrarySync();
        };

        m_neteaseApi.onStatusChanged = [this](const QString &status) {
            if (!ReleaseConfig::platformAvailable(MusicPlatform::NetEaseCloud)) return;
            m_loginPage->setNeteaseStatus(status);
            statusBar()->showMessage(status, 5000);
        };
        m_neteaseApi.onError = [this](const QString &error) {
            if (!ReleaseConfig::platformAvailable(MusicPlatform::NetEaseCloud)) return;
            m_loginPage->setNeteaseStatus(QStringLiteral("连接失败"));
            statusBar()->showMessage(error, 10000);
        };
        m_neteaseApi.onQrCodeReady = [this](const QImage &image) {
            if (ReleaseConfig::platformAvailable(MusicPlatform::NetEaseCloud)) m_loginPage->setNeteaseQr(image);
        };
        m_neteaseApi.onLoginChanged = [this](bool connected) {
            if (!ReleaseConfig::platformAvailable(MusicPlatform::NetEaseCloud)) return;
            ++m_recommendationRequests[static_cast<int>(MusicPlatform::NetEaseCloud)];
            m_dailyTracks.remove(static_cast<int>(MusicPlatform::NetEaseCloud));
            m_recommendationLoaded.remove(static_cast<int>(MusicPlatform::NetEaseCloud));
            m_recommendationBusy.remove(static_cast<int>(MusicPlatform::NetEaseCloud));
            m_homePage->setRecommendations(MusicPlatform::NetEaseCloud, {});
            if (connected) {
                m_loginPage->clearNeteaseQr();
                QVector<MusicPlatform> selected = m_loginPage->selectedPlatforms();
                if (!selected.contains(MusicPlatform::NetEaseCloud))
                    selected.push_back(MusicPlatform::NetEaseCloud);
                m_loginPage->setSelectedPlatforms(selected);
                QStringList keys;
                for (MusicPlatform platform : selected)
                    keys.append(platformKey(platform));
                QSettings().setValue(QStringLiteral("selectedPlatforms"), keys);
                if (m_pages->currentWidget() == m_playerPage) {
                    configurePlatforms(selected);
                    refresh();
                }
            }
            requestRecommendations(MusicPlatform::NetEaseCloud, true);
        };

        QSettings appearance;
        bool validVolume = false;
        const qreal storedVolume = appearance.value(QStringLiteral("audio/musicVolume"), 1.0).toDouble(&validVolume);
        const qreal musicVolume = validVolume && qIsFinite(storedVolume) ? qBound<qreal>(0, storedVolume, 1) : 1;
        m_audioOutput->setVolume(musicVolume);
        m_audioOutput->setMuted(appearance.value(QStringLiteral("audio/musicMuted"), false).toBool());
        m_dock->setVolume(musicVolume);
        m_dock->setMuted(m_audioOutput->isMuted());
        QColor accent = appearance.value(QStringLiteral("appearance/accent"), Aero::defaultAccent()).value<QColor>();
        if (!accent.isValid())
            accent = Aero::defaultAccent();
        applyAeroTheme(accent);
        applyBackgroundTheme(loadBackgroundTheme(appearance));
        pinDrawer->setChecked(appearance.value(QStringLiteral("appearance/drawerPinned"), false).toBool());
        m_dock->setPinned(appearance.value(QStringLiteral("appearance/dockPinned"), false).toBool());
        const bool shuffle = appearance.value(QStringLiteral("playback/shuffle"), false).toBool();
        m_playbackQueue.setShuffle(shuffle);
        m_dock->setShuffle(shuffle);

        if (setupCompleted && !allowedPlatforms.isEmpty()) {
            configurePlatforms(allowedPlatforms);
            m_pages->setCurrentWidget(m_playerPage);
            refresh();
        } else {
            m_pages->setCurrentWidget(m_loginPage);
        }
        if (!smokeMode && allowedPlatforms.contains(MusicPlatform::Kugou))
            m_kugouApi.restoreSession();
        if (!smokeMode && allowedPlatforms.contains(MusicPlatform::NetEaseCloud))
            m_neteaseApi.restoreSession();
        m_previousContentPage = m_homePage;
        m_contentPages->setCurrentWidget(m_homePage);
        requestRecommendations(MusicPlatform::Kugou);
        requestRecommendations(MusicPlatform::NetEaseCloud);
        m_dayTimer.setInterval(30000);
        connect(&m_dayTimer, &QTimer::timeout, this, [this] {
            const QDate date = QDate::currentDate();
            if (date != m_recommendationDate) {
                m_recommendationDate = date;
                requestRecommendations(MusicPlatform::Kugou, true);
                requestRecommendations(MusicPlatform::NetEaseCloud, true);
            }
        });
        m_recommendationDate = QDate::currentDate();
        m_dayTimer.start();
    }

    ~PlayerWindow() override
    {
        if (m_audioSettingsTimer.isActive()) {
            m_audioSettingsTimer.stop();
            saveMusicAudioSettings();
        }
        qApp->removeEventFilter(this);
        if (m_wallpaperCapture)
            m_wallpaperCapture->stop();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!m_catalogPanel || !m_search)
            return QMainWindow::eventFilter(watched, event);
        if ((watched == this || watched == m_surface) && event->type() == QEvent::DevicePixelRatioChange)
            QTimer::singleShot(0, this, [this] { refreshDevicePixelRatioAssets(); });
        if (watched == m_search) {
            if (event->type() == QEvent::FocusIn && !m_search->text().trimmed().isEmpty()) {
                QTimer::singleShot(0, this, [this] { startCatalogSearch(); });
            } else if (event->type() == QEvent::KeyPress) {
                const auto *key = static_cast<QKeyEvent *>(event);
                if (key->key() == Qt::Key_Escape) {
                    m_searchTimer.stop();
                    m_catalogPanel->dismiss();
                    return true;
                }
                if (key->key() == Qt::Key_Down && m_catalogPanel->isVisible()
                    && m_catalogPanel->resultsTable()->rowCount() > 0) {
                    m_catalogPanel->resultsTable()->setCurrentCell(0, 1);
                    m_catalogPanel->resultsTable()->setFocus();
                    return true;
                }
            }
        }
        if (event->type() == QEvent::MouseButtonPress && m_catalogPanel->isVisible()) {
            auto *widget = qobject_cast<QWidget *>(watched);
            if (widget && widget != m_search && !m_search->isAncestorOf(widget)
                && widget != m_catalogPanel && !m_catalogPanel->isAncestorOf(widget)) {
                m_searchTimer.stop();
                m_catalogPanel->dismiss();
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QMainWindow::resizeEvent(event);
        QTimer::singleShot(0, this, [this] {
            if (auto *brand = findChild<QLabel *>(QStringLiteral("toolbarBrand")))
                brand->setVisible(width() >= 1080);
            if (m_edgeHandle && m_playerPage)
                m_edgeHandle->setGeometry(0, qMax(80, m_playerPage->height() / 2 - 32), 28, 64);
            if (m_drawer && m_navigation && m_playlistSearchScope) {
                const bool compact = m_playerPage->height() < 640;
                m_drawer->contentLayout()->setContentsMargins(compact ? 12 : 16, compact ? 12 : 16,
                                                               compact ? 12 : 16, compact ? 12 : 16);
                m_drawer->contentLayout()->setSpacing(compact ? 6 : 10);
                for (auto *button : m_navigationButtons)
                    button->setFixedHeight(compact ? 32 : 38);
                m_navigationControls->setFixedHeight(compact ? 72 : 84);
                m_drawerHint->setVisible(!compact);
                m_drawerListSummary->setVisible(!compact);
                m_playlistSearchScope->setWordWrap(!compact);
            }
            if (m_catalogPanel && m_catalogPanel->isVisible())
                showCatalogPanel();
            if (m_wallpaperCapture) {
                m_wallpaperResizeTimer.start();
                m_wallpaperCapture->setFrameRate(Aero::displayRefreshRate(m_surface));
            }
            if (m_drawer && m_toolbarPanel)
                m_drawer->setTopInset(m_toolbarPanel->geometry().bottom() + 12);
        });
    }

#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override
    {
        const auto *nativeMessage = static_cast<MSG *>(message);
        if (nativeMessage->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
            RECT bounds{};
            GetWindowRect(nativeMessage->hwnd, &bounds);
            const int x = static_cast<short>(LOWORD(nativeMessage->lParam));
            const int y = static_cast<short>(HIWORD(nativeMessage->lParam));
            const int edge = qMax(5, qRound(6 * devicePixelRatioF()));
            const bool left = x >= bounds.left && x < bounds.left + edge;
            const bool right = x < bounds.right && x >= bounds.right - edge;
            const bool top = y >= bounds.top && y < bounds.top + edge;
            const bool bottom = y < bounds.bottom && y >= bounds.bottom - edge;
            if (top && left) *result = HTTOPLEFT;
            else if (top && right) *result = HTTOPRIGHT;
            else if (bottom && left) *result = HTBOTTOMLEFT;
            else if (bottom && right) *result = HTBOTTOMRIGHT;
            else if (left) *result = HTLEFT;
            else if (right) *result = HTRIGHT;
            else if (top) *result = HTTOP;
            else if (bottom) *result = HTBOTTOM;
            else return QMainWindow::nativeEvent(eventType, message, result);
            return true;
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }
#endif

private:
    void saveMusicAudioSettings()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("audio/musicVolume"), m_audioOutput->volume());
        settings.setValue(QStringLiteral("audio/musicMuted"), m_audioOutput->isMuted());
        settings.sync();
    }

    void showUpdatePanel()
    {
        m_dock->setInteractionHeld(true);
        UpdatePanel panel(QString::fromUtf8(PHSRADIO_VERSION), m_accent, m_surface, this);
        panel.onInstallerStarted = [this] {
            m_player->stop();
            m_wallpaperCapture->stop();
            QSettings settings;
            settings.sync();
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        };
        panel.exec();
        m_dock->setInteractionHeld(false);
    }

    void refreshDevicePixelRatioAssets()
    {
        const qreal dpr = m_surface->devicePixelRatioF();
        if (qFuzzyCompare(dpr, m_thumbnailDpr))
            return;
        m_thumbnailDpr = dpr;
        ++m_trackCoverGeneration;
        m_readyTrackCovers.clear();
        m_requestedCovers.clear();
        m_requestedCatalogCovers.clear();
        m_catalogCoverIcons.clear();
        m_drawerSignature.clear();
        rebuildDrawerPlaylists();
        loadVisibleTrackCovers();
        loadVisibleCatalogCovers();
        m_wallpaperCapture->setRenderSize(wallpaperRenderPixels(m_surface));
        m_wallpaperCapture->setFrameRate(Aero::displayRefreshRate(m_surface));
    }

    void showLibraryPage()
    {
        if (m_contentPages && m_libraryPage) {
            m_contentPages->setCurrentWidget(m_libraryPage);
            m_previousContentPage = m_libraryPage;
            QTimer::singleShot(0, this, [this] { loadVisibleTrackCovers(); });
        }
    }

    void showHomePage()
    {
        m_catalogPanel->dismiss();
        m_contentPages->setCurrentWidget(m_homePage);
        m_previousContentPage = m_homePage;
        requestRecommendations(MusicPlatform::Kugou);
        requestRecommendations(MusicPlatform::NetEaseCloud);
    }

    void showSongPage()
    {
        if (m_contentPages->currentWidget() != m_songPage)
            m_previousContentPage = m_contentPages->currentWidget();
        m_catalogPanel->dismiss();
        m_contentPages->setCurrentWidget(m_songPage);
        m_songPage->setPosition(m_player->position());
    }

    void showPlatformLogin(MusicPlatform platform)
    {
        if (!ReleaseConfig::platformAvailable(platform)) return;
        if ((platform == MusicPlatform::Kugou && m_kugouAccountConnected)
            || (platform == MusicPlatform::NetEaseCloud && m_neteaseApi.isLoggedIn())) {
            requestRecommendations(platform, true);
            return;
        }
        m_catalogPanel->dismiss();
        QVector<MusicPlatform> selected = m_enabledPlatforms;
        if (!selected.contains(platform))
            selected.push_back(platform);
        m_loginPage->setSelectedPlatforms(selected);
        m_pages->setCurrentWidget(m_loginPage);
        if (platform == MusicPlatform::Kugou)
            m_kugouApi.startQrLogin();
        else if (platform == MusicPlatform::NetEaseCloud)
            m_neteaseApi.startQrLogin();
    }

    void requestRecommendations(MusicPlatform platform, bool force = false)
    {
        if (!ReleaseConfig::platformAvailable(platform)) return;
        const int key = static_cast<int>(platform);
        const bool connected = platform == MusicPlatform::Kugou ? m_kugouAccountConnected : m_neteaseApi.isLoggedIn();
        if (!connected) {
            m_homePage->setRecommendationState(platform, QStringLiteral("连接%1，查看你的每日推荐").arg(platformName(platform)));
            return;
        }
        if (!force && (m_recommendationBusy.contains(key)
            || m_recommendationLoaded.value(key) == QDate::currentDate()))
            return;
        const int request = ++m_recommendationRequests[key];
        const QDate date = QDate::currentDate();
        m_recommendationBusy.insert(key);
        m_homePage->setRecommendationState(platform, QStringLiteral("正在读取今日推荐…"), true);
        const QPointer<PlayerWindow> guard(this);
        auto ready = [guard, platform, key, request, date](const QVector<Track> &tracks, const QString &error) {
            if (!guard || guard->m_recommendationRequests.value(key) != request)
                return;
            guard->m_recommendationBusy.remove(key);
            if (!error.isEmpty()) {
                guard->m_homePage->setRecommendationState(platform, error);
                return;
            }
            guard->m_dailyTracks.insert(key, tracks);
            guard->m_recommendationLoaded.insert(key, date);
            guard->m_homePage->setRecommendations(platform, tracks);
            guard->m_homePage->setRecommendationState(platform, tracks.isEmpty()
                ? QStringLiteral("平台暂未返回今日推荐，可稍后重试") : QStringLiteral("今日为你推荐 %1 首").arg(tracks.size()));
            guard->loadRecommendationCovers(platform, request);
        };
        if (platform == MusicPlatform::Kugou) {
            PlaylistSnapshotProvider source(platform, {}, &m_kugouApi);
            source.fetchDailyRecommendations(std::move(ready));
        } else {
            NeteaseMusicProvider source(&m_neteaseApi);
            source.fetchDailyRecommendations(std::move(ready));
        }
    }

    void loadRecommendationCovers(MusicPlatform platform, int request)
    {
        const int key = static_cast<int>(platform);
        const auto tracks = m_dailyTracks.value(key);
        const QPointer<PlayerWindow> guard(this);
        for (int row = 0; row < tracks.size(); ++row) {
            const Track track = tracks.at(row);
            auto urlReady = [guard, platform, key, request, row](const QUrl &url) {
                if (!guard || guard->m_recommendationRequests.value(key) != request || url.isEmpty())
                    return;
                guard->requestHighResolutionCover(url, [guard, platform, key, request, row](const QPixmap &image) {
                    if (guard && guard->m_recommendationRequests.value(key) == request)
                        guard->m_homePage->setCover(platform, row, image);
                });
            };
            if (!track.coverUrl.isEmpty())
                urlReady(track.coverUrl);
            else if (platform == MusicPlatform::Kugou)
                m_kugouApi.fetchTrackCover(track, std::move(urlReady));
        }
    }

    void playRecommendation(MusicPlatform platform, int row)
    {
        if (!ReleaseConfig::platformAvailable(platform)) return;
        const auto tracks = m_dailyTracks.value(static_cast<int>(platform));
        if (row < 0 || row >= tracks.size())
            return;
        m_queuePlatform = platform;
        m_queueUsesKugou = platform == MusicPlatform::Kugou;
        m_playbackQueue.setTracks(tracks, row);
        playQueueTrack();
        m_dock->setExpanded(true);
        m_dock->triggerPlaybackMeteor();
        showSongPage();
    }

    void chooseBackgroundTheme()
    {
        m_dock->setInteractionHeld(true);
        ThemePanel panel(this);
        panel.setTheme(m_surface->backgroundTheme());
        panel.setWallpaperStatus(m_wallpaperCapture->statusText());
        panel.diagnosticsProvider = [this] { return m_wallpaperCapture->diagnosticReport(); };
        panel.onThemeSelected = [this](const BackgroundTheme &theme) { applyBackgroundTheme(theme); };
        m_activeThemePanel = &panel;
        panel.exec();
        m_activeThemePanel.clear();
        m_dock->setInteractionHeld(false);
        QTimer::singleShot(0, this, [this] {
            m_wallpaperCapture->setPaused(!m_surface->synchronizeBackgroundActivity());
        });
    }

    void applyBackgroundTheme(const BackgroundTheme &theme)
    {
        const BackgroundTheme previous = m_surface->backgroundTheme();
        const bool sameEngineSource = theme.kind == BackgroundKind::WallpaperEngine
            && previous.kind == theme.kind && previous.sourcePath == theme.sourcePath
            && previous.engineExecutable == theme.engineExecutable;
        QString error;
        if (!m_surface->setBackground(theme, &error)) {
            statusBar()->showMessage(error, 10000);
            return;
        }
        if (!sameEngineSource)
            m_wallpaperCapture->stop();
        if (theme.kind == BackgroundKind::WallpaperEngine && (!sameEngineSource || !m_wallpaperCapture->isRequested()))
            m_wallpaperCapture->start(theme.engineExecutable, theme.sourcePath,
                                      wallpaperRenderPixels(m_surface));
        m_wallpaperCapture->setFrameRate(Aero::displayRefreshRate(m_surface));
        m_wallpaperCapture->setPaused(!m_surface->synchronizeBackgroundActivity());
        QSettings appearance;
        saveBackgroundTheme(appearance, theme);
    }

    void requestHighResolutionCover(const QUrl &url, std::function<void(const QPixmap &)> callback)
    {
        if (url.isEmpty())
            return;
        const QString key = url.toString();
        if (const QPixmap *cached = m_highResolutionCovers.object(key)) {
            callback(*cached);
            return;
        }
        if (m_highResolutionPending.contains(key)) {
            m_highResolutionPending[key].push_back(std::move(callback));
            return;
        }
        m_highResolutionPending[key].push_back(std::move(callback));
        QNetworkRequest request(url);
        request.setTransferTimeout(12000);
        auto *reply = m_coverNetwork->get(request);
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
            if (received > 16 * 1024 * 1024 || total > 16 * 1024 * 1024)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
            QByteArray bytes;
            if (reply->error() == QNetworkReply::NoError)
                bytes = reply->readAll();
            reply->deleteLater();
            const auto ready = [this, key](const QImage &decoded) {
                const auto callbacks = m_highResolutionPending.take(key);
                if (decoded.isNull())
                    return;
                const QPixmap image = QPixmap::fromImage(decoded);
                const int cost = qMax(1, int(qint64(image.width()) * image.height() * 4 / 1024));
                m_highResolutionCovers.insert(key, new QPixmap(image), cost);
                for (const auto &done : callbacks)
                    done(image);
            };
            if (!m_coverDecoder.decode(std::move(bytes), {}, ready))
                ready({});
        });
    }

    void loadSongDetails(const Track &track, int request)
    {
        const MusicPlatform platform = m_queuePlatform;
        m_songPage->setTrack(track, platform);
        m_songPage->setCover(QPixmap());
        m_songPage->setLyrics({}, QStringLiteral("正在加载歌词…"));
        m_songPage->setSeekable(false);
        m_dock->setCurrentLyric(QString());
        const QPointer<PlayerWindow> guard(this);
        auto coverReady = [guard, request](const QUrl &url) {
            if (!guard || request != guard->m_playRequest || url.isEmpty())
                return;
            guard->requestHighResolutionCover(url, [guard, request](const QPixmap &image) {
                if (guard && request == guard->m_playRequest)
                    guard->m_songPage->setCover(image);
            });
        };
        if (platform == MusicPlatform::Kugou && m_kugouAccountConnected)
            m_kugouApi.fetchTrackCover(track, std::move(coverReady));
        else
            coverReady(track.coverUrl);
        auto lyricsReady = [guard, request](const TrackLyrics &lyrics, const QString &error) {
            if (!guard || request != guard->m_playRequest)
                return;
            guard->m_songPage->setLyrics(lyrics.lines, error.isEmpty()
                ? (lyrics.plainText.isEmpty() ? QStringLiteral("暂无歌词") : lyrics.plainText)
                : QStringLiteral("歌词暂不可用，可继续播放歌曲"));
            guard->m_songPage->setPosition(guard->m_player->position());
            if (!error.isEmpty())
                guard->m_dock->setCurrentLyric(QStringLiteral("歌词暂不可用"));
        };
        if (platform == MusicPlatform::Kugou && m_kugouAccountConnected) {
            PlaylistSnapshotProvider source(platform, {}, &m_kugouApi);
            source.fetchLyrics(track, std::move(lyricsReady));
        } else if (platform == MusicPlatform::NetEaseCloud && m_neteaseApi.isLoggedIn()) {
            NeteaseMusicProvider source(&m_neteaseApi);
            source.fetchLyrics(track, std::move(lyricsReady));
        } else {
            m_songPage->setLyrics({}, QStringLiteral("暂无歌词"));
        }
    }

    void updateCatalogSearchHint()
    {
        m_search->setPlaceholderText(QStringLiteral("搜索酷狗全平台歌曲、歌手"));
    }

    const MusicProvider *catalogProvider() const
    {
        // Catalog integrations are independent of the playlist browser. Add
        // other real online providers here when their integrations are ready.
        for (const auto &entry : m_providers)
            if (entry->platform() == MusicPlatform::Kugou)
                return entry.get();
        return nullptr;
    }

    void resetCatalogSearch()
    {
        ++m_catalogRequest;
        m_catalogBusy = false;
        m_catalogPage = 0;
        m_catalogTotal = -1;
        m_catalogHasMore = false;
        m_catalogQuery.clear();
        m_catalogTracks.clear();
        m_requestedCatalogCovers.clear();
        if (m_catalogPanel)
            m_catalogPanel->dismiss();
    }

    void showCatalogPanel()
    {
        if (!m_catalogPanel || !m_search || m_pages->currentWidget() != m_playerPage)
            return;
        m_catalogPanel->showAnchored(QRect(m_search->mapTo(m_playerPage, QPoint()), m_search->size()));
    }

    void startCatalogSearch(bool force = false)
    {
        const QString query = m_search->text().trimmed();
        const MusicProvider *active = catalogProvider();
        if (query.isEmpty()) {
            resetCatalogSearch();
            return;
        }
        const MusicPlatform platform = MusicPlatform::Kugou;
        if (!force && query == m_catalogQuery && platform == m_catalogPlatform
            && (m_catalogBusy || m_catalogPage > 0)) {
            showCatalogPanel();
            return;
        }
        resetCatalogSearch();
        m_catalogQuery = query;
        m_catalogPlatform = platform;
        m_catalogBusy = true;
        m_catalogPanel->setLoading(query);
        showCatalogPanel();
        if (!m_kugouAccountConnected || !active) {
            m_catalogBusy = false;
            m_catalogPanel->setError(QStringLiteral("请先在“平台连接”中登录酷狗，再搜索在线曲库。"));
            return;
        }
        requestCatalogPage(1);
    }

    void loadMoreCatalogResults()
    {
        if (m_catalogBusy || !m_catalogHasMore || m_catalogPage < 1 || !catalogProvider()
            || !m_kugouAccountConnected || m_search->text().trimmed() != m_catalogQuery)
            return;
        m_catalogBusy = true;
        m_catalogPanel->setLoadingMore(true);
        requestCatalogPage(m_catalogPage + 1);
    }

    void requestCatalogPage(int page)
    {
        const MusicProvider *active = catalogProvider();
        if (!active) {
            m_catalogBusy = false;
            m_catalogPanel->setError(QStringLiteral("请先连接酷狗账号，再搜索在线曲库。"));
            return;
        }
        const int request = ++m_catalogRequest;
        const int generation = m_kugouGeneration;
        const MusicPlatform platform = m_catalogPlatform;
        const QString query = m_catalogQuery;
        const QPointer<PlayerWindow> guard(this);
        active->searchCatalog(query, page,
            [guard, request, generation, platform, query, page](const CatalogSearchPage &result, const QString &error) {
                if (guard)
                    guard->finishCatalogSearch(request, generation, platform, query, page, result, error);
            });
    }

    void finishCatalogSearch(int request, int generation, MusicPlatform platform, const QString &query,
                             int page, const CatalogSearchPage &result, const QString &error)
    {
        // Playlist synchronization replaces provider objects. Identity is the
        // catalog source and account generation, never the playlist browser.
        if (request != m_catalogRequest || generation != m_kugouGeneration || !m_kugouAccountConnected || !catalogProvider()
            || catalogProvider()->platform() != platform || m_catalogQuery != query
            || m_search->text().trimmed() != query)
            return;
        m_catalogBusy = false;
        m_catalogPanel->setLoadingMore(false);
        if (!error.isEmpty()) {
            m_catalogPanel->setError(error);
            return;
        }
        QSet<QString> known;
        for (const CatalogTrack &item : m_catalogTracks)
            known.insert(platformKey(item.platform) + QLatin1Char(':') + trackKey(item.track));
        for (const CatalogTrack &item : result.tracks) {
            if (item.platform != platform)
                continue;
            const QString key = platformKey(item.platform) + QLatin1Char(':') + trackKey(item.track);
            if (!known.contains(key)) {
                known.insert(key);
                m_catalogTracks.append(item);
            }
        }
        m_catalogPage = page;
        if (result.total >= 0)
            m_catalogTotal = result.total;
        m_catalogHasMore = result.hasMore && !result.tracks.isEmpty();
        m_catalogPanel->setResults(query, m_catalogTracks, m_catalogTotal, m_catalogHasMore);
        showCatalogPanel();
        loadVisibleCatalogCovers();
    }

    void playCatalogTrack(int row)
    {
        if (row < 0 || row >= m_catalogTracks.size())
            return;
        const CatalogTrack selected = m_catalogTracks.at(row);
        if (selected.platform != MusicPlatform::Kugou) {
            statusBar()->showMessage(QStringLiteral("%1的在线播放尚未接入。").arg(platformName(selected.platform)), 5000);
            return;
        }
        if (!m_kugouAccountConnected && selected.track.audioUrl.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("请先登录酷狗再播放。"), 5000);
            return;
        }
        QVector<Track> tracks;
        int selectedIndex = -1;
        for (int index = 0; index < m_catalogTracks.size(); ++index) {
            const CatalogTrack &item = m_catalogTracks.at(index);
            if (item.platform == selected.platform) {
                if (index == row)
                    selectedIndex = tracks.size();
                tracks.append(item.track);
            }
        }
        m_queueUsesKugou = selected.platform == MusicPlatform::Kugou;
        m_queuePlatform = selected.platform;
        m_playbackQueue.setTracks(tracks, selectedIndex);
        playQueueTrack();
        m_dock->setExpanded(true);
        m_dock->triggerPlaybackMeteor();
    }

    void loadVisibleCatalogCovers()
    {
        if (!m_catalogPanel->isVisible() || m_catalogTracks.isEmpty())
            return;
        QTableWidget *table = m_catalogPanel->resultsTable();
        const int first = qMax(0, table->rowAt(0));
        int last = table->rowAt(table->viewport()->height() - 1);
        if (last < first)
            last = qMin(static_cast<int>(m_catalogTracks.size()) - 1, first + 8);
        const QString query = m_catalogQuery;
        const int generation = m_kugouGeneration;
        const qreal requestDpr = m_surface->devicePixelRatioF();
        const QPointer<PlayerWindow> guard(this);
        for (int row = first; row <= last; ++row) {
            const CatalogTrack item = m_catalogTracks.at(row);
            const QString key = platformKey(item.platform) + QLatin1Char(':') + trackKey(item.track);
            if (m_catalogCoverIcons.contains(key)) {
                if (auto *cell = table->item(row, 0))
                    cell->setIcon(m_catalogCoverIcons.value(key));
                continue;
            }
            if (m_requestedCatalogCovers.contains(key))
                continue;
            m_requestedCatalogCovers.insert(key);
            auto apply = [guard, query, generation, key, requestDpr](const QUrl &url) {
                if (!guard || url.isEmpty() || generation != guard->m_kugouGeneration || query != guard->m_catalogQuery)
                    return;
                if (!qFuzzyCompare(requestDpr, guard->m_surface->devicePixelRatioF()))
                    return;
                guard->requestCover(url, [guard, query, generation, key, requestDpr](const QIcon &icon) {
                    if (!guard || generation != guard->m_kugouGeneration || query != guard->m_catalogQuery)
                        return;
                    if (!qFuzzyCompare(requestDpr, guard->m_surface->devicePixelRatioF()))
                        return;
                    if (icon.isNull()) {
                        guard->m_requestedCatalogCovers.remove(key);
                        return;
                    }
                    guard->m_catalogCoverIcons.insert(key, icon);
                    for (int index = 0; index < guard->m_catalogTracks.size(); ++index) {
                        const CatalogTrack &candidate = guard->m_catalogTracks.at(index);
                        if (platformKey(candidate.platform) + QLatin1Char(':') + trackKey(candidate.track) == key)
                            if (auto *cell = guard->m_catalogPanel->resultsTable()->item(index, 0))
                                cell->setIcon(icon);
                    }
                });
            };
            if (!item.track.coverUrl.isEmpty())
                apply(item.track.coverUrl);
            else if (item.platform == MusicPlatform::Kugou && m_kugouAccountConnected)
                m_kugouApi.fetchTrackCover(item.track, apply);
        }
    }

    void applyAeroTheme(const QColor &accent)
    {
        m_accent = accent;
        const QString tone = QStringLiteral("%1,%2,%3").arg(accent.red()).arg(accent.green()).arg(accent.blue());
        setStyleSheet(QStringLiteral(
            "QMainWindow, QStackedWidget { background: #070a0f; }"
            "QWidget { color: #e9f1f9; }"
            "QLabel { background: transparent; }"
            "QLineEdit, QComboBox { color: #e9f1f9; background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(255,255,255,16),stop:1 rgba(0,0,0,32)); border: 1px solid rgba(255,255,255,34); border-top: 1px solid rgba(255,255,255,65); border-radius: 16px; padding: 10px 12px; selection-background-color: rgba(%1,80); }"
            "QLineEdit:focus { border: 1px solid rgba(%1,110); background: rgba(255,255,255,13); }"
            "QComboBox::drop-down { border: 0; width: 22px; }"
            "QComboBox QAbstractItemView { background: #121922; color: #e9f1f9; border: 1px solid #343e48; selection-background-color: rgba(%1,38); }"
            "QListWidget { background: transparent; border: 0; border-radius: 16px; outline: 0; }"
            "QListWidget::item { color: #e9f1f9; background: rgba(255,255,255,5); border: 1px solid rgba(255,255,255,22); border-radius: 14px; padding: 6px; }"
            "QListWidget::item:hover { background: rgba(255,255,255,14); border-color: rgba(255,255,255,60); }"
            "QListWidget::item:selected { background: rgba(%1,20); border: 1px solid rgba(%1,100); }"
            "QTableWidget { background: transparent; border: 0; outline: 0; }"
            "QTableWidget::item { border: 0; padding: 0; background: transparent; }"
            "QPushButton { color: #e9f1f9; background: rgba(255,255,255,9); border: 1px solid rgba(255,255,255,38); border-radius: 16px; padding: 10px 15px; }"
            "QPushButton:hover { background: rgba(255,255,255,20); }"
            "QPushButton:pressed { background: rgba(%1,32); padding-top: 12px; padding-bottom: 8px; }"
            "QHeaderView { background: transparent; color: #a8bacb; border: 0; font-size: 13px; }"
            "QHeaderView::section { background: transparent; color: #a8bacb; border: 0; padding: 6px 10px; font-size: 13px; }"
            "QTableCornerButton::section { background: transparent; border: 0; }"
            "QScrollBar:vertical { background: transparent; width: 6px; margin: 5px 0; border-radius: 3px; }"
            "QScrollBar::handle:vertical { background: rgba(220,236,245,60); border: 0; border-radius: 3px; min-height: 28px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
            "QStatusBar { background: #080c12; color: #8a99aa; border-top: 1px solid rgba(255,255,255,10); }"
            "QToolTip { color: #eaf5fa; background: #141b24; border: 1px solid #354551; padding: 6px; }"
            "QColorDialog { background: #111820; }"
        ).arg(tone));
        m_surface->setAccentColor(accent);
        m_toolbarPanel->setAccentColor(accent);
        m_drawer->setAccentColor(accent);
        m_dock->setAccentColor(accent);
        m_trackDelegate->setAccentColor(accent);
        m_catalogPanel->setAccentColor(accent);
        m_titleBar->setAccentColor(accent);
        m_loginTitleBar->setAccentColor(accent);
        m_homePage->setAccentColor(accent);
        m_songPage->setAccentColor(accent);
        for (QPushButton *button : findChildren<QPushButton *>())
            if (auto *jelly = dynamic_cast<JellyButton *>(button))
                jelly->setAccentColor(accent);
        QPalette searchPalette = m_search->palette();
        searchPalette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#7c8c9f")));
        m_search->setPalette(searchPalette);
        m_playlistSearch->setPalette(searchPalette);
        m_tracks->viewport()->setAutoFillBackground(false);
    }

    void chooseAeroColor()
    {
        m_dock->setInteractionHeld(true);
        const QColor original = m_accent;
        QColorDialog colors(original, this);
        colors.setWindowTitle(QStringLiteral("选择玻璃颜色"));
        colors.setOption(QColorDialog::DontUseNativeDialog);
        const QVector<QColor> presets = {QColor("#35bfe7"), QColor("#52d6c6"), QColor("#9bd979"),
            QColor("#9e9aec"), QColor("#eda8cb"), QColor("#e8bc78")};
        for (int i = 0; i < presets.size(); ++i)
            QColorDialog::setCustomColor(i, presets[i]);
        connect(&colors, &QColorDialog::currentColorChanged, this,
                [this](const QColor &color) { if (color.isValid()) applyAeroTheme(color); });
        if (colors.exec() == QDialog::Accepted) {
            applyAeroTheme(colors.selectedColor());
            QSettings().setValue(QStringLiteral("appearance/accent"), m_accent);
        } else {
            applyAeroTheme(original);
        }
        m_dock->setInteractionHeld(false);
    }

    void configurePlatforms(const QVector<MusicPlatform> &platforms)
    {
        resetCatalogSearch();
        const QString previousPlatform = m_platform->currentText();
        m_enabledPlatforms = ReleaseConfig::filterPlatforms(platforms);
        m_providers.clear();
        m_platform->blockSignals(true);
        m_platform->clear();
        for (MusicPlatform platform : m_enabledPlatforms) {
            if (platform == MusicPlatform::Kugou && m_kugouAccountConnected) {
                m_providers.emplace_back(std::make_unique<PlaylistSnapshotProvider>(platform, m_kugouPlaylists, &m_kugouApi));
            } else if (platform == MusicPlatform::NetEaseCloud && m_neteaseApi.isLoggedIn()) {
                m_providers.emplace_back(std::make_unique<NeteaseMusicProvider>(&m_neteaseApi));
            } else {
                m_providers.emplace_back(std::make_unique<MockMusicProvider>(platform));
            }
            m_platform->addItem(platformName(platform));
        }
        const int previousIndex = m_platform->findText(previousPlatform);
        if (previousIndex >= 0)
            m_platform->setCurrentIndex(previousIndex);
        m_platform->blockSignals(false);
        updateCatalogSearchHint();
    }

    void completeSetup()
    {
        const QVector<MusicPlatform> selected = m_loginPage->selectedPlatforms();
        if (selected.isEmpty())
            return;

        QStringList keys;
        for (MusicPlatform platform : selected)
            keys.push_back(platformKey(platform));
        QSettings settings;
        settings.setValue(QStringLiteral("selectedPlatforms"), keys);
        settings.setValue(QStringLiteral("setupCompleted"), true);
        settings.setValue(QStringLiteral("demoMode"), !m_kugouAccountConnected);

        configurePlatforms(selected);
        m_pages->setCurrentWidget(m_playerPage);
        showHomePage();
        statusBar()->showMessage(m_kugouAccountConnected
            ? QStringLiteral("酷狗账号已连接，正在显示账号音乐库。")
            : QStringLiteral("演示模式：平台账号未授权，当前显示本地示例数据。"));
        refresh();
    }

    const MusicProvider *provider() const
    {
        const int index = m_platform->currentIndex();
        return index >= 0 && index < static_cast<int>(m_providers.size()) ? m_providers.at(index).get() : nullptr;
    }

    void refresh(bool preserveCatalog = false)
    {
        const MusicProvider *active = provider();
        if (!active)
            return;
        m_librarySummary->setVisible(active->platform() == MusicPlatform::Kugou && m_kugouAccountConnected);
        rebuildDrawerPlaylists();
        const QString query = m_search->text().trimmed();
        const int section = m_navigation->currentRow();
        if (!query.isEmpty()) {
            startCatalogSearch();
        } else if (section == 3) {
            m_heading->setText(QStringLiteral("我收藏的音乐（全部歌单）"));
            if (active->platform() == MusicPlatform::Kugou && m_kugouAccountConnected) {
                showLibraryTracks();
                return;
            }
            const QVector<Track> liked = active->likedTracks();
            setTracks(liked);
            if (liked.isEmpty() && active->platform() == MusicPlatform::Kugou)
                statusBar()->showMessage(QStringLiteral("酷狗接口未返回“我喜欢”系统歌单，或该歌单当前为空。"), 6000);
        } else {
            if (!m_visiblePlaylists.isEmpty())
                showSelectedPlaylist(preserveCatalog);
            else {
                m_heading->setText(QStringLiteral("这个分类没有歌单"));
                setTracks({});
            }
        }
    }

    QString playlistLabel(const Playlist &playlist) const
    {
        const int count = qMax(playlist.expectedTrackCount, static_cast<int>(playlist.tracks.size()));
        return count >= 0 ? QStringLiteral("%1\n%2 首").arg(playlist.name).arg(count) : playlist.name;
    }

    void rebuildDrawerPlaylists()
    {
        const auto *active = provider();
        if (!active)
            return;
        const int section = m_navigation->currentRow() == 3 ? 0 : m_navigation->currentRow();
        QVector<Playlist> lists;
        QString signature = QString::number(static_cast<int>(active->platform())) + QLatin1Char(':') + QString::number(section);
        for (const Playlist &playlist : active->playlists()) {
            if (section == 1 && playlist.kind != PlaylistKind::Favorite)
                continue;
            if (section == 2 && playlist.kind != PlaylistKind::Created)
                continue;
            lists.append(playlist);
            signature += QLatin1Char('|') + playlist.id + QLatin1Char(':') + playlist.name;
        }
        m_visiblePlaylists = lists;
        if (signature == m_drawerSignature)
            return;
        m_drawerSignature = signature;
        const QString previous = m_selectedPlaylistId;
        const QSignalBlocker listSignals(m_playlistList);
        m_playlistList->clear();
        const QIcon cover(placeholderCover());
        int selectedRow = lists.isEmpty() ? -1 : 0;
        for (int row = 0; row < lists.size(); ++row) {
            const Playlist &playlist = lists.at(row);
            auto *item = new QListWidgetItem(cover, playlistLabel(playlist), m_playlistList);
            item->setData(Qt::UserRole, playlist.id);
            item->setSizeHint(QSize(0, 70));
            item->setToolTip(playlist.name);
            if (playlist.id == previous)
                selectedRow = row;
            loadPlaylistCover(playlist);
        }
        m_playlistList->setCurrentRow(selectedRow);
        m_drawerListSummary->setText(QStringLiteral("%1 个歌单").arg(lists.size()));
        if (selectedRow >= 0)
            rememberSelectedPlaylist(lists.at(selectedRow));
        else {
            m_selectedPlaylistId.clear();
            m_selectedPlaylistTracks.clear();
            m_playlistSearchIndex.reset();
            m_playlistSearchScope->setText(QStringLiteral("先选择一个歌单"));
            m_playlistSearchSummary->clear();
            m_playlistSearch->setEnabled(false);
        }
    }

    void rememberSelectedPlaylist(const Playlist &playlist)
    {
        if (playlist.id != m_selectedPlaylistId) {
            const QSignalBlocker localSignals(m_playlistSearch);
            m_playlistSearch->clear();
            m_playlistSearchTimer.stop();
        }
        m_selectedPlaylistId = playlist.id;
        m_selectedPlaylistName = playlist.name;
        m_selectedPlaylistTracks = provider()->platform() == MusicPlatform::Kugou && m_kugouAccountConnected
            ? m_kugouTrackCache.value(playlist.id, playlist.tracks) : playlist.tracks;
        m_playlistSearchIndex.reset();
        m_playlistSearch->setEnabled(true);
        m_playlistSearchScope->setText(QStringLiteral("当前歌单 · %1").arg(playlist.name));
        m_playlistSearchSummary->setText(QStringLiteral("%1 首歌曲 · 仅在此歌单内搜索").arg(m_selectedPlaylistTracks.size()));
    }

    void setPlaylistTracks(const QVector<Track> &tracks)
    {
        m_selectedPlaylistTracks = tracks;
        m_playlistSearchIndex.reset();
        applyPlaylistSearch();
    }

    void applyPlaylistSearch()
    {
        if (m_selectedPlaylistId.isEmpty())
            return;
        m_searchTimer.stop();
        resetCatalogSearch();
        {
            const QSignalBlocker globalSignals(m_search);
            m_search->clear();
        }
        if (m_navigation->currentRow() == 3) {
            const QSignalBlocker navigationSignals(m_navigation);
            m_navigation->setCurrentRow(0);
        }
        m_heading->setText(m_selectedPlaylistName);
        const QString query = m_playlistSearch->text().trimmed();
        QVector<Track> tracks = m_selectedPlaylistTracks;
        if (!query.isEmpty()) {
            if (!m_playlistSearchIndex)
                m_playlistSearchIndex = std::make_unique<MusicSearchIndex>(m_selectedPlaylistTracks);
            tracks = m_playlistSearchIndex->search(query);
            tracks = searchWithPlatformNames(m_selectedPlaylistTracks, tracks, query);
        }
        setTracks(tracks);
        m_playlistSearchSummary->setText(query.isEmpty()
            ? QStringLiteral("%1 首歌曲 · 仅在此歌单内搜索").arg(tracks.size())
            : QStringLiteral("找到 %1 / %2 首 · 已显示在右侧").arg(tracks.size()).arg(m_selectedPlaylistTracks.size()));
    }

    QString nameQueryKey(const QString &query) const
    {
        return query.normalized(QString::NormalizationForm_KC).simplified().toCaseFolded();
    }

    QVector<Track> searchWithPlatformNames(const QVector<Track> &source, const QVector<Track> &localMatches,
                                           const QString &query)
    {
        if (!provider() || provider()->platform() != MusicPlatform::Kugou || !m_kugouAccountConnected)
            return localMatches;
        const QString key = nameQueryKey(query);
        if (m_nameMatches.contains(key))
            return mergeLibrarySearchMatches(source, localMatches, m_nameMatches.value(key));
        if (!localMatches.isEmpty() || key.size() < 2 || m_nameLookups.contains(key)
            || m_nameLookupFailures.contains(key) || (source.isEmpty() && m_librarySyncActive))
            return localMatches;
        m_nameLookups.insert(key);
        const int generation = m_kugouGeneration;
        statusBar()->showMessage(QStringLiteral("正在查找相关名称…"));
        m_kugouApi.searchTrackNames(query, [this, key, generation](const QVector<Track> &matches, const QString &error) {
            if (generation != m_kugouGeneration)
                return;
            m_nameLookups.remove(key);
            if (error.isEmpty())
                m_nameMatches.insert(key, matches);
            else
                m_nameLookupFailures.insert(key);
            if (!provider() || provider()->platform() != MusicPlatform::Kugou)
                return;
            const bool local = !m_playlistSearch->text().trimmed().isEmpty()
                && nameQueryKey(m_playlistSearch->text()) == key;
            if (!local || !m_search->text().trimmed().isEmpty())
                return;
            applyPlaylistSearch();
            if (!error.isEmpty())
                statusBar()->showMessage(QStringLiteral("名称补全暂不可用，仍可搜索已加载的名称和别名。重新输入可重试。"), 6000);
            else if (m_visibleTracks.isEmpty())
                statusBar()->showMessage(m_librarySyncActive
                    ? QStringLiteral("暂未找到匹配歌曲，音乐库仍在加载…")
                    : QStringLiteral("已加载的音乐库中没有匹配歌曲。"), 6000);
            else
                statusBar()->showMessage(QStringLiteral("已找到账号音乐库中的 %1 首歌曲。")
                    .arg(m_visibleTracks.size()), 5000);
        });
        return localMatches;
    }

    void showSelectedPlaylist(bool preserveCatalog = false)
    {
        const int row = m_playlistList->currentRow();
        if (row < 0 || row >= m_visiblePlaylists.size())
            return;
        const Playlist playlist = m_visiblePlaylists.at(row);
        rememberSelectedPlaylist(playlist);
        if (preserveCatalog) {
            m_heading->setText(m_selectedPlaylistName);
            setTracks(m_selectedPlaylistTracks);
            return;
        }
        if (!preserveCatalog) {
            m_searchTimer.stop();
            resetCatalogSearch();
            const QSignalBlocker globalSignals(m_search);
            m_search->clear();
        }
        if (provider() && provider()->platform() == MusicPlatform::Kugou && m_kugouAccountConnected)
            loadKugouPlaylist(playlist, false);
        else
            applyPlaylistSearch();
    }

    void loadKugouPlaylist(const Playlist &playlist, bool liked)
    {
        const QString key = playlist.id;
        if (m_completedPlaylists.contains(key)) {
            if (!liked)
                setPlaylistTracks(m_kugouTrackCache.value(key));
            return;
        }
        if (!liked)
            setPlaylistTracks(m_kugouTrackCache.value(key, playlist.tracks));
        if (m_loadingPlaylists.contains(key))
            return;
        m_loadingPlaylists.insert(key);
        if (!liked)
            statusBar()->showMessage(QStringLiteral("正在加载：%1").arg(playlist.name));
        const int generation = m_kugouGeneration;
        m_kugouApi.fetchPlaylistTracks(playlist, [this, key, generation, liked]
            (const QVector<Track> &received, const QString &error) {
            if (generation != m_kugouGeneration)
                return;
            QVector<Track> tracks = received;
            if (!error.isEmpty() && m_kugouTrackCache.value(key).size() > tracks.size())
                tracks = m_kugouTrackCache.value(key);
            m_loadingPlaylists.remove(key);
            m_kugouTrackCache.insert(key, tracks);
            if (error.isEmpty()) {
                m_completedPlaylists.insert(key);
                m_libraryErrors.remove(key);
            } else {
                m_libraryErrors.insert(key, error);
            }
            for (Playlist &entry : m_kugouPlaylists)
                if (entry.id == key)
                    entry.tracks = tracks;
            for (Playlist &entry : m_visiblePlaylists)
                if (entry.id == key)
                    entry.tracks = tracks;
            if (m_selectedPlaylistId == key && provider() && provider()->platform() == MusicPlatform::Kugou) {
                m_selectedPlaylistTracks = tracks;
                m_playlistSearchIndex.reset();
                m_playlistSearchSummary->setText(QStringLiteral("%1 首歌曲 · 仅在此歌单内搜索").arg(tracks.size()));
            }
            for (auto &entry : m_providers)
                if (entry->platform() == MusicPlatform::Kugou)
                    entry = std::make_unique<PlaylistSnapshotProvider>(MusicPlatform::Kugou, m_kugouPlaylists, &m_kugouApi);
            scheduleLibraryView();
            pumpLibrarySync();
            updateLibrarySummary();
            if (!provider() || provider()->platform() != MusicPlatform::Kugou
                || !m_search->text().trimmed().isEmpty())
                return;
            const int row = m_playlistList->currentRow();
            const bool visible = m_navigation->currentRow() != 3
                && row >= 0 && row < m_visiblePlaylists.size() && m_visiblePlaylists.at(row).id == key;
            if (!visible)
                return;
            if (!error.isEmpty()) {
                setPlaylistTracks(tracks);
                statusBar()->showMessage(QStringLiteral("加载失败：%1。重新点击可重试。").arg(error));
                return;
            }
            setPlaylistTracks(tracks);
            statusBar()->showMessage(QStringLiteral("已加载 %1 首歌曲。").arg(tracks.size()));
        });
    }

    void startLibrarySync(bool retry = false)
    {
        if (retry)
            m_libraryErrors.clear();
        m_librarySyncActive = true;
        pumpLibrarySync();
    }

    void pumpLibrarySync()
    {
        if (!m_librarySyncActive)
            return;
        for (const Playlist &playlist : m_kugouPlaylists) {
            if (m_loadingPlaylists.size() >= 3)
                break;
            if (m_completedPlaylists.contains(playlist.id) || m_loadingPlaylists.contains(playlist.id)
                || m_libraryErrors.contains(playlist.id))
                continue;
            loadKugouPlaylist(playlist, true);
        }
        if (m_loadingPlaylists.isEmpty())
            m_librarySyncActive = false;
        updateLibrarySummary();
    }

    void updateLibrarySummary()
    {
        const auto tracks = allPlaylistTracks(m_kugouPlaylists);
        m_librarySummary->setText(QStringLiteral("音乐库：%1 首（去重），%2 / %3 个歌单完整加载，%4 个未完整加载%5")
            .arg(tracks.size()).arg(m_completedPlaylists.size()).arg(m_kugouPlaylists.size())
            .arg(m_libraryErrors.size()).arg(m_librarySyncActive ? QStringLiteral("，正在加载…") : QString()));
        QStringList errors;
        for (const Playlist &playlist : m_kugouPlaylists)
            if (m_libraryErrors.contains(playlist.id))
                errors.push_back(playlist.name + QStringLiteral("：") + m_libraryErrors.value(playlist.id));
        m_librarySummary->setToolTip(errors.join(QLatin1Char('\n')));
        for (int row = 0; row < m_playlistList->count(); ++row) {
            auto *item = m_playlistList->item(row);
            if (row < m_visiblePlaylists.size()) {
                const Playlist &playlist = m_visiblePlaylists.at(row);
                const QString label = playlistLabel(playlist);
                if (item->text() != label)
                    item->setText(label);
                const QString tooltip = QStringLiteral("%1\n已加载 %2 首%3").arg(playlist.name).arg(playlist.tracks.size())
                    .arg(m_libraryErrors.contains(playlist.id)
                        ? QLatin1Char('\n') + m_libraryErrors.value(playlist.id) : QString());
                if (item->toolTip() != tooltip)
                    item->setToolTip(tooltip);
            }
        }
    }

    void scheduleLibraryView()
    {
        if (m_libraryViewPending)
            return;
        m_libraryViewPending = true;
        const int generation = m_kugouGeneration;
        QTimer::singleShot(1500, this, [this, generation] {
            if (generation != m_kugouGeneration)
                return;
            m_libraryViewPending = false;
            if (provider() && provider()->platform() == MusicPlatform::Kugou
                && m_navigation->currentRow() == 3 && m_search->text().trimmed().isEmpty())
                showLibraryTracks();
        });
    }

    void showLibraryTracks()
    {
        if (!m_search->text().trimmed().isEmpty())
            return;
        const int row = m_tracks->currentRow();
        const QString selected = row >= 0 && row < m_visibleTracks.size() ? trackKey(m_visibleTracks.at(row)) : QString();
        const int scroll = m_tracks->verticalScrollBar()->value();
        const auto source = allPlaylistTracks(m_kugouPlaylists);
        const auto &tracks = source;
        setTracks(tracks);
        if (!selected.isEmpty()) {
            for (int current = 0; current < tracks.size(); ++current)
                if (trackKey(tracks.at(current)) == selected) {
                    m_tracks->setCurrentCell(current, 1);
                    break;
                }
        }
        m_tracks->verticalScrollBar()->setValue(scroll);
    }

    void setTracks(const QVector<Track> &tracks)
    {
        // Loading another playlist often leaves this visible list unchanged.
        // Preserve its cards, covers, selection and scroll instead of rebuilding it.
        bool sameRows = tracks.size() == m_visibleTracks.size();
        for (int row = 0; sameRows && row < tracks.size(); ++row) {
            const Track &fresh = tracks.at(row), &old = m_visibleTracks.at(row);
            sameRows = fresh.id == old.id && fresh.hash == old.hash
                && fresh.albumAudioId == old.albumAudioId && fresh.title == old.title
                && fresh.artist == old.artist && fresh.album == old.album && fresh.coverUrl == old.coverUrl;
        }
        if (sameRows) {
            m_visibleTracks = tracks; // Keep fresh audio/alias metadata even when the UI is unchanged.
            return;
        }
        const QVector<Track> previous = m_visibleTracks;
        // Account loading usually appends songs or fills existing metadata.
        // Preserve those items instead of reallocating the complete library.
        bool preserveRows = !previous.isEmpty() && tracks.size() >= previous.size();
        bool rebuildCoverRows = false;
        for (int row = 0; preserveRows && row < previous.size(); ++row) {
            preserveRows = trackKey(tracks.at(row)) == trackKey(previous.at(row));
            rebuildCoverRows |= coverRequestKey(tracks.at(row)) != coverRequestKey(previous.at(row));
        }
        rebuildCoverRows |= !preserveRows;
        const int preservedCount = preserveRows ? previous.size() : 0;
        const QSignalBlocker scrollSignals(m_tracks->verticalScrollBar());
        m_tracks->setUpdatesEnabled(false);
        if (!preserveRows) {
            m_tracks->setCurrentItem(nullptr);
            m_tracks->clearSelection();
            m_tracks->clearContents();
        }
        m_tracks->setRowCount(tracks.size());
        m_visibleTracks = tracks;
        if (rebuildCoverRows) {
            ++m_trackCoverGeneration;
            m_coverRows.clear();
            m_requestedCovers.clear();
            m_readyTrackCovers.clear();
        }
        const QIcon placeholder(placeholderCover());
        {
            // Row changes are already notified; suppress per-cell layout work during bulk insertion.
            const QSignalBlocker itemSignals(m_tracks->model());
            for (int row = 0; row < tracks.size(); ++row) {
                const QString key = coverRequestKey(tracks.at(row));
                if (rebuildCoverRows || row >= preservedCount)
                    m_coverRows[key].append(row);
                if (row >= preservedCount) {
                    auto *cover = new QTableWidgetItem;
                    QIcon initialCover = placeholder;
                    if (!rebuildCoverRows) {
                        // A duplicate appended after its shared request already
                        // completed must inherit the resolved icon immediately.
                        const auto existingRows = m_coverRows.constFind(key);
                        if (existingRows != m_coverRows.cend() && !existingRows->isEmpty()
                            && existingRows->first() < row) {
                            const auto *existing = m_tracks->item(existingRows->first(), 0);
                            if (existing && !existing->icon().isNull())
                                initialCover = existing->icon();
                        }
                    }
                    cover->setIcon(initialCover);
                    m_tracks->setItem(row, 0, cover);
                } else if (key != coverRequestKey(previous.at(row))) {
                    m_tracks->item(row, 0)->setIcon(placeholder);
                }
                for (int column = 1; column < 4; ++column) {
                    const QString text = column == 1 ? tracks.at(row).title
                        : column == 2 ? tracks.at(row).artist : tracks.at(row).album;
                    QTableWidgetItem *item = row < preservedCount ? m_tracks->item(row, column) : nullptr;
                    if (!item) {
                        item = new QTableWidgetItem(text);
                        item->setToolTip(text);
                        m_tracks->setItem(row, column, item);
                    } else if (item->text() != text) {
                        item->setText(text);
                        item->setToolTip(text);
                    }
                }
            }
        }
        int artistWidth = 140;
        const QFontMetrics metrics(m_tracks->font());
        for (int row = 0; row < qMin(64, tracks.size()); ++row)
            artistWidth = qMax(artistWidth, metrics.horizontalAdvance(tracks.at(row).artist) + 28);
        m_tracks->setColumnWidth(2, qBound(140, artistWidth, 260));
        if (!preserveRows)
            m_tracks->verticalScrollBar()->setValue(0);
        m_tracks->setUpdatesEnabled(true);
        Liquid::invalidateBackdrop(m_tracks);
        QTimer::singleShot(0, this, [this] { loadVisibleTrackCovers(); });
    }

    void loadPlaylistCover(const Playlist &playlist)
    {
        const qreal requestDpr = m_surface->devicePixelRatioF();
        auto applyCover = [this, playlistId = playlist.id, name = playlist.name, requestDpr](const QUrl &url) {
            if (url.isEmpty() || !qFuzzyCompare(requestDpr, m_surface->devicePixelRatioF()))
                return;
            requestCover(url, [this, playlistId, name, requestDpr](const QIcon &icon) {
                if (icon.isNull() || !qFuzzyCompare(requestDpr, m_surface->devicePixelRatioF()))
                    return;
                for (int row = 0; row < m_playlistList->count(); ++row) {
                    if (row < m_visiblePlaylists.size()
                        && m_visiblePlaylists.at(row).id == playlistId
                        && m_visiblePlaylists.at(row).name == name) {
                        m_playlistList->item(row)->setIcon(icon);
                        Liquid::invalidateBackdrop(m_playlistList);
                        return;
                    }
                }
            });
        };
        if (!playlist.coverUrl.isEmpty()) {
            applyCover(playlist.coverUrl);
        } else if (provider() && provider()->platform() == MusicPlatform::Kugou) {
            m_kugouApi.fetchPlaylistCover(playlist, applyCover);
        }
    }

    void loadVisibleTrackCovers()
    {
        if (m_tracks->rowCount() == 0)
            return;
        int firstRow = m_tracks->rowAt(0);
        if (firstRow < 0)
            firstRow = 0;
        int lastRow = m_tracks->rowAt(m_tracks->viewport()->height() - 1);
        if (lastRow < firstRow)
            lastRow = qMin(m_tracks->rowCount() - 1, firstRow + 12);

        for (int row = firstRow; row <= lastRow && row < m_visibleTracks.size(); ++row) {
            const Track track = m_visibleTracks.at(row);
            const QString requestKey = coverRequestKey(track);
            if (m_requestedCovers.contains(requestKey))
                continue;
            m_requestedCovers.insert(requestKey);
            const quint64 generation = m_trackCoverGeneration;
            auto applyCover = [this, requestKey, generation](const QUrl &url) {
                if (generation != m_trackCoverGeneration)
                    return;
                if (url.isEmpty()) {
                    m_requestedCovers.remove(requestKey);
                    return;
                }
                requestCover(url, [this, requestKey, generation](const QIcon &icon) {
                    if (generation != m_trackCoverGeneration)
                        return;
                    if (icon.isNull()) {
                        m_requestedCovers.remove(requestKey);
                        return;
                    }
                    m_readyTrackCovers.insert(requestKey, icon);
                    if (!m_coverApplyTimer.isActive())
                        m_coverApplyTimer.start(Aero::animationInterval(m_tracks));
                });
            };
            if (!track.coverUrl.isEmpty()) {
                applyCover(track.coverUrl);
            } else if (provider() && provider()->platform() == MusicPlatform::Kugou) {
                m_kugouApi.fetchTrackCover(track, applyCover);
            }
        }
    }

    void flushTrackCovers()
    {
        const auto ready = std::move(m_readyTrackCovers);
        m_readyTrackCovers.clear();
        bool changed = false;
        for (auto cover = ready.cbegin(); cover != ready.cend(); ++cover) {
            const auto rows = m_coverRows.constFind(cover.key());
            if (rows == m_coverRows.cend())
                continue;
            for (int row : rows.value()) {
                if (auto *item = m_tracks->item(row, 0)) {
                    item->setIcon(cover.value());
                    changed = true;
                }
            }
        }
        if (changed)
            Liquid::invalidateBackdrop(m_tracks);
    }

    void requestCover(const QUrl &url, std::function<void(const QIcon &)> callback)
    {
        const qreal dpr = m_surface->devicePixelRatioF();
        const QString key = url.toString() + QLatin1Char('|') + QString::number(qCeil(80 * dpr));
        if (const auto *cached = m_coverCache.object(key)) {
            callback(*cached);
            return;
        }
        if (m_pendingCovers.contains(key)) {
            m_pendingCovers[key].push_back(std::move(callback));
            return;
        }
        m_pendingCovers[key].push_back(std::move(callback));
        QNetworkRequest request(url);
        request.setTransferTimeout(8000);
        QNetworkReply *reply = m_coverNetwork->get(request);
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
            if (received > 16 * 1024 * 1024 || total > 16 * 1024 * 1024)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, dpr] {
            QByteArray bytes;
            if (reply->error() == QNetworkReply::NoError)
                bytes = reply->readAll();
            reply->deleteLater();
            const int pixels = qCeil(80 * dpr);
            const auto ready = [this, key, dpr, pixels](const QImage &decoded) {
                QIcon icon;
                if (!decoded.isNull()) {
                    QPixmap thumbnail = QPixmap::fromImage(decoded);
                    thumbnail.setDevicePixelRatio(dpr);
                    icon = QIcon(thumbnail);
                    m_coverCache.insert(key, new QIcon(icon), qMax(1, pixels * pixels * 4 / 1024));
                }
                const auto callbacks = m_pendingCovers.take(key);
                for (const auto &done : callbacks)
                    done(icon);
            };
            if (!m_coverDecoder.decode(std::move(bytes), QSize(pixels, pixels), ready))
                ready({});
        });
    }

    void playSelected()
    {
        const int row = m_tracks->currentRow();
        if (row < 0 || row >= m_visibleTracks.size()) {
            statusBar()->showMessage(QStringLiteral("请先选择一首歌曲。"), 3000);
            return;
        }
        const Track track = m_visibleTracks.at(row);
        if (track.audioUrl.isEmpty()
            && (!provider() || provider()->platform() != MusicPlatform::Kugou || !m_kugouAccountConnected)) {
            statusBar()->showMessage(QStringLiteral("当前为演示数据，没有可播放的音频地址。"), 5000);
            return;
        }
        m_queueUsesKugou = provider() && provider()->platform() == MusicPlatform::Kugou
            && m_kugouAccountConnected;
        m_queuePlatform = provider() ? provider()->platform() : MusicPlatform::Kugou;
        m_playbackQueue.setTracks(m_visibleTracks, row);
        playQueueTrack();
        m_dock->setExpanded(true);
        m_dock->triggerPlaybackMeteor();
    }

    void scheduleAudioOutputSync()
    {
        const auto outputs = QMediaDevices::audioOutputs();
        const QByteArray currentId = m_audioOutput->device().id();
        const bool currentAvailable = std::any_of(outputs.cbegin(), outputs.cend(),
            [&currentId](const QAudioDevice &device) { return device.id() == currentId; });
        // Ignore ordinary property notifications for an available, unchanged
        // endpoint: rebuilding a sink can itself generate such notifications.
        m_audioRouteInterrupted |= !currentAvailable;
        m_audioRouteTimer.start();
    }

    void syncDefaultAudioOutput()
    {
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull())
            return; // A later device notification will retry after reconnection.
        const bool reopen = m_audioRouteInterrupted;
        m_audioRouteInterrupted = false;
        if (m_audioOutput->device() != device) {
            m_audioOutput->setDevice(device);
        } else if (reopen) {
            // setDevice ignores an equal device, so recreate its disconnected
            // sink without resetting the source, position, queue or play state.
            const qint64 position = m_player->position();
            m_player->setAudioOutput(nullptr);
            m_player->setAudioOutput(m_audioOutput);
            // A fully buffered source can already have reached decoder EOF.
            // Reseek explicitly so reopening at position zero primes it again.
            if (m_player->isSeekable() && m_player->mediaStatus() != QMediaPlayer::EndOfMedia)
                m_player->setPosition(position);
        }
    }

    void togglePlayback()
    {
        if (m_sourceRequestPending)
            return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else if (m_player->playbackState() == QMediaPlayer::PausedState) {
            m_player->play();
        } else if (!m_playbackQueue.empty()) {
            playQueueTrack();
        } else {
            playSelected();
        }
    }

    void playQueueTrack()
    {
        const Track *current = m_playbackQueue.current();
        if (!current)
            return;
        const Track track = *current;
        const bool canResolve = (m_queueUsesKugou && m_kugouAccountConnected)
            || (m_queuePlatform == MusicPlatform::NetEaseCloud && m_neteaseApi.isLoggedIn());
        if (track.audioUrl.isEmpty() && !canResolve) {
            ++m_playRequest;
            m_switchingTrack = true;
            m_sourceRequestPending = false;
            m_pendingTitle.clear();
            m_player->stop();
            m_player->setSource(QUrl());
            m_dock->setTrack(track.title, track.artist);
            m_dock->setPlaying(false);
            m_dock->setBusy(false);
            m_dock->setDuration(0);
            m_dock->setPosition(0);
            m_dock->setSeekable(false);
            m_switchingTrack = false;
            statusBar()->showMessage(QStringLiteral("当前歌曲没有可播放的音频地址。"), 5000);
            return;
        }
        const int request = ++m_playRequest;
        m_switchingTrack = true;
        m_sourceRequestPending = true;
        m_pendingTitle = track.title;
        m_player->stop();
        m_player->setSource(QUrl());
        m_dock->setTrack(track.title, track.artist);
        m_dock->setPlaying(false);
        m_dock->setBusy(true);
        m_dock->setDuration(0);
        m_dock->setPosition(0);
        m_dock->setSeekable(false);
        m_switchingTrack = false;
        loadSongDetails(track, request);
        statusBar()->showMessage(QStringLiteral("正在获取音源：%1").arg(track.title));
        auto ready = [this, request, title = track.title](const QUrl &url, const QString &error) {
            if (request != m_playRequest)
                return;
            m_sourceRequestPending = false;
            if (!error.isEmpty() || url.isEmpty()) {
                m_pendingTitle.clear();
                m_dock->setBusy(false);
                statusBar()->showMessage(error.isEmpty() ? QStringLiteral("没有可播放的音源。") : error);
                return;
            }
            m_pendingTitle = title;
            statusBar()->showMessage(QStringLiteral("正在加载：%1").arg(title));
            m_player->setSource(url);
            m_player->play();
        };
        if (!track.audioUrl.isEmpty())
            ready(track.audioUrl, {});
        else if (m_queuePlatform == MusicPlatform::NetEaseCloud)
            m_neteaseApi.fetchAudioUrl(track, ready);
        else
            m_kugouApi.fetchAudioUrl(track, ready);
    }

    std::vector<std::unique_ptr<MusicProvider>> m_providers;
    KugouApiClient m_kugouApi;
    NeteaseApiClient m_neteaseApi;
    QStackedWidget *m_pages = nullptr;
    PlatformLoginPage *m_loginPage = nullptr;
    QWidget *m_playerPage = nullptr;
    AeroSurface *m_surface = nullptr;
    AeroPanel *m_toolbarPanel = nullptr;
    GlassTitleBar *m_titleBar = nullptr;
    GlassTitleBar *m_loginTitleBar = nullptr;
    QStackedWidget *m_contentPages = nullptr;
    HomePage *m_homePage = nullptr;
    QWidget *m_libraryPage = nullptr;
    SongPage *m_songPage = nullptr;
    QWidget *m_previousContentPage = nullptr;
    WallpaperEngineCapture *m_wallpaperCapture = nullptr;
    QPointer<ThemePanel> m_activeThemePanel;
    QTimer m_wallpaperResizeTimer;
    QHash<int, QVector<Track>> m_dailyTracks;
    QHash<int, int> m_recommendationRequests;
    QHash<int, QDate> m_recommendationLoaded;
    QSet<int> m_recommendationBusy;
    QDate m_recommendationDate;
    QTimer m_dayTimer;
    QColor m_accent;
    QVector<MusicPlatform> m_enabledPlatforms;
    QVector<Playlist> m_kugouPlaylists;
    QHash<QString, QVector<Track>> m_kugouTrackCache;
    QSet<QString> m_loadingPlaylists;
    QSet<QString> m_completedPlaylists;
    QHash<QString, QString> m_libraryErrors;
    bool m_librarySyncActive = false;
    bool m_libraryViewPending = false;
    int m_kugouGeneration = 0;
    bool m_kugouAccountConnected = false;
    QComboBox *m_platform = nullptr;
    QLineEdit *m_search = nullptr;
    QTimer m_searchTimer;
    CatalogSearchPanel *m_catalogPanel = nullptr;
    QVector<CatalogTrack> m_catalogTracks;
    QString m_catalogQuery;
    MusicPlatform m_catalogPlatform = MusicPlatform::Kugou;
    int m_catalogRequest = 0;
    int m_catalogPage = 0;
    int m_catalogTotal = -1;
    bool m_catalogBusy = false;
    bool m_catalogHasMore = false;
    QSet<QString> m_requestedCatalogCovers;
    QHash<QString, QIcon> m_catalogCoverIcons;
    PlaylistDrawer *m_drawer = nullptr;
    QPushButton *m_edgeHandle = nullptr;
    QLabel *m_drawerListSummary = nullptr;
    QLabel *m_drawerHint = nullptr;
    QLabel *m_playlistSearchScope = nullptr;
    QLabel *m_playlistSearchSummary = nullptr;
    QLineEdit *m_playlistSearch = nullptr;
    QTimer m_playlistSearchTimer;
    QString m_drawerSignature;
    QString m_selectedPlaylistId;
    QString m_selectedPlaylistName;
    QVector<Track> m_selectedPlaylistTracks;
    std::unique_ptr<MusicSearchIndex> m_playlistSearchIndex;
    QHash<QString, QVector<Track>> m_nameMatches;
    QSet<QString> m_nameLookups;
    QSet<QString> m_nameLookupFailures;
    QListWidget *m_navigation = nullptr;
    QWidget *m_navigationControls = nullptr;
    std::array<JellyButton *, 4> m_navigationButtons{};
    QListWidget *m_playlistList = nullptr;
    QLabel *m_heading = nullptr;
    QLabel *m_librarySummary = nullptr;
    QLabel *m_nowPlaying = nullptr;
    QTableWidget *m_tracks = nullptr;
    SongGlassDelegate *m_trackDelegate = nullptr;
    QVector<Playlist> m_visiblePlaylists;
    QVector<Track> m_visibleTracks;
    QAudioOutput *m_audioOutput = nullptr;
    QMediaPlayer *m_player = nullptr;
    QMediaDevices *m_mediaDevices = nullptr;
    QTimer m_audioRouteTimer;
    QTimer m_audioSettingsTimer;
    bool m_audioRouteInterrupted = false;
    PlaybackDock *m_dock = nullptr;
    PlaybackQueue m_playbackQueue;
    bool m_queueUsesKugou = false;
    MusicPlatform m_queuePlatform = MusicPlatform::Kugou;
    bool m_sourceRequestPending = false;
    bool m_switchingTrack = false;
    QString m_pendingTitle;
    int m_playRequest = 0;
    QNetworkAccessManager *m_coverNetwork = nullptr;
    QCache<QString, QIcon> m_coverCache{32 * 1024};
    QHash<QString, std::vector<std::function<void(const QIcon &)>>> m_pendingCovers;
    qreal m_thumbnailDpr = 0;
    QSet<QString> m_requestedCovers;
    quint64 m_trackCoverGeneration = 0;
    QHash<QString, QVector<int>> m_coverRows;
    QHash<QString, QIcon> m_readyTrackCovers;
    QTimer m_coverApplyTimer;
    QCache<QString, QPixmap> m_highResolutionCovers{64 * 1024};
    QHash<QString, std::vector<std::function<void(const QPixmap &)>>> m_highResolutionPending;
    CoverImageDecoder m_coverDecoder;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] {
        if (!NativeWindowCapture::shutdownWorkers(1000))
            qWarning("Native wallpaper capture did not finish within the shutdown deadline.");
    });
    const QStringList arguments = QCoreApplication::arguments();
    const int mediaArgument = arguments.indexOf(QStringLiteral("--smoke-media"));
    const bool smokeMode = arguments.contains(QStringLiteral("--smoke-test")) || mediaArgument >= 0;
    QString smokeMediaPath;
    if (mediaArgument >= 0) {
        if (mediaArgument + 1 >= arguments.size()
            || arguments.at(mediaArgument + 1).startsWith(QStringLiteral("--"))) {
            qCritical("--smoke-media requires an existing local WAV file.");
            return 2;
        }
        const QFileInfo wave(arguments.at(mediaArgument + 1));
        if (!wave.isFile() || wave.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) != 0) {
            qCritical("--smoke-media accepts an existing local WAV file only.");
            return 2;
        }
        smokeMediaPath = wave.absoluteFilePath();
    }
    std::unique_ptr<QTemporaryDir> smokeSettings;
    if (smokeMode) {
        smokeSettings = std::make_unique<QTemporaryDir>();
        if (!smokeSettings->isValid()) {
            qCritical("Portable smoke settings directory could not be created.");
            return 5;
        }
        QCoreApplication::setOrganizationName(QStringLiteral("PHS Radio Runtime Verification"));
        QCoreApplication::setApplicationName(QStringLiteral("Portable Smoke Test"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, smokeSettings->path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, smokeSettings->path());
        app.setQuitOnLastWindowClosed(false);
    } else {
        QCoreApplication::setOrganizationName(QStringLiteral("PHS Radio"));
        QCoreApplication::setApplicationName(QStringLiteral("PHS Radio"));
    }
    Aero::installAnimationDriver(app);
    QCoreApplication::setApplicationVersion(QString::fromUtf8(PHSRADIO_VERSION));
    Q_INIT_RESOURCE(app);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/app.ico")));
    std::unique_ptr<AppUpdater> smokeUpdater;
    if (smokeMode) {
        smokeUpdater = std::make_unique<AppUpdater>();
        if (!QFile::exists(QStringLiteral(":/updater/update-install.ps1"))) {
            qCritical("Bundled update installer resource is missing.");
            return 6;
        }
    }

    QSettings settings;
    const QVector<MusicPlatform> selected = smokeMode ? QVector<MusicPlatform>{MusicPlatform::Kugou}
                                                     : loadSelectedPlatforms(settings);
    const bool setupCompleted = smokeMode
        || (settings.value(QStringLiteral("setupCompleted")).toBool() && !selected.isEmpty());
    PlayerWindow window(selected, setupCompleted, smokeMode);
    if (smokeMode) window.setEnabled(false);
    window.show();
    if (!smokeMode) return app.exec();

    // No real account is restored and the disabled smoke UI cannot open login
    // or service actions. Native plugins load exactly as they do in the package.
    std::unique_ptr<QAudioOutput> smokeAudio;
    std::unique_ptr<QMediaPlayer> smokePlayer;
    bool finished = false;
    const auto finish = [&](int code) {
        if (finished) return;
        finished = true;
        if (smokePlayer) smokePlayer->stop();
        if (code == 0) qInfo("Portable smoke verification passed.");
        app.exit(code);
    };
    if (smokeMediaPath.isEmpty()) {
        QTimer::singleShot(3000, &app, [&] { finish(0); });
    } else {
        smokeAudio = std::make_unique<QAudioOutput>();
        smokeAudio->setVolume(0);
        smokeAudio->setMuted(true);
        smokePlayer = std::make_unique<QMediaPlayer>();
        smokePlayer->setAudioOutput(smokeAudio.get());
        QObject::connect(smokePlayer.get(), &QMediaPlayer::errorOccurred, &app,
                         [&](QMediaPlayer::Error code, const QString &) {
            if (code != QMediaPlayer::NoError) {
                qCritical("Portable media smoke failed: media backend error %d.", int(code));
                finish(3);
            }
        });
        QObject::connect(smokePlayer.get(), &QMediaPlayer::positionChanged, &app, [&](qint64 position) {
            if (!finished && position > 0 && smokePlayer->duration() > 0
                && smokePlayer->playbackState() == QMediaPlayer::PlayingState) finish(0);
        });
        QTimer::singleShot(10000, &app, [&] {
            if (!finished) {
                qCritical("Portable media smoke timed out before playback advanced.");
                finish(4);
            }
        });
        QTimer::singleShot(0, &app, [&] {
            smokePlayer->setSource(QUrl::fromLocalFile(smokeMediaPath));
            smokePlayer->play();
        });
    }
    const int result = app.exec();
    if (smokePlayer) {
        QObject::disconnect(smokePlayer.get(), nullptr, &app, nullptr);
        smokePlayer->stop();
        smokePlayer->setAudioOutput(nullptr);
    }
    return result;
}
