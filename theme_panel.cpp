#include "theme_panel.h"
#include "aero_widgets.h"
#include "wallpaper_engine_capture.h"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

ThemePanel::ThemePanel(QWidget *parent)
    : QDialog(parent), m_wallpapers(new QComboBox(this)), m_current(new QLabel(this)),
      m_libraryStatus(new QLabel(this)), m_dimming(new QSlider(Qt::Horizontal, this))
{
    setWindowTitle(QStringLiteral("背景主题"));
    setObjectName(QStringLiteral("themePanel"));
    setMinimumWidth(510);
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(14);
    auto *heading = new QLabel(QStringLiteral("让音乐拥有自己的背景"), this);
    heading->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 600;"));
    layout->addWidget(heading);
    m_current->setWordWrap(true);
    m_current->setTextFormat(Qt::PlainText);
    layout->addWidget(m_current);
    auto *local = new QHBoxLayout;
    auto *builtIn = new JellyButton(QStringLiteral("默认水光"), this);
    auto *import = new JellyButton(QStringLiteral("导入图片 / 动态背景"), this);
    local->addWidget(builtIn);
    local->addWidget(import, 1);
    layout->addLayout(local);
    connect(builtIn, &QPushButton::clicked, this, [this] {
        BackgroundTheme selected;
        selected.dimming = m_theme.dimming;
        selectTheme(selected);
    });
    connect(import, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择背景"), {},
            QStringLiteral("背景文件 (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.mp4 *.webm *.mkv *.mov *.avi *.m4v *.wmv *.json);;所有文件 (*)"));
        if (path.isEmpty())
            return;
        BackgroundTheme selected;
        QString error;
        if (!backgroundThemeForFile(path, &selected, &error)) {
            m_libraryStatus->setText(error);
            return;
        }
        selected.dimming = m_theme.dimming;
        if (selected.kind == BackgroundKind::WallpaperEngine) {
            selected.engineExecutable = m_library.executable;
            if (selected.engineExecutable.isEmpty()) {
                m_libraryStatus->setText(QStringLiteral("此背景需要 Wallpaper Engine，请先选择 Steam 安装目录。"));
                return;
            }
        }
        selectTheme(selected);
    });
    auto *wallpaperHeading = new QLabel(QStringLiteral("本机 Wallpaper Engine 壁纸"), this);
    layout->addWidget(wallpaperHeading);
    m_wallpapers->setMinimumContentsLength(25);
    m_wallpapers->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    layout->addWidget(m_wallpapers);
    auto *engineActions = new QHBoxLayout;
    auto *applyWallpaper = new JellyButton(QStringLiteral("使用所选壁纸"), this);
    auto *rescan = new JellyButton(QStringLiteral("刷新"), this);
    auto *steamFolder = new JellyButton(QStringLiteral("选择 Steam 目录"), this);
    engineActions->addWidget(applyWallpaper, 1);
    engineActions->addWidget(rescan);
    engineActions->addWidget(steamFolder);
    layout->addLayout(engineActions);
    connect(rescan, &QPushButton::clicked, this, [this] { refreshWallpapers(); });
    connect(steamFolder, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 Steam 安装目录"));
        if (!path.isEmpty())
            refreshWallpapers({path});
    });
    connect(applyWallpaper, &QPushButton::clicked, this, [this] {
        const int index = m_wallpapers->currentIndex();
        if (index < 0 || index >= m_library.projects.size())
            return;
        const auto &project = m_library.projects[index];
        BackgroundTheme selected;
        selected.displayName = project.name;
        selected.dimming = m_theme.dimming;
        if (project.type == QStringLiteral("video")) {
            selected.kind = BackgroundKind::Video;
            selected.sourcePath = project.assetPath;
        } else {
            if (m_library.executable.isEmpty()) {
                m_libraryStatus->setText(QStringLiteral("场景和网页壁纸需要本机已安装的 Wallpaper Engine。"));
                return;
            }
            selected.kind = BackgroundKind::WallpaperEngine;
            selected.sourcePath = project.projectPath;
            selected.engineExecutable = m_library.executable;
        }
        selectTheme(selected);
    });
    m_libraryStatus->setWordWrap(true);
    m_libraryStatus->setTextFormat(Qt::PlainText);
    layout->addWidget(m_libraryStatus);
    auto *hint = new QLabel(QStringLiteral("视频背景保持静音。场景和网页由 Wallpaper Engine 实时运行，原桌面壁纸保持原样。"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #94a3b4; font-size: 12px;"));
    layout->addWidget(hint);
    auto *frameRate = new QHBoxLayout;
    m_engineFrameRate = new QLabel(this);
    m_engineFrameRate->setObjectName(QStringLiteral("wallpaperEngineFrameRate"));
    m_engineFrameRate->setWordWrap(true);
    m_engineSettings = new JellyButton(QStringLiteral("打开 Wallpaper Engine"), this);
    m_engineSettings->setToolTip(QStringLiteral("在设置 → 性能中调整源画面帧率并应用。此设置同时影响桌面和播放器中的场景／网页壁纸；播放器内当前最高为 60 FPS。"));
    frameRate->addWidget(m_engineFrameRate, 1);
    frameRate->addWidget(m_engineSettings);
    layout->addLayout(frameRate);
    connect(m_engineSettings, &QPushButton::clicked, this, [this] {
        const QFileInfo executable(engineExecutable());
        QProcess process;
        process.setProgram(executable.absoluteFilePath());
        process.setArguments({QStringLiteral("-showbrowse")});
        process.setWorkingDirectory(executable.absolutePath());
        if (!process.startDetached())
            m_libraryStatus->setText(QStringLiteral("无法打开 Wallpaper Engine。请在它的设置 → 性能中调整画面帧率并应用。"));
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && isVisible())
            updateEngineFrameRate();
    });
    auto *darkness = new QHBoxLayout;
    darkness->addWidget(new QLabel(QStringLiteral("背景遮暗"), this));
    m_dimming->setRange(0, 85);
    m_dimming->setValue(m_theme.dimming);
    m_dimming->setAccessibleName(QStringLiteral("背景遮暗程度"));
    darkness->addWidget(m_dimming, 1);
    layout->addLayout(darkness);
    connect(m_dimming, &QSlider::valueChanged, this, [this](int value) {
        BackgroundTheme selected = m_theme;
        selected.dimming = value;
        selectTheme(selected);
    });
    auto *done = new JellyButton(QStringLiteral("完成"), this);
    layout->addWidget(done, 0, Qt::AlignRight);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    setStyleSheet(QStringLiteral(
        "QDialog#themePanel { background: #0b111a; color: #edf4fc; }"
        "QLabel { background: transparent; color: #d4e0ed; }"
        "QComboBox { background: #15202e; color: #edf4fc; border: 1px solid #304357; border-radius: 8px; padding: 9px; }"
        "QComboBox QAbstractItemView { background: #15202e; color: #edf4fc; selection-background-color: #24475c; }"
        "QSlider::groove:horizontal { background: #25394b; height: 5px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #bce5f7; width: 15px; margin: -5px 0; border-radius: 7px; }"));
    refreshWallpapers();
    updateCurrent();
}

