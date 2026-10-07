#include "home_page.h"
#include "aero_surface.h"
#include "aero_widgets.h"
#include "liquid_backdrop.h"

#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {

class RecommendationDelegate final : public QStyledItemDelegate {
public:
    explicit RecommendationDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QColor accent = Aero::defaultAccent();
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    { return QSize(154, 200); }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF card = QRectF(option.rect).adjusted(4, 4, -4, -4);
        Aero::paintGlass(*painter, card, accent, 20);
        const QRectF cover(card.left() + 10, card.top() + 10, card.width() - 20, card.width() - 20);
        QPainterPath clip;
        clip.addRoundedRect(cover, 13, 13);
        painter->save();
        painter->setClipPath(clip, Qt::IntersectClip);
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const QPixmap image = icon.pixmap(cover.size().toSize(), painter->device()->devicePixelRatioF());
            const qreal side = std::min(image.width(), image.height());
            painter->drawPixmap(cover, image, QRectF((image.width() - side) / 2.0,
                                (image.height() - side) / 2.0, side, side));
        } else {
            QLinearGradient water(cover.topLeft(), cover.bottomRight());
            QColor tint = accent; tint.setAlpha(48);
            water.setColorAt(0, QColor(255, 255, 255, 18));
            water.setColorAt(1, tint);
            painter->fillRect(cover, water);
            painter->setPen(QColor(220, 234, 246, 125));
            QFont note = option.font; note.setPointSize(34); painter->setFont(note);
            painter->drawText(cover, Qt::AlignCenter, QStringLiteral("♪"));
        }
        painter->restore();
        QFont title = option.font; title.setPointSize(11); title.setWeight(QFont::DemiBold);
        painter->setFont(title); painter->setPen(QColor("#eef4fa"));
        painter->drawText(QRectF(card.left() + 11, cover.bottom() + 9, card.width() - 22, 21),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(title).elidedText(index.data().toString(), Qt::ElideRight,
                                                      qRound(card.width() - 22)));
        QFont artist = option.font; artist.setPointSize(9);
        painter->setFont(artist); painter->setPen(QColor("#96a5b7"));
        painter->drawText(QRectF(card.left() + 11, cover.bottom() + 32, card.width() - 22, 20),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(artist).elidedText(index.data(Qt::UserRole).toString(),
                                                       Qt::ElideRight, qRound(card.width() - 22)));
        if (option.state & (QStyle::State_MouseOver | QStyle::State_Selected)) {
            painter->setPen(QPen(QColor(225, 240, 255, 105), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(card.adjusted(.5, .5, -.5, -.5), 20, 20);
        }
        painter->restore();
    }
};

QLabel *plainLabel(const QString &text, QWidget *parent, const QString &style)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet(style + QStringLiteral("; background: transparent;"));
    return label;
}

} // namespace

