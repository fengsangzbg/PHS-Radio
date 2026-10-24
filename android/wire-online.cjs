const fs=require('fs');const base=__dirname+'/app/src/main/java/io/github/fengsangzbg/phsradio/mobile/';
let p=base+'PlaybackService.java',s=fs.readFileSync(p,'utf8');
s=s.replace('private MediaPlayer player;','private MediaPlayer player;\n    private final java.util.concurrent.ExecutorService resolver=java.util.concurrent.Executors.newSingleThreadExecutor();\n    private final List<JSONObject> tracks=new ArrayList<>();\n    private OnlineApi online;\n    private int loadGeneration;\n    private boolean destroyed,loading;\n    private String currentLyrics="";');
s=s.replace('super.onCreate();','super.onCreate();online=new OnlineApi(this);');
s=s.replace('uris.clear();titles.clear();','uris.clear();titles.clear();tracks.clear();');s=s.replace('uris.add(item.getString("uri"));','tracks.add(item);uris.add(item.getString("uri"));');
s=s.replace('if(player==null)stopSelf();','if(player==null&&!loading)stopSelf();');
s=s.replace('playing=false;prepared=false;\n        if(audio.requestAudioFocus','playing=false;prepared=false;currentLyrics="";int ticket=++loadGeneration;loading=false;\n        if(audio.requestAudioFocus');
s=s.replace('        player=new MediaPlayer();','PLACEHOLDER');
// Split preparation so network resolution never runs on the main thread.
s=s.replace('PLACEHOLDER',`        JSONObject track=tracks.get(index);String source=uris.get(index);
        if(source.startsWith("phs://")){loading=true;publish("");resolver.execute(()->{try{String url=online.resolve(track);handler.post(()->{if(!destroyed&&ticket==loadGeneration){loading=false;prepareSource(url);}});String lyrics="";try{lyrics=online.lyrics(track);}catch(Exception ignored){}String lrc=lyrics;handler.post(()->{if(!destroyed&&ticket==loadGeneration){currentLyrics=lrc;publish("");}});}catch(Exception error){handler.post(()->{if(!destroyed&&ticket==loadGeneration){loading=false;wantPlay=false;publish(error instanceof java.io.IOException?error.getMessage():"平台播放请求失败，请重新登录后重试");}});}});return;}
        prepareSource(source);
    }
    private void prepareSource(String source){
        player=new MediaPlayer();`);
s=s.replace('player.setDataSource(this,Uri.parse(uris.get(index)))','player.setDataSource(this,Uri.parse(source))');
s=s.replace('if(player==null)return;','if(player==null){if(loading){wantPlay=true;return;}if(index>=0)load(index);return;}');
s=s.replace('.putExtra("error",error);','.putExtra("error",error).putExtra("loading",loading).putExtra("track",index>=0&&index<tracks.size()?tracks.get(index).toString():"{}").putExtra("lyrics",currentLyrics);');
s=s.replace('onDestroy(){handler','onDestroy(){destroyed=true;++loadGeneration;loading=false;resolver.shutdownNow();online.close();handler');
fs.writeFileSync(p,s);
