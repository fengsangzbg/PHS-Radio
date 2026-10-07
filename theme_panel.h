#pragma once

#include "background_theme.h"

#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QSlider;

class ThemePanel final : public QDialog {
public:
    explicit ThemePanel(QWidget *parent = nullptr);
    void setTheme(const BackgroundTheme &theme);
    BackgroundTheme theme() const;
    std::function<void(const BackgroundTheme &)> onThemeSelected;

private:
    BackgroundTheme m_theme;
    WallpaperEngineLibrary m_library;
    QComboBox *m_wallpapers;
    QLabel *m_current;
    QLabel *m_libraryStatus;
    QSlider *m_dimming;
    void selectTheme(const BackgroundTheme &theme);
    void refreshWallpapers(const QStringList &steamRoots = {});
    void updateCurrent();
};
