#include "../src/gui/controller.hpp"
#include "mediabridge/media.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <algorithm>
namespace {
int passed=0;
void check(bool value,const char* name){if(!value)throw std::runtime_error(name);++passed;std::cout<<"PASS "<<name<<'\n';}
void wait(Controller& c){QElapsedTimer timer;timer.start();while(c.busy()&&timer.elapsed()<60000){QCoreApplication::processEvents();QThread::msleep(1);}QCoreApplication::processEvents();check(!c.busy(),"workers complete without UI deadlock");}
QString state(Controller& c,int row){return c.tasks()->tasks[size_t(row)].state;}
QUrl url(const QString& p){return QUrl::fromLocalFile(p);}
void put(const QString&p,const QByteArray&data){QFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("write fixture");}
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);app.setOrganizationName("MediaBridge");app.setApplicationName("QueueChecks");
    if(argc<5){std::cerr<<"queue_checks FIXTURE WORKSPACE WAV MP3\n";return 1;}
    try{
        QTemporaryDir tmp(QString::fromLocal8Bit(argv[2])+"/queue-checks-XXXXXX");check(tmp.isValid(),"workspace test directory");
        QString root=tmp.path(),input=root+"/inputs",out=root+"/outputs",data=root+"/data";
        QDir().mkpath(input);QDir().mkpath(out);QDir().mkpath(input+"/nested");
        QString a=input+"/A.mp4",b=input+"/B.mp4",broken=input+"/C.mp4";
        QFile::copy(QString::fromLocal8Bit(argv[1]),a);QFile::copy(a,b);put(broken,"invalid media");
        QFile::copy(a,input+"/nested/D.mp4");put(input+"/ignore.txt","ignored");
        const QString wav=root+QStringLiteral("/ビートまりお - ナイト・オブ・ナイツ.wav"),mp3=root+"/original.mp3";
        check(QFile::copy(QString::fromLocal8Bit(argv[3]),wav)&&QFile::copy(QString::fromLocal8Bit(argv[4]),mp3),"Japanese WAV and encoded MP3 fixtures");
        {
            qputenv("MEDIABRIDGE_DATA_DIR",(root+"/audio-mode-data").toUtf8());
            Controller c;c.setSetting("outputDirectory",url(out).toString());c.setProfileValue("format","wav");c.setProfileValue("mode","remux");
            c.addFiles({url(wav)});QCoreApplication::processEvents();wait(c);
            check(c.profile().value("mode")=="remux"&&c.tasks()->tasks[0].media.value("audioCodec")=="pcm_s16le","PCM WAV to WAV retains compatible remux mode");
            c.setProfileValue("format","mp3");
            check(c.profile().value("mode")=="transcode"&&c.tasks()->tasks[0].profile.value("mode")=="transcode","WAV to MP3 target selection automatically uses transcode");
            c.setProfileValue("mode","remux");
            check(c.profile().value("mode")=="transcode"&&!c.profile().value("copySubtitles").toBool(),"incompatible remux selection automatically corrected");
            c.startQueue();wait(c);
            check(state(c,0)=="done"&&mediabridge::probe(QUrl(c.tasks()->tasks[0].output).toLocalFile().toUtf8().toStdString()).streams[0].codec=="mp3","Japanese WAV conversion produces genuine MP3");
        }
        {
            qputenv("MEDIABRIDGE_DATA_DIR",(root+"/audio-immediate-data").toUtf8());
            Controller c;c.setSetting("outputDirectory",url(out).toString());c.setProfileValue("format","mp3");c.setProfileValue("mode","remux");
            c.addFiles({url(wav),url(mp3)});c.startQueue();wait(c);
            check(state(c,0)=="done"&&c.tasks()->tasks[0].profile.value("mode")=="transcode","start before inspection safely transcodes incompatible WAV");
            check(state(c,1)=="done"&&c.tasks()->tasks[1].profile.value("mode")=="remux","batch retains compatible MP3 remux for active task");
        }
        const QString oldData=root+"/audio-recovery-data";
        {
            qputenv("MEDIABRIDGE_DATA_DIR",oldData.toUtf8());
            Controller c;c.setSetting("outputDirectory",url(out).toString());c.setProfileValue("format","mp3");c.setProfileValue("mode","remux");c.addFiles({url(wav)});
        }
        {
            QFile f(oldData+"/queue.json");f.open(QIODevice::ReadOnly);auto obj=QJsonDocument::fromJson(f.readAll()).object();f.close();auto entries=obj["tasks"].toArray();
            auto task=entries[0].toObject();task["state"]="failed";task["message"]="Input codec is incompatible with the output container; use transcode";
            task["media"]=QJsonObject{{"audioCodec","pcm_s16le"}};entries[0]=task;obj["tasks"]=entries;put(oldData+"/queue.json",QJsonDocument(obj).toJson());
            Controller c;
            check(state(c,0)=="queued"&&c.tasks()->tasks[0].profile.value("mode")=="transcode"&&c.profile().value("mode")=="transcode","old incompatible failed task restored as usable transcode task");
            c.startQueue();wait(c);check(state(c,0)=="done","old failed WAV task can start directly after upgrade");
        }
        {
            Controller c;check(c.profile().value("mode")=="transcode","repaired global mode persists across another restart");
            c.tasks()->tasks[0].state="queued";c.tasks()->tasks[0].output.clear();c.tasks()->tasks[0].profile["mode"]="remux";
            c.tasks()->tasks[0].media={{"audioCodec","pcm_s16le"}};c.startQueue();wait(c);
            check(state(c,0)=="done"&&c.tasks()->tasks[0].profile.value("mode")=="transcode","legacy codec-only metadata also prevents incompatible remux");
        }
        {
            QString workflow=root+"/workflow-data",disguised=input+"/video-renamed.mp3",unknown=input+"/video.data";
            QFile::copy(a,disguised);QFile::copy(a,unknown);
            qputenv("MEDIABRIDGE_DATA_DIR",workflow.toUtf8());
            Controller c;c.setSetting("outputDirectory",url(out).toString());c.addFiles({url(disguised)});
            QCoreApplication::processEvents();wait(c);
            check(c.tasks()->tasks[0].media.value("format").toString().contains("mp4")&&c.tasks()->tasks[0].media.value("width").toInt()==320,"import probes actual content despite misleading extension");
            c.setProfileValue("format","flac");
            check(c.tasks()->tasks[0].profile.value("format")=="flac","format selection immediately updates queued task");
            c.useProfile("无损换封装");c.setProfileValue("mode","transcode");
            check(!c.profile().value("copySubtitles").toBool()&&c.tasks()->tasks[0].profile.value("mode")=="transcode","remux to transcode clears incompatible subtitle copy");
            c.setProfileValue("videoEncoder","libx264");c.setProfileValue("audioEncoder","libopus");c.setProfileValue("crf",24);c.setProfileValue("format","mp4");
            check(c.profile().value("videoEncoder").toString().isEmpty()&&c.profile().value("audioEncoder").toString().isEmpty()&&c.profile().value("crf").toInt()==-1,"format change resets incompatible encoder overrides");
            c.setProfileValue("format","webm");c.setProfileValue("crf",28);
            check(c.profile().value("videoEncoder")=="libvpx-vp9","WebM quality mode selects compatible VP9 encoder");
            const auto available=c.videoEncoders();
            check(std::none_of(available.begin(),available.end(),[](const QVariant& v){return v.toMap().value("id")=="libx264";}),"WebM encoder choices exclude incompatible H264");
            c.setProfileValue("format","mp3");c.setProfileValue("crf",24);
            check(c.profile().value("crf").toInt()==-1&&c.videoEncoders().size()==1,"audio formats do not inherit video quality settings");
            c.setProfileValue("format","mp4");
            c.setProfileValue("width",160);c.startQueue();c.setProfileValue("width",96);
            check(c.tasks()->tasks[0].profile.value("width").toInt()==160,"active conversion keeps immutable profile while fields change");
            wait(c);check(state(c,0)=="done","conversion succeeds after remux to transcode transition");
            c.selectAll(true);c.removeSelected();c.addFolder(url(input));
            check(std::any_of(c.tasks()->tasks.begin(),c.tasks()->tasks.end(),[&](const Task&t){return t.name=="video.data";}),"folder import recognizes media content without known extension");
            QCoreApplication::processEvents();wait(c);
            QFile::remove(disguised);QFile::remove(unknown);
        }
        qputenv("MEDIABRIDGE_DATA_DIR",data.toUtf8());
        {
            Controller c;
            check(c.count()==0&&c.settings().value("parallel").toInt()==2,"fresh queue and defaults");
            c.setSetting("outputDirectory",url(out).toString());
            c.addFolder(url(input));c.addFiles({url(a)});
            check(c.count()==3,"directory import filters and duplicate removal");
            auto id=c.tasks()->tasks[0].id;
            c.selectAll(false);c.setSelected(id,true);c.setProfileValue("width",160);c.applyProfile();
            check(c.tasks()->tasks[0].profile.value("width").toInt()==160&&c.tasks()->tasks[1].profile.value("width").toInt()==0,"per-task profile snapshots and selection");
            c.saveProfile("Test preset");c.setProfileValue("width",240);c.useProfile("Test preset");
            check(c.profile().value("width").toInt()==160,"custom preset round trip");
            c.exportProfile(url(root+"/preset.json"));c.resetProfile();c.importProfile(url(root+"/preset.json"));
            check(c.profile().value("width").toInt()==160,"preset JSON import and export");
            c.moveTask(id,1);c.moveTask(id,-1);check(c.tasks()->tasks[0].id==id,"queue reorder retains task identity");
            c.startQueue();check(c.activeCount()==2,"bounded parallel worker startup");c.pauseQueue();wait(c);
            check(c.paused()&&c.waitingCount()==1&&c.completedCount()==2,"pause scheduling lets active tasks drain");
            c.setSetting("parallel",1);c.pauseQueue();wait(c);
            check(state(c,2)=="failed"&&c.completedCount()==2,"isolated conversion failure preserves successful tasks");
            c.exportReport(url(root+"/report.json"));QFile report(root+"/report.json");report.open(QIODevice::ReadOnly);
            auto entries=QJsonDocument::fromJson(report.readAll()).object().value("tasks").toArray();
            check(entries.size()==3&&entries[2].toObject().value("message").toString().size()>0,"report includes task options and error evidence");
            c.selectAll(false);c.setSelected(c.tasks()->tasks[2].id,true);QFile::remove(broken);QFile::copy(a,broken);c.retrySelected();c.startQueue();wait(c);
            check(state(c,2)=="done","failed task can be repaired and retried");
            c.setSetting("conflict","skip");c.addFiles({url(a)});c.startQueue();wait(c);check(state(c,3)=="skipped","existing output skip policy");
            c.setSetting("conflict","rename");c.addFiles({url(a)});c.startQueue();wait(c);
            check(state(c,4)=="done"&&QUrl(c.tasks()->tasks[4].output).toLocalFile().contains("(1)"),"automatic output collision rename");
            c.addFiles({url(b)});c.startQueue();c.cancelAll();wait(c);
            check(state(c,5)=="cancelled"&&!QFile::exists(QUrl(c.tasks()->tasks[5].output).toLocalFile()),"active cancellation leaves no partial result");
            c.addFiles({url(a)});auto queuedId=c.tasks()->tasks.back().id;c.inspectTask(queuedId);wait(c);
            check(c.tasks()->tasks.back().details.contains("#0")&&c.tasks()->tasks.back().state=="queued","asynchronous media stream inspection");
            c.setSetting("theme","light");c.setSetting("parallel",99);check(c.settings().value("parallel").toInt()==4,"settings enforce concurrency range");
        }
        {
            QFile file(data+"/queue.json");file.open(QIODevice::ReadOnly);
            auto obj=QJsonDocument::fromJson(file.readAll()).object();file.close();
            auto entries=obj.value("tasks").toArray();auto last=entries.last().toObject();last["state"]="running";last["progress"]=0.75;entries[entries.size()-1]=last;obj["tasks"]=entries;
            put(data+"/queue.json",QJsonDocument(obj).toJson());
            QFile::copy(a,data+"/pending/orphan.mp4");
            Controller c;
            check(c.settings().value("theme")=="light"&&c.profile().value("width").toInt()==160&&c.profiles().contains("Test preset"),"settings and presets survive application restart");
            check(c.tasks()->tasks[c.tasks()->tasks.size()-2].state=="queued"&&c.tasks()->tasks[c.tasks()->tasks.size()-2].progress==0,"interrupted tasks recover without auto-start");
            auto id=c.tasks()->tasks.back().id;check(state(c,c.count()-1)=="pending","unreferenced encoded result recovered");
            c.selectAll(true);c.removeSelected();check(c.count()==1&&state(c,0)=="pending","pending result protected from queue cleanup");
            c.exportResult(id,url(out+"/recovered.mp4"));wait(c);
            check(state(c,0)=="done"&&QFile::exists(out+"/recovered.mp4")&&!QFile::exists(data+"/pending/orphan.mp4"),"recovered result exported and retained copy cleared");
            c.clearFinished();check(c.count()==0,"completed task cleanup");
            c.setSetting("recursive",true);c.addFolder(url(input));check(c.count()==4,"recursive directory import");
            c.selectAll(true);c.removeSelected();c.setSetting("namingTemplate","collision");c.setSetting("parallel",2);
            c.setProfileValue("threads",3);c.addFiles({url(a),url(b)});check(c.tasks()->tasks[0].profile.value("threads").toInt()==3,"new tasks preserve explicit profile thread count");c.startQueue();wait(c);
            check(state(c,0)=="done"&&state(c,1)=="done"&&c.tasks()->tasks[0].output!=c.tasks()->tasks[1].output,"active tasks reserve colliding output names");
        }
        {
            put(data+"/queue.json","{invalid json");
            Controller c;
            check(c.count()==0&&QDir(data).entryList({"queue.corrupt-*.json"},QDir::Files).size()==1,"corrupt queue backed up before replacement");
            c.setSetting("restoreQueue",false);
            c.addFiles({url(a)});
        }
        {
            QFile::copy(a,data+"/pending/kept.mp4");
            Controller c;
            check(c.count()==1&&state(c,0)=="pending","pending results restored when queue restoration disabled");
            auto outside=root+"/outside.mp4";QFile::copy(a,outside);
            c.tasks()->tasks[0].result=outside;c.exportResult(c.tasks()->tasks[0].id,url(out+"/invalid-export.mp4"));
            check(QFile::exists(outside)&&!QFile::exists(out+"/invalid-export.mp4"),"result cleanup refuses files outside managed pending directory");
        }
        std::cout<<passed<<" queue checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
