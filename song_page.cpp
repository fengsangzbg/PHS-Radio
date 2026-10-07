#include "song_page.h"
#include "aero_surface.h"
#include "aero_widgets.h"
#include "liquid_backdrop.h"

#include <QElapsedTimer>
#include <QEasingCurve>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QtMath>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

constexpr int LyricActiveRole = Qt::UserRole + 1;
constexpr int LyricTranslationRole = Qt::UserRole + 2;

class LyricDelegate final : public QStyledItemDelegate {
public:
    explicit LyricDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int width = std::max(140, option.rect.width() - 40);
        QFont line = option.font; line.setPointSize(17); line.setWeight(QFont::DemiBold);
        const int height = QFontMetrics(line).boundingRect(QRect(0, 0, width, 2000),
            Qt::TextWordWrap, index.data().toString()).height();
        QFont translation = option.font; translation.setPointSize(11);
        const QString translated = index.data(LyricTranslationRole).toString();
        const int second = translated.isEmpty() ? 0
            : QFontMetrics(translation).boundingRect(QRect(0, 0, width, 2000),
                Qt::TextWordWrap, translated).height() + 8;
        return QSize(width + 40, std::max(56, height + second + 32));
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        const bool active = index.data(LyricActiveRole).toBool();
        const QRectF bounds = QRectF(option.rect).adjusted(16, 12, -16, -12);
        QFont line = option.font; line.setPointSize(17);
        line.setWeight(active ? QFont::DemiBold : QFont::Normal);
        painter->setFont(line);
        painter->setPen(active ? QColor("#f3f9ff") : QColor("#8998ac"));
        const QString text = index.data().toString();
        const int height = QFontMetrics(line).boundingRect(bounds.toRect(),
            Qt::AlignLeft | Qt::TextWordWrap, text).height();
        painter->drawText(QRectF(bounds.topLeft(), QSizeF(bounds.width(), height)),
                          Qt::AlignLeft | Qt::TextWordWrap, text);
        const QString translated = index.data(LyricTranslationRole).toString();
        if (!translated.isEmpty()) {
            QFont translation = option.font; translation.setPointSize(11);
            painter->setFont(translation);
            painter->setPen(active ? QColor("#bdcede") : QColor("#65748a"));
            painter->drawText(QRectF(bounds.left(), bounds.top() + height + 8,
                bounds.width(), std::max<qreal>(20, bounds.height() - height - 8)),
                Qt::AlignLeft | Qt::TextWordWrap, translated);
        }
        if (active) {
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen); painter->setBrush(QColor(164, 221, 255, 160));
            painter->drawRoundedRect(QRectF(option.rect.left() + 3,
                option.rect.center().y() - 12, 3, 24), 1.5, 1.5);
        }
        painter->restore();
    }
};

QLabel *label(const QString &text, QWidget *parent, const QString &style)
{
    auto *result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText); result->setWordWrap(true);
    result->setStyleSheet(style + QStringLiteral(";background:transparent;"));
    return result;
}

} // namespace

