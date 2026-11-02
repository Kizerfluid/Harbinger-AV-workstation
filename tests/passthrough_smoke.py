import subprocess,tempfile,os,json,sys
from pathlib import Path
ff='ffmpeg';fp='ffprobe'
def run(a):
    x=subprocess.run(a,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=45)
    if x.returncode: raise RuntimeError(str(a)+'\n'+x.stderr[-1600:])

def codecs(p):
    s=subprocess.run([fp,'-v','error','-show_entries','stream=codec_name,codec_type','-of','json',str(p)],capture_output=True,text=True,timeout=20,check=True)
    return [(j['codec_type'],j['codec_name']) for j in json.loads(s.stdout)['streams']]

with tempfile.TemporaryDirectory(prefix='harbinger-passthrough-test-') as d:
    d=Path(d)
    src=d/'source.mkv'
    run([ff,'-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc2=size=320x240:rate=25',
         '-f','lavfi','-i','sine=frequency=440:sample_rate=48000', '-t','1.5',
         '-c:v','mpeg4','-c:a','pcm_s16le','-shortest',str(src)])
    first=codecs(src);print('source:',first)
    assert first == [('video','mpeg4'),('audio','pcm_s16le')],first
    # Both copy, new MKV: no transcode
    p=d/'both_copy.mkv';run([ff,'-hide_banner','-loglevel','error','-n','-i',str(src),'-map','0:v:0','-c:v','copy','-map','0:a:0','-c:a','copy','-sn','-dn',str(p)])
    assert codecs(p)==first
    # Video copy, audio transcode
    p=d/'video_copy_audio_encode.mkv';run([ff,'-hide_banner','-loglevel','error','-n','-i',str(src),'-map','0:v:0','-c:v','copy','-map','0:a:0','-c:a','aac','-b:a','128k','-sn','-dn',str(p)])
    assert codecs(p)==[('video','mpeg4'),('audio','aac')]
    # Video transcode, audio copy
    p=d/'video_encode_audio_copy.mkv';run([ff,'-hide_banner','-loglevel','error','-n','-i',str(src),'-map','0:v:0','-c:v','libx264','-crf','23','-preset','veryfast','-map','0:a:0','-c:a','copy','-sn','-dn',str(p)])
    assert codecs(p)==[('video','h264'),('audio','pcm_s16le')]
    # Remove video
    p=d/'audio_only.mkv';run([ff,'-hide_banner','-loglevel','error','-n','-i',str(src),'-vn','-map','0:a:0','-c:a','copy','-sn','-dn',str(p)])
    assert codecs(p)==[('audio','pcm_s16le')]
    # Remove audio
    p=d/'video_only.mkv';run([ff,'-hide_banner','-loglevel','error','-n','-i',str(src),'-map','0:v:0','-c:v','copy','-an','-sn','-dn',str(p)])
    assert codecs(p)==[('video','mpeg4')]
    # Container incompatible copy should error; fail clearly, not silently transcode
    p=d/'bad.webm';r=subprocess.run([ff,'-hide_banner','-loglevel','error','-n','-i',str(src),'-map','0:v:0','-c:v','copy','-map','0:a:0','-c:a','copy',str(p)],capture_output=True,text=True,timeout=15)
    assert r.returncode != 0, 'Invalid container unexpectedly accepted source formats'
    print('PASS: 5 valid passthrough/removal combinations; incompatible WebM rejected without re-encoding')
