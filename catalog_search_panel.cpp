#include "catalog_search_panel.h"

#include "aero_widgets.h"
#include "liquid_backdrop.h"
#include "song_glass_delegate.h"

#include <QAbstractItemView>
#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResource>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QVBoxLayout>

// Keep the shared delegate's bounded caches and jelly row animation. Its title,
// artist and capsule paints remain untouched; only the final platform badge adds
// an official icon. Platforms without a bundled official asset show their name.
class CatalogGlassDelegate final : public QStyledItemDelegate {
public:
    explicit CatalogGlassDelegate(QTableWidget *table)
        : QStyledItemDelegate(table), m_glass(new SongGlassDelegate(table)) {}

    void setAccentColor(const QColor &color) { m_glass->setAccentColor(color); }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        m_glass->paint(painter, option, index);
        if (index.column() != 3)
            return;
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (icon.isNull())
            return;
        painter->save();
        painter->setClipRect(option.rect, Qt::IntersectClip);
        const int side = 22;
        const QRect badge(option.rect.right() - side - 17,
                          option.rect.center().y() - side / 2, side, side);
        icon.paint(painter, badge, Qt::AlignCenter);
        painter->restore();
    }

private:
    SongGlassDelegate *m_glass;
};

namespace {
QString resultKey(const CatalogTrack &result)
{
    return QString::number(static_cast<int>(result.platform)) + QLatin1Char(':')
        + trackKey(result.track);
}

QTableWidgetItem *textItem(const QString &text, const QString &tooltip = {})
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setToolTip(tooltip.isEmpty() ? text : tooltip);
    return item;
}
} // namespace

CatalogSearchPanel::CatalogSearchPanel(QWidget *parent)
    : QWidget(parent), m_host(parent), m_accent(Aero::defaultAccent()),
      m_queryLabel(new QLabel(this)), m_summary(new QLabel(this)),
      m_message(new QLabel(this)), m_content(new QStackedWidget(this)),
      m_table(new QTableWidget(this)), m_delegate(new CatalogGlassDelegate(m_table)),
      m_close(new JellyButton(QString(), this)),
      m_more(new JellyButton(QStringLiteral("加载更多"), this)),
      m_reveal(new QPropertyAnimation(this, "geometry", this)),
      m_escape(new QShortcut(QKeySequence(Qt::Key_Escape), this))
{
    Q_ASSERT(parent);
    Q_INIT_RESOURCE(platforms);
    setObjectName(QStringLiteral("catalogSearchPanel"));
    setProperty("liquidOverlay", true);
    setProperty("aeroGlassOverlay", true);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(23, 19, 23, 19);
    layout->setSpacing(9);
    auto *header = new QHBoxLayout;
    auto *heading = new QLabel(QStringLiteral("猜你想找"), this);
    heading->setObjectName(QStringLiteral("catalogHeading"));
    heading->setTextFormat(Qt::PlainText);
    header->addWidget(heading);
    header->addStretch();
    m_queryLabel->setTextFormat(Qt::PlainText);
    m_queryLabel->setObjectName(QStringLiteral("catalogQuery"));
    m_queryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_queryLabel->setMaximumWidth(330);
    header->addWidget(m_queryLabel, 1);
    m_close->setObjectName(QStringLiteral("catalogClose"));
    m_close->setGlyph(JellyButton::Glyph::Close);
    m_close->setFixedSize(32, 32);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setToolTip(QStringLiteral("关闭搜索结果（Esc）"));
    m_close->setAccessibleName(QStringLiteral("关闭搜索结果"));
    header->addWidget(m_close);
    layout->addLayout(header);

    m_table->setObjectName(QStringLiteral("catalogResultsTable"));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("封面"), QStringLiteral("歌曲"),
                                       QStringLiteral("歌手"), QStringLiteral("平台")});
    m_table->horizontalHeader()->hide();
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 64);
    m_table->setColumnWidth(2, 142);
    m_table->setColumnWidth(3, 142);
    m_table->verticalHeader()->setDefaultSectionSize(64);
    m_table->verticalHeader()->setMinimumSectionSize(64);
    m_table->setIconSize(QSize(48, 48));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setShowGrid(false);
    m_table->setSortingEnabled(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setAlternatingRowColors(false);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setItemDelegate(m_delegate);
    m_table->viewport()->setAutoFillBackground(false);
    m_table->setAccessibleName(QStringLiteral("在线搜索歌曲，点击即可播放"));
    m_table->installEventFilter(this);

    m_message->setObjectName(QStringLiteral("catalogMessage"));
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setMargin(18);
    m_content->addWidget(m_message);
    m_content->addWidget(m_table);
    layout->addWidget(m_content, 1);

    auto *footer = new QHBoxLayout;
    m_summary->setObjectName(QStringLiteral("catalogSummary"));
    m_summary->setTextFormat(Qt::PlainText);
    footer->addWidget(m_summary, 1);
    m_more->setObjectName(QStringLiteral("catalogLoadMore"));
    m_more->setFocusPolicy(Qt::NoFocus);
    m_more->setFixedHeight(34);
    m_more->setMinimumWidth(110);
    footer->addWidget(m_more);
    layout->addLayout(footer);

    m_reveal->setDuration(170);
    m_reveal->setEasingCurve(QEasingCurve::OutCubic);
    m_escape->setContext(Qt::WindowShortcut);
    m_escape->setEnabled(false);
    connect(m_escape, &QShortcut::activated, this, [this] { dismiss(); });
    connect(m_close, &QPushButton::clicked, this, [this] { dismiss(); });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (m_loading || row < 0 || row >= m_results.size())
            return;
        const auto callback = onTrackActivated;
        if (callback)
            callback(row);
    });
    connect(m_more, &QPushButton::clicked, this, [this] {
        if (m_loading || m_loadingMore)
            return;
        const auto callback = onMoreRequested;
        if (callback)
            callback();
    });
    m_host->installEventFilter(this);
    updateStyles();
    setLoading({});
    hide();
}

