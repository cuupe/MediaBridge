#pragma once
#include <QUrl>
#include <QString>
#include <QIODevice>
#include <atomic>
namespace platformio {
QString inputName(const QUrl& url);
void rememberAccess(const QUrl& url);
struct OutputDocument { QUrl url; bool skipped = false; };
OutputDocument createInFolder(const QUrl& folder, const QString& name, const QString& mime, const QString& conflict = "rename");
void copy(QIODevice& source, QIODevice& destination, const std::atomic_bool* cancel = nullptr);
void exportFile(const QString& source, const QUrl& destination, bool overwrite = false);
void writeDocument(const QUrl& destination, const QByteArray& bytes);
}
