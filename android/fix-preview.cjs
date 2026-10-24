const fs=require('fs');const base=__dirname+'/app/src/main/java/io/github/fengsangzbg/phsradio/mobile/';
function edit(name,fn){const p=base+name+'.java';fs.writeFileSync(p,fn(fs.readFileSync(p,'utf8')));}
edit('MainActivity',s=>{
 s=s.replace('button("??",','button("账号",').replace('private SongPage songPage;','private SongPage songPage;\n    private PlayMeteor meteor;\n    private boolean wasPlaying;');
 s=s.replace('mode.setText(i.getBooleanExtra("shuffle",false)?"随机":"顺序");','mode.setText(new String[]{"顺序","随机","单曲循环"}[Math.floorMod(i.getIntExtra("playMode",0),3)]);\n        boolean playing=i.getBooleanExtra("playing",false);if(playing&&!wasPlaying&&getSharedPreferences("mobile",0).getBoolean("meteors",true))meteor.flash("fire".equals(wallpaper.mode()));wasPlaying=playing;\n        songPage.state(i.getStringExtra("uri")==null?"":i.getStringExtra("uri"),i.getStringExtra("title"),i.getLongExtra("position",0),playing);');
 s=s.replace('setContentView(root);','meteor=new PlayMeteor(this);root.addView(meteor,new FrameLayout.LayoutParams(-1,-1));\n        setContentView(root);');
 if(!s.includes('水晶唱片 · 歌词'))s=s.replace('dock.addView(now);','dock.addView(now);now.setOnClickListener(v->songPage.show());dock.addView(button("水晶唱片 · 歌词",()->songPage.show()),new LinearLayout.LayoutParams(-1,dp(40)));');
 s=s.replace('addTheme(panel,"默认水光",','addTheme(panel,"火流星 · 盛夏夜空",()->{wallpaper.set("fire","");dialog.dismiss();});\n        addTheme(panel,"播放流星：开启 / 关闭",()->{boolean enabled=!getSharedPreferences("mobile",0).getBoolean("meteors",true);getSharedPreferences("mobile",0).edit().putBoolean("meteors",enabled).apply();if(!enabled)meteor.stop();Toast.makeText(this,enabled?"流星已开启":"流星已关闭",Toast.LENGTH_SHORT).show();});\n        addTheme(panel,"默认水光",');
 s=s.replace('new Button(this)','new AeroButton(this)');s=s.replace(/view\.setOnTouchListener\(\(v,event\)->\{.*?return false;\}\);/,'');
 return s;
});
edit('SongPage',s=>s.replace('panel.setBackgroundColor(0xEE122A3E);','panel.setBackgroundColor(0x33122A3E);').replace('dialog.show();dialog.getWindow().setLayout(-1,-1);','dialog.show();dialog.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);dialog.getWindow().setLayout(-1,-1);').replaceAll('new Button(activity)','new AeroButton(activity)'));
edit('MusicAccounts',s=>s.replaceAll('new Button(activity)','new AeroButton(activity)'));
for(const p of ['app/build.gradle','tools/build-preview.ps1']){const name=__dirname+'/'+p;let s=fs.readFileSync(name,'utf8').replaceAll('0.1.1-preview','0.1.2-preview').replace('versionCode 2','versionCode 3').replace("'--version-code','2'","'--version-code','3'");fs.writeFileSync(name,s);}
