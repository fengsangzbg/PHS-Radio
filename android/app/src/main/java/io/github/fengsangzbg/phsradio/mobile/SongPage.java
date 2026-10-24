package io.github.fengsangzbg.phsradio.mobile;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.os.*;
import android.text.*;
import android.text.method.LinkMovementMethod;
import android.text.style.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.concurrent.*;
import org.json.*;

/** Centre page shows either the large record or the complete scrolling lyrics. */
final class SongPage implements AutoCloseable {
    private final Activity activity;
    private final ExecutorService io=Executors.newSingleThreadExecutor();
    private final Handler main=new Handler(Looper.getMainLooper());
    private ViewGroup host;
    private LinearLayout page,lyricPanel;
    private FrameLayout discPanel;
    private RecordGLView record;
    private ScrollView scroll;
    private TextView title,words,status;
    private String song="",songTitle="选择一首音乐",onlineCover="",onlineLyrics="",lyricState="idle",lyricError="";
    private LrcTimeline timeline=LrcTimeline.parse("");
    private long position,anchor,manualUntil;
    private boolean playing,lyricMode,resumed=true;
    private volatile boolean closed;
    private volatile int generation;
    private int activeLine=-2;
    private final Runnable follow=new Runnable(){public void run(){if(!closed&&resumed&&lyricMode&&page!=null){render(false);main.postDelayed(this,150);}}};
    SongPage(Activity activity){this.activity=activity;}
    void home(View ignored){}
    void mode(boolean lyrics){lyricMode=lyrics;}
    boolean showingLyrics(){return page!=null&&lyricMode;}
    void showIn(ViewGroup target){
        if(page!=null)return;host=target;page=new LinearLayout(activity);page.setOrientation(1);page.setPadding(dp(4),dp(8),dp(4),dp(8));
        title=text(songTitle,20);title.setGravity(Gravity.CENTER);title.setMaxLines(2);page.addView(title,new LinearLayout.LayoutParams(-1,-2));
        FrameLayout body=new FrameLayout(activity);page.addView(body,new LinearLayout.LayoutParams(-1,0,1));
        discPanel=new FrameLayout(activity);record=new RecordGLView(activity);record.playing(playing);discPanel.addView(record,new FrameLayout.LayoutParams(-1,-1));body.addView(discPanel,new FrameLayout.LayoutParams(-1,-1));
        lyricPanel=new LinearLayout(activity);lyricPanel.setOrientation(1);lyricPanel.setPadding(dp(12),dp(8),dp(12),dp(8));lyricPanel.setBackground(glass());
        LinearLayout bar=new LinearLayout(activity);Button back=new AeroButton(activity);back.setText("返回唱片");back.setOnClickListener(v->{lyricMode=false;displayMode();});bar.addView(back,new LinearLayout.LayoutParams(dp(92),dp(40)));TextView caption=text("歌词",16);caption.setGravity(Gravity.CENTER);bar.addView(caption,new LinearLayout.LayoutParams(0,dp(40),1));Button options=new AeroButton(activity);options.setText("···");options.setOnClickListener(v->options());bar.addView(options,new LinearLayout.LayoutParams(dp(48),dp(40)));lyricPanel.addView(bar);
        status=text("",14);status.setGravity(Gravity.CENTER);status.setOnClickListener(v->retry());lyricPanel.addView(status,new LinearLayout.LayoutParams(-1,-2));
        scroll=new ScrollView(activity);scroll.setFillViewport(true);scroll.setClipToPadding(false);words=text("",20);words.setGravity(Gravity.CENTER_HORIZONTAL);words.setLineSpacing(dp(12),1.1f);words.setPadding(dp(8),dp(64),dp(8),dp(100));words.setMovementMethod(LinkMovementMethod.getInstance());scroll.addView(words,new ScrollView.LayoutParams(-1,-2));scroll.setOnTouchListener((v,e)->{if(e.getActionMasked()==MotionEvent.ACTION_DOWN||e.getActionMasked()==MotionEvent.ACTION_MOVE)manualUntil=SystemClock.elapsedRealtime()+4500;return false;});lyricPanel.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));body.addView(lyricPanel,new FrameLayout.LayoutParams(-1,-1));
        host.addView(page,new ViewGroup.LayoutParams(-1,-1));displayMode();updateCover();
    }
    private void displayMode(){if(page==null)return;discPanel.setVisibility(lyricMode?View.GONE:View.VISIBLE);lyricPanel.setVisibility(lyricMode?View.VISIBLE:View.GONE);record.active(resumed&&!lyricMode);activeLine=-2;main.removeCallbacks(follow);if(lyricMode){render(true);if(resumed)main.post(follow);}}
    boolean back(){if(page==null)return false;++generation;main.removeCallbacks(follow);record.active(false);host.removeView(page);page=null;return true;}
    void online(String metadata,String lyric){try{JSONObject track=new JSONObject(metadata==null?"{}":metadata);String cover=track.optString("cover");if(!cover.equals(onlineCover)){onlineCover=cover;if(page!=null)updateCover();}String value=lyric==null?"":lyric;if(!value.equals(onlineLyrics)){onlineLyrics=value;readLyrics();}}catch(JSONException ignored){}}
    void lyricStatus(String state,String error){lyricState=state==null?"idle":state;lyricError=error==null?"":error;render(false);}
    void state(String uri,String name,long time,boolean playing){boolean changed=!song.equals(uri);song=uri;songTitle=name;position=time;anchor=SystemClock.elapsedRealtime();this.playing=playing;if(title!=null)title.setText(name);if(changed){manualUntil=0;readLyrics();if(page!=null)updateCover();}if(record!=null)record.playing(playing);render(false);}
    private void readLyrics(){String source=activity.getSharedPreferences("lyrics",0).getString(song,onlineLyrics);timeline=LrcTimeline.parse(source);activeLine=-2;render(true);}
    private void render(boolean force){
        if(page==null||!lyricMode)return;
        if(timeline.lines.isEmpty()){
            if(!timeline.plain.isEmpty()){status.setText("当前为非逐句歌词");words.setText(timeline.plain);return;}
            String message=song.isEmpty()?"选择一首歌，开始听音乐":lyricState.equals("loading")?"正在获取歌词…":lyricState.equals("error")?"歌词加载失败，点击重试\n"+lyricError:lyricState.equals("empty")?"平台未返回这首歌的歌词":song.startsWith("phs://")?"正在等待歌词…":"本地歌曲可通过右上角导入 LRC 歌词";
            status.setText(message);words.setText("");return;
        }
        status.setText("");long now=position+(playing&&resumed?Math.max(0,SystemClock.elapsedRealtime()-anchor):0);int selected=timeline.at(now);if(manualUntil>0&&SystemClock.elapsedRealtime()>=manualUntil){manualUntil=0;force=true;}if(!force&&selected==activeLine)return;activeLine=selected;
        SpannableStringBuilder text=new SpannableStringBuilder();int highlighted=0;
        for(int i=0;i<timeline.lines.size();i++){LrcTimeline.Line line=timeline.lines.get(i);int start=text.length();text.append(line.text).append("\n\n");final long target=line.time;text.setSpan(new ClickableSpan(){public void onClick(View view){manualUntil=0;position=target;anchor=SystemClock.elapsedRealtime();activity.startService(new Intent(activity,PlaybackService.class).setAction("seek").putExtra("position",target));render(true);}public void updateDrawState(TextPaint p){p.setUnderlineText(false);}},start,text.length(),Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);text.setSpan(new ForegroundColorSpan(i==selected?0xFFF0FDFF:0xFF86B2C1),start,text.length(),Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);if(i==selected){highlighted=start;text.setSpan(new StyleSpan(Typeface.BOLD),start,text.length(),Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);}}
        words.setText(text);final int offset=highlighted;if(SystemClock.elapsedRealtime()>=manualUntil)words.post(()->{if(page!=null&&words.getLayout()!=null){int line=words.getLayout().getLineForOffset(Math.min(offset,words.length()));int y=words.getLayout().getLineTop(line)+words.getPaddingTop()-scroll.getHeight()/3;scroll.smoothScrollTo(0,Math.max(0,y));}});
    }
    private void retry(){if(song.startsWith("phs://")){lyricState="loading";render(true);activity.startService(new Intent(activity,PlaybackService.class).setAction("lyrics"));}}
    private void options(){new AlertDialog.Builder(activity).setItems(new String[]{"重新获取在线歌词","导入 LRC 歌词"},(d,n)->{if(n==0)retry();else if(!song.isEmpty())activity.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE),13);}).show();}
    void importLyrics(Uri uri){String target=song;if(target.isEmpty())return;io.execute(()->{try(InputStream input=activity.getContentResolver().openInputStream(uri)){if(input==null)throw new IOException();ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int n;while((n=input.read(buffer))!=-1){if(out.size()+n>512000)throw new IOException();out.write(buffer,0,n);}String text=out.toString("UTF-8");LrcTimeline parsed=LrcTimeline.parse(text);if(parsed.lines.isEmpty()&&parsed.plain.isEmpty())throw new IOException();activity.getSharedPreferences("lyrics",0).edit().putString(target,text).apply();main.post(()->{if(!closed&&target.equals(song))readLyrics();});}catch(Exception error){main.post(()->Toast.makeText(activity,"无法读取歌词，请选择 UTF-8 歌词文件",Toast.LENGTH_LONG).show());}});}
    private void updateCover(){
        if(page==null||record==null)return;record.cover(null);record.playing(playing);String uri=song,remote=onlineCover;int ticket=++generation;if(uri.isEmpty())return;
        io.execute(()->{if(closed||ticket!=generation)return;Bitmap result=null;if(uri.startsWith("phs://")){try{result=CoverImages.fetch(remote);}catch(Exception ignored){}}else try(MediaMetadataRetriever retriever=new MediaMetadataRetriever()){retriever.setDataSource(activity,Uri.parse(uri));byte[] bytes=retriever.getEmbeddedPicture();if(bytes!=null){BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);options.inSampleSize=1;while(Math.max(options.outWidth,options.outHeight)/options.inSampleSize>2048)options.inSampleSize*=2;options.inJustDecodeBounds=false;result=BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);}}catch(Exception ignored){}Bitmap bitmap=result;main.post(()->{if(!closed&&ticket==generation&&page!=null)record.cover(bitmap);});});
    }
    void pause(){resumed=false;main.removeCallbacks(follow);if(record!=null){record.active(false);record.onPause();}}
    void resume(){resumed=true;if(record!=null)record.onResume();if(page!=null){record.active(!lyricMode);main.removeCallbacks(follow);if(lyricMode)main.post(follow);}}
    private TextView text(String value,int size){TextView t=new TextView(activity);t.setText(value);t.setTextSize(size);t.setTextColor(0xFFE6FAFF);t.setPadding(dp(6),dp(6),dp(6),dp(6));return t;}
    private GradientDrawable glass(){GradientDrawable d=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{0x44316E80,0x33122B46});d.setCornerRadius(dp(26));d.setStroke(dp(1),0x4486CEDA);return d;}
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    public void close(){closed=true;back();main.removeCallbacksAndMessages(null);io.shutdownNow();}
}