// Only the record is animated. The original artwork is retained and sampled
// once at the actual display DPR into the static disc, then that one layer
// rotates. Resize, monitor-DPR and artwork changes rebuild the precise cache.
class GlassRecord final : public QWidget {
public:
    explicit GlassRecord(QWidget *parent) : QWidget(parent), timer(new QTimer(this))
    {
        setAttribute(Qt::WA_TranslucentBackground); setAutoFillBackground(false);
        setMinimumSize(220, 220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        timer->setTimerType(Qt::PreciseTimer);
        connect(timer, &QTimer::timeout, this, [this] {
            advance(); update(recordBounds().adjusted(-3, -3, 3, 3).toAlignedRect());
        });
    }
    ~GlassRecord() override { timer->stop(); }
    void setAccent(const QColor &value) { accent = value; texture = {}; update(); }
    void setCover(const QPixmap &value) { cover = value; texture = {}; update(); }
    void setPlaying(bool value)
    {
        if (playing == value) return;
        if (timer->isActive()) advance();
        playing = value; updateTimer();
    }
    qreal angleValue() const { return angle; }
    bool isRotating() const { return timer->isActive(); }
    QSize sourceSize() const { return cover.size(); }

protected:
    void showEvent(QShowEvent *event) override { QWidget::showEvent(event); updateTimer(); }
    void hideEvent(QHideEvent *event) override
    {
        if (timer->isActive()) advance();
        timer->stop(); QWidget::hideEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        const QRectF disk = recordBounds();
        const qreal diameter = disk.width();
        if (diameter < 1) return;
        const qreal dpr = devicePixelRatioF();
        const QSize pixels(qCeil(diameter * dpr), qCeil(diameter * dpr));
        if (texture.isNull() || texture.size() != pixels || texture.devicePixelRatio() != dpr) {
            texture = QPixmap(pixels); texture.setDevicePixelRatio(dpr); texture.fill(Qt::transparent);
            QPainter cache(&texture); cache.setRenderHint(QPainter::Antialiasing);
            cache.setRenderHint(QPainter::SmoothPixmapTransform);
            const QRectF circle(1, 1, diameter - 2, diameter - 2);
            Aero::paintGlassBase(cache, circle, accent, diameter / 2);
            cache.setBrush(Qt::NoBrush);
            for (int ring = 0; ring < 19; ++ring) {
                const qreal inset = diameter * (.025 + ring * .008);
                cache.setPen(QPen(QColor(215, 235, 250, ring % 3 == 0 ? 25 : 10), .8));
                cache.drawEllipse(circle.adjusted(inset, inset, -inset, -inset));
            }
            QConicalGradient reflection(circle.center(), 36);
            reflection.setColorAt(0, QColor(255, 255, 255, 0));
            reflection.setColorAt(.09, QColor(231, 247, 255, 48));
            reflection.setColorAt(.17, QColor(255, 255, 255, 0));
            reflection.setColorAt(.56, QColor(255, 255, 255, 0));
            reflection.setColorAt(.66, QColor(224, 245, 255, 29));
            reflection.setColorAt(.77, QColor(255, 255, 255, 0));
            reflection.setColorAt(1, QColor(255, 255, 255, 0));
            cache.setPen(Qt::NoPen); cache.setBrush(reflection); cache.drawEllipse(circle);

            const qreal labelDiameter = diameter * .58;
            const QRectF art((diameter - labelDiameter) / 2, (diameter - labelDiameter) / 2,
                             labelDiameter, labelDiameter);
            QPainterPath artClip; artClip.addEllipse(art);
            cache.save(); cache.setClipPath(artClip, Qt::IntersectClip);
            if (!cover.isNull()) {
                const qreal side = std::min(cover.width(), cover.height());
                cache.drawPixmap(art, cover, QRectF((cover.width() - side) / 2.0,
                                                   (cover.height() - side) / 2.0, side, side));
            } else {
                QRadialGradient blank(art.center(), labelDiameter / 2);
                QColor tint = accent; tint.setAlpha(52);
                blank.setColorAt(0, tint); blank.setColorAt(1, QColor(18, 27, 40, 130));
                cache.fillRect(art, blank);
                QFont note = font(); note.setPointSize(38); cache.setFont(note);
                cache.setPen(QColor(226, 239, 250, 160));
                cache.drawText(art, Qt::AlignCenter, QStringLiteral("♪"));
            }
            cache.restore(); cache.setBrush(Qt::NoBrush);
            cache.setPen(QPen(QColor(245, 250, 255, 80), 1)); cache.drawEllipse(art);
        }
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        Liquid::paintSurfaceBackdrop(painter, this, disk, diameter / 2);
        painter.save(); painter.translate(disk.center()); painter.rotate(angle);
        painter.drawPixmap(QRectF(-diameter / 2, -diameter / 2, diameter, diameter),
                           texture, QRectF(texture.rect()));
        painter.restore();
        painter.setPen(QPen(QColor(255, 255, 255, 140), 1));
        painter.setBrush(QColor(15, 22, 32, 180)); painter.drawEllipse(disk.center(), 4, 4);
    }

private:
    QRectF recordBounds() const
    {
        const qreal diameter = std::max<qreal>(0, std::min({width() - 20, height() - 20, 520}));
        return QRectF((width() - diameter) / 2.0, (height() - diameter) / 2.0, diameter, diameter);
    }
    void advance()
    {
        if (!clock.isValid()) { clock.start(); return; }
        angle = std::fmod(angle + clock.restart() * .012, 360.0);
    }
    void updateTimer()
    {
        if (playing && isVisible()) {
            clock.start(); timer->start(Aero::animationInterval(this));
        } else timer->stop();
    }
    QTimer *timer;
    QElapsedTimer clock;
    QPixmap cover;
    QPixmap texture;
    QColor accent = Aero::defaultAccent();
    qreal angle = 0;
    bool playing = false;
};

SongPage::SongPage(QWidget *parent)
    : QWidget(parent), m_record(new GlassRecord(this)),
      m_title(label(QStringLiteral("选择一首歌"), this, QStringLiteral("color:#f1f7fd;font-size:23px;font-weight:600"))),
      m_artist(label({}, this, QStringLiteral("color:#afc0d1;font-size:14px"))),
      m_album(label({}, this, QStringLiteral("color:#7f93a9;font-size:12px"))),
      m_platform(label({}, this, QStringLiteral("color:#8da4ba;font-size:12px"))),
      m_lyricsMessage(label(QStringLiteral("播放歌曲后查看歌词"), this, QStringLiteral("color:#91a3b8;font-size:16px"))),
      m_lyricsContent(new QStackedWidget(this)), m_lyrics(new QListWidget(this)),
      m_back(new JellyButton(QStringLiteral("返回"), this)), m_scroll(new QVariantAnimation(this)),
      m_recordCard(new AeroPanel(this)), m_lyricsCard(new AeroPanel(this)), m_accent(Aero::defaultAccent())
{
    setObjectName(QStringLiteral("songPage"));
    setAttribute(Qt::WA_TranslucentBackground); setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 174); layout->setSpacing(14);
    auto *header = new QHBoxLayout;
    m_back->setFixedSize(82, 36); m_back->setObjectName(QStringLiteral("songPageBack"));
    header->addWidget(m_back); header->addStretch(); header->addWidget(m_platform);
    layout->addLayout(header);
    auto *body = new QHBoxLayout; body->setSpacing(20);
    auto *recordLayout = new QVBoxLayout(m_recordCard);
    recordLayout->setContentsMargins(20, 19, 20, 16); recordLayout->setSpacing(6);
    for (QLabel *entry : {m_title, m_artist, m_album}) entry->setAlignment(Qt::AlignCenter);
    recordLayout->addWidget(m_title); recordLayout->addWidget(m_artist);
    recordLayout->addWidget(m_record, 1); recordLayout->addWidget(m_album);
    body->addWidget(m_recordCard, 1);
    auto *lyricLayout = new QVBoxLayout(m_lyricsCard);
    lyricLayout->setContentsMargins(20, 20, 20, 20); lyricLayout->setSpacing(14);
    auto *lyricHeading = label(QStringLiteral("歌词"), m_lyricsCard,
                              QStringLiteral("color:#dceaf6;font-size:15px;font-weight:600"));
    auto *hint = label(QStringLiteral("点击歌词跳转到这一句"), m_lyricsCard,
                      QStringLiteral("color:#70859c;font-size:11px"));
    auto *lyricHeader = new QHBoxLayout;
    lyricHeader->addWidget(lyricHeading); lyricHeader->addStretch(); lyricHeader->addWidget(hint);
    lyricLayout->addLayout(lyricHeader); lyricLayout->addWidget(m_lyricsContent, 1);
    auto *messageArea = new QScrollArea(m_lyricsContent);
    messageArea->setWidgetResizable(true); messageArea->setFrameShape(QFrame::NoFrame);
    m_lyricsMessage->setAlignment(Qt::AlignCenter);
    messageArea->setWidget(m_lyricsMessage);
    messageArea->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:none;}"));
    messageArea->viewport()->setAutoFillBackground(false);
    m_lyricsContent->addWidget(messageArea); m_lyricsContent->addWidget(m_lyrics);
    m_lyricsContent->setStyleSheet(QStringLiteral("background:transparent;"));
    m_lyrics->setObjectName(QStringLiteral("synchronizedLyrics"));
    m_lyrics->setFrameShape(QFrame::NoFrame); m_lyrics->setItemDelegate(new LyricDelegate(m_lyrics));
    m_lyrics->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_lyrics->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyrics->setStyleSheet(QStringLiteral(
        "QListWidget{background:transparent;border:none;outline:none;}"
        "QScrollBar:vertical{background:transparent;width:4px;margin:6px;}"
        "QScrollBar::handle:vertical{background:rgba(210,230,250,50);border-radius:2px;min-height:24px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"));
    m_lyrics->viewport()->setAutoFillBackground(false);
    body->addWidget(m_lyricsCard, 1); layout->addLayout(body, 1);
    m_scroll->setDuration(250); m_scroll->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_scroll, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_lyrics->verticalScrollBar()->setValue(value.toInt());
    });
    connect(m_back, &QPushButton::clicked, this, [this] { if (onBackRequested) onBackRequested(); });
    connect(m_lyrics, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (m_seekable && onSeekRequested) onSeekRequested(item->data(Qt::UserRole).toLongLong());
    });
    setAccentColor(m_accent);
}

