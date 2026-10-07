#pragma once

#include "music_domain.h"

#include <QImage>
#include <QPair>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QPushButton;

class PlatformLoginPage final : public QWidget {
public:
    explicit PlatformLoginPage(QWidget *parent = nullptr);

    QVector<MusicPlatform> selectedPlatforms() const;
    void setSelectedPlatforms(const QVector<MusicPlatform> &platforms);
    QPushButton *continueButton() const;
    QPushButton *kugouLoginButton() const;
    // A platform absent from this build has no login control (returns nullptr).
    QPushButton *neteaseLoginButton() const;
    void setKugouStatus(const QString &status);
    void setKugouQr(const QImage &image);
    void clearKugouQr();
    void setNeteaseStatus(const QString &status);
    void setNeteaseQr(const QImage &image);
    void clearNeteaseQr();

private:
    void updateSelection();

    QVector<QPair<MusicPlatform, QCheckBox *>> m_options;
    class QLabel *m_selectionCount = nullptr;
    QPushButton *m_continueButton = nullptr;
    QPushButton *m_kugouLoginButton = nullptr;
    QPushButton *m_neteaseLoginButton = nullptr;
    class QLabel *m_kugouStatus = nullptr;
    class QLabel *m_neteaseStatus = nullptr;
    class QLabel *m_kugouQrLabel = nullptr;
    QWidget *m_kugouQrPanel = nullptr;
    class QLabel *m_qrHint = nullptr;
    MusicPlatform m_qrPlatform = MusicPlatform::Kugou;
};
