package io.github.fengsangzbg.phsradio.mobile;

import android.app.*;
import android.content.*;
import android.media.*;
import android.media.session.*;
import android.net.Uri;
import android.os.*;
import org.json.*;
import java.util.*;

/** Audio belongs to a foreground media service, independently of the activity/wallpaper. */
public final class PlaybackService extends Service {
    static final String STATE="io.github.fengsangzbg.phsradio.mobile.PLAYBACK_STATE";
    private static final String CHANNEL="music";
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final List<String> uris=new ArrayList<>(), titles=new ArrayList<>();
    private MediaPlayer player;
    private final java.util.concurrent.ExecutorService resolver=java.util.concurrent.Executors.newSingleThreadExecutor();
    private final List<JSONObject> tracks=new ArrayList<>();
    private OnlineApi online;
    private volatile int loadGeneration;
    private volatile boolean destroyed;
    private boolean loading;
    private String currentLyrics="",currentLyricState="idle",currentLyricError="";
    private int lyricGeneration;
    private final java.util.concurrent.ExecutorService lyricPool=java.util.concurrent.Executors.newFixedThreadPool(2);
    private MediaSession session;
    private AudioManager audio;
    private AudioFocusRequest focus;
    private int index=-1;
    private int playMode;
    private boolean prepared, playing, foreground, shuffle, ducked, wantPlay;
    private float volume=1f;
    private final BroadcastReceiver noisy=new BroadcastReceiver(){ public void onReceive(Context c,Intent i){pause();} };
    private final Runnable tick=new Runnable(){public void run(){publish("");if(playing)handler.postDelayed(this,1000);}};

