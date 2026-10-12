#include "controller.hpp"
#ifdef MB_UI_CHECKS
#include "ui_checks.hpp"
#endif
#include <QGuiApplication>
#include <QFont>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QUrl>
#include <QImage>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
#ifdef Q_OS_ANDROID
    QSurfaceFormat surface;
    surface.setRenderableType(QSurfaceFormat::OpenGLES);
    surface.setVersion(2, 0);
    QSurfaceFormat::setDefaultFormat(surface);
#endif
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("MediaBridge"));
    app.setApplicationName(QStringLiteral("MediaBridge"));
    app.setApplicationVersion(QStringLiteral("2.0.3"));
#ifdef Q_OS_WIN
    int fontId = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"));
    app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).value(0, QStringLiteral("Microsoft YaHei UI"))));
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    Controller controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("converter"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFontFamily"), app.font().family());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("MediaBridge"), QStringLiteral("Main"));
    const auto args = app.arguments();
    if (args.contains(QStringLiteral("--compact")) && !engine.rootObjects().isEmpty()) {
        if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front())) window->resize(390, 844);
    }
    int size = args.indexOf(QStringLiteral("--size"));
    if (size >= 0 && size + 1 < args.size()) {
        const auto dimensions = args[size + 1].split('x');
        if (dimensions.size() == 2 && dimensions[0].toInt() >= 280 && dimensions[1].toInt() >= 260)
            if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) window->resize(dimensions[0].toInt(), dimensions[1].toInt());
    }
    int view = args.indexOf(QStringLiteral("--page"));
    if (view >= 0 && view + 1 < args.size() && !engine.rootObjects().isEmpty()) engine.rootObjects().front()->setProperty("route", args[view + 1].toInt());
    int section = args.indexOf(QStringLiteral("--section"));
    if (section >= 0 && section + 1 < args.size() && !engine.rootObjects().isEmpty()) engine.rootObjects().front()->setProperty("processingTab", args[section + 1].toInt());
#ifdef MB_UI_CHECKS
    int ui = args.indexOf(QStringLiteral("--ui-test"));
    if (ui >= 0 && ui + 1 < args.size()) QTimer::singleShot(400, &app, [&] { runUiChecks(app, engine.rootObjects().value(0), controller, args[ui + 1]); });
    int audioUi = args.indexOf(QStringLiteral("--ui-test-audio"));
    if (audioUi >= 0 && audioUi + 1 < args.size()) QTimer::singleShot(400, &app, [&] { runAudioUiChecks(app, engine.rootObjects().value(0), controller, args[audioUi + 1]); });
    int layoutUi = args.indexOf(QStringLiteral("--ui-test-layout"));
    if (layoutUi >= 0 && layoutUi + 1 < args.size()) QTimer::singleShot(400, &app, [&] { runLayoutUiChecks(app, engine.rootObjects().value(0), controller, args[layoutUi + 1], layoutUi + 2 < args.size() ? args[layoutUi + 2] : QString()); });
#endif
    int screenshot = args.indexOf(QStringLiteral("--screenshot"));
    if (screenshot >= 0 && screenshot + 1 < args.size()) {
        QTimer::singleShot(1000, &app, [&] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            bool ok = window && window->grabWindow().save(args[screenshot + 1]);
            app.exit(ok ? 0 : 1);
        });
    }
    int test = args.indexOf(QStringLiteral("--self-test"));
    if (test >= 0 && test + 2 < args.size()) {
        QObject::connect(&controller, &Controller::finished, &app, [&app](bool success) { app.exit(success ? 0 : 1); });
        QTimer::singleShot(200, &controller, [&] {
            controller.start(QUrl::fromLocalFile(args[test + 1]), QUrl::fromLocalFile(args[test + 2]),
                             QStringLiteral("mp4"), false, 0, 0, 0, 2500, 192, false);
        });
    }
    return app.exec();
}
