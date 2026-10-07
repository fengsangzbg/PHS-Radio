#pragma once

#include "music_domain.h"

#include <QColor>
#include <QPointer>
#include <QRect>
#include <QWidget>
#include <functional>

struct CatalogTrack;
class JellyButton;
class QLabel;
class QPropertyAnimation;
class QShortcut;
class QStackedWidget;
class QTableWidget;
class CatalogGlassDelegate;

// Child overlay below the application's online search field. It owns a result
// snapshot; the controller owns requests, pagination, covers and playback.
class CatalogSearchPanel final : public QWidget {
public:
    explicit CatalogSearchPanel(QWidget *parent);
    ~CatalogSearchPanel() override;

    void setAccentColor(const QColor &accent);
    void setLoading(const QString &query);
    // Pass the full accumulated result snapshot, including previously loaded pages.
    void setResults(const QString &query, const QVector<CatalogTrack> &results,
                    int total, bool hasMore);
    void setError(const QString &error);
    void setLoadingMore(bool loading);
    // anchor is in parent coordinates. Showing never transfers keyboard focus.
    void showAnchored(const QRect &anchor);
    void dismiss();
    QTableWidget *resultsTable() const;

    // Track index refers to the last setResults snapshot, not a filtered/model index.
    std::function<void(int)> onTrackActivated;
    std::function<void()> onMoreRequested;
    std::function<void()> onDismissed;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QRect anchoredRect() const;
    void updateSummary();
    void updateGeometryForState();
    void updateStyles();

    QPointer<QWidget> m_host;
    QColor m_accent;
    QRect m_anchor;
    QVector<CatalogTrack> m_results;
    QString m_query;
    QString m_error;
    int m_total = -1;
    bool m_hasMore = false;
    bool m_loading = false;
    bool m_loadingMore = false;
    bool m_open = false;
    QLabel *m_queryLabel;
    QLabel *m_summary;
    QLabel *m_message;
    QStackedWidget *m_content;
    QTableWidget *m_table;
    CatalogGlassDelegate *m_delegate;
    JellyButton *m_close;
    JellyButton *m_more;
    QPropertyAnimation *m_reveal;
    QShortcut *m_escape;
};
