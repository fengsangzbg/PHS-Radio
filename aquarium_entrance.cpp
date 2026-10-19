#include "aquarium_entrance.h"

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
constexpr double Pi = 3.141592653589793;
const QString Greeting = QStringLiteral("新大地-今天想听点什么");
QImage armsImage()
{
    QImage image(2048,2048,QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.scale(4,4);
    QPainterPath arm;
    arm.moveTo(94,438); arm.cubicTo(39,384,28,308,43,234);
    arm.cubicTo(64,126,160,60,267,63); arm.cubicTo(339,65,403,101,443,159);
    arm.cubicTo(371,121,294,117,238,145); arm.cubicTo(175,177,145,238,158,299);
    arm.cubicTo(171,354,213,390,271,392); arm.cubicTo(213,438,143,462,94,438); arm.closeSubpath();
    QLinearGradient fill(68,64,400,450);
    fill.setColorAt(0,QColor(187,255,255,250)); fill.setColorAt(.26,QColor(75,217,236,250));
    fill.setColorAt(.62,QColor(30,151,195,245)); fill.setColorAt(1,QColor(20,81,114,245));
    QLinearGradient rim(70,55,415,405);
    rim.setColorAt(0,QColor(245,255,255,240)); rim.setColorAt(.5,QColor(148,234,255,110)); rim.setColorAt(1,QColor(138,224,245,210));
    for(int i=0;i<2;++i) {
        p.save(); if(i){p.translate(256,256);p.rotate(180);p.translate(-256,-256);}
        p.setPen(QPen(rim,3.3));p.setBrush(fill);p.drawPath(arm);
        QPainterPath gleam;gleam.moveTo(69,243);gleam.cubicTo(91,140,177,86,268,88);gleam.cubicTo(325,89,373,110,409,137);
        p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(238,255,255,170),4.5,Qt::SolidLine,Qt::RoundCap));p.drawPath(gleam);p.restore();
    }
    return image;
}
QImage letterImage()
{
    QImage image(2048,2048,QImage::Format_RGBA8888);image.fill(Qt::transparent);
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.scale(4,4);
    QLinearGradient eye(210,132,282,390);eye.setColorAt(0,QColor(37,79,95,230));eye.setColorAt(1,QColor(5,15,27,245));
    p.setBrush(eye);p.setPen(QPen(QColor(171,244,255,140),3.4));p.drawEllipse(QPointF(256,256),122,122);
    QPainterPath letter;letter.setFillRule(Qt::OddEvenFill);
    letter.moveTo(189,158);letter.lineTo(263,158);letter.cubicTo(313,158,343,186,343,230);
    letter.cubicTo(343,274,312,300,264,300);letter.lineTo(238,300);letter.lineTo(238,360);letter.lineTo(189,360);letter.closeSubpath();
    letter.moveTo(238,204);letter.lineTo(238,255);letter.lineTo(259,255);letter.cubicTo(281,255,294,246,294,230);letter.cubicTo(294,214,281,204,259,204);letter.closeSubpath();
    QLinearGradient pearl(209,160,303,363);pearl.setColorAt(0,Qt::white);pearl.setColorAt(.47,QColor(232,251,255));pearl.setColorAt(1,QColor(176,224,241));
    p.setBrush(pearl);p.setPen(QPen(QColor(242,255,255,170),1.2));p.drawPath(letter);
    return image;
}
QImage typingImage(QVector<float> &ends)
{
    // Supersample normal antialiased UI glyphs once. Linear mipmapped sampling
    // keeps strokes smooth at the actual high-DPI output size.
    QFont font(QStringLiteral("Microsoft YaHei UI"));font.setPixelSize(112);font.setWeight(QFont::Medium);
    font.setHintingPreference(QFont::PreferNoHinting);
    const QFontMetricsF metrics(font);
    QImage image(qCeil(metrics.horizontalAdvance(Greeting))+32,192,QImage::Format_RGBA8888);image.fill(Qt::transparent);
    QPainter p(&image);p.setFont(font);p.setPen(Qt::white);
    p.setRenderHint(QPainter::TextAntialiasing,true);
    qreal x=16;
    const qreal baseline=(192-metrics.height())*.5+metrics.ascent();
    ends.clear();
    for(int i=0;i<Greeting.size();++i){
        const QString glyph=Greeting.mid(i,1);
        p.drawText(QPointF(x,baseline),glyph);
        x+=metrics.horizontalAdvance(glyph);
        ends.append(x/image.width());
    }
    return image;
}
}

