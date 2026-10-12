#include "platform_io.hpp"
#include "mediabridge/media.hpp"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QMap>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <unistd.h>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QtCore/qcoreapplication_platform.h>
#endif
namespace {
void fail(const QString& text) { throw mediabridge::Error(text.toUtf8().toStdString()); }
QString path(const QUrl& url) {
    if (url.isLocalFile()) return url.toLocalFile();
    if (url.scheme() == "content") return url.toString(QUrl::FullyEncoded);
    fail(QStringLiteral("仅支持本地文件与 Android 系统文件位置")); return {};
}
#ifdef Q_OS_ANDROID
QJniObject resolver() {
    QJniObject context(QNativeInterface::QAndroidApplication::context());
    return context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
}
QJniObject uri(const QUrl& url) {
    return QJniObject::callStaticObjectMethod("android/net/Uri", "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;", QJniObject::fromString(url.toString(QUrl::FullyEncoded)).object());
}
bool clearException() {
    QJniEnvironment env;
    if (env->ExceptionCheck()) { env->ExceptionClear(); return true; }
    return false;
}
int openDescriptor(const QUrl& url) {
    auto handle = resolver().callObjectMethod("openFileDescriptor",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;",
        uri(url).object(), QJniObject::fromString("wt").object());
    if (clearException() || !handle.isValid()) fail(QStringLiteral("无法打开系统保存位置"));
    int fd = handle.callMethod<jint>("detachFd", "()I");
    if (clearException() || fd < 0) fail(QStringLiteral("无法取得输出文件句柄"));
    return fd;
}
#endif
}
namespace platformio {
QString inputName(const QUrl& url) {
    if (url.isLocalFile()) return QFileInfo(url.toLocalFile()).fileName();
#ifdef Q_OS_ANDROID
    auto cursor = resolver().callObjectMethod("query",
        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
        uri(url).object(), jobject(nullptr), jobject(nullptr), jobject(nullptr), jobject(nullptr));
    if (!clearException() && cursor.isValid()) {
        QString result;
        if (cursor.callMethod<jboolean>("moveToFirst", "()Z")) {
            int column = cursor.callMethod<jint>("getColumnIndex", "(Ljava/lang/String;)I",
                QJniObject::fromString("_display_name").object());
            if (column >= 0) result = cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", column).toString();
        }
        cursor.callMethod<void>("close", "()V"); clearException();
        if (!result.isEmpty()) return result;
    }
#endif
    return url.fileName().isEmpty() ? QStringLiteral("media") : url.fileName();
}
void rememberAccess(const QUrl& url) {
#ifdef Q_OS_ANDROID
    if (url.scheme() == "content") {
        resolver().callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", uri(url).object(), jint(3));
        if(clearException()){
            resolver().callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", uri(url).object(), jint(1));
            clearException(); // Some providers only grant temporary access.
        }
    }
#else
    Q_UNUSED(url);
#endif
}
OutputDocument createInFolder(const QUrl& folder, const QString& name, const QString& mime, const QString& conflict) {
#ifdef Q_OS_ANDROID
    if (folder.scheme() == "content") {
        auto tree = uri(folder);
        auto id = QJniObject::callStaticObjectMethod("android/provider/DocumentsContract", "getTreeDocumentId",
            "(Landroid/net/Uri;)Ljava/lang/String;", tree.object());
        auto parent = QJniObject::callStaticObjectMethod("android/provider/DocumentsContract", "buildDocumentUriUsingTree",
            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/net/Uri;", tree.object(), id.object());
        auto children = QJniObject::callStaticObjectMethod("android/provider/DocumentsContract", "buildChildDocumentsUriUsingTree",
            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/net/Uri;", tree.object(), id.object());
        auto cursor = resolver().callObjectMethod("query",
            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
            children.object(), jobject(nullptr), jobject(nullptr), jobject(nullptr), jobject(nullptr));
        if (clearException() || !cursor.isValid()) fail(QStringLiteral("无法读取输出目录，现有文件未修改"));
        QMap<QString,QString> existing;
        const int nameColumn = cursor.callMethod<jint>("getColumnIndex", "(Ljava/lang/String;)I", QJniObject::fromString("_display_name").object());
        const int idColumn = cursor.callMethod<jint>("getColumnIndex", "(Ljava/lang/String;)I", QJniObject::fromString("document_id").object());
        if (nameColumn >= 0 && idColumn >= 0) while (cursor.callMethod<jboolean>("moveToNext", "()Z"))
            existing.insert(cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", nameColumn).toString(),
                            cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", idColumn).toString());
        cursor.callMethod<void>("close", "()V");
        if (clearException()) fail(QStringLiteral("读取输出目录失败，现有文件未修改"));
        if (existing.contains(name) && conflict != "rename") {
            auto document = QJniObject::callStaticObjectMethod("android/provider/DocumentsContract", "buildDocumentUriUsingTree",
                "(Landroid/net/Uri;Ljava/lang/String;)Landroid/net/Uri;", tree.object(), QJniObject::fromString(existing.value(name)).object());
            if (clearException() || !document.isValid()) fail(QStringLiteral("无法访问已有输出文件"));
            return {QUrl(document.callObjectMethod("toString", "()Ljava/lang/String;").toString()), conflict == "skip"};
        }
        QString chosen = name;
        for (int suffix = 1; existing.contains(chosen); ++suffix) {
            QFileInfo info(name);
            chosen = info.completeBaseName() + QStringLiteral(" (%1)").arg(suffix) + (info.suffix().isEmpty() ? QString() : '.' + info.suffix());
        }
        auto child = QJniObject::callStaticObjectMethod("android/provider/DocumentsContract", "createDocument",
            "(Landroid/content/ContentResolver;Landroid/net/Uri;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;",
            resolver().object(), parent.object(), QJniObject::fromString(mime).object(), QJniObject::fromString(chosen).object());
        if (clearException() || !child.isValid()) fail(QStringLiteral("无法在选定系统目录中创建输出文件"));
        return {QUrl(child.callObjectMethod("toString", "()Ljava/lang/String;").toString()), false};
    }
#else
    Q_UNUSED(mime); Q_UNUSED(conflict);
#endif
    if (!folder.isLocalFile()) fail(QStringLiteral("请选择本地输出目录"));
    return {QUrl::fromLocalFile(folder.toLocalFile() + '/' + name), false};
}
void copy(QIODevice& source, QIODevice& destination, const std::atomic_bool* cancel) {
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (;;) {
        if (cancel && *cancel) throw mediabridge::Cancelled();
        qint64 count = source.read(buffer.data(), buffer.size());
        if (count < 0) fail(source.errorString());
        if (!count) break;
        qint64 offset = 0;
        while (offset < count) {
            qint64 written = destination.write(buffer.constData() + offset, count - offset);
            if (written <= 0) fail(destination.errorString());
            offset += written;
        }
    }
}
void exportFile(const QString& sourcePath, const QUrl& destination, bool overwrite) {
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) fail(source.errorString());
    if (destination.isLocalFile()) {
        if (!overwrite && QFile::exists(destination.toLocalFile())) fail(QStringLiteral("输出文件已存在"));
        // QSaveFile provides atomic replacement; new-only exports use QFile's no-clobber copy.
        if (!overwrite) {
            source.close();
            if (!QFile::copy(sourcePath, destination.toLocalFile())) fail(QStringLiteral("无法保存结果，文件可能已存在"));
            return;
        }
        QSaveFile target(destination.toLocalFile());
        if (!target.open(QIODevice::WriteOnly)) fail(target.errorString());
        copy(source, target);
        if (!target.commit()) fail(target.errorString());
        return;
    }
    QFile target;
#ifdef Q_OS_ANDROID
    int descriptor = openDescriptor(destination);
    if (!target.open(descriptor, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor); fail(target.errorString());
    }
#else
    target.setFileName(path(destination));
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail(target.errorString());
#endif
    // A provider may not support atomic rename. Retain the local result until flush succeeds.
    copy(source, target);
    if (!target.flush()) fail(target.errorString());
}
void writeDocument(const QUrl& destination, const QByteArray& bytes) {
    if (destination.isLocalFile()) {
        QSaveFile target(destination.toLocalFile());
        if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit())
            fail(target.errorString());
        return;
    }
    QFile target;
#ifdef Q_OS_ANDROID
    int descriptor = openDescriptor(destination);
    if (!target.open(descriptor, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor); fail(target.errorString());
    }
#else
    target.setFileName(path(destination));
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) fail(target.errorString());
#endif
    if (target.write(bytes) != bytes.size() || !target.flush()) fail(target.errorString());
}
}
