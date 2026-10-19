#include "platform_login_page.h"
#include "aero_widgets.h"
#include "release_config.h"
#include "aquarium_entrance.h"
#include <QScrollArea>
#include <QResizeEvent>

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QVBoxLayout>

PlatformLoginPage::PlatformLoginPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("platformLoginPage"));
    setStyleSheet(QStringLiteral(
        "#platformLoginPage { background: #090c12; color: #edf3fa; }"
        "QCheckBox { color: #edf3fa; font-size: 16px; font-weight: 600; spacing: 12px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
        "QLabel#eyebrow { color: #89d2e5; font-size: 12px; font-weight: 600; }"
        "QLabel#status { color: #9bbdcc; background: rgba(255,255,255,8); border: 1px solid rgba(255,255,255,18); border-radius: 10px; padding: 5px 8px; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(64, 40, 64, 40);
    layout->setSpacing(18);

    auto *header = new QHBoxLayout;
    auto *brand = new QLabel(QStringLiteral("PHS Radio"), this);
    brand->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 600; color: #edf4fa;"));
    auto *step = new QLabel(QStringLiteral("首次连接  /  01"), this);
    step->setObjectName(QStringLiteral("eyebrow"));
    header->addWidget(brand);
    header->addStretch();
    header->addWidget(step);
    layout->addLayout(header);

    auto *title = new QLabel(QStringLiteral("连接你的音乐平台"), this);
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 600; color: #edf4fa;"));
    auto *description = new QLabel(
        ReleaseConfig::KugouOnly
            ? QStringLiteral("使用酷狗 App 扫码，连接你的歌单、收藏音乐和每日推荐。")
            : QStringLiteral("酷狗与网易云可以同时登录，连接各自的歌单、收藏音乐和每日推荐。"), this);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("font-size: 14px; color: #96a4b4;"));
    layout->addWidget(title);
    layout->addWidget(description);

    const QVector<MusicPlatform> platforms = ReleaseConfig::availablePlatforms();
    for (MusicPlatform platform : platforms) {
        auto *row = new QFrame(this);
        row->setObjectName(QStringLiteral("platformRow"));
        row->setMinimumHeight(68);
        row->setStyleSheet(QStringLiteral(
            "#platformRow { background: rgba(255,255,255,5); border: 1px solid rgba(255,255,255,24); border-top: 1px solid rgba(255,255,255,42); border-radius: 18px; }"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(18, 10, 18, 10);

        auto *checkbox = new QCheckBox(platformName(platform), row);
        auto *status = new QLabel(platform != MusicPlatform::QQMusic
                                      ? QStringLiteral("本机扫码服务")
                                      : QStringLiteral("官方授权待接入"), row);
        status->setObjectName(QStringLiteral("status"));
        rowLayout->addWidget(checkbox, 1);
        rowLayout->addWidget(status);
        if (platform == MusicPlatform::Kugou) {
            m_kugouStatus = status;
            m_kugouLoginButton = new JellyButton(QStringLiteral("扫码登录"), row);
            rowLayout->addWidget(m_kugouLoginButton);
        } else if (platform == MusicPlatform::NetEaseCloud) {
            m_neteaseStatus = status;
            m_neteaseLoginButton = new JellyButton(QStringLiteral("扫码登录"), row);
            rowLayout->addWidget(m_neteaseLoginButton);
        }
        layout->addWidget(row);

        m_options.push_back(qMakePair(platform, checkbox));
        if (ReleaseConfig::KugouOnly) checkbox->setChecked(true);
        connect(checkbox, &QCheckBox::toggled, this, [this] { updateSelection(); });
    }

    m_kugouQrPanel = new QWidget(this);
    auto *qrLayout = new QHBoxLayout(m_kugouQrPanel);
    qrLayout->setContentsMargins(0, 0, 0, 0);
    m_kugouQrLabel = new QLabel(m_kugouQrPanel);
    m_kugouQrLabel->setFixedSize(168, 168);
    m_kugouQrLabel->setAlignment(Qt::AlignCenter);
    m_kugouQrLabel->setStyleSheet(QStringLiteral("background: white; border: 1px solid #dfe5df;"));
    auto *qrHint = new QLabel(QStringLiteral("使用酷狗 App 扫码\n\n确认登录后会读取你的创建和收藏歌单。"), m_kugouQrPanel);
    qrHint->setWordWrap(true);
    m_qrHint = qrHint;
    qrHint->setStyleSheet(QStringLiteral("color: #96a4b4; font-size: 14px;"));
    qrLayout->addWidget(m_kugouQrLabel);
    qrLayout->addSpacing(16);
    qrLayout->addWidget(qrHint, 1);
    layout->addWidget(m_kugouQrPanel);
    m_kugouQrPanel->hide();

    auto *footer = new QHBoxLayout;
    m_selectionCount = new QLabel(this);
    m_selectionCount->setStyleSheet(QStringLiteral("color: #96a4b4;"));
    m_continueButton = new JellyButton(QStringLiteral("进入播放器"), this);
    m_continueButton->setEnabled(false);
    footer->addWidget(m_selectionCount, 1);
    footer->addWidget(m_continueButton);
    layout->addLayout(footer);

    auto *notice = new QLabel(
        QStringLiteral("连接使用本机第三方 API 服务，不会收集或保存平台密码。"), this);
    notice->setWordWrap(true);
    notice->setStyleSheet(QStringLiteral("color: #708197; font-size: 12px;"));
    layout->addWidget(notice);
    layout->addStretch();

    updateSelection();
}

QVector<MusicPlatform> PlatformLoginPage::selectedPlatforms() const
{
    QVector<MusicPlatform> selected;
    for (const auto &option : m_options) {
        if (option.second->isChecked())
            selected.push_back(option.first);
    }
    return selected;
}

void PlatformLoginPage::setSelectedPlatforms(const QVector<MusicPlatform> &platforms)
{
    for (const auto &option : m_options)
        option.second->setChecked(ReleaseConfig::KugouOnly || platforms.contains(option.first));
    updateSelection();
}

QPushButton *PlatformLoginPage::continueButton() const
{
    return m_continueButton;
}

QPushButton *PlatformLoginPage::kugouLoginButton() const
{
    return m_kugouLoginButton;
}

void PlatformLoginPage::setKugouStatus(const QString &status)
{
    m_kugouStatus->setText(status);
}

void PlatformLoginPage::setKugouQr(const QImage &image)
{
    m_qrPlatform = MusicPlatform::Kugou;
    m_qrHint->setText(QStringLiteral("使用酷狗 App 扫码\n\n确认登录后会读取你的创建和收藏歌单。"));
    m_kugouQrLabel->setPixmap(QPixmap::fromImage(image).scaled(
        m_kugouQrLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_kugouQrPanel->show();
}

void PlatformLoginPage::clearKugouQr()
{
    if (m_qrPlatform != MusicPlatform::Kugou)
        return;
    m_kugouQrLabel->clear();
    m_kugouQrPanel->hide();
}

QPushButton *PlatformLoginPage::neteaseLoginButton() const
{
    return m_neteaseLoginButton;
}

void PlatformLoginPage::setNeteaseStatus(const QString &status)
{
    if (m_neteaseStatus) m_neteaseStatus->setText(status);
}

void PlatformLoginPage::setNeteaseQr(const QImage &image)
{
    if (!ReleaseConfig::platformAvailable(MusicPlatform::NetEaseCloud)) return;
    m_qrPlatform = MusicPlatform::NetEaseCloud;
    m_qrHint->setText(QStringLiteral("使用网易云音乐 App 扫码\n\n确认登录后会读取你的创建、收藏歌单和每日推荐。"));
    m_kugouQrLabel->setPixmap(QPixmap::fromImage(image).scaled(
        m_kugouQrLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_kugouQrPanel->show();
}

void PlatformLoginPage::clearNeteaseQr()
{
    if (m_qrPlatform != MusicPlatform::NetEaseCloud)
        return;
    m_kugouQrLabel->clear();
    m_kugouQrPanel->hide();
}

void PlatformLoginPage::updateSelection()
{
    const int count = selectedPlatforms().size();
    m_selectionCount->setText(QStringLiteral("已选择 %1 / %2 个平台").arg(count).arg(m_options.size()));
    m_continueButton->setEnabled(count >= 1 && count <= m_options.size());
}

void PlatformLoginPage::enableEntrance(QWidget *titleBar)
{
    if(m_entrance)return;
    m_entranceTitleBar=titleBar;
    auto *formLayout=layout();
    if(titleBar){formLayout->removeWidget(titleBar);titleBar->setParent(this);}
    m_formHost=new QWidget(this);
    // QWidget::setLayout transfers an existing layout from its old widget.
    m_formHost->setLayout(formLayout);
    formLayout->setContentsMargins(26,22,26,24);
    m_entrance=new AquariumEntrance(this);
    m_entrance->lower();
    m_formScroller=new QScrollArea(this);
    m_formScroller->setFrameShape(QFrame::NoFrame);
    m_formScroller->setWidgetResizable(true);
    m_formScroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_formScroller->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; } QScrollBar:vertical { background: transparent; width: 6px; } QScrollBar::handle:vertical {background: rgba(205,246,255,100); min-height: 30px; border-radius: 3px;}"));
    m_formScroller->viewport()->setAutoFillBackground(false);
    m_formScroller->setWidget(m_formHost);
    m_formScroller->hide();
    m_entrance->onLoginRequested=[this]{showConnectionForm(true);};
    m_entrance->onEntryFinished=[this]{if(onEntranceFinished)onEntranceFinished();};
    m_entrance->onRendererFailed=[this](const QString &){showConnectionForm(true);};
    arrangeEntrance();
    if(titleBar)titleBar->raise();
}
void PlatformLoginPage::arrangeEntrance()
{
    if(!m_entrance)return;
    m_entrance->setGeometry(rect());
    if(m_entranceTitleBar)m_entranceTitleBar->setGeometry(16,8,qMax(1,width()-32),38);
    const int panelWidth=qMax(1,qMin(width()-60,qRound(height()*.94)));
    const int panelHeight=qMax(1,qRound(height()*.76));
    m_formScroller->setGeometry((width()-panelWidth)/2,(height()-panelHeight)/2,panelWidth,panelHeight);
}
void PlatformLoginPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);arrangeEntrance();
}
void PlatformLoginPage::showEntranceWelcome(bool ready)
{
    if(!m_entrance)return;
    m_formScroller->hide();m_entrance->showWelcome(ready);
}
void PlatformLoginPage::showConnectionForm(bool startupJourney)
{
    if(!m_entrance)return;
    m_continueButton->setText(startupJourney?QStringLiteral("完成登录"):QStringLiteral("返回播放器"));
    m_formScroller->show();m_formScroller->raise();
    if(m_entranceTitleBar)m_entranceTitleBar->raise();
    m_entrance->showLogin();
}
void PlatformLoginPage::setEntranceReady(bool ready){if(m_entrance)m_entrance->setReady(ready);}
bool PlatformLoginPage::entranceLoginVisible()const{return m_entrance&&m_formScroller&&!m_formScroller->isHidden();}