SongPage::~SongPage() { m_scroll->stop(); }

void SongPage::setAccentColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color; m_record->setAccent(color); m_back->setAccentColor(color);
    static_cast<AeroPanel *>(m_recordCard)->setAccentColor(color);
    static_cast<AeroPanel *>(m_lyricsCard)->setAccentColor(color);
}

void SongPage::setTrack(const Track &track, MusicPlatform platform)
{
    m_title->setText(track.title.isEmpty() ? QStringLiteral("选择一首歌") : track.title);
    m_artist->setText(track.artist); m_album->setText(track.album);
    m_platform->setText(platformName(platform)); m_record->setCover({});
    m_position = 0;
    setLyrics({}, QStringLiteral("歌词尚未加载"));
    Liquid::invalidateBackdrop(this);
}

void SongPage::setCover(const QPixmap &original) { m_record->setCover(original); }
void SongPage::setPlaying(bool playing) { m_record->setPlaying(playing); }
void SongPage::setPosition(qint64 position)
{ m_position = std::max<qint64>(0, position); updateActiveLyric(); }
void SongPage::setSeekable(bool seekable) { m_seekable = seekable; }

void SongPage::setLyrics(const QVector<TimedLyricLine> &lines, const QString &emptyMessage)
{
    m_scroll->stop(); m_lines.clear();
    for (const TimedLyricLine &line : lines)
        if (line.timeMs >= 0 && !line.text.trimmed().isEmpty()) m_lines.append(line);
    std::stable_sort(m_lines.begin(), m_lines.end(),
        [](const TimedLyricLine &a, const TimedLyricLine &b) { return a.timeMs < b.timeMs; });
    m_lyrics->clear(); m_activeLine = -1;
    for (const TimedLyricLine &line : m_lines) {
        auto *item = new QListWidgetItem(line.text, m_lyrics);
        item->setData(Qt::UserRole, line.timeMs);
        item->setData(LyricTranslationRole, line.translation);
    }
    m_lyricsMessage->setText(emptyMessage.isEmpty() ? QStringLiteral("暂时没有歌词") : emptyMessage);
    m_lyricsContent->setCurrentIndex(m_lines.isEmpty() ? 0 : 1);
    if (onCurrentLyricChanged) onCurrentLyricChanged({});
    updateActiveLyric(); Liquid::invalidateBackdrop(this);
}

