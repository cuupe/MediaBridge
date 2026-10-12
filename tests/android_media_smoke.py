"""Execute the native Android core and decode every result with independent host FFmpeg."""
import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--adb',required=True);p.add_argument('--device',default='emulator-5556')
p.add_argument('--cli',required=True,type=Path);p.add_argument('--ffmpeg-sdk',required=True,type=Path)
p.add_argument('--cxx-runtime',required=True,type=Path);p.add_argument('--ffmpeg',required=True);p.add_argument('--ffprobe',required=True)
a=p.parse_args();adb=[a.adb,'-s',a.device];remote='/data/local/tmp/mediabridge-core-check'
def run(cmd):
    r=subprocess.run([str(x) for x in cmd],capture_output=True,timeout=90)
    if r.returncode:raise AssertionError(r.stderr.decode('utf8',errors='replace')+r.stdout.decode('utf8',errors='replace'))
    return r.stdout
run([*adb,'shell','mkdir','-p',remote]);run([*adb,'push',a.cli,remote+'/cli'])
for library in a.ffmpeg_sdk.glob('*.so'):run([*adb,'push',library,remote+'/'+library.name])
run([*adb,'push',a.cxx_runtime,remote+'/libc++_shared.so']);run([*adb,'shell','chmod','755',remote+'/cli'])
def cli(*args):return run([*adb,'shell','env','LD_LIBRARY_PATH='+remote,remote+'/cli',*args])
with tempfile.TemporaryDirectory(prefix='mb-android-') as tmp:
    root=Path(tmp);source=root/'input.data'
    run([a.ffmpeg,'-v','error','-f','lavfi','-i','testsrc2=size=320x180:rate=24','-f','lavfi','-i','sine=sample_rate=44100','-t','2','-c:v','mpeg4','-c:a','aac','-f','mp4',source])
    run([*adb,'push',source,remote+'/input.data'])
    info=json.loads(cli('probe',remote+'/input.data'))
    assert 'mp4' in info['format'] and len(info['streams'])==2
    print('PASS Android content-based detection with unknown extension',flush=True)
    values=json.loads(cli('presets'));assert len(values)==12 and all(v['available'] for v in values)
    for value in values:
        fmt=value['id'];out=remote+'/result.'+fmt
        cli('convert','-i',remote+'/input.data','-o',out,'--format',fmt,'--overwrite')
        local=root/('result.'+fmt);run([*adb,'pull',out,local])
        run([a.ffmpeg,'-v','error','-xerror','-i',local,'-f','null','-'])
        data=json.loads(run([a.ffprobe,'-v','error','-show_streams','-show_format','-of','json',local]))
        assert len(data['streams'])==(1 if fmt in {'mp3','ogg','opus','m4a','wav','flac'} else 2)
        print('PASS Android '+fmt+' encode and independent decode',flush=True)
    for name,flags in [('advanced',['--width','160','--start','0.25','--duration','1','--video-encoder','libx264','--crf','25']),('remux',['--format','mkv','--mode','remux'])]:
        out=remote+'/'+name+'.mkv';cli('convert','-i',remote+'/input.data','-o',out,'--overwrite',*flags)
        local=root/(name+'.mkv');run([*adb,'pull',out,local]);run([a.ffmpeg,'-v','error','-xerror','-i',local,'-f','null','-'])
        if name=='advanced':
            data=json.loads(run([a.ffprobe,'-v','error','-show_streams','-of','json',local]));assert data['streams'][0]['width']==160
        print('PASS Android '+name,flush=True)
print('15 Android media checks passed',flush=True)
