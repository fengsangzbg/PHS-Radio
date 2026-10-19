#include "../aquarium_entrance.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <cstdlib>

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    AquariumEntrance scene;
    scene.resize(1440,900);scene.setWindowTitle(QStringLiteral("PHS Radio · 玻璃水族箱启动场景预览"));
    const auto args=app.arguments();const int output=args.indexOf(QStringLiteral("--qa"));
    bool loginRequested=false,entered=false;
    scene.onLoginRequested=[&]{loginRequested=true;scene.showLogin();};
    scene.onEntryFinished=[&]{entered=true;};
    scene.show();scene.activateWindow();scene.setFocus();
    if(output>=0&&output+1<args.size()) {
        const QString directory=args.at(output+1);
        QTimer::singleShot(3500,&scene,[&scene,directory]{scene.grabFramebuffer().save(directory+"/entrance-welcome.png");});
        QTimer::singleShot(3800,&scene,[&scene]{QKeyEvent key(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(&scene,&key);});
        QTimer::singleShot(4400,&scene,[&scene,directory]{scene.grabFramebuffer().save(directory+"/entrance-login-background.png");scene.showWelcome(true);});
        QTimer::singleShot(6400,&scene,[&scene,directory]{scene.grabFramebuffer().save(directory+"/entrance-ready.png");});
        QTimer::singleShot(6700,&scene,[&scene]{scene.enterPlayer();});
        QTimer::singleShot(8250,&scene,[&scene,directory]{scene.grabFramebuffer().save(directory+"/entrance-typing.png");});
        QTimer::singleShot(12000,&scene,[&,directory]{
            const QJsonObject report{{"renderer",scene.rendererName()},{"ready",scene.rendererReady()},{"error",scene.rendererError()},
                {"logicalWidth",scene.width()},{"logicalHeight",scene.height()},{"devicePixelRatio",scene.devicePixelRatioF()},
                {"displayRefreshHz",scene.screen()->refreshRate()},{"fps",scene.measuredFps()},
                {"frameP95Ms",scene.frameTimePercentile(.95)},{"frames",double(scene.presentedFrames())},
                {"loginRequested",loginRequested},{"entryFinished",entered}};
            QFile file(directory+"/entrance-qa.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(report).toJson());
            app.exit(scene.rendererReady()&&loginRequested&&entered?0:1);
        });
    } else scene.onLoginRequested=[&]{scene.showLogin();QTimer::singleShot(1800,&scene,[&]{scene.showWelcome(true);});};
    return app.exec();
}