    @Override public void onCreate(){
        super.onCreate();online=new OnlineApi(this);
        audio=(AudioManager)getSystemService(AUDIO_SERVICE);
        volume=getSharedPreferences("mobile",0).getFloat("volume",1f);
        playMode=Math.floorMod(getSharedPreferences("mobile",0).getInt("playMode",0),3);shuffle=playMode==1;
        NotificationChannel channel=new NotificationChannel(CHANNEL,"音乐播放",NotificationManager.IMPORTANCE_LOW);
        channel.setSound(null,null);
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
        session=new MediaSession(this,"PHS Radio");
        session.setCallback(new MediaSession.Callback(){
            public void onPlay(){resume();} public void onPause(){pause();}
            public void onSkipToNext(){next(1);} public void onSkipToPrevious(){next(-1);}
            public void onSeekTo(long p){seek(p);} public void onStop(){stopSelf();}
        });
        focus=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(change->{
                if(change==AudioManager.AUDIOFOCUS_LOSS||change==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT)pause();
                else {ducked=change==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK;applyVolume();}
            },handler).build();
        if(Build.VERSION.SDK_INT>=33)registerReceiver(noisy,new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY),Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(noisy,new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY));
    }

    @Override public int onStartCommand(Intent intent,int flags,int id){
        if(intent==null)return START_NOT_STICKY;
        String action=intent.getAction();
        if("play".equals(action)){
            startForeground(1,notification());foreground=true;
            try{
                String input=intent.getStringExtra("queue");String queuePath=intent.getStringExtra("queueFile");if(queuePath!=null){java.io.File file=new java.io.File(queuePath);if(!file.getCanonicalFile().getParentFile().equals(getCacheDir().getCanonicalFile())||!file.getName().startsWith("play-queue-")||file.length()>16*1024*1024)throw new java.io.IOException();input=new String(java.nio.file.Files.readAllBytes(file.toPath()),java.nio.charset.StandardCharsets.UTF_8);file.delete();}JSONArray items=new JSONArray(input);
                uris.clear();titles.clear();tracks.clear();
                for(int n=0;n<items.length();n++){JSONObject item=items.getJSONObject(n);tracks.add(item);uris.add(item.getString("uri"));titles.add(item.optString("title","音乐"));}
                load(intent.getIntExtra("index",0));
            }catch(Exception e){publish("无法读取播放列表");if(player==null&&!loading)stopSelf();}
        }else if("toggle".equals(action)){if(playing)pause();else resume();}
        else if("next".equals(action))next(1);
        else if("previous".equals(action))next(-1);
        else if("lyrics".equals(action)){if(index>=0&&index<tracks.size()&&uris.get(index).startsWith("phs://"))requestLyrics(tracks.get(index));}
        else if("seek".equals(action))seek(intent.getLongExtra("position",0));
        else if("volume".equals(action)){
            volume=Math.max(0f,Math.min(1f,intent.getFloatExtra("volume",1f)));
            getSharedPreferences("mobile",0).edit().putFloat("volume",volume).apply();applyVolume();publish("");
        }else if("shuffle".equals(action)){playMode=(playMode+1)%3;shuffle=playMode==1;getSharedPreferences("mobile",0).edit().putInt("playMode",playMode).apply();publish("");}
        else if("stop".equals(action)){stopSelf();return START_NOT_STICKY;}
        else {publish("");if(player==null&&!loading)stopSelf();}
        return START_NOT_STICKY;
    }

    private void load(int selected){
        if(uris.isEmpty())return;
        index=Math.floorMod(selected,uris.size());
        handler.removeCallbacks(tick);
        if(player!=null){player.release();player=null;}
        playing=false;prepared=false;currentLyrics="";currentLyricError="";currentLyricState="idle";++lyricGeneration;int ticket=++loadGeneration;loading=false;
        if(audio.requestAudioFocus(focus)!=AudioManager.AUDIOFOCUS_REQUEST_GRANTED){publish("暂时无法取得音乐播放焦点");return;}
        wantPlay=true;
        JSONObject track=tracks.get(index);String source=uris.get(index);
        if(source.startsWith("phs://")){loading=true;requestLyrics(track);requestCover(track,ticket);publish("");resolver.execute(()->{if(destroyed||ticket!=loadGeneration)return;try{String url=online.resolve(track);handler.post(()->{if(!destroyed&&ticket==loadGeneration){loading=false;prepareSource(url);}});}catch(Exception error){handler.post(()->{if(!destroyed&&ticket==loadGeneration){loading=false;wantPlay=false;publish(OnlineApi.message(error));}});}});return;}
        prepareSource(source);
    }
    private void requestLyrics(JSONObject track){
        int ticket=++lyricGeneration,trackTicket=loadGeneration;currentLyricState="loading";currentLyricError="";publish("");lyricPool.execute(()->{if(destroyed||trackTicket!=loadGeneration)return;try{String text=online.lyrics(track);handler.post(()->{if(!destroyed&&ticket==lyricGeneration&&trackTicket==loadGeneration){currentLyrics=text;currentLyricState=text.trim().isEmpty()?"empty":"ready";publish("");}});}catch(Exception error){handler.post(()->{if(!destroyed&&ticket==lyricGeneration&&trackTicket==loadGeneration){currentLyricState="error";currentLyricError=OnlineApi.message(error);publish("");}});}});
    }
    private void requestCover(JSONObject track,int ticket){if(track.optString("cover").startsWith("https://"))return;lyricPool.execute(()->{if(destroyed||ticket!=loadGeneration)return;try{String cover=online.cover(track);handler.post(()->{if(!destroyed&&ticket==loadGeneration&&!cover.isEmpty()){try{track.put("cover",cover);}catch(Exception ignored){}publish("");}});}catch(Exception ignored){}});}
    private void prepareSource(String source){
        player=new MediaPlayer();
        player.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build());
        player.setWakeMode(this,PowerManager.PARTIAL_WAKE_LOCK);
        MediaPlayer expected=player;
        player.setOnPreparedListener(p->{if(player!=expected)return;prepared=true;applyVolume();if(wantPlay){p.start();playing=true;handler.post(tick);}session.setActive(true);publish("");});
        player.setOnCompletionListener(p->{if(player==expected){if(playMode==2){p.seekTo(0);p.start();}else if(playMode==0&&index==uris.size()-1){pause();}else next(1);}});
        player.setOnErrorListener((p,what,extra)->{if(player==expected){playing=false;prepared=false;wantPlay=false;handler.removeCallbacks(tick);player=null;p.release();publish("此文件或音源暂时无法播放，可重试或选择其他歌曲");}return true;});
        try{player.setDataSource(this,Uri.parse(source));player.prepareAsync();publish("");}
        catch(Exception e){player.release();player=null;publish("无法打开歌曲，文件可能已移动或授权失效");}
    }
    private void next(int direction){
        if(uris.isEmpty())return;
        int selected=index+direction;
        if(shuffle&&direction>0&&uris.size()>1)selected=(index+1+new Random().nextInt(uris.size()-1))%uris.size();
        load(selected);
    }
    private void resume(){
        if(player==null){if(loading){wantPlay=true;return;}if(index>=0)load(index);return;}
        if(audio.requestAudioFocus(focus)!=AudioManager.AUDIOFOCUS_REQUEST_GRANTED)return;
        wantPlay=true;
        if(!prepared)return;
        if(!foreground){startForeground(1,notification());foreground=true;}
        player.start();playing=true;session.setActive(true);handler.removeCallbacks(tick);handler.post(tick);publish("");
    }
    private void pause(){
        wantPlay=false;if(player!=null&&prepared)player.pause();playing=false;
        handler.removeCallbacks(tick);publish("");
    }
    private void seek(long position){if(player!=null&&prepared){player.seekTo(Math.max(0L,Math.min(position,player.getDuration())),MediaPlayer.SEEK_CLOSEST);publish("");}}
    private void applyVolume(){if(player!=null){float level=volume*(ducked?.2f:1f);player.setVolume(level,level);}}
    private String title(){return index>=0&&index<titles.size()?titles.get(index):"选择一首音乐";}

    private void publish(String error){
        long position=0,duration=0;
        if(player!=null&&prepared){position=player.getCurrentPosition();duration=player.getDuration();}
        session.setMetadata(new MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE,title()).putString(MediaMetadata.METADATA_KEY_ARTIST,"PHS Radio 新大地").putLong(MediaMetadata.METADATA_KEY_DURATION,duration).build());
        long actions=PlaybackState.ACTION_PLAY|PlaybackState.ACTION_PAUSE|PlaybackState.ACTION_PLAY_PAUSE|PlaybackState.ACTION_SKIP_TO_NEXT|PlaybackState.ACTION_SKIP_TO_PREVIOUS|PlaybackState.ACTION_SEEK_TO;
        session.setPlaybackState(new PlaybackState.Builder().setActions(actions).setState(playing?PlaybackState.STATE_PLAYING:PlaybackState.STATE_PAUSED,position,playing?1:0).build());
        Intent state=new Intent(STATE).setPackage(getPackageName()).putExtra("title",title()).putExtra("index",index).putExtra("playing",playing).putExtra("shuffle",shuffle).putExtra("playMode",playMode).putExtra("uri",index>=0&&index<uris.size()?uris.get(index):"").putExtra("position",position).putExtra("duration",duration).putExtra("volume",volume).putExtra("error",error).putExtra("loading",loading).putExtra("track",index>=0&&index<tracks.size()?tracks.get(index).toString():"{}").putExtra("lyrics",currentLyrics).putExtra("lyricState",currentLyricState).putExtra("lyricError",currentLyricError);
        sendBroadcast(state);
        if(foreground)getSystemService(NotificationManager.class).notify(1,notification());
    }
    private PendingIntent command(String action,int request){return PendingIntent.getService(this,request,new Intent(this,PlaybackService.class).setAction(action),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);}
    private Notification notification(){
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this,CHANNEL).setSmallIcon(R.drawable.ic_notification).setContentTitle(title()).setContentText("PHS Radio 新大地播放器")
            .setContentIntent(open).setVisibility(Notification.VISIBILITY_PUBLIC).setOnlyAlertOnce(true).setOngoing(playing)
            .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_previous,"上一首",command("previous",1)).build())
            .addAction(new Notification.Action.Builder(playing?android.R.drawable.ic_media_pause:android.R.drawable.ic_media_play,playing?"暂停":"播放",command("toggle",2)).build())
            .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_next,"下一首",command("next",3)).build())
            .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0,1,2)).build();
    }
    @Override public void onDestroy(){destroyed=true;++loadGeneration;loading=false;resolver.shutdownNow();++lyricGeneration;lyricPool.shutdownNow();online.close();handler.removeCallbacksAndMessages(null);unregisterReceiver(noisy);if(player!=null){player.release();player=null;}playing=false;prepared=false;foreground=false;index=-1;publish("");audio.abandonAudioFocusRequest(focus);session.release();stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy();}
    @Override public IBinder onBind(Intent intent){return null;}
}
