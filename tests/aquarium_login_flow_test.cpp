#include <QtWidgets>
#include <QtMultimedia>
#include <QtNetwork>
#include <functional>
#include <memory>
#include <vector>
#define private public
#define main originalPlayerMain
#include "../main.cpp"
#undef main
#undef private
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool condition,const char *message){if(!condition){std::fprintf(stderr,"%s\n",message);std::fflush(stderr);std::abort();}}
bool until(const std::function<bool()> &ready,int timeout=6000){QElapsedTimer clock;clock.start();while(!ready()&&clock.elapsed()<timeout){QEventLoop loop;QTimer::singleShot(15,&loop,&QEventLoop::quit);loop.exec();}return ready();}
void activate(AquariumEntrance *scene){QKeyEvent event(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(scene,&event);}
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);QTemporaryDir settings;
    app.setOrganizationName("PHS Radio Entrance Verification");app.setApplicationName("Isolated Entrance Flow");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    PlayerWindow window({MusicPlatform::Kugou,MusicPlatform::NetEaseCloud},true,true);
    window.startEntrance();window.show();window.activateWindow();
    check(until([&]{return window.m_loginPage->entrance()->rendererReady();}),"Native entrance renderer must initialize.");
    auto *scene=window.m_loginPage->entrance();
    check(window.m_pages->currentWidget()==window.m_loginPage&&scene->stage()==AquariumEntrance::Stage::Welcome&&!scene->isReady(),"Startup must begin on MP3, before platform login.");
    window.m_kugouAccountConnected=true;window.updateEntranceAccount();
    check(!scene->isReady(),"A restored account must not make the first logo activation skip login.");
    activate(scene);
    check(window.m_loginPage->entranceLoginVisible()&&scene->stage()==AquariumEntrance::Stage::Login,"First activation must open the actual login form over the same scene.");
    check(window.m_loginPage->continueButton()->text()==QStringLiteral("完成登录"),"Startup login must offer the explicit completion button.");
    window.m_loginPage->setSelectedPlatforms({MusicPlatform::Kugou,MusicPlatform::NetEaseCloud});
    window.m_kugouAccountConnected=true;window.updateEntranceAccount();
    QEventLoop loginEvents;QTimer::singleShot(150,&loginEvents,&QEventLoop::quit);loginEvents.exec();
    check(window.m_loginPage->entranceLoginVisible()&&scene->stage()==AquariumEntrance::Stage::Login&&!scene->isReady(),"Successful login must stay on the form until manual completion, including queued callbacks.");
    window.m_loginPage->continueButton()->click();
    check(!window.m_loginPage->entranceLoginVisible()&&scene->stage()==AquariumEntrance::Stage::Welcome&&scene->isReady(),"Manual completion must return to the illuminated MP3 without entering the player.");
    check(window.m_pages->currentWidget()==window.m_loginPage&&window.m_entranceJourney,"Login completion must not skip the second MP3 activation.");
    activate(scene);
    check(scene->stage()==AquariumEntrance::Stage::Zoom,"Second activation must zoom into the display.");
    check(until([&]{return window.m_pages->currentWidget()==window.m_playerPage;},8000),"Zoom and typing must finish on the existing player.");
    check(!window.m_entranceJourney&&QSettings().value("setupCompleted").toBool(),"Entry must persist setup without changing account credentials.");
    window.showPlatformLogin(MusicPlatform::NetEaseCloud);
    check(window.m_loginPage->entranceLoginVisible(),"Platform connection from the player must still open the login form.");
    window.completeSetup();
    check(window.m_pages->currentWidget()==window.m_playerPage&&!window.m_entranceJourney,"Connection management after entry must return directly to the player.");
    std::puts("Native aquarium login, authenticated return, zoom, typing and subsequent platform management passed.");
    return 0;
}
