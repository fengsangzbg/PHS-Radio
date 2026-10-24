const fs=require('fs'),path=require('path');
const base=path.join(__dirname,'app/src/main/java/io/github/fengsangzbg/phsradio/mobile');
function edit(file,fn){const p=path.join(base,file+'.java');fs.writeFileSync(p,fn(fs.readFileSync(p,'utf8')));}
edit('GlassVisual',s=>{
 s=s.replace('private final boolean sky;','private final boolean sky;\n    private final FireSky fireSky=new FireSky();\n    private double skySeconds;\n    private long skyTick;');
 s=s.replace('setLayerType(View.LAYER_TYPE_HARDWARE,null);','');
 s=s.replace('active=value;last=0;','active=value;last=0;skyTick=0;');
 s=s.replace('background=new LinearGradient','fireSky.resize(w,h);background=new LinearGradient');
 const begin=s.indexOf('            paint.setShader(background);canvas.drawRect');
 const end=s.indexOf('        }else{',begin);
 if(begin<0||end<0)throw Error('Sky block not found');
 s=s.slice(0,begin)+'            long tick=SystemClock.elapsedRealtimeNanos();\n            if(active&&skyTick!=0)skySeconds+=(tick-skyTick)/1_000_000_000.0;\n            skyTick=active?tick:0;\n            fireSky.draw(canvas,getWidth(),getHeight(),skySeconds,getResources().getDisplayMetrics().density);\n'+s.slice(end);
 return s;
});
edit('MainActivity',s=>s.replaceAll('"水晶唱片 · 歌词"','"歌词"'));
for(const p of ['app/build.gradle','tools/build-preview.ps1']){const file=path.join(__dirname,p);let s=fs.readFileSync(file,'utf8').replaceAll('0.1.2-preview','0.1.3-preview').replace('versionCode 3','versionCode 4').replace("'--version-code','3'","'--version-code','4'");fs.writeFileSync(file,s);}
