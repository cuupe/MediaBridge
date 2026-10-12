#include <QString>
#include "ui_checks.hpp"
#include "controller.hpp"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QTest>
#include <QKeyEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QQmlProperty>
#include <QQmlContext>
#include <QMarginsF>
#include <iostream>
#include <functional>
#include <memory>
#include <algorithm>
namespace {
struct Checks {
    QGuiApplication* app;
    QQuickWindow* window;
    Controller* controller;
    QUrl input;
    int passed=0;
    void check(bool result,const char* name){
        if(!result){
            const auto screenshot=qEnvironmentVariable("MB_UI_FAILURE_IMAGE");if(!screenshot.isEmpty())window->grabWindow().save(screenshot);
            if(auto* editor=find(window->contentItem(),"field-width"))std::cerr<<"width editor text="<<editor->property("text").toString().toStdString()<<" focus="<<editor->hasActiveFocus()<<" y="<<editor->mapToScene(QPointF()).y()<<" profile="<<controller->profile().value("width").toInt()<<'\n';
            throw std::runtime_error(name);
        }++passed;std::cout<<"PASS "<<name<<'\n';
    }
    QQuickItem* find(QQuickItem* root,const QString& name){
        if(root->objectName()==name)return root;
        for(auto* child:root->childItems())if(auto* result=find(child,name))return result;
        return nullptr;
    }
    QQuickItem* item(const QString& name){auto*p=find(window->contentItem(),name);if(!p)throw std::runtime_error(("missing UI item "+name).toStdString());return p;}
    void click(QQuickItem* target){
        for(auto* parent=target->parentItem();parent;parent=parent->parentItem()){
            if(parent->metaObject()->indexOfProperty("contentY")<0)continue;
            QPointF local=target->mapToItem(parent,QPointF(target->width()/2,target->height()/2));
            double y=parent->property("contentY").toDouble(),h=parent->height(),max=parent->property("contentHeight").toDouble()-h;
            if(local.y()>h-30)y+=local.y()-h+target->height()+30;
            else if(local.y()<30)y+=local.y()-target->height()-30;
            parent->setProperty("contentY",std::clamp(y,0.0,std::max(0.0,max)));
        }
        QTest::qWait(50);
        QPointF p=target->mapToScene(QPointF(target->width()/2,target->height()/2));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,p.toPoint());
    }
    void key(int code,const QString& text={},Qt::KeyboardModifiers mods=Qt::NoModifier){
        if(text.isEmpty())QTest::keyClick(window,Qt::Key(code),mods);
        else QTest::keyClick(window,text.front().toLatin1(),mods);
    }    void drop(){
        QMimeData mime;mime.setUrls({input});QPoint p(window->width()/2,window->height()/2);
        QDragEnterEvent enter(p,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QDragMoveEvent move(p,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QDropEvent end(p,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&enter);QCoreApplication::sendEvent(window,&move);QCoreApplication::sendEvent(window,&end);
    }
};
void later(const std::shared_ptr<Checks>& test,std::function<void()> fn){QTimer::singleShot(180,test->app,[test,fn]{try{fn();}catch(const std::exception&e){std::cerr<<"UI FAIL "<<e.what()<<'\n';test->app->exit(1);}});}
}
void runLayoutUiChecks(QGuiApplication& app,QObject* root,Controller& controller,const QString& fixture,const QString& screenshotDirectory){
    auto test=std::make_shared<Checks>();test->app=&app;test->window=qobject_cast<QQuickWindow*>(root);test->controller=&controller;
    if(!test->window){app.exit(1);return;}
    test->input=QUrl::fromLocalFile(fixture);
    later(test,[test,root,screenshotDirectory]{
        test->drop();QTest::qWait(100);
        struct ScreenCase{const char* name;int width,height;QMarginsF insets;};
        const ScreenCase cases[]{
            {"small-portrait",320,480,{}},{"classic-portrait",360,640,{}},
            {"tall-portrait",390,844,{}},{"extra-tall",360,980,{}},
            {"notch",390,844,{0,44,0,24}},{"punch-hole",412,915,{0,32,0,24}},
            {"rounded-corners",360,780,{12,32,12,24}},{"fold-cover",320,760,{0,32,0,24}},
            {"landscape-notch-left",844,390,{52,0,0,24}},{"landscape-notch-right",844,390,{0,0,52,24}},
            {"short-landscape",640,360,{0,24,24,0}},{"split-window",320,360,{}},
            {"square-window",480,480,{}},{"fold-inner",720,720,{}},
            {"tablet-portrait",800,1280,{0,24,0,24}},{"tablet-landscape",1280,800,{0,24,24,0}},
            {"fold-expanded",1024,768,{0,24,0,24}}
        };
        for(const auto& screen:cases){
            QQmlProperty insets(root,"SafeArea.additionalMargins",qmlContext(root));
            test->check(insets.write(QVariant::fromValue(screen.insets)),"safe area injection uses actual Qt attached property");
            test->window->resize(screen.width,screen.height);QTest::qWait(80);
            const auto safe=QRectF(screen.insets.left(),screen.insets.top(),screen.width-screen.insets.left()-screen.insets.right(),screen.height-screen.insets.top()-screen.insets.bottom());
            auto bounds=[&](QQuickItem* item){return QRectF(item->mapToScene(QPointF()),QSizeF(item->width(),item->height()));};
            auto fits=[&](QQuickItem* item){const auto r=bounds(item);return r.width()>0&&r.height()>0&&safe.adjusted(-1,-1,1,1).contains(r);};
            test->check(fits(test->item("footer-controls")),"footer controls stay inside all four safe edges");
            test->check(fits(test->item("start-queue"))&&test->item("start-queue")->width()>=72,"start remains reachable on every aspect ratio");
            if(test->window->property("compact").toBool())test->check(fits(test->item("mobile-navigation")),"mobile tabs avoid side cutouts");
            for(int route=0;route<4;++route){
                auto* button=test->item((test->window->property("compact").toBool()?"mobile-route-":"route-")+QString::number(route));
                test->click(button);QTest::qWait(50);
                test->check(test->window->property("route").toInt()==route,"navigation works after live resize and inset change");
                auto* content=test->item("main-content");auto* header=test->item("mobile-header");auto* footer=test->item("queue-footer");
                test->check(fits(content)&&content->height()>=40&&(!header->isVisible()||bounds(content).top()>=bounds(header).bottom()-1)&&bounds(content).bottom()<=bounds(footer).top()+1,"page viewport avoids navigation and footer on every route");
                test->check(test->item("page-stack")->width()>100&&test->item("page-stack")->height()>=179,"queue and forms retain a usable scroll viewport");
                if(!screenshotDirectory.isEmpty()&&(route==0||route==1)&&QString(screen.name).contains("landscape")){
                    QDir().mkpath(screenshotDirectory);test->window->grabWindow().save(screenshotDirectory+'/'+screen.name+"-"+QString::number(route)+".png");
                }
            }
            std::cout<<"PASS screen "<<screen.name<<' '<<screen.width<<'x'<<screen.height<<'\n';
        }
        test->window->resize(390,844);root->setProperty("route",1);root->setProperty("keyboardOpen",true);root->setProperty("keyboardInset",280);QTest::qWait(100);
        test->check(!test->item("queue-footer")->isVisible()&&!test->item("mobile-header")->isVisible(),"keyboard frees space by hiding navigation and queue actions");
        test->click(test->item("field-width"));QTest::qWait(100);
        auto* editor=test->item("field-width");auto* viewport=test->item("main-content");
        const auto e=editor->mapToScene(QPointF()),v=viewport->mapToScene(QPointF());
        test->check(e.y()>=v.y()-1&&e.y()+editor->height()<=v.y()+viewport->height()+1,"focused numeric editor scrolls above keyboard");
        root->setProperty("keyboardOpen",false);root->setProperty("keyboardInset",0);QTest::qWait(50);
        test->check(test->item("queue-footer")->isVisible(),"queue actions return after keyboard closes");
        std::cout<<test->passed<<" layout checks passed\n";test->app->exit(0);
    });
    QTimer::singleShot(60000,&app,[&app]{std::cerr<<"Layout UI test timeout\n";app.exit(1);});
}
void runAudioUiChecks(QGuiApplication& app,QObject* root,Controller& controller,const QString& fixture){
    auto test=std::make_shared<Checks>();test->app=&app;test->window=qobject_cast<QQuickWindow*>(root);test->controller=&controller;
    if(!test->window){app.exit(1);return;}
    QString copy=controller.dataDirectory()+QStringLiteral("/ビートまりお - ナイト・オブ・ナイツ.wav");
    if(!QFile::copy(fixture,copy)){app.exit(1);return;}test->input=QUrl::fromLocalFile(copy);
    QDir().mkpath(controller.dataDirectory()+"/ui-output");controller.setSetting("outputDirectory",QUrl::fromLocalFile(controller.dataDirectory()+"/ui-output").toString());
    controller.setProfileValue("format","wav");controller.setProfileValue("mode","remux");
    later(test,[test]{
        test->drop();test->check(test->controller->count()==1,"Japanese WAV native drop adds task");
        QElapsedTimer timer;timer.start();while((test->controller->tasks()->tasks[0].media.isEmpty()||test->controller->busy())&&timer.elapsed()<10000)QTest::qWait(20);
        test->check(test->controller->tasks()->tasks[0].media.value("audioCodec")=="pcm_s16le","UI identifies actual WAV PCM encoding");
        test->check(test->controller->profile().value("mode")=="remux","UI retains compatible WAV remux");
        auto* format=test->item("quick-format");int target=-1,index=0;
        for(const auto& value:test->controller->formats()){if(value.toMap().value("id")=="mp3")target=index;++index;}
        test->check(target>=0,"MP3 target available");
        int current=format->property("currentIndex").toInt();test->click(format);
        for(int i=0;i<std::abs(target-current);++i)test->key(target>current?Qt::Key_Down:Qt::Key_Up);
        test->key(Qt::Key_Return);QTest::qWait(30);
        test->check(test->controller->tasks()->tasks[0].profile.value("format")=="mp3"&&test->controller->tasks()->tasks[0].profile.value("mode")=="transcode","UI MP3 selection switches WAV task to transcode");
        test->check(test->item("quick-mode")->property("currentIndex").toInt()==0,"processing selector displays automatic transcode mode");
        test->click(test->item("quick-mode"));test->key(Qt::Key_Down);test->key(Qt::Key_Return);QTest::qWait(30);
        test->check(test->controller->profile().value("mode")=="transcode"&&test->item("quick-mode")->property("currentIndex").toInt()==0,"incompatible UI remux choice returns to transcode visually");
        QObject::connect(test->controller,&Controller::finished,test->app,[test](bool success){
            try{test->check(success&&test->controller->completedCount()==1,"WAV to MP3 finishes through actual UI start");
                test->check(QFile::exists(QUrl(test->controller->tasks()->tasks[0].output).toLocalFile()),"converted MP3 saved with Japanese filename");
                std::cout<<test->passed<<" UI checks passed\n";test->app->exit(0);
            }catch(const std::exception&e){std::cerr<<"UI FAIL "<<e.what()<<'\n';test->app->exit(1);}
        });
        test->click(test->item("start-queue"));
    });
    QTimer::singleShot(45000,&app,[&app]{std::cerr<<"Audio UI test timeout\n";app.exit(1);});
}
void runUiChecks(QGuiApplication& app,QObject* root,Controller& controller,const QString& fixture){
    auto test=std::make_shared<Checks>();test->app=&app;test->window=qobject_cast<QQuickWindow*>(root);test->controller=&controller;
    if(!test->window){app.exit(1);return;}
    QString copy=controller.dataDirectory()+"/ui-fixture.mp4";QFile::remove(copy);
    if(!QFile::copy(fixture,copy)){app.exit(1);return;}test->input=QUrl::fromLocalFile(copy);
    QDir().mkpath(controller.dataDirectory()+"/ui-output");controller.setSetting("outputDirectory",QUrl::fromLocalFile(controller.dataDirectory()+"/ui-output").toString());
    later(test,[test]{
        auto* content = test->item("main-content");
        auto* header = test->item("mobile-header");
        auto* footer = test->item("queue-footer");
        test->check(!header->isVisible() || content->mapToScene(QPointF()).y() >= header->mapToScene(QPointF(0,header->height())).y()-1,
                    "content stays below mobile navigation");
        test->check(content->mapToScene(QPointF(0,content->height())).y() <= footer->mapToScene(QPointF()).y()+1,
                    "content stays above queue footer");
        test->drop();test->check(test->controller->count()==1,"native file drop adds queue task");
        test->drop();test->check(test->controller->count()==1,"repeated drop ignores queued duplicate");
        test->click(test->item("quick-format"));test->key(Qt::Key_Down);test->key(Qt::Key_Return);
        test->check(test->controller->tasks()->tasks[0].profile.value("format")=="mkv","quick format choice updates queued task without manual apply");
        test->click(test->item("quick-mode"));test->key(Qt::Key_Down);test->key(Qt::Key_Return);
        test->check(test->controller->tasks()->tasks[0].profile.value("mode")=="remux","quick mode choice updates selected task");
        test->click(test->item("quick-mode"));test->key(Qt::Key_Up);test->key(Qt::Key_Return);
        test->check(test->controller->profile().value("mode")=="transcode"&&!test->controller->profile().value("copySubtitles").toBool(),"UI mode transition returns to usable transcode profile");
        test->click(test->item("quick-format"));test->key(Qt::Key_Up);test->key(Qt::Key_Return);
        test->click(test->item(test->window->property("compact").toBool()?"mobile-route-1":"route-1"));
        test->check(test->window->property("route").toInt()==1,"mouse navigation opens processing page");
        later(test,[test]{
            test->click(test->item("field-width"));
            test->check(test->item("field-width")->hasActiveFocus(),"numeric field receives pointer focus");
            test->key(Qt::Key_A,{},Qt::ControlModifier);
            test->key(Qt::Key_1,"1");test->key(Qt::Key_6,"6");test->key(Qt::Key_0,"0");
            test->click(test->item(test->window->property("compact").toBool()?"apply-profile-mobile":"apply-profile"));
            test->check(test->controller->profile().value("width").toInt()==160,"numeric field commits through UI");
            test->check(test->controller->tasks()->tasks[0].profile.value("width").toInt()==160,"UI applies profile to selected task");
            test->click(test->item(test->window->property("compact").toBool()?"mobile-route-2":"route-2"));
            later(test,[test]{
                test->click(test->item("combo-theme"));test->key(Qt::Key_Down);test->key(Qt::Key_Return);
                test->check(test->controller->settings().value("theme")=="light","theme combo persists activated selection");
                test->click(test->item(test->window->property("compact").toBool()?"mobile-route-0":"route-0"));
                later(test,[test]{
                    test->click(test->item("start-queue"));
                    test->check(test->controller->busy(),"mouse start launches selected profile conversion");
                    QObject::connect(test->controller,&Controller::finished,test->app,[test](bool success){
                        try{
                            test->check(success&&test->controller->completedCount()==1,"UI batch reaches successful result");
                            auto output=QUrl(test->controller->tasks()->tasks[0].output).toLocalFile();
                            test->check(QFile::exists(output),"UI output saved at configured directory");
                            std::cout<<test->passed<<" UI checks passed\n";test->app->exit(0);
                        }catch(const std::exception&e){std::cerr<<"UI FAIL "<<e.what()<<'\n';test->app->exit(1);}
                    });
                });
            });
        });
    });
    QTimer::singleShot(30000,&app,[&app]{std::cerr<<"UI test timeout\n";app.exit(1);});
}