CatalogSearchPanel::~CatalogSearchPanel()
{
    m_reveal->stop();
    if (m_host)
        m_host->removeEventFilter(this);
}

void CatalogSearchPanel::setAccentColor(const QColor &accent)
{
    const QColor color = accent.isValid() ? accent : Aero::defaultAccent();
    if (color == m_accent)
        return;
    m_accent = color;
    updateStyles();
    update();
}

void CatalogSearchPanel::setLoading(const QString &query)
{
    m_query = query;
    m_error.clear();
    m_results.clear();
    m_total = -1;
    m_hasMore = false;
    m_loading = true;
    m_loadingMore = false;
    m_table->setRowCount(0);
    m_content->setCurrentWidget(m_message);
    m_message->setText(QStringLiteral("正在搜索…\n为你寻找歌曲和歌手"));
    updateSummary();
    updateGeometryForState();
}

void CatalogSearchPanel::setResults(const QString &query, const QVector<CatalogTrack> &results,
                                   int total, bool hasMore)
{
    QHash<QString, QIcon> covers;
    if (query == m_query) {
        for (int row = 0; row < m_results.size(); ++row)
            if (const auto *item = m_table->item(row, 0); item && !item->icon().isNull())
                covers.insert(resultKey(m_results.at(row)), item->icon());
    }
    const int oldScroll = query == m_query ? m_table->verticalScrollBar()->value() : 0;
    const QSignalBlocker tableSignals(m_table);
    m_table->setUpdatesEnabled(false);
    m_query = query;
    m_results = results;
    m_total = total;
    m_hasMore = hasMore;
    m_loading = false;
    m_loadingMore = false;
    m_error.clear();
    m_table->setRowCount(0);
    m_table->setRowCount(m_results.size());
    for (int row = 0; row < m_results.size(); ++row) {
        const CatalogTrack &result = m_results.at(row);
        const Track &track = result.track;
        const QString title = track.title.isEmpty() ? QStringLiteral("未命名歌曲") : track.title;
        const QString artist = track.artist.isEmpty() ? QStringLiteral("未知歌手") : track.artist;
        const QString tooltip = title + QLatin1Char('\n') + artist
            + (track.album.isEmpty() ? QString() : QLatin1Char('\n') + track.album)
            + QLatin1Char('\n') + platformName(result.platform);
        auto *cover = textItem({}, tooltip);
        cover->setIcon(covers.value(resultKey(result)));
        cover->setData(Qt::UserRole, row);
        m_table->setItem(row, 0, cover);
        m_table->setItem(row, 1, textItem(title, tooltip));
        m_table->setItem(row, 2, textItem(artist, tooltip));
        auto *platform = textItem(platformName(result.platform));
        platform->setData(Qt::UserRole, static_cast<int>(result.platform));
        if (result.platform == MusicPlatform::Kugou)
            platform->setIcon(QIcon(QStringLiteral(":/platforms/kugou.ico")));
        m_table->setItem(row, 3, platform);
    }
    m_table->verticalScrollBar()->setValue(oldScroll);
    m_table->setUpdatesEnabled(true);
    m_content->setCurrentWidget(m_results.isEmpty() ? static_cast<QWidget *>(m_message) : m_table);
    if (m_results.isEmpty())
        m_message->setText(QStringLiteral("没有找到相关歌曲\n试试歌曲别名、歌手或更短的关键词"));
    updateSummary();
    updateGeometryForState();
    Liquid::invalidateBackdrop(m_table);
}

void CatalogSearchPanel::setError(const QString &error)
{
    m_loading = false;
    m_loadingMore = false;
    m_error = error.trimmed().isEmpty() ? QStringLiteral("搜索暂时失败，请重试") : error;
    if (m_results.isEmpty()) {
        m_content->setCurrentWidget(m_message);
        m_message->setText(QStringLiteral("暂时没能完成搜索\n%1").arg(m_error));
    }
    updateSummary();
    updateGeometryForState();
}

void CatalogSearchPanel::setLoadingMore(bool loading)
{
    m_loadingMore = loading;
    if (loading)
        m_error.clear();
    updateSummary();
}

