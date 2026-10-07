#include <QtWidgets>
#include <QtMultimedia>
#include <QtNetwork>
#include <array>
#include <functional>
#include <memory>
#include <vector>

#ifndef PHSRADIO_KUGOU_ONLY
#define PHSRADIO_KUGOU_ONLY 1
#endif
#define private public
#define main originalReleasePlayerMain
#include "../main.cpp"
#undef main
#undef private

#include <cstdlib>
#ifdef Q_OS_WIN
#include <wincrypt.h>
#endif

namespace {

void check(bool condition, const char *message)
{
    if (!condition) { qCritical("%s", message); std::abort(); }
}

QString fixtureProtectedSession()
{
#ifdef Q_OS_WIN
    const QByteArray cookie("MUSIC_U=deliberately-fake-release-test-cookie");
    const QByteArray salt("PHS Radio NetEase session v1");
    DATA_BLOB input{DWORD(cookie.size()), reinterpret_cast<BYTE *>(const_cast<char *>(cookie.constData()))};
    DATA_BLOB entropy{DWORD(salt.size()), reinterpret_cast<BYTE *>(const_cast<char *>(salt.constData()))};
    DATA_BLOB output{};
    check(CryptProtectData(&input, L"PHS Radio isolated release test", &entropy, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output),
          "A valid protected fake account must be generated for restore gating.");
    const QByteArray protectedBytes(reinterpret_cast<const char *>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return QString::fromLatin1(protectedBytes.toBase64());
#else
    return {};
#endif
}

} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(), "Release test settings must be isolated in an owned temporary directory.");
    QCoreApplication::setOrganizationName(QStringLiteral("PHS Radio Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Release Platform Gate"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    check(ReleaseConfig::KugouOnly && ReleaseConfig::availablePlatforms() == QVector<MusicPlatform>{MusicPlatform::Kugou},
          "The public build must expose exactly Kugou.");
    QSettings settings;
    settings.setValue(QStringLiteral("selectedPlatforms"),
                      QStringList{QStringLiteral("qq-music"), QStringLiteral("netease-cloud")});
    settings.setValue(QStringLiteral("setupCompleted"), true);
    settings.setValue(QStringLiteral("accounts/netease/session"), fixtureProtectedSession());
    settings.sync();
    check(loadSelectedPlatforms(settings).isEmpty(), "Legacy selections of unreleased platforms must be removed at startup.");
    settings.setValue(QStringLiteral("selectedPlatforms"),
                      QStringList{QStringLiteral("qq-music"), QStringLiteral("kugou"), QStringLiteral("netease-cloud")});
    check(loadSelectedPlatforms(settings) == QVector<MusicPlatform>{MusicPlatform::Kugou},
          "A mixed saved selection must retain only its released platform.");

    PlayerWindow window({MusicPlatform::QQMusic, MusicPlatform::NetEaseCloud}, true);
    window.show(); QCoreApplication::processEvents();
    check(window.m_pages->currentWidget() == window.m_loginPage,
          "An old closed-platform-only setup must return to the available login page.");
    check(window.m_loginPage->findChildren<QCheckBox *>().size() == 1
          && window.m_loginPage->selectedPlatforms() == QVector<MusicPlatform>{MusicPlatform::Kugou},
          "First-time and legacy setup must offer exactly the Kugou selection.");
    check(window.m_loginPage->kugouLoginButton() && !window.m_loginPage->neteaseLoginButton(),
          "Closed platforms must not create actionable login controls.");
    check(window.m_neteaseApi.m_generation == 0 && window.m_neteaseApi.m_cookie.isEmpty()
          && !window.m_neteaseApi.m_loginInProgress && !window.m_neteaseApi.m_serviceStartedByApp,
          "A valid saved unreleased account must not be restored or start its service.");
    window.m_loginPage->setSelectedPlatforms({MusicPlatform::NetEaseCloud, MusicPlatform::QQMusic});
    check(window.m_loginPage->selectedPlatforms() == QVector<MusicPlatform>{MusicPlatform::Kugou}
          && window.m_loginPage->continueButton()->isEnabled(),
          "A stale login selection must recover to the sole released platform.");
    window.configurePlatforms({MusicPlatform::QQMusic, MusicPlatform::NetEaseCloud, MusicPlatform::Kugou});
    check(window.m_platform->count() == 1 && window.provider()
          && window.provider()->platform() == MusicPlatform::Kugou,
          "Toolbar and configured providers must filter unreleased platforms too.");
    window.m_pages->setCurrentWidget(window.m_playerPage);
    window.showHomePage(); QCoreApplication::processEvents();
    auto *kugou = window.m_homePage->findChild<QListWidget *>(QStringLiteral("kugouRecommendations"));
    auto *netease = window.m_homePage->findChild<QListWidget *>(QStringLiteral("neteaseRecommendations"));
    check(kugou && kugou->isVisible() && netease && !netease->isVisible() && !netease->isEnabled(),
          "The public homepage must show the expanded Kugou section and hide unavailable recommendation entrances.");
    Track closedTrack; closedTrack.id = QStringLiteral("closed"); closedTrack.title = QStringLiteral("Never played");
    window.m_homePage->setRecommendations(MusicPlatform::NetEaseCloud, {closedTrack});
    check(netease->count() == 0, "Late closed-platform data must not populate hidden recommendation content.");
    const int playRequest = window.m_playRequest;
    window.m_dailyTracks.insert(static_cast<int>(MusicPlatform::NetEaseCloud), {closedTrack});
    window.requestRecommendations(MusicPlatform::NetEaseCloud, true);
    window.showPlatformLogin(MusicPlatform::NetEaseCloud);
    window.playRecommendation(MusicPlatform::NetEaseCloud, 0);
    window.m_neteaseApi.onLoginChanged(true);
    check(window.m_playRequest == playRequest && window.m_playbackQueue.empty()
          && window.m_pages->currentWidget() == window.m_playerPage
          && window.m_neteaseApi.m_generation == 0
          && !window.m_recommendationBusy.contains(static_cast<int>(MusicPlatform::NetEaseCloud))
          && window.m_enabledPlatforms == QVector<MusicPlatform>{MusicPlatform::Kugou},
          "Unreleased recommendation/login callbacks must not start playback, requests or platform reconfiguration.");
    window.m_loginPage->continueButton()->click();
    check(QSettings().value(QStringLiteral("selectedPlatforms")).toStringList() == QStringList{QStringLiteral("kugou")},
          "Completing setup must persist only the released platform.");
    QCoreApplication::processEvents();
    return 0;
}