void SongPage::updateActiveLyric()
{
    const auto upper = std::upper_bound(m_lines.cbegin(), m_lines.cend(), m_position,
        [](qint64 position, const TimedLyricLine &line) { return position < line.timeMs; });
    const int active = int(upper - m_lines.cbegin()) - 1;
    if (active == m_activeLine) return;
    if (auto *old = m_lyrics->item(m_activeLine)) old->setData(LyricActiveRole, false);
    m_activeLine = active;
    if (auto *current = m_lyrics->item(active)) current->setData(LyricActiveRole, true);
    { const QSignalBlocker block(m_lyrics); m_lyrics->setCurrentRow(active); }
    centerActiveLyric(true);
    if (onCurrentLyricChanged) onCurrentLyricChanged(currentLyric());
}

void SongPage::centerActiveLyric(bool animate)
{
    auto *item = m_lyrics->item(m_activeLine);
    m_scroll->stop();
    QScrollBar *bar = m_lyrics->verticalScrollBar();
    const int target = item ? std::clamp(bar->value() + m_lyrics->visualItemRect(item).center().y()
        - m_lyrics->viewport()->height() / 2, bar->minimum(), bar->maximum()) : bar->minimum();
    if (!animate || !isVisible()) { bar->setValue(target); return; }
    m_scroll->setStartValue(bar->value()); m_scroll->setEndValue(target); m_scroll->start();
}

void SongPage::focusLyrics() { m_lyrics->setFocus(Qt::OtherFocusReason); centerActiveLyric(false); }
QString SongPage::currentLyric() const
{ return m_activeLine >= 0 && m_activeLine < m_lines.size() ? m_lines[m_activeLine].text : QString(); }
int SongPage::currentLyricIndex() const { return m_activeLine; }
qreal SongPage::recordAngle() const { return m_record->angleValue(); }
bool SongPage::rotationActive() const { return m_record->isRotating(); }
QSize SongPage::coverSourceSize() const { return m_record->sourceSize(); }
QListWidget *SongPage::lyricsList() const { return m_lyrics; }