void CatalogSearchPanel::updateSummary()
{
    m_queryLabel->setText(QFontMetrics(m_queryLabel->font()).elidedText(
        m_query, Qt::ElideRight, qMin(330, qMax(80, width() / 2 - 90))));
    m_queryLabel->setToolTip(m_query);
    if (m_loading)
        m_summary->setText(QStringLiteral("正在搜索在线音乐"));
    else if (!m_error.isEmpty())
        m_summary->setText(m_results.isEmpty() ? QStringLiteral("搜索失败，可以重试")
                                             : QStringLiteral("更多结果加载失败，可以重试"));
    else if (m_total >= 0)
        m_summary->setText(QStringLiteral("已显示 %1 首 · 共 %2 首").arg(m_results.size()).arg(m_total));
    else
        m_summary->setText(QStringLiteral("已显示 %1 首").arg(m_results.size()));
    m_summary->setToolTip(m_error);
    m_more->setText(m_loadingMore ? QStringLiteral("正在加载…")
        : !m_error.isEmpty() ? (m_results.isEmpty() ? QStringLiteral("重试") : QStringLiteral("重试加载"))
                            : QStringLiteral("加载更多"));
    m_more->setVisible(m_loadingMore || m_hasMore || !m_error.isEmpty());
    m_more->setEnabled(!m_loading && !m_loadingMore);
}

QRect CatalogSearchPanel::anchoredRect() const
{
    if (!m_host)
        return {};
    const int margin = qMin(12, qMin(m_host->width(), m_host->height()) / 4);
    const int width = qMin(qBound(640, m_anchor.width(), 900), qMax(1, m_host->width() - margin * 2));
    const int left = qBound(margin, m_anchor.left(), qMax(margin, m_host->width() - width - margin));
    const int top = qBound(margin, m_anchor.bottom() + 9, qMax(margin, m_host->height() - margin - 1));
    const int desiredHeight = m_results.isEmpty() ? 250
        : qMin(520, 128 + qMin(6, int(m_results.size())) * 64);
    const int height = qMin(desiredHeight, qMax(1, m_host->height() - top - margin));
    return QRect(left, top, width, height);
}

void CatalogSearchPanel::showAnchored(const QRect &anchor)
{
    m_anchor = anchor;
    if (!m_host)
        return;
    const QRect target = anchoredRect();
    m_reveal->stop();
    const bool reveal = !m_open || isHidden();
    m_open = true;
    m_escape->setEnabled(true);
    if (reveal) {
        QRect start = target;
        start.setHeight(qMin(72, target.height()));
        setGeometry(start);
        show();
        raise();
        m_reveal->setStartValue(start);
        m_reveal->setEndValue(target);
        m_reveal->start();
    } else {
        setGeometry(target);
        raise();
    }
    updateSummary();
}

void CatalogSearchPanel::dismiss()
{
    if (!m_open)
        return;
    m_open = false;
    m_escape->setEnabled(false);
    m_reveal->stop();
    hide();
    const auto callback = onDismissed;
    if (callback)
        callback();
}

QTableWidget *CatalogSearchPanel::resultsTable() const
{
    return m_table;
}

void CatalogSearchPanel::updateGeometryForState()
{
    if (!m_open || !m_host)
        return;
    m_reveal->stop();
    setGeometry(anchoredRect());
    updateSummary();
}

void CatalogSearchPanel::updateStyles()
{
    setStyleSheet(QStringLiteral(
        "QWidget#catalogSearchPanel { background: transparent; color: #eef4fa; }"
        "QLabel { background: transparent; color: #a9b5c4; border: 0; }"
        "QLabel#catalogHeading { color: #f2f7fd; font-size: 18px; font-weight: 600; }"
        "QLabel#catalogMessage { color: #c9d5e2; font-size: 14px; }"
        "QLabel#catalogSummary, QLabel#catalogQuery { font-size: 12px; }"
        "QStackedWidget, QTableWidget { background: transparent; border: 0; outline: 0; }"
        "QTableWidget::item { background: transparent; border: 0; }"
        "QTableWidget::item:selected { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 7px; margin: 6px 0; }"
        "QScrollBar::handle:vertical { background: rgba(234,243,250,70); border-radius: 3px; min-height: 26px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
    m_close->setAccentColor(m_accent);
    m_more->setAccentColor(m_accent);
    m_delegate->setAccentColor(m_accent);
}

bool CatalogSearchPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            const int row = m_table->currentRow();
            if (!m_loading && row >= 0 && row < m_results.size() && onTrackActivated)
                onTrackActivated(row);
            return true;
        }
    }
    if (watched == m_host) {
        if (event->type() == QEvent::Resize && m_open)
            updateGeometryForState();
        else if (event->type() == QEvent::Hide) {
            // Host teardown/hide is not a user dismissal. Avoid calling controller
            // callbacks after its derived class has already started destruction.
            m_reveal->stop();
            m_escape->setEnabled(false);
            m_open = false;
            hide();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CatalogSearchPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Liquid::paintBackdrop(painter, this, 24);
    Aero::paintGlass(painter, QRectF(rect()).adjusted(6, 6, -6, -6), m_accent, 24);
}