void ThemePanel::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    updateEngineFrameRate();
}

QString ThemePanel::engineExecutable() const
{
    return QFileInfo(m_theme.engineExecutable).isFile() ? m_theme.engineExecutable : m_library.executable;
}

void ThemePanel::updateEngineFrameRate()
{
    const QString executable = engineExecutable();
    const bool installed = !executable.isEmpty() && QFileInfo(executable).isFile();
    m_engineSettings->setEnabled(installed);
    QString error;
    const int fps = installed ? WallpaperEngineCapture::configuredFrameRate(executable, &error) : 0;
    const QString setting = !installed ? QStringLiteral("未发现 Wallpaper Engine")
        : fps > 0 ? QStringLiteral("WE 画面帧率设置：%1 FPS（全局）").arg(fps)
        : QStringLiteral("暂无法读取 WE 的画面帧率设置");
    m_engineFrameRate->setText(setting + (m_theme.kind == BackgroundKind::Video
        ? QStringLiteral("\n当前视频背景保持原视频帧率。")
        : QStringLiteral("\n场景／网页背景当前最高 60 FPS，实际显示受屏幕和性能限制。")));
    m_engineFrameRate->setToolTip(error);
}

void ThemePanel::setTheme(const BackgroundTheme &theme)
{
    m_theme = theme;
    const QSignalBlocker dimmingSignals(m_dimming);
    m_dimming->setValue(theme.dimming);
    updateCurrent();
}

BackgroundTheme ThemePanel::theme() const { return m_theme; }

void ThemePanel::selectTheme(const BackgroundTheme &theme)
{
    setTheme(theme);
    if (onThemeSelected)
        onThemeSelected(m_theme);
}

void ThemePanel::refreshWallpapers(const QStringList &steamRoots)
{
    m_library = discoverWallpaperEngine(steamRoots);
    m_wallpapers->clear();
    int selected = 0;
    for (int index = 0; index < m_library.projects.size(); ++index) {
        const auto &project = m_library.projects[index];
        const QString type = project.type == QStringLiteral("video") ? QStringLiteral("视频")
            : project.type == QStringLiteral("scene") ? QStringLiteral("场景") : QStringLiteral("网页");
        m_wallpapers->addItem(project.name + QStringLiteral(" · ") + type);
        if (project.projectPath.compare(m_library.currentProjectPath, Qt::CaseInsensitive) == 0)
            selected = index;
    }
    m_wallpapers->setCurrentIndex(m_library.projects.isEmpty() ? -1 : selected);
    m_wallpapers->setEnabled(!m_library.projects.isEmpty());
    m_libraryStatus->setText(m_library.projects.isEmpty()
        ? QStringLiteral("未找到本机壁纸。可选择 Steam 目录，或直接导入图片和视频。")
        : QStringLiteral("已找到 %1 个本机壁纸，选择后可直接使用。").arg(m_library.projects.size()));
    updateEngineFrameRate();
}

void ThemePanel::updateCurrent()
{
    const QString name = m_theme.kind == BackgroundKind::Liquid ? QStringLiteral("默认水光")
        : m_theme.displayName.isEmpty() ? QFileInfo(m_theme.sourcePath).fileName() : m_theme.displayName;
    m_current->setText(QStringLiteral("当前背景：%1").arg(name));
    m_current->setToolTip(m_theme.sourcePath);
    updateEngineFrameRate();
}
