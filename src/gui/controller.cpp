#include "controller.hpp"
#include "platform_io.hpp"
#include "mediabridge/media.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QDateTime>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QTimer>
#include <QMimeDatabase>
#include <algorithm>
namespace {
std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }
QString qt(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }
void fail(const QString& s) { throw mediabridge::Error(utf8(s)); }
QVariantMap defaultProfile() {
    return {{"format","mp4"},{"mode","transcode"},{"videoEncoder",""},{"audioEncoder",""},{"encoderPreset","fast"},
        {"start",0.0},{"duration",0.0},{"width",0},{"height",0},{"fit","fit"},{"fps",0.0},{"speed",1.0},
        {"videoKbps",2500},{"audioKbps",192},{"crf",-1},{"threads",2},{"gop",48},
        {"rotation",0},{"autoRotate",true},{"cropX",0},{"cropY",0},{"cropWidth",0},{"cropHeight",0},
        {"flipHorizontal",false},{"flipVertical",false},{"deinterlace",false},
        {"brightness",0.0},{"contrast",1.0},{"saturation",1.0},{"sampleRate",0},{"channels",2},
        {"videoStream",-1},{"audioStream",-1},{"volumeDb",0.0},{"normalizeAudio",false},{"loudness",-16.0},
        {"fadeIn",0.0},{"fadeOut",0.0},{"disableVideo",false},{"disableAudio",false},
        {"copyMetadata",true},{"copySubtitles",false},{"title",""},{"artist",""}};
}
QVariantMap defaultSettings() {
    return {{"outputDirectory",""},{"namingTemplate","{name}_converted"},{"conflict","rename"},
        {"parallel",2},{"threads",2},{"recursive",false},{"restoreQueue",true},{"theme","dark"}};
}
QVariantMap mergeProfile(const QVariantMap& source) {
    auto result=defaultProfile();
    for(auto it=source.begin();it!=source.end();++it)if(result.contains(it.key()))result[it.key()]=it.value();
    return result;
}
mediabridge::Options optionsFrom(const QVariantMap& p) {
    mediabridge::Options o;
    o.preset=utf8(p.value("format").toString());
    o.mode=p.value("mode")=="remux"?mediabridge::Mode::Remux:mediabridge::Mode::Transcode;
#define MB_STRING(k) o.k=utf8(p.value(#k).toString())
#define MB_INT(k) o.k=p.value(#k).toInt()
#define MB_DOUBLE(k) o.k=p.value(#k).toDouble()
#define MB_BOOL(k) o.k=p.value(#k).toBool()
    MB_STRING(videoEncoder);MB_STRING(audioEncoder);MB_STRING(encoderPreset);MB_STRING(fit);MB_STRING(title);MB_STRING(artist);
    MB_DOUBLE(start);MB_DOUBLE(duration);MB_DOUBLE(fps);MB_DOUBLE(speed);MB_DOUBLE(brightness);MB_DOUBLE(contrast);
    MB_DOUBLE(saturation);MB_DOUBLE(volumeDb);MB_DOUBLE(loudness);MB_DOUBLE(fadeIn);MB_DOUBLE(fadeOut);
    MB_INT(width);MB_INT(height);MB_INT(videoKbps);MB_INT(audioKbps);MB_INT(crf);MB_INT(threads);MB_INT(gop);
    MB_INT(rotation);MB_INT(cropX);MB_INT(cropY);MB_INT(cropWidth);MB_INT(cropHeight);MB_INT(sampleRate);
    MB_INT(channels);MB_INT(videoStream);MB_INT(audioStream);
    MB_BOOL(flipHorizontal);MB_BOOL(flipVertical);MB_BOOL(deinterlace);MB_BOOL(autoRotate);
    MB_BOOL(normalizeAudio);MB_BOOL(disableVideo);MB_BOOL(disableAudio);MB_BOOL(copyMetadata);MB_BOOL(copySubtitles);
#undef MB_STRING
#undef MB_INT
#undef MB_DOUBLE
#undef MB_BOOL
    return o;
}
QString stateText(const QString& s) {
    static const QMap<QString,QString> labels{{"queued","等待"},{"running","处理中"},{"inspecting","分析中"},
        {"done","已完成"},{"failed","失败"},{"cancelled","已取消"},{"skipped","已跳过"},{"pending","待保存"}};
    return labels.value(s,s);
}
void useTranscode(QVariantMap& profile) {
    profile["mode"]="transcode";profile["copySubtitles"]=false;
}
QString transcodeNotice(const QVariantMap& profile) {
    return QStringLiteral("原编码无法直接封装为 %1，已自动切换为转码").arg(profile.value("format").toString().toUpper());
}
bool needsTranscode(const QVariantMap& media,const QVariantMap& profile) {
    if(profile.value("mode")!="remux"||media.isEmpty())return false;
    mediabridge::MediaInfo info;
    for(const auto& value:media.value("streams").toList()) {
        const auto stream=value.toMap();
        info.streams.push_back({stream.value("index").toInt(),utf8(stream.value("type").toString()),utf8(stream.value("codec").toString())});
    }
    // Queues saved by earlier versions only recorded the primary audio/video codec.
    if(info.streams.empty()) {
        if(profile.value("videoStream").toInt()>=0||profile.value("audioStream").toInt()>=0)return false;
        for(const auto& type:QStringList{"video","audio"}) {
            const auto codec=media.value(type+"Codec").toString();
            if(!codec.isEmpty())info.streams.push_back({int(info.streams.size()),utf8(type),utf8(codec)});
        }
    }
    try{return !info.streams.empty()&&!mediabridge::remuxCompatible(info,optionsFrom(profile));}
    catch(const mediabridge::Error&){return false;} // Let the worker report an invalid restored preset as a task failure.
}
bool editable(const Task& t){return t.state=="queued"||t.state=="failed"||t.state=="cancelled"||t.state=="skipped"||t.state=="inspecting";}
QString summary(const QVariantMap& p) {
    QString s=p.value("format").toString().toUpper()+" · "+(p.value("mode")=="remux"?QStringLiteral("无损封装"):QStringLiteral("转码"));
    if(p.value("width").toInt())s+=QStringLiteral(" · %1 px").arg(p.value("width").toInt());
    if(p.value("speed").toDouble()!=1)s+=QStringLiteral(" · %1×").arg(p.value("speed").toDouble());
    return s;
}
QString safeName(QString name) {
    name.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"),"_");
    name=name.trimmed();
    while(name.endsWith('.')||name.endsWith(' '))name.chop(1);
    name=name.left(160);
    if(name.isEmpty()||name=="."||name=="..")name="media";
    if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)",QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())name.prepend('_');
    return name;
}
QString outputName(const Task& t,const QVariantMap& settings,int index) {
    auto ext=qt(mediabridge::preset(utf8(t.profile.value("format").toString())).extension);
    auto name=settings.value("namingTemplate").toString();
    name.replace("{name}",QFileInfo(t.name).completeBaseName()).replace("{index}",QString::number(index+1).rightJustified(3,'0')).replace("{format}",ext);
    return safeName(name)+'.'+ext;
}
bool mediaFile(const QString& name) {
    static const QStringList extensions{"mp4","mkv","mov","avi","webm","mpeg","mpg","m4v","3gp","mts","m2ts","ts",
        "mp3","m4a","aac","wav","flac","ogg","opus","wma","aiff","aif","mka","vob"};
    if(extensions.contains(QFileInfo(name).suffix().toLower()))return true;
    const auto mime=QMimeDatabase().mimeTypeForFile(name,QMimeDatabase::MatchContent).name();
    return mime.startsWith("audio/")||mime.startsWith("video/")||mime=="application/ogg";
}
QVariantMap taskJson(const Task& t) {
    return {{"id",t.id},{"name",t.name},{"input",t.input},{"output",t.output},{"result",t.result},{"state",t.state},
        {"details",t.details},{"message",t.message},{"media",t.media},{"profile",t.profile},{"progress",t.progress},{"selected",t.selected}};
}
bool samePath(const QString& a,const QString& b){
#ifdef Q_OS_WIN
    return a.compare(b,Qt::CaseInsensitive)==0;
#else
    return a==b;
#endif
}
bool ownedResult(const QString& path,const QString& root){
    QFileInfo info(path);
    return info.isFile()&&!info.isSymLink()&&samePath(info.canonicalPath(),QFileInfo(root+"/pending").canonicalFilePath());
}
QString mimeType(const QString& ext) {
    if(ext=="mp4"||ext=="m4a")return ext=="m4a"?"audio/mp4":"video/mp4";
    if(ext=="mp3")return "audio/mpeg";
    if(ext=="wav")return "audio/wav";
    if(ext=="flac")return "audio/flac";
    if(ext=="ogg"||ext=="opus")return "audio/ogg";
    return "video/"+ext;
}
}
QVariant TaskModel::data(const QModelIndex& index,int role) const {
    if(!index.isValid()||index.row()<0||index.row()>=rowCount())return {};
    const auto&t=tasks[size_t(index.row())];
    switch(role) {
    case Id:return t.id;case Name:return t.name;case Input:return t.input;case Output:return t.output;
    case State:return t.state;case StateText:return stateText(t.state);case Progress:return t.progress;
    case Selected:return t.selected;case Details:return t.details;case Message:return t.message;
    case Summary:return summary(t.profile);case Result:return t.result;case SourceSummary:return t.media.value("summary");default:return {};
    }
}
QHash<int,QByteArray> TaskModel::roleNames() const {
    return {{Id,"taskId"},{Name,"fileName"},{Input,"inputUrl"},{Output,"outputUrl"},{State,"taskState"},{StateText,"stateText"},
        {Progress,"taskProgress"},{Selected,"taskSelected"},{Details,"taskDetails"},{Message,"taskMessage"},{Summary,"taskSummary"},{Result,"localResult"},{SourceSummary,"sourceSummary"}};
}
int TaskModel::find(const QString& id) const{for(int i=0;i<rowCount();++i)if(tasks[size_t(i)].id==id)return i;return -1;}
void TaskModel::update(int row){if(row>=0)emit dataChanged(index(row),index(row));}
void TaskModel::append(Task task){int n=rowCount();beginInsertRows({},n,n);tasks.push_back(std::move(task));endInsertRows();}
void TaskModel::remove(int row){beginRemoveRows({},row,row);tasks.erase(tasks.begin()+row);endRemoveRows();}
void TaskModel::move(int row,int direction){
    int other=row+(direction<0?-1:1);if(other<0||other>=rowCount())return;
    beginMoveRows({},row,row,{},direction<0?other:other+1);std::swap(tasks[size_t(row)],tasks[size_t(other)]);endMoveRows();
}
Controller::Controller(QObject* parent):QObject(parent),model_(this) {
    root_=qEnvironmentVariable("MEDIABRIDGE_DATA_DIR");
    if(root_.isEmpty())root_=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(root_.isEmpty()||!QDir().mkpath(root_+"/cache")||!QDir().mkpath(root_+"/pending"))fail(QStringLiteral("无法创建应用数据目录"));
    store_=std::make_unique<QSettings>(root_+"/settings.ini",QSettings::IniFormat);
    settings_=defaultSettings();
    for(auto it=settings_.begin();it!=settings_.end();++it)it.value()=store_->value("settings/"+it.key(),it.value());
    settings_["parallel"]=std::clamp(settings_.value("parallel").toInt(),1,4);
    settings_["threads"]=std::clamp(settings_.value("threads").toInt(),1,32);
    profile_=mergeProfile(QJsonDocument::fromJson(store_->value("profile").toByteArray()).object().toVariantMap());
    if(!store_->contains("profile"))profile_["threads"]=settings_.value("threads");
    auto saved=QJsonDocument::fromJson(store_->value("customProfiles").toByteArray()).object();
    for(auto it=saved.begin();it!=saved.end();++it)if(it.value().isObject())customProfiles_[it.key()]=mergeProfile(it.value().toObject().toVariantMap());
    restoreQueue();
    QTimer::singleShot(0,this,[this]{schedule();});
}
Controller::~Controller(){
    closing_=true;for(auto&entry:jobs_)entry.second->cancel=true;
    for(auto&entry:jobs_)if(entry.second->thread.joinable())entry.second->thread.join();
    saveQueue();
}
QString Controller::version() const{return QStringLiteral("MediaBridge 2.0.3 · FFmpeg");}
bool Controller::android() const{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}
QVariantList Controller::formats() const{
    QVariantList values;for(const auto&p:mediabridge::presets()){
        bool ready=mediabridge::encoderAvailable(p.videoEncoder)&&mediabridge::encoderAvailable(p.audioEncoder);
        values.append(QVariantMap{{"id",qt(p.id)},{"label",qt(p.label)+(ready?QString():QStringLiteral(" · 不可用"))},{"extension",qt(p.extension)},{"available",ready}});
    }return values;
}
QVariantList Controller::videoEncoders() const{
    QVariantList values{QVariantMap{{"id",""},{"label",QStringLiteral("格式默认")}}};
    for(const auto&e:mediabridge::encoders())if(e.type=="video"&&mediabridge::encoderCompatible(utf8(profile_.value("format").toString()),e.name))values.append(QVariantMap{{"id",qt(e.name)},{"label",qt(e.name)}});
    return values;
}
QVariantList Controller::audioEncoders() const{
    QVariantList values{QVariantMap{{"id",""},{"label",QStringLiteral("格式默认")}}};
    for(const auto&e:mediabridge::encoders())if(e.type=="audio"&&mediabridge::encoderCompatible(utf8(profile_.value("format").toString()),e.name))values.append(QVariantMap{{"id",qt(e.name)},{"label",qt(e.name)}});
    return values;
}
QVariantList Controller::profiles() const{
    QVariantList values{"标准转换","手机 1080p","节省空间","提取 MP3","无损换封装"};
    for(auto it=customProfiles_.begin();it!=customProfiles_.end();++it)values.append(it.key());return values;
}
int Controller::waitingCount() const{return int(std::count_if(model_.tasks.begin(),model_.tasks.end(),[](const Task&t){return t.state=="queued";}));}
int Controller::completedCount() const{return int(std::count_if(model_.tasks.begin(),model_.tasks.end(),[](const Task&t){return t.state=="done"||t.state=="pending";}));}
int Controller::selectedCount() const{return int(std::count_if(model_.tasks.begin(),model_.tasks.end(),[](const Task&t){return t.selected;}));}
double Controller::progress() const{
    if(!count())return 0;double sum=0;for(const auto&t:model_.tasks){
        if(t.state=="done"||t.state=="pending"||t.state=="skipped"||t.state=="failed"||t.state=="cancelled")sum+=1;else sum+=t.progress;
    }return sum/count();
}
QString Controller::displayName(const QUrl& url) const {
    if(url.isEmpty())return android()?QStringLiteral("应用内"):QStringLiteral("原文件目录");
    if(url.isLocalFile())return QDir::toNativeSeparators(url.toLocalFile());
    if(url.scheme()=="content") {
        const auto name=url.path(QUrl::FullyDecoded).section('/',-1).section(':',-1);
        return name.isEmpty()?QStringLiteral("系统目录"):name;
    }
    return url.toDisplayString();
}
QString Controller::taskExtension(const QString& id) const{
    int row=model_.find(id);if(row<0)return "mp4";
    try{return qt(mediabridge::preset(utf8(model_.tasks[size_t(row)].profile.value("format").toString())).extension);}catch(...){return "mp4";}
}
void Controller::note(const QString& text){
    status_=text;logs_.append(QDateTime::currentDateTime().toString("HH:mm:ss")+"  "+text);
    while(logs_.size()>500)logs_.removeFirst();emit changed();
}
void Controller::post(std::function<void()> fn){QMetaObject::invokeMethod(this,std::move(fn),Qt::QueuedConnection);}
void Controller::saveSettings(){
    for(auto it=settings_.begin();it!=settings_.end();++it)store_->setValue("settings/"+it.key(),it.value());
    store_->setValue("profile",QJsonDocument(QJsonObject::fromVariantMap(profile_)).toJson(QJsonDocument::Compact));
    QJsonObject saved;for(auto it=customProfiles_.begin();it!=customProfiles_.end();++it)saved[it.key()]=QJsonObject::fromVariantMap(it.value());
    store_->setValue("customProfiles",QJsonDocument(saved).toJson(QJsonDocument::Compact));store_->sync();
    if(store_->status()!=QSettings::NoError)note(QStringLiteral("设置保存失败，请检查数据目录权限"));
}
void Controller::saveQueue(){
    QJsonArray entries;for(const auto&t:model_.tasks)entries.append(QJsonObject::fromVariantMap(taskJson(t)));
    QSaveFile file(root_+"/queue.json");auto bytes=QJsonDocument(QJsonObject{{"version",2},{"tasks",entries}}).toJson();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())if(!closing_)note(QStringLiteral("队列保存失败：")+file.errorString());
}
void Controller::restoreQueue(){
    QFile file(root_+"/queue.json");
    if(file.open(QIODevice::ReadOnly)){
        QJsonParseError error;auto doc=QJsonDocument::fromJson(file.readAll(),&error);
        if(error.error!=QJsonParseError::NoError){
            file.close();QString backup=root_+"/queue.corrupt-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz")+".json";
            bool saved=QFile::rename(root_+"/queue.json",backup);
            note(saved?QStringLiteral("队列文件损坏，原文件已备份：")+backup:QStringLiteral("队列文件损坏，无法建立备份"));
        }
        auto entries=doc.object().value("tasks").toArray();
        for(const auto&value:entries){
            if(model_.rowCount()>=10000)break;auto p=value.toObject();Task t;
            t.id=p.value("id").toString();t.name=p.value("name").toString();t.input=p.value("input").toString();
            if(t.id.isEmpty()||model_.find(t.id)>=0)continue;
            t.output=p.value("output").toString();t.result=p.value("result").toString();t.state=p.value("state").toString("queued");
            t.media=p.value("media").toObject().toVariantMap();t.details=p.value("details").toString();t.message=p.value("message").toString();t.progress=p.value("progress").toDouble();
            t.profile=mergeProfile(p.value("profile").toObject().toVariantMap());t.selected=p.value("selected").toBool(true);
            if(t.state=="failed"&&t.profile.value("mode")=="remux"&&t.message=="Input codec is incompatible with the output container; use transcode") {
                if(t.selected&&profile_==t.profile){useTranscode(profile_);saveSettings();}
                useTranscode(t.profile);t.state="queued";t.progress=0;t.output.clear();t.message=transcodeNotice(t.profile);
            }
            if(t.state=="running"||t.state=="inspecting"){t.state="queued";t.progress=0;t.output.clear();t.message=QStringLiteral("上次退出前未完成，可重新开始");}
            if(!t.result.isEmpty()&&ownedResult(t.result,root_)){t.state="pending";t.progress=1;}
            else if(t.state=="pending"){t.state="failed";t.message=QStringLiteral("应用内结果已不存在");t.result.clear();}
            if(settings_.value("restoreQueue").toBool()||t.state=="pending")model_.append(std::move(t));
        }
    }
    for(const auto&entry:QDir(root_+"/pending").entryInfoList(QDir::Files|QDir::NoSymLinks)){
        bool found=std::any_of(model_.tasks.begin(),model_.tasks.end(),[&](const Task&t){return t.result==entry.absoluteFilePath();});
        if(!found&&mediaFile(entry.fileName())){
            Task t;t.id=QUuid::createUuid().toString(QUuid::WithoutBraces);t.name=entry.fileName();t.result=entry.absoluteFilePath();
            t.state="pending";t.progress=1;t.profile=defaultProfile();t.profile["format"]=entry.suffix();t.message=QStringLiteral("恢复的未保存结果");
            model_.append(std::move(t));
        }
    }
    if(count())note(QStringLiteral("已恢复 %1 个任务，点击开始继续").arg(count()));
}
void Controller::addFiles(const QList<QUrl>& urls){
    int added=0,duplicates=0;
    for(const auto&url:urls){
        if(count()>=10000){note(QStringLiteral("队列最多容纳 10000 个任务"));break;}
        if(url.isLocalFile()&&QFileInfo(url.toLocalFile()).isDir()){addFolder(url);continue;}
        if(!url.isLocalFile()&&url.scheme()!="content")continue;
        QString value=url.toString(QUrl::FullyEncoded);
        if(std::any_of(model_.tasks.begin(),model_.tasks.end(),[&](const Task&t){return t.input==value&&(t.state=="queued"||t.state=="running"||t.state=="inspecting");})){++duplicates;continue;}
        platformio::rememberAccess(url);
        Task t;t.id=QUuid::createUuid().toString(QUuid::WithoutBraces);t.input=value;t.name=platformio::inputName(url);t.profile=profile_;
        model_.append(std::move(t));++added;
    }
    saveQueue();note(QStringLiteral("添加 %1 个文件%2").arg(added).arg(duplicates?QStringLiteral("，忽略 %1 个重复任务").arg(duplicates):QString()));
    QTimer::singleShot(0,this,[this]{schedule();});
}
void Controller::addFolder(const QUrl& url){
    if(!url.isLocalFile()){note(QStringLiteral("Android 输入请使用多文件选择；系统目录可用于批量输出"));return;}
    QList<QUrl> files;
    QDirIterator it(url.toLocalFile(),QDir::Files|QDir::NoSymLinks,settings_.value("recursive").toBool()?QDirIterator::Subdirectories:QDirIterator::NoIteratorFlags);
    while(it.hasNext()&&files.size()+count()<10000){QString p=it.next();if(mediaFile(p))files.append(QUrl::fromLocalFile(p));}
    std::sort(files.begin(),files.end(),[](const QUrl&a,const QUrl&b){return a.toString()<b.toString();});addFiles(files);
}
void Controller::setSelected(const QString& id,bool value){int row=model_.find(id);if(row>=0){model_.tasks[size_t(row)].selected=value;model_.update(row);emit changed();}}
void Controller::selectAll(bool value){for(int i=0;i<count();++i){model_.tasks[size_t(i)].selected=value;model_.update(i);}emit changed();}
void Controller::removeSelected(){
    int removed=0;for(int i=count()-1;i>=0;--i){const auto&t=model_.tasks[size_t(i)];if(t.selected&&!jobs_.count(t.id)&&t.state!="pending"){model_.remove(i);++removed;}}
    saveQueue();note(QStringLiteral("移除 %1 个任务；正在运行和待保存结果会保留").arg(removed));
}
void Controller::clearFinished(){for(int i=count()-1;i>=0;--i)if(model_.tasks[size_t(i)].state=="done"||model_.tasks[size_t(i)].state=="skipped")model_.remove(i);saveQueue();emit changed();}
void Controller::retrySelected(){
    int n=0;for(int i=0;i<count();++i){auto&t=model_.tasks[size_t(i)];if(t.selected&&(t.state=="failed"||t.state=="cancelled"||t.state=="skipped")){t.state="queued";t.progress=0;t.message.clear();t.output.clear();model_.update(i);++n;}}
    saveQueue();note(QStringLiteral("%1 个任务已重新排队").arg(n));
}
void Controller::moveTask(const QString& id,int direction){int row=model_.find(id);if(row>=0){model_.move(row,direction);saveQueue();}}
void Controller::setProfileValue(const QString& key,const QVariant& value){
    if(!profile_.contains(key))return;
    if(key=="mode"&&value=="remux"){
        auto previous=profile_;profile_=defaultProfile();
        for(const auto& k:QStringList{"format","title","artist","copyMetadata","videoStream","audioStream","disableAudio","disableVideo"})profile_[k]=previous.value(k);
        profile_["autoRotate"]=false;
    }
    profile_[key]=value;
    if(key=="mode"&&value=="transcode")profile_["copySubtitles"]=false;
    if(key=="format"){profile_["videoEncoder"]="";profile_["audioEncoder"]="";profile_["crf"]=-1;}
    if(key=="videoEncoder"&&!QStringList{"libx264","libx265","libvpx-vp9","libaom-av1","libsvtav1"}.contains(value.toString()))profile_["crf"]=-1;
    if(key=="crf"&&value.toInt()>=0&&profile_.value("videoEncoder").toString().isEmpty()){
        profile_["crf"]=-1;
        const auto format=utf8(profile_.value("format").toString());
        for(const auto& encoder:QStringList{"libx264","libx265","libvpx-vp9","libaom-av1","libsvtav1"}){
            if(mediabridge::encoderCompatible(format,utf8(encoder))){profile_["videoEncoder"]=encoder;profile_["crf"]=value;break;}
        }
    }
    syncSelectedProfiles();saveSettings();emit changed();
}
void Controller::resetProfile(){profile_=defaultProfile();profile_["threads"]=settings_.value("threads");syncSelectedProfiles();saveSettings();emit changed();}
int Controller::syncSelectedProfiles(){
    if(profile_.value("mode")=="remux"&&std::any_of(model_.tasks.begin(),model_.tasks.end(),[this](const Task&t){return t.selected&&editable(t)&&needsTranscode(t.media,profile_);})) {
        useTranscode(profile_);note(transcodeNotice(profile_));
    }
    int n=0;
    for(int i=0;i<count();++i){
        auto&t=model_.tasks[size_t(i)];if(!t.selected||!editable(t))continue;
        t.profile=profile_;if(t.state!="inspecting")t.state="queued";
        t.progress=0;t.output.clear();t.message.clear();model_.update(i);++n;
    }
    if(n)saveQueue();return n;
}
bool Controller::adaptRemuxMode(Task& task) {
    if(!needsTranscode(task.media,task.profile))return false;
    const auto previous=task.profile;
    useTranscode(task.profile);
    if(task.selected&&profile_==previous) {
        useTranscode(profile_);syncSelectedProfiles();saveSettings();
    }
    task.message=transcodeNotice(task.profile);note(task.message);return true;
}
void Controller::applyProfile(){int n=syncSelectedProfiles();note(QStringLiteral("已更新 %1 个任务").arg(n));}
void Controller::loadTaskProfile(const QString& id){int row=model_.find(id);if(row>=0){profile_=model_.tasks[size_t(row)].profile;saveSettings();emit changed();}}
void Controller::useProfile(const QString& name){
    if(customProfiles_.contains(name))profile_=customProfiles_[name];
    else{
        profile_=defaultProfile();profile_["threads"]=settings_.value("threads");const QString h264=mediabridge::encoderAvailable("libx264")?"libx264":"";
        if(name=="手机 1080p"){profile_["width"]=1920;profile_["height"]=1080;profile_["videoEncoder"]=h264;profile_["videoKbps"]=4000;}
        else if(name=="节省空间"){profile_["width"]=1280;profile_["videoEncoder"]=h264;profile_["videoKbps"]=1000;if(!h264.isEmpty())profile_["crf"]=28;profile_["audioKbps"]=128;}
        else if(name=="提取 MP3")profile_["format"]="mp3";
        else if(name=="无损换封装"){profile_["format"]="mkv";profile_["mode"]="remux";profile_["autoRotate"]=false;profile_["copySubtitles"]=true;}
    }
    syncSelectedProfiles();saveSettings();note(QStringLiteral("方案已载入：")+name);
}
void Controller::saveProfile(const QString& name){
    QString n=name.trimmed();
    if(n.isEmpty()||n.size()>64||QStringList{"标准转换","手机 1080p","节省空间","提取 MP3","无损换封装"}.contains(n)){note(QStringLiteral("请输入 1–64 字符的自定义方案名称"));return;}
    customProfiles_[n]=profile_;saveSettings();note(QStringLiteral("已保存方案：")+n);
}
void Controller::deleteProfile(const QString& name){if(customProfiles_.remove(name)){saveSettings();note(QStringLiteral("已删除自定义方案：")+name);}}
void Controller::setSetting(const QString& key,const QVariant& value){
    if(!settings_.contains(key))return;QVariant adjusted=value;
    if(key=="parallel")adjusted=std::clamp(value.toInt(),1,4);
    if(key=="threads")adjusted=std::clamp(value.toInt(),1,32);
    if(key=="conflict"&&!QStringList{"rename","skip","overwrite"}.contains(value.toString()))return;
    if(key=="theme"&&!QStringList{"dark","light","system"}.contains(value.toString()))return;
    if(key=="namingTemplate"&&(value.toString().isEmpty()||value.toString().size()>160)){note(QStringLiteral("命名模板须为 1–160 字符"));return;}
    if(key=="outputDirectory"){QUrl url(value.toString());if(!url.isEmpty()&&!url.isLocalFile()&&url.scheme()!="content")return;platformio::rememberAccess(url);}
    settings_[key]=adjusted;if(key=="threads")profile_["threads"]=adjusted;saveSettings();emit changed();if(running_)schedule();
}
void Controller::resetSettings(){settings_=defaultSettings();saveSettings();emit changed();}
QString Controller::chooseOutput(Task& task){
    if(!task.output.isEmpty())return QUrl(task.output).isLocalFile()?QUrl(task.output).toLocalFile():task.output;
    QUrl folder(settings_.value("outputDirectory").toString());
    if((android()&&folder.isEmpty())||folder.scheme()=="content")return root_+"/pending/"+task.id+'.'+taskExtension(task.id);
    QString directory=folder.isEmpty()?QFileInfo(QUrl(task.input).toLocalFile()).absolutePath():folder.toLocalFile();
    if(!QFileInfo(directory).isDir())fail(QStringLiteral("输出目录不存在，请在设置中重新选择"));
    QString path=directory+'/'+outputName(task,settings_,model_.find(task.id));
    QString conflict=settings_.value("conflict").toString();
    auto reserved=[&](const QString& p){return std::any_of(model_.tasks.begin(),model_.tasks.end(),[&](const Task&t){return t.id!=task.id&&jobs_.count(t.id)&&samePath(QUrl(t.output).toLocalFile(),p);});};
    if(conflict=="rename"){
        auto info=QFileInfo(path);int suffix=1;
        while(QFile::exists(path)||reserved(path))path=directory+'/'+info.completeBaseName()+QStringLiteral(" (%1).").arg(suffix++)+info.suffix();
    }else if(reserved(path))fail(QStringLiteral("另一任务正在写入相同输出，请使用自动重命名"));
    return path;
}
QString Controller::materialize(const QUrl& input,const QString& directory,Job& job){
    if(input.isLocalFile())return input.toLocalFile();
    QFile source(input.toString(QUrl::FullyEncoded));if(!source.open(QIODevice::ReadOnly))fail(QStringLiteral("读取系统文件失败；恢复的任务可能需要重新选择文件：")+source.errorString());
    QString path=directory+"/input";QFile target(path);if(!target.open(QIODevice::WriteOnly|QIODevice::NewOnly))fail(target.errorString());
    platformio::copy(source,target,&job.cancel);if(!target.flush())fail(target.errorString());return path;
}
void Controller::startQueue(){running_=true;paused_=false;hadFailure_=false;note(QStringLiteral("正在处理批量队列"));schedule();}
void Controller::pauseQueue(){paused_=!paused_;note(paused_?QStringLiteral("已暂停排队，正在运行的任务会继续完成"):QStringLiteral("继续处理队列"));if(!paused_)schedule();}
void Controller::cancelTask(const QString& id){
    auto it=jobs_.find(id);if(it!=jobs_.end()){it->second->cancel=true;note(QStringLiteral("正在取消任务"));return;}
    int row=model_.find(id);if(row>=0&&model_.tasks[size_t(row)].state=="queued"){auto&t=model_.tasks[size_t(row)];t.state="cancelled";t.message=QStringLiteral("用户取消");model_.update(row);saveQueue();emit changed();}
}
void Controller::cancelAll(){running_=false;paused_=false;for(auto&entry:jobs_)entry.second->cancel=true;for(int i=0;i<count();++i)if(model_.tasks[size_t(i)].state=="queued"){model_.tasks[size_t(i)].state="cancelled";model_.update(i);}saveQueue();note(QStringLiteral("已取消排队任务，正在停止活动任务"));}
void Controller::inspectTask(const QString& id){if(jobs_.count(id)||activeCount()>=settings_.value("parallel").toInt())return;int row=model_.find(id);if(row>=0&&editable(model_.tasks[size_t(row)]))launch(id,true);}
void Controller::schedule(){
    if(closing_)return;
    if(!running_){
        for(int i=0;i<count()&&activeCount()<settings_.value("parallel").toInt();++i){
            auto&t=model_.tasks[size_t(i)];
            if(t.state=="queued"&&t.media.isEmpty()&&!jobs_.count(t.id))launch(t.id,true);
        }
        return;
    }
    if(paused_)return;
    for(int i=0;i<count()&&activeCount()<settings_.value("parallel").toInt();++i)if(model_.tasks[size_t(i)].state=="queued"&&!jobs_.count(model_.tasks[size_t(i)].id))launch(model_.tasks[size_t(i)].id);
    if(!busy()&&!waitingCount()){running_=false;note(QStringLiteral("批量处理结束 · 完成 %1 / %2").arg(completedCount()).arg(count()));emit finished(!hadFailure_);}
}
void Controller::launch(const QString& id,bool inspect){
    int row=model_.find(id);if(row<0)return;auto&t=model_.tasks[size_t(row)];
    try{
        if(!inspect){
            adaptRemuxMode(t);
            QString target=chooseOutput(t);t.output=QUrl::fromLocalFile(target).toString();
            if(settings_.value("conflict")=="skip"&&QFile::exists(target)){t.state="skipped";t.message=QStringLiteral("输出文件已存在");t.progress=1;model_.update(row);saveQueue();emit changed();return;}
        }
    }catch(const std::exception&e){t.state="failed";t.message=qt(e.what());hadFailure_=true;model_.update(row);saveQueue();note(t.message);return;}
    t.state=inspect?"inspecting":"running";t.message.clear();t.progress=0;model_.update(row);
    auto job=std::make_unique<Job>();Job* ptr=job.get();jobs_[id]=std::move(job);
    Task snapshot=t;auto settings=settings_;QString folder=settings.value("outputDirectory").toString();
    QString name;
    try{if(!inspect)name=outputName(t,settings_,row);}catch(const std::exception&e){jobs_.erase(id);t.state="failed";t.message=qt(e.what());hadFailure_=true;model_.update(row);saveQueue();emit changed();return;}
    saveQueue();emit changed();
    ptr->thread=std::thread([this,id,ptr,snapshot,settings,folder,name,inspect]{
        QString state=inspect?"queued":"done",message;
        try{
            QTemporaryDir temporary(root_+"/cache/job-XXXXXX");if(!temporary.isValid())fail(QStringLiteral("无法创建缓存"));
            auto source=materialize(QUrl(snapshot.input),temporary.path(),*ptr);
            auto info=mediabridge::probe(utf8(source),&ptr->cancel);
            QStringList details{QStringLiteral("%1 · %2 秒").arg(qt(info.format)).arg(info.duration,0,'f',2)};
            for(const auto&s:info.streams){
                if(s.type=="video")details<<QStringLiteral("#%1 视频 %2 · %3×%4").arg(s.index).arg(qt(s.codec)).arg(s.width).arg(s.height);
                else if(s.type=="audio")details<<QStringLiteral("#%1 音频 %2 · %3 Hz · %4 声道").arg(s.index).arg(qt(s.codec)).arg(s.sampleRate).arg(s.channels);
                else details<<QStringLiteral("#%1 %2 · %3").arg(s.index).arg(qt(s.type),qt(s.codec));
            }
            if(std::none_of(info.streams.begin(),info.streams.end(),[](const auto&s){return s.type=="video"||s.type=="audio";}))fail(QStringLiteral("文件不包含可处理的音频或视频"));
            QString container=qt(info.format).toUpper();
            if(container.contains("MOV,"))container="MP4 / MOV";
            else if(container.contains("MATROSKA"))container="MKV / WebM";
            const int seconds=int(info.duration);
            QString brief=container+" · "+QStringLiteral("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0'));
            QVariantMap media{{"format",qt(info.format)},{"duration",info.duration}};
            for(const auto&stream:info.streams){
                if(stream.type=="video"&&!media.contains("width")){
                    media["width"]=stream.width;media["height"]=stream.height;media["videoCodec"]=qt(stream.codec);
                    brief+=QStringLiteral(" · %1×%2").arg(stream.width).arg(stream.height);
                }else if(stream.type=="audio"&&!media.contains("audioCodec"))media["audioCodec"]=qt(stream.codec);
            }
            media["summary"]=brief;
            QVariantList streams;
            for(const auto& stream:info.streams)streams.append(QVariantMap{{"index",stream.index},{"type",qt(stream.type)},{"codec",qt(stream.codec)}});
            media["streams"]=streams;
            post([this,id,text=details.join('\n'),media,inspect]{int r=model_.find(id);if(r>=0){auto&t=model_.tasks[size_t(r)];t.details=text;t.media=media;if(inspect&&editable(t))adaptRemuxMode(t);model_.update(r);saveQueue();emit changed();}});
            if(!inspect){
                auto options=optionsFrom(snapshot.profile);options.input=utf8(source);options.output=utf8(QUrl(snapshot.output).toLocalFile());
                if(options.mode==mediabridge::Mode::Remux&&!mediabridge::remuxCompatible(info,options)) {
                    options.mode=mediabridge::Mode::Transcode;options.copySubtitles=false;
                    message=transcodeNotice(snapshot.profile);
                    post([this,id,original=snapshot.profile,message]{int r=model_.find(id);if(r>=0){auto&t=model_.tasks[size_t(r)];useTranscode(t.profile);t.message=message;if(t.selected&&profile_==original){useTranscode(profile_);syncSelectedProfiles();saveSettings();}model_.update(r);saveQueue();note(message);}});
                }
                options.overwrite=settings.value("conflict")=="overwrite";
                mediabridge::convert(options,ptr->cancel,[this,id](const mediabridge::Progress&p){post([this,id,p]{int r=model_.find(id);if(r>=0){model_.tasks[size_t(r)].progress=p.fraction;model_.update(r);emit changed();}});});
                bool retained=QUrl(folder).scheme()=="content"||(android()&&folder.isEmpty());
                if(retained){
                    QString result=qt(options.output);
                    post([this,id,result]{int r=model_.find(id);if(r>=0){model_.tasks[size_t(r)].result=result;saveQueue();}});
                    if(folder.isEmpty()){state="pending";message=QStringLiteral("转换已完成，请另存结果");}
                    else{
                        try{
                            // Serialize provider name lookup and commit so parallel jobs cannot share a write descriptor.
                            std::lock_guard<std::mutex> exportGuard(exportMutex_);
                            auto document=platformio::createInFolder(QUrl(folder),name,mimeType(QFileInfo(result).suffix()),settings.value("conflict").toString());
                            if(document.skipped){state="skipped";message=QStringLiteral("输出文件已存在");}
                            else platformio::exportFile(result,document.url,true);
                            QFile::remove(result);
                            post([this,id,destination=document.url]{int r=model_.find(id);if(r>=0){auto&t=model_.tasks[size_t(r)];t.output=destination.toString();t.result.clear();}});
                        }catch(const std::exception&e){state="pending";message=QStringLiteral("系统保存失败，完整结果已保留：")+qt(e.what());}
                    }
                }
            }
        }catch(const mediabridge::Cancelled&){state="cancelled";message=QStringLiteral("用户取消");}
        catch(const std::exception&e){state="failed";message=qt(e.what());}
        post([this,id,state,message]{complete(id,state,message);});
    });
}
void Controller::complete(const QString& id,const QString& state,const QString& message){
    auto it=jobs_.find(id);if(it!=jobs_.end()){if(it->second->thread.joinable())it->second->thread.join();jobs_.erase(it);}
    int row=model_.find(id);if(row>=0){auto&t=model_.tasks[size_t(row)];t.state=state;t.message=message;t.progress=(state=="done"||state=="pending")?1:0;model_.update(row);if(state=="failed"||state=="cancelled")hadFailure_=true;note(t.name+" · "+stateText(state)+(message.isEmpty()?QString():" · "+message));}
    saveQueue();emit changed();schedule();
}
void Controller::exportReport(const QUrl& destination){
    try{
        QJsonArray entries;for(const auto&t:model_.tasks)entries.append(QJsonObject::fromVariantMap(taskJson(t)));
        platformio::writeDocument(destination,QJsonDocument(QJsonObject{{"application",version()},{"created",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"tasks",entries}}).toJson());
        note(QStringLiteral("已导出任务报告"));
    }catch(const std::exception&e){note(qt(e.what()));}
}
void Controller::exportProfile(const QUrl& destination){
    try{platformio::writeDocument(destination,QJsonDocument(QJsonObject{{"version",2},{"profile",QJsonObject::fromVariantMap(profile_)}}).toJson());note(QStringLiteral("已导出处理方案"));}
    catch(const std::exception&e){note(qt(e.what()));}
}
void Controller::importProfile(const QUrl& source){
    try{
        QFile file(source.isLocalFile()?source.toLocalFile():source.toString(QUrl::FullyEncoded));
        if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024)fail(QStringLiteral("无法读取方案文件（最多 1 MB）"));
        QJsonParseError error;auto doc=QJsonDocument::fromJson(file.readAll(),&error);
        if(error.error!=QJsonParseError::NoError||!doc.object().value("profile").isObject())fail(QStringLiteral("无效的方案文件"));
        auto p=mergeProfile(doc.object().value("profile").toObject().toVariantMap());
        mediabridge::preset(utf8(p.value("format").toString()));profile_=p;syncSelectedProfiles();saveSettings();note(QStringLiteral("已导入处理方案"));
    }catch(const std::exception&e){note(qt(e.what()));}
}
void Controller::exportResult(const QString& id,const QUrl& destination){
    int row=model_.find(id);if(row<0||jobs_.count(id)||model_.tasks[size_t(row)].result.isEmpty())return;
    QString result=model_.tasks[size_t(row)].result;
    if(!ownedResult(result,root_)){note(QStringLiteral("应用内结果路径无效"));return;}
    bool overwrite=settings_.value("conflict")=="overwrite";
    auto job=std::make_unique<Job>();Job* ptr=job.get();jobs_[id]=std::move(job);
    model_.tasks[size_t(row)].state="running";model_.update(row);emit changed();
    ptr->thread=std::thread([this,id,result,destination,overwrite]{
        QString state="done",message;
        try{std::lock_guard<std::mutex> exportGuard(exportMutex_);platformio::exportFile(result,destination,overwrite);QFile::remove(result);post([this,id,destination]{int r=model_.find(id);if(r>=0){model_.tasks[size_t(r)].result.clear();model_.tasks[size_t(r)].output=destination.toString();}});}
        catch(const std::exception&e){state="pending";message=qt(e.what());}
        post([this,id,state,message]{complete(id,state,message);});
    });
}
void Controller::openResult(const QString& id){int row=model_.find(id);if(row>=0){const auto&t=model_.tasks[size_t(row)];QUrl url=t.result.isEmpty()?QUrl(t.output):QUrl::fromLocalFile(t.result);if(!url.isEmpty()&&!QDesktopServices::openUrl(url))note(QStringLiteral("系统无法打开该结果"));}}
void Controller::start(const QUrl& input,const QUrl& output,const QString& format,bool remux,double startSeconds,double duration,int width,int videoKbps,int audioKbps,bool overwrite){
    if(busy())return;
    profile_=defaultProfile();profile_["format"]=format;profile_["mode"]=remux?"remux":"transcode";profile_["start"]=startSeconds;profile_["duration"]=duration;profile_["width"]=width;profile_["videoKbps"]=videoKbps;profile_["audioKbps"]=audioKbps;
    settings_["conflict"]=overwrite?"overwrite":"skip";
    Task t;t.id=QUuid::createUuid().toString(QUuid::WithoutBraces);t.input=input.toString();t.output=output.toString();t.name=platformio::inputName(input);t.profile=profile_;model_.append(t);startQueue();
}
