#pragma once
#include <QString>
class QGuiApplication;
class QObject;
class Controller;
void runUiChecks(QGuiApplication& app, QObject* root, Controller& controller, const QString& fixture);
void runAudioUiChecks(QGuiApplication& app, QObject* root, Controller& controller, const QString& fixture);
void runLayoutUiChecks(QGuiApplication& app, QObject* root, Controller& controller, const QString& fixture, const QString& screenshotDirectory);