class HomeRecommendationSection final : public QWidget {
public:
    explicit HomeRecommendationSection(MusicPlatform platform, QWidget *parent)
        : QWidget(parent), card(new AeroPanel(this)), list(new QListWidget(card)),
          state(plainLabel(QStringLiteral("连接账号，查看你的每日推荐"), card,
                           QStringLiteral("color: #98a8bb; font-size: 12px"))),
          connectButton(new JellyButton(QStringLiteral("连接账号"), card)),
          delegate(new RecommendationDelegate(list)), platform(platform)
    {
        setMinimumWidth(0);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0); outer->addWidget(card);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 17, 18, 16); layout->setSpacing(10);
        auto *header = new QHBoxLayout;
        auto *title = plainLabel(platformName(platform) + QStringLiteral(" · 每日推荐"), card,
                                QStringLiteral("color: #eef4fa; font-size: 16px; font-weight: 600"));
        header->addWidget(title, 1);
        connectButton->setFixedSize(82, 34);
        header->addWidget(connectButton); layout->addLayout(header);
        state->setWordWrap(true); layout->addWidget(state);
        list->setObjectName(platform == MusicPlatform::Kugou
            ? QStringLiteral("kugouRecommendations") : QStringLiteral("neteaseRecommendations"));
        list->setViewMode(QListView::IconMode);
        list->setMovement(QListView::Static);
        list->setResizeMode(QListView::Adjust);
        list->setFlow(QListView::LeftToRight);
        list->setWrapping(true);
        list->setSpacing(1);
        list->setFrameShape(QFrame::NoFrame);
        list->setItemDelegate(delegate);
        list->setMouseTracking(true);
        list->setMinimumHeight(170);
        list->setStyleSheet(QStringLiteral(
            "QListWidget{background:transparent;border:none;outline:none;}"
            "QScrollBar:vertical{background:transparent;width:5px;margin:4px;}"
            "QScrollBar::handle:vertical{background:rgba(210,230,250,55);border-radius:2px;min-height:22px;}"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
            "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"));
        list->viewport()->setAutoFillBackground(false);
        layout->addWidget(list, 1);
        QObject::connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            if (activated) activated(list->row(item));
        });
        QObject::connect(connectButton, &QPushButton::clicked, this, [this] {
            if (connectRequested) connectRequested();
        });
    }
    void setTracks(const QVector<Track> &tracks)
    {
        list->clear();
        for (const Track &track : tracks) {
            auto *item = new QListWidgetItem(track.title, list);
            item->setData(Qt::UserRole, track.artist);
            item->setToolTip(track.title + QStringLiteral("\n") + track.artist);
        }
        state->setText(tracks.isEmpty() ? QStringLiteral("平台暂未返回今日推荐歌曲")
            : QStringLiteral("为你推荐 %1 首，点击即可播放").arg(tracks.size()));
        connectButton->setText(QStringLiteral("刷新推荐"));
        connectButton->setEnabled(true);
        Liquid::invalidateBackdrop(this);
    }
    AeroPanel *card;
    QListWidget *list;
    QLabel *state;
    JellyButton *connectButton;
    RecommendationDelegate *delegate;
    MusicPlatform platform;
    std::function<void(int)> activated;
    std::function<void()> connectRequested;
};

HomePage::HomePage(QWidget *parent)
    : QWidget(parent), m_clock(new QLabel(this)), m_date(new QLabel(this)),
      m_year(new QLabel(this)), m_timezone(new QLabel(this)), m_clockTimer(new QTimer(this)),
      m_clockCard(new AeroPanel(this)),
      m_kugou(new HomeRecommendationSection(MusicPlatform::Kugou, this)),
      m_netease(new HomeRecommendationSection(MusicPlatform::NetEaseCloud, this)),
      m_accent(Aero::defaultAccent())
{
    setObjectName(QStringLiteral("homePage"));
    setAttribute(Qt::WA_TranslucentBackground); setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 18, 24, 174); layout->setSpacing(18);
    auto *heading = plainLabel(QStringLiteral("今天，听点什么"), this,
        QStringLiteral("color: #eef4fa; font-size: 23px; font-weight: 600"));
    layout->addWidget(heading);
    auto *clockLayout = new QHBoxLayout(m_clockCard);
    clockLayout->setContentsMargins(28, 18, 28, 22); clockLayout->setSpacing(22);
    auto *time = new QVBoxLayout;
    time->setSpacing(1);
    QFont digits(QStringLiteral("Segoe UI")); digits.setPointSize(57); digits.setWeight(QFont::Light);
    m_clock->setFont(digits); m_clock->setMinimumWidth(0);
    m_clock->setObjectName(QStringLiteral("localClock"));
    m_clock->setStyleSheet(QStringLiteral("color:#eff8ff;background:transparent;"));
    m_clock->setTextFormat(Qt::PlainText);
    m_timezone->setStyleSheet(QStringLiteral("color:#91aac0;background:transparent;font-size:12px;"));
    m_timezone->setObjectName(QStringLiteral("localTimeZone"));
    time->addWidget(m_clock); time->addWidget(m_timezone);
    clockLayout->addLayout(time, 1);
    auto *calendar = new QVBoxLayout; calendar->setSpacing(3);
    m_year->setStyleSheet(QStringLiteral("color:rgba(238,247,255,150);font-size:32px;font-weight:300;background:transparent;"));
    m_date->setStyleSheet(QStringLiteral("color:#c7d6e5;font-size:15px;background:transparent;"));
    m_date->setObjectName(QStringLiteral("localDate"));
    m_year->setAlignment(Qt::AlignRight); m_date->setAlignment(Qt::AlignRight);
    calendar->addWidget(m_year); calendar->addWidget(m_date);
    clockLayout->addLayout(calendar);
    layout->addWidget(m_clockCard);
    auto *recommendations = new QHBoxLayout; recommendations->setSpacing(18);
    recommendations->addWidget(m_kugou, 1); recommendations->addWidget(m_netease, 1);
    layout->addLayout(recommendations, 1);
    for (HomeRecommendationSection *section : {m_kugou, m_netease}) {
        section->activated = [this, section](int index) {
            if (m_kugouOnly && section->platform != MusicPlatform::Kugou) return;
            if (onRecommendationActivated) onRecommendationActivated(section->platform, index);
        };
        section->connectRequested = [this, section] {
            if (m_kugouOnly && section->platform != MusicPlatform::Kugou) return;
            if (onConnectRequested) onConnectRequested(section->platform);
        };
    }
    m_clockTimer->setInterval(1000); m_clockTimer->setTimerType(Qt::CoarseTimer);
    connect(m_clockTimer, &QTimer::timeout, this, [this] { refreshClock(); });
    refreshClock(); setAccentColor(m_accent);
}

