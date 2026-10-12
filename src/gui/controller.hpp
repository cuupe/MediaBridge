#pragma once
#include <QObject>
#include <QAbstractListModel>
#include <QUrl>
#include <QVariantMap>
#include <QSettings>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <map>
#include <vector>
#include <functional>

struct Task {
    QString id, name, input, output, result, state = "queued", details, message;
    QVariantMap profile, media;
    double progress = 0;
    bool selected = true;
};
class TaskModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { Id = Qt::UserRole + 1, Name, Input, Output, State, StateText, Progress, Selected, Details, Message, Summary, Result, SourceSummary };
    explicit TaskModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(tasks.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int find(const QString& id) const;
    void update(int row);
    void append(Task task);
    void remove(int row);
    void move(int row, int direction);
    std::vector<Task> tasks;
};
class Controller final : public QObject {
    Q_OBJECT
    Q_PROPERTY(TaskModel* tasks READ tasks CONSTANT)
    Q_PROPERTY(QVariantList formats READ formats CONSTANT)
    Q_PROPERTY(QVariantList videoEncoders READ videoEncoders NOTIFY changed)
    Q_PROPERTY(QVariantList audioEncoders READ audioEncoders NOTIFY changed)
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
    Q_PROPERTY(int waitingCount READ waitingCount NOTIFY changed)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString logText READ logText NOTIFY changed)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool android READ android CONSTANT)
public:
    explicit Controller(QObject* parent = nullptr);
    ~Controller() override;
    TaskModel* tasks() { return &model_; }
    QVariantList formats() const;
    QVariantList videoEncoders() const;
    QVariantList audioEncoders() const;
    QVariantMap profile() const { return profile_; }
    QVariantMap settings() const { return settings_; }
    QVariantList profiles() const;
    bool busy() const { return !jobs_.empty(); }
    bool paused() const { return paused_; }
    int count() const { return model_.rowCount(); }
    int activeCount() const { return int(jobs_.size()); }
    int waitingCount() const;
    int completedCount() const;
    int selectedCount() const;
    double progress() const;
    QString status() const { return status_; }
    QString logText() const { return logs_.join('\n'); }
    QString dataDirectory() const { return root_; }
    QString version() const;
    bool android() const;
    Q_INVOKABLE void addFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void addFolder(const QUrl& url);
    Q_INVOKABLE void setSelected(const QString& id, bool value);
    Q_INVOKABLE void selectAll(bool value);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void retrySelected();
    Q_INVOKABLE void moveTask(const QString& id, int direction);
    Q_INVOKABLE void startQueue();
    Q_INVOKABLE void pauseQueue();
    Q_INVOKABLE void cancelTask(const QString& id);
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE void inspectTask(const QString& id);
    Q_INVOKABLE void setProfileValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void applyProfile();
    Q_INVOKABLE void loadTaskProfile(const QString& id);
    Q_INVOKABLE void resetProfile();
    Q_INVOKABLE void useProfile(const QString& name);
    Q_INVOKABLE void saveProfile(const QString& name);
    Q_INVOKABLE void deleteProfile(const QString& name);
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE void resetSettings();
    Q_INVOKABLE void exportReport(const QUrl& destination);
    Q_INVOKABLE void exportProfile(const QUrl& destination);
    Q_INVOKABLE void importProfile(const QUrl& source);
    Q_INVOKABLE void exportResult(const QString& id, const QUrl& destination);
    Q_INVOKABLE void openResult(const QString& id);
    Q_INVOKABLE QString displayName(const QUrl& url) const;
    Q_INVOKABLE QString taskExtension(const QString& id) const;
    Q_INVOKABLE void start(const QUrl& input, const QUrl& output, const QString& format,
                           bool remux, double start, double duration, int width,
                           int videoKbps, int audioKbps, bool overwrite);
signals:
    void changed();
    void finished(bool success);
private:
    struct Job { std::atomic_bool cancel{false}; std::thread thread; };
    void schedule();
    int syncSelectedProfiles();
    bool adaptRemuxMode(Task& task);
    void launch(const QString& id, bool inspect = false);
    void post(std::function<void()> fn);
    void complete(const QString& id, const QString& state, const QString& message);
    void note(const QString& text);
    void saveQueue();
    void restoreQueue();
    void saveSettings();
    QString chooseOutput(Task& task);
    QString materialize(const QUrl& input, const QString& directory, Job& job);
    TaskModel model_;
    QVariantMap profile_, settings_;
    QMap<QString, QVariantMap> customProfiles_;
    QString root_, status_ = QStringLiteral("添加文件以建立批量队列");
    QStringList logs_;
    std::unique_ptr<QSettings> store_;
    std::map<QString, std::unique_ptr<Job>> jobs_;
    std::mutex exportMutex_;
    bool running_ = false, paused_ = false, hadFailure_ = false, closing_ = false;
};