AquariumEntrance::AquariumEntrance(QWidget *parent):QOpenGLWidget(parent)
{
    Q_INIT_RESOURCE(entrance);
    setObjectName(QStringLiteral("aquariumEntrance"));
    setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("玻璃 MP3：点击 P 图标登录或进入播放器"));
    QSurfaceFormat f;f.setVersion(3,3);f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(0);f.setStencilBufferSize(0);f.setSamples(0);f.setSwapInterval(1);
    setFormat(f);setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    m_clock.start();m_presentClock.start();qApp->installEventFilter(this);
    connect(this,&QOpenGLWidget::frameSwapped,this,[this]{
        const auto now=m_presentClock.nsecsElapsed();
        if(m_previousPresent&&isVisible()&&window()->isActiveWindow()) {
            const double seconds=(now-m_previousPresent)/1e9;
            if(seconds>0&&seconds<.25){m_frameTimes.append(seconds);if(m_frameTimes.size()>600)m_frameTimes.remove(0);}
        }
        m_previousPresent=now;++m_presented;
        if(isVisible()&&window()->isActiveWindow()&&m_stage!=Stage::Finished)
            QTimer::singleShot(0,this,[this]{update();});
    });
}
AquariumEntrance::~AquariumEntrance(){cleanup();if(context())disconnect(context(),nullptr,this,nullptr);}
void AquariumEntrance::cleanup()
{
    if(!context())return;
    makeCurrent();m_background.reset();m_whale.reset();m_arms.reset();m_letter.reset();m_screen.reset();m_type.reset();m_program.reset();
    if(m_vbo){glDeleteBuffers(1,&m_vbo);m_vbo=0;}if(m_vao){glDeleteVertexArrays(1,&m_vao);m_vao=0;}
    m_rendererReady=false;doneCurrent();
}
std::unique_ptr<QOpenGLTexture> AquariumEntrance::texture(const QImage &image,bool nearest)
{
    auto tex=std::make_unique<QOpenGLTexture>(image.convertToFormat(QImage::Format_RGBA8888));
    tex->setWrapMode(QOpenGLTexture::ClampToEdge);
    tex->setMinificationFilter(nearest?QOpenGLTexture::Nearest:QOpenGLTexture::LinearMipMapLinear);
    tex->setMagnificationFilter(nearest?QOpenGLTexture::Nearest:QOpenGLTexture::Linear);
    if(!nearest)tex->setMaximumAnisotropy(8);
    return tex;
}
void AquariumEntrance::initializeGL()
{
    if(!initializeOpenGLFunctions()){m_error=QStringLiteral("无法初始化 OpenGL 3.3");return;}
    connect(context(),&QOpenGLContext::aboutToBeDestroyed,this,[this]{cleanup();},Qt::DirectConnection);
    m_rendererName=QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    m_program=std::make_unique<QOpenGLShaderProgram>();
    const char *vertex="#version 330 core\nlayout(location=0) in vec2 position; out vec2 vUV; void main(){vUV=position*.5+.5;gl_Position=vec4(position,0.,1.);}";
    QFile fragment(QStringLiteral(":/entrance/scene.frag"));
    if(!fragment.open(QIODevice::ReadOnly)){m_error=QStringLiteral("无法读取启动场景着色器");return;}
    if(!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)
        ||!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,fragment.readAll())||!m_program->link()) {
        m_error=m_program->log();QTimer::singleShot(0,this,[this]{if(onRendererFailed)onRendererFailed(m_error);});return;
    }
    const QImage background(QStringLiteral(":/entrance/sky.png")),whale(QStringLiteral(":/entrance/whale.png"));
    if(background.isNull()||whale.isNull()){m_error=QStringLiteral("启动场景素材未载入");return;}
    m_background=texture(background);m_whale=texture(whale);m_arms=texture(armsImage());m_letter=texture(letterImage());
    const QImage greeting=typingImage(m_glyphEnds);m_typeAspect=float(greeting.width())/greeting.height();m_type=texture(greeting);
    const float quad[]={-1,-1,1,-1,-1,1,1,1};
    glGenVertexArrays(1,&m_vao);glGenBuffers(1,&m_vbo);glBindVertexArray(m_vao);glBindBuffer(GL_ARRAY_BUFFER,m_vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);glBindVertexArray(0);
    m_rendererReady=true;m_screenDirty=true;
}
QVector2D AquariumEntrance::deviceCenter()const{return QVector2D(.0f,.025f+std::sin(m_time*.72)*.008f);}
float AquariumEntrance::deviceAngle()const{return .018f+std::sin(m_time*.48)*.014f;}
void AquariumEntrance::advance(double dt)
{
    const double elapsed=std::max(0.,dt);
    m_time+=elapsed;m_stageTime+=elapsed;
    dt=std::min(elapsed,.05);
    auto approach=[dt](float &value,float target,float rate){value+=(target-value)*(1-std::exp(-rate*dt));};
    approach(m_hover,m_hovered?1.f:0.f,8);approach(m_light,m_ready?1.f:0.f,1.5);
    approach(m_login,m_stage==Stage::Login?1.f:0.f,7);
    approach(m_spinVelocity,m_hovered?3.5f:.13f,m_hovered?3.f:1.5f);m_spin+=m_spinVelocity*dt;
    for(int i=0;i<4;++i){float step=dt/4;m_pressVelocity+=(145*((m_pressed?1.f:0.f)-m_press)-13*m_pressVelocity)*step;m_press+=m_pressVelocity*step;}
    if(m_stage==Stage::Zoom&&m_stageTime>=.92){m_stage=Stage::Typing;m_stageTime-=.92;}
    if(m_stage==Stage::Typing&&m_stageTime>=1.18){m_stage=Stage::Finished;if(onEntryFinished)QTimer::singleShot(0,this,[this]{if(onEntryFinished)onEntryFinished();});}
}
void AquariumEntrance::updateScreenTexture()
{
    QImage image(1280,1024,QImage::Format_RGBA8888);image.fill(Qt::transparent);
    QPainter p(&image);p.setRenderHint(QPainter::TextAntialiasing);
    QFont font(QStringLiteral("Segoe UI"));font.setPixelSize(62);font.setLetterSpacing(QFont::AbsoluteSpacing,15);p.setFont(font);
    p.setPen(QColor(218,250,255,210));p.drawText(QRect(80,120,1120,110),Qt::AlignCenter,QStringLiteral("P H S   R A D I O"));
    font.setFamily(QStringLiteral("Microsoft YaHei UI"));font.setPixelSize(m_ready?100:79);font.setLetterSpacing(QFont::AbsoluteSpacing,3);font.setWeight(QFont::DemiBold);p.setFont(font);
    const QString instruction=m_ready?QStringLiteral("登陆播放器"):QStringLiteral("点击软件图标\n进入登录界面");
    p.setPen(QColor(8,54,73,170));p.drawText(QRect(60,340,1160,330).translated(0,6),Qt::AlignCenter,instruction);
    p.setPen(QColor(248,255,255));p.drawText(QRect(60,340,1160,330),Qt::AlignCenter,instruction);
    font.setPixelSize(44);font.setWeight(QFont::Normal);font.setLetterSpacing(QFont::AbsoluteSpacing,5);p.setFont(font);p.setPen(QColor(211,245,255,210));
    p.drawText(QRect(50,810,1180,85),Qt::AlignCenter,m_ready?QStringLiteral("轻触 P · 今天想听点什么"):QStringLiteral("轻触 P · 让音乐开始流动"));
    p.end();m_screen=texture(image);m_screenDirty=false;
}
void AquariumEntrance::paintGL()
{
    glClearColor(.42,.78,.9,1);glClear(GL_COLOR_BUFFER_BIT);
    const auto now=m_clock.nsecsElapsed();advance(m_previousTick?(now-m_previousTick)/1e9:0);m_previousTick=now;
    if(!m_rendererReady)return;
    if(m_screenDirty)updateScreenTexture();
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);m_program->bind();
    QOpenGLTexture *textures[]={m_background.get(),m_whale.get(),m_arms.get(),m_letter.get(),m_screen.get(),m_type.get()};
    const char *names[]={"skyTex","whaleTex","armsTex","letterTex","screenTex","typeTex"};
    for(int i=0;i<6;++i){textures[i]->bind(i);m_program->setUniformValue(names[i],i);}
    const float ratio=float(width())/std::max(1,height());
    m_program->setUniformValue("viewport",QVector2D(width()*devicePixelRatioF(),height()*devicePixelRatioF()));
    m_program->setUniformValue("deviceCenter",deviceCenter());m_program->setUniformValue("deviceAngle",deviceAngle());
    m_program->setUniformValue("pointer",QVector2D((m_pointer.x()/std::max(1,width())-.5)*ratio,m_pointer.y()/std::max(1,height())-.5));
    m_program->setUniformValue("time",float(m_time));m_program->setUniformValue("spin",m_spin);
    m_program->setUniformValue("hover",m_hover);m_program->setUniformValue("press",m_press);
    m_program->setUniformValue("light",m_light);m_program->setUniformValue("loginBlend",m_login);
    float zoom=0,white=0,type=0;
    if(m_stage==Stage::Zoom){zoom=std::min(1.,m_stageTime/.86);white=std::clamp((m_stageTime-.38)/.45,0.,1.);}
    if(m_stage==Stage::Typing||m_stage==Stage::Finished){zoom=1;white=1;type=std::clamp((m_stageTime-.08)/.68,0.,1.);}
    m_program->setUniformValue("zoomProgress",zoom);m_program->setUniformValue("whiteProgress",white);m_program->setUniformValue("typeProgress",type);m_program->setUniformValue("glyphCount",int(Greeting.size()));
    m_program->setUniformValue("typeAspect",m_typeAspect);
    m_program->setUniformValueArray("glyphEnds",m_glyphEnds.constData(),m_glyphEnds.size(),1);
    glBindVertexArray(m_vao);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glBindVertexArray(0);m_program->release();
}
void AquariumEntrance::setReady(bool ready){if(m_ready==ready)return;m_ready=ready;m_screenDirty=true;update();}
void AquariumEntrance::showWelcome(bool ready){m_stage=Stage::Welcome;m_stageTime=0;setReady(ready);m_hovered=m_pressed=false;m_previousTick=0;update();}
void AquariumEntrance::showLogin(){m_stage=Stage::Login;m_stageTime=0;m_hovered=m_pressed=false;update();}
void AquariumEntrance::enterPlayer(){if(!m_ready||m_stage!=Stage::Welcome)return;m_stage=Stage::Zoom;m_stageTime=0;m_hovered=m_pressed=false;update();}
QPointF AquariumEntrance::logoCenter()const
{
    const auto center=deviceCenter();const float a=deviceAngle();
    return QPointF(width()*.5+(center.x()-std::sin(a)*.164)*height(),height()*(.5+center.y()+std::cos(a)*.164));
}
bool AquariumEntrance::hitLogo(const QPointF &point)const{return m_stage==Stage::Welcome&&QLineF(point,logoCenter()).length()<height()*.063;}
void AquariumEntrance::activateLogo(){if(m_stage!=Stage::Welcome)return;if(m_ready)enterPlayer();else if(onLoginRequested)onLoginRequested();}
void AquariumEntrance::mouseMoveEvent(QMouseEvent *e){m_pointer=e->position();m_hovered=hitLogo(m_pointer);setCursor(m_hovered?Qt::PointingHandCursor:Qt::ArrowCursor);}
void AquariumEntrance::mousePressEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton&&hitLogo(e->position())){m_pressed=true;setFocus();e->accept();}else QOpenGLWidget::mousePressEvent(e);}
void AquariumEntrance::mouseReleaseEvent(QMouseEvent *e){const bool activated=m_pressed&&hitLogo(e->position())&&e->button()==Qt::LeftButton;m_pressed=false;if(activated)QTimer::singleShot(95,this,[this]{activateLogo();});}
void AquariumEntrance::leaveEvent(QEvent *e){m_hovered=false;QOpenGLWidget::leaveEvent(e);}
void AquariumEntrance::showEvent(QShowEvent *e){m_previousTick=m_previousPresent=0;QOpenGLWidget::showEvent(e);update();}
void AquariumEntrance::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space){activateLogo();e->accept();}else QOpenGLWidget::keyPressEvent(e);}
bool AquariumEntrance::eventFilter(QObject *watched,QEvent *event){if(watched==window()&&event->type()==QEvent::WindowActivate){m_previousTick=m_previousPresent=0;update();}return QOpenGLWidget::eventFilter(watched,event);}
double AquariumEntrance::measuredFps()const{return m_frameTimes.isEmpty()?0:m_frameTimes.size()/std::accumulate(m_frameTimes.begin(),m_frameTimes.end(),0.);}
double AquariumEntrance::frameTimePercentile(double percentile)const{if(m_frameTimes.isEmpty())return 0;auto sorted=m_frameTimes;std::sort(sorted.begin(),sorted.end());return sorted[std::clamp(int(percentile*(sorted.size()-1)),0,int(sorted.size()-1))]*1000;}
