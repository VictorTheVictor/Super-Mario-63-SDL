"""Decode source XFL sound resources to uniform WAVs; no runtime codec dependency."""
from pathlib import Path
import sys, re, subprocess, json
import xml.etree.ElementTree as ET
N={'x':'http://ns.adobe.com/xfl/2008/'}
SOUNDS={'tutorial_intro':'PrincessPeachesCastle-Intro','tutorial_loop':'PrincessPeachesCastle-Repeat','snow_intro':'Snowmans Land - Intro','snow_loop':'Snowmans Land - Repeat','wing_intro':'WingCap - Intro','wing_loop':'WingCap - Repeat','boss_intro':'Boss Music 64 - Intro','boss_loop':'Boss Music 64 - Repeat','boss_hurt':'thwomp-1','boss_land':'BowserStep','cannon_enter':'cannon1','cannon_rise':'cannon2','cannon_ready':'cannon3','cannon_shot':'Cannon Shot','star_swish':'Star Swish','get_star':'GetStar','mario_fall':'Mario-Fall','star_appear':'StarAppear','secret_intro':'Secret Course - Intro','secret_loop':'Secret Course - Repeat','intro':'Bombomb Battlefield Intro','loop':'Bombomb Battlefield Repeat',
'jump':'Mario Jump-1','double':'Mario Jump-2','triple':'Mario Tripple Jump','dive':'Mario-Dive',
'hurt':'Mario - Doh','coin':'CoinJingle','box':'boxbreak','stomp':'Goomba-Stomped','die':'Goomba-Die',
'fuse':'fuse-lite','explosion':'Explosion','notice':'Goomba-Attention','step':'step-grass','impact':'MarioImpact','nozzle':'Fludd Nozzle','switch':'FluddSwitch','spray':'fluddrepeat','nozzlebox':'Nozzle Box Break'}
def generate(root,out,ffmpeg):
    out.mkdir(parents=True,exist_ok=True)
    sounds={s.get('linkageIdentifier'):s for s in ET.parse(root/'DOMDocument.xml').findall('.//x:DOMSoundItem',N)}
    report={}
    for key,link in SOUNDS.items():
        s=sounds[link]; source=root/'bin'/s.get('soundDataHRef')
        rate,bits,channels=re.fullmatch(r'(\d+)kHz (\d+)bit (Mono|Stereo)',s.get('format')).groups()
        rate={'5':5512,'11':11025,'22':22050,'44':44100}[rate];bits=int(bits);channels=1 if channels=='Mono' else 2
        expected=int(s.get('sampleCount'))*channels*bits//8
        raw=source.stat().st_size==expected
        args=['-f','u8' if bits==8 else 's16le','-ar',str(rate),'-ac',str(channels)] if raw else ['-f','mp3']
        if not raw:
            # XFL stores the MP3 stream followed by its low-rate PCM preview.
            cached=int(s.get('cachedSampleCount','0'))*channels
            payload=source.read_bytes()
            if cached and len(payload)-cached!=int(s.get('dataLength'))+1:
                raise ValueError('Unexpected XFL audio cache boundary: '+link)
            encoded=out/(key+'.mp3')
            encoded.write_bytes(payload[:int(s.get("dataLength"))] if cached else payload)
            source=encoded
        subprocess.run([ffmpeg,'-v','error','-xerror','-y',*args,'-i',str(source),'-ar','48000','-ac','2','-c:a','pcm_f32le',str(out/(key+'.wav'))],check=True)
        if not raw: encoded.unlink()
        report[key]={'linkage':link,'source':s.get('soundDataHRef'),'encoding':'PCM' if raw else 'MP3'}
    (out/'manifest.json').write_text(json.dumps(report,indent=2))
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3])