HomeRecommendationSection *HomePage::section(MusicPlatform platform) const
{
    if (m_kugouOnly && platform != MusicPlatform::Kugou) return nullptr;
    if (platform == MusicPlatform::Kugou) return m_kugou;
    if (platform == MusicPlatform::NetEaseCloud) return m_netease;
    return nullptr;
}

void HomePage::setKugouOnly(bool enabled)
{
    if (m_kugouOnly == enabled) return;
    m_kugouOnly = enabled;
    m_netease->setVisible(!enabled);
    m_netease->setEnabled(!enabled);
    if (enabled) m_netease->list->clear();
    Liquid::invalidateBackdrop(this);
}

void HomePage::setAccentColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    static_cast<AeroPanel *>(m_clockCard)->setAccentColor(color);
    for (HomeRecommendationSection *entry : {m_kugou, m_netease}) {
        entry->card->setAccentColor(color); entry->connectButton->setAccentColor(color);
        entry->delegate->accent = color; entry->list->viewport()->update();
    }
}

void HomePage::setRecommendations(MusicPlatform platform, const QVector<Track> &tracks)
{ if (auto *entry = section(platform)) entry->setTracks(tracks); }

void HomePage::setRecommendationState(MusicPlatform platform, const QString &message, bool loading)
{
    if (auto *entry = section(platform)) {
        entry->state->setText(message);
        entry->connectButton->setEnabled(!loading);
        entry->connectButton->setText(entry->list->count() ? QStringLiteral("刷新推荐") : QStringLiteral("连接账号"));
        Liquid::invalidateBackdrop(this);
    }
}

void HomePage::setCover(MusicPlatform platform, int index, const QPixmap &cover)
{
    if (auto *entry = section(platform))
        if (auto *item = entry->list->item(index)) item->setIcon(QIcon(cover));
}

QString HomePage::utcOffsetText(const QDateTime &now)
{
    const int offset = now.offsetFromUtc();
    const int seconds = std::abs(offset);
    QString result = QStringLiteral("UTC%1%2:%3")
        .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'));
    if (seconds % 60) result += QStringLiteral(":%1").arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return result;
}

void HomePage::refreshClock(const QDateTime &now)
{
    if (!now.isValid()) return;
    m_clock->setText(now.toString(QStringLiteral("HH:mm:ss")));
    m_year->setText(now.toString(QStringLiteral("yyyy")));
    const QLocale chinese(QLocale::Chinese, QLocale::China);
    m_date->setText(chinese.toString(now.date(), QStringLiteral("M月d日 dddd")));
    m_timezone->setText(QStringLiteral("本地时间 · ") + utcOffsetText(now));
}

QLabel *HomePage::clockLabel() const { return m_clock; }
QLabel *HomePage::dateLabel() const { return m_date; }
QLabel *HomePage::timeZoneLabel() const { return m_timezone; }

void HomePage::showEvent(QShowEvent *event)
{ QWidget::showEvent(event); refreshClock(); m_clockTimer->start(); }
void HomePage::hideEvent(QHideEvent *event)
{ m_clockTimer->stop(); QWidget::hideEvent(event); }
