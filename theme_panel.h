#pragma once

#include "background_theme.h"

#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QSlider;
class JellyButton;
class QShowEvent;

class ThemePanel final : public QDialog {
public:
    explicit ThemePanel(QWidget *parent = nullptr);
    void setTheme(const BackgroundTheme &theme);
    BackgroundTheme theme() const;
    std::function<void(const BackgroundTheme &)> onThemeSelected;

protected:
    void showEvent(QShowEvent *event) override;

private:
    BackgroundTheme m_theme;
    WallpaperEngineLibrary m_library;
    QComboBox *m_wallpapers;
    QLabel *m_current;
    QLabel *m_libraryStatus;
    QSlider *m_dimming;
    QLabel *m_engineFrameRate;
    JellyButton *m_engineSettings;
    void selectTheme(const BackgroundTheme &theme);
    void refreshWallpapers(const QStringList &steamRoots = {});
    void updateCurrent();
    QString engineExecutable() const;
    void updateEngineFrameRate();
};
