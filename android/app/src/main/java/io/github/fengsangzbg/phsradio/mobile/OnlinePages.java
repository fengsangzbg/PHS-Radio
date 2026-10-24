package io.github.fengsangzbg.phsradio.mobile;

import android.app.Activity;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.text.*;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Supplier;

/** One page host: home -> platform playlists -> playlist tracks. Dock stays outside. */
final class OnlinePages implements AutoCloseable {
    private final Activity activity;
    private final FrameLayout host;
    private final OnlineApi api;
    private final CoverImages images=new CoverImages();
    private final ExecutorService network=Executors.newFixedThreadPool(3);
    private final Handler main=new Handler(Looper.getMainLooper());
    private final Supplier<JSONArray> local;
    private volatile int generation;
    private boolean closed,home;
    private Runnable back;
    Runnable onCentre,onConnect;
    private String homePlatform="kugou";
    
    java.util.function.Consumer<JSONObject> onPlaylist;
    java.util.function.Predicate<Float> onRouteSwipe;
    private boolean routePage;
    OnlinePages(Activity a,FrameLayout host,Supplier<JSONArray> local){activity=a;this.host=host;this.local=local;api=new OnlineApi(a);}
    void songPage(SongPage song){begin();routePage=true;back=null;song.showIn(host);}
    void home(String platform){homePlatform=platform;home();}
    boolean swipe(float dx){return routePage&&onRouteSwipe!=null&&onRouteSwipe.test(dx);}
    void home(){
        int ticket=begin();routePage=true;back=null;
        LinearLayout page=column();ScrollView scroll=new ScrollView(activity);scroll.setFillViewport(true);scroll.addView(page);host.addView(scroll);
        page.addView(text("今天，听点什么",26,true));page.addView(text(name(homePlatform)+" · 每日推荐",14,false));
        for(String platform:new String[]{homePlatform}){
            LinearLayout card=column();card.setPadding(dp(14),dp(14),dp(14),dp(14));card.setBackground(glass());LinearLayout.LayoutParams cardSize=new LinearLayout.LayoutParams(-1,-2);cardSize.topMargin=dp(14);page.addView(card,cardSize);
            LinearLayout heading=new LinearLayout(activity);TextView title=text(name(platform)+" · 每日推荐",18,true);heading.addView(title,new LinearLayout.LayoutParams(0,-2,1));heading.addView(button("刷新",()->home()),new LinearLayout.LayoutParams(dp(62),dp(40)));card.addView(heading);
            TextView status=text(api.logged(platform)?"正在读取今日推荐…":"登录后查看你的每日推荐",13,false);card.addView(status);
            if(!api.logged(platform)){card.addView(button("连接音乐平台",()->{if(onConnect!=null)onConnect.run();}));continue;}
            HorizontalScrollView slider=new HorizontalScrollView(activity);LinearLayout strip=new LinearLayout(activity);slider.addView(strip);card.addView(slider);
            job(ticket,status,()->{JSONArray songs=api.recommendations(platform);main.post(()->{if(valid(ticket)){status.setText("今日为你推荐 "+songs.length()+" 首");for(int i=0;i<songs.length();i++){JSONObject song=songs.optJSONObject(i);final int index=i;GlassRow tile=new GlassRow(activity);tile.vertical();tile.setPadding(dp(6),dp(6),dp(6),dp(6));ImageView image=cover(song);tile.addView(image,new LinearLayout.LayoutParams(dp(110),dp(110)));TextView label=text(song.optString("title"),15,true);label.setSingleLine();label.setEllipsize(TextUtils.TruncateAt.END);tile.addView(label);TextView artist=text(song.optString("artist"),12,false);artist.setSingleLine();tile.addView(artist);tile.setOnClickListener(v->play(songs,index));LinearLayout.LayoutParams size=new LinearLayout.LayoutParams(dp(124),-2);size.rightMargin=dp(8);strip.addView(tile,size);}}});});
        }
    }
    void playlists(String platform){
        int ticket=begin();routePage=true;back=onCentre!=null?onCentre:this::home;LinearLayout page=page(name(platform)+"歌单",back);TextView status=text("正在加载全部歌单…",13,false);page.addView(status);ListView list=list(page);ArrayList<JSONObject> rows=new ArrayList<>();Rows adapter=new Rows(rows,true);list.setAdapter(adapter);list.setOnItemClickListener((p,v,pos,id)->{if(onPlaylist!=null)onPlaylist.accept(rows.get(pos));else tracks(rows.get(pos));});
        if(!api.logged(platform)){status.setText("请先在右上角「账号」登录"+name(platform));return;}
        job(ticket,status,()->{Set<String> seen=new HashSet<>();for(int n=0;n<1000&&valid(ticket);n++){JSONArray batch=api.playlists(platform,n);ArrayList<JSONObject> added=new ArrayList<>();for(int i=0;i<batch.length();i++){JSONObject row=batch.getJSONObject(i);if(seen.add(row.getString("id")))added.add(row);}if(batch.length()>0&&added.isEmpty())throw new IOException("平台分页重复，歌单尚未完整加载");main.post(()->{if(valid(ticket)){rows.addAll(added);adapter.notifyDataSetChanged();status.setText("已读取 "+rows.size()+" 个歌单，继续加载…");}});if(batch.length()<100){main.post(()->{if(valid(ticket))status.setText("共 "+rows.size()+" 个歌单 · 点击封面查看歌曲");});return;}}if(valid(ticket))throw new IOException("歌单过多，读取未完成");});
    }
    void tracks(JSONObject playlist){
        String platform=playlist.optString("platform");int ticket=begin();back=()->playlists(platform);LinearLayout page=page(playlist.optString("title"),back);TextView status=text("正在读取完整歌曲列表…",13,false);page.addView(status);EditText search=searchBox("搜索此歌单中的歌曲、歌手");page.addView(search,new LinearLayout.LayoutParams(-1,dp(44)));ListView list=list(page);ArrayList<JSONObject> all=new ArrayList<>(),visible=new ArrayList<>();Rows adapter=new Rows(visible,false);list.setAdapter(adapter);
        list.setOnItemClickListener((p,v,pos,id)->{JSONObject selected=visible.get(pos);int original=all.indexOf(selected);play(json(all),original);});
        search.addTextChangedListener(watcher(query->{visible.clear();String key=query.trim().toLowerCase(Locale.ROOT);for(JSONObject song:all)if((song.optString("title")+" "+song.optString("artist")).toLowerCase(Locale.ROOT).contains(key))visible.add(song);adapter.notifyDataSetChanged();status.setText("共 "+all.size()+" 首 · 匹配 "+visible.size()+" 首");}));
        job(ticket,status,()->{api.tracks(playlist,()->!valid(ticket),batch->main.post(()->{if(!valid(ticket))return;for(int i=0;i<batch.length();i++)all.add(batch.optJSONObject(i));String key=search.getText().toString().trim().toLowerCase(Locale.ROOT);visible.clear();for(JSONObject song:all)if((song.optString("title")+" "+song.optString("artist")).toLowerCase(Locale.ROOT).contains(key))visible.add(song);adapter.notifyDataSetChanged();status.setText("已载入 "+all.size()+" 首，继续加载…");}));main.post(()->{if(valid(ticket))status.setText("共 "+all.size()+" 首 · 点击歌曲播放");});});
    }
    void search(String query){
        if(query.trim().isEmpty()){home();return;}int ticket=begin();back=onCentre!=null?onCentre:this::home;LinearLayout page=page("猜你想找 · "+query,back);TextView status=text("正在搜索本地、酷狗与网易云…",13,false);page.addView(status);ListView list=list(page);ArrayList<JSONObject> rows=new ArrayList<>();Rows adapter=new Rows(rows,false);list.setAdapter(adapter);list.setOnItemClickListener((p,v,pos,id)->play(json(rows),pos));
        JSONArray imported=local.get();for(int i=0;i<imported.length();i++){JSONObject song=imported.optJSONObject(i);if(song.optString("title").toLowerCase(Locale.ROOT).contains(query.toLowerCase(Locale.ROOT)))rows.add(song);}adapter.notifyDataSetChanged();
        // Sources fail independently, so local results survive an unavailable online platform.
        final int[] remaining={2};final StringBuilder errors=new StringBuilder();
        for(String platform:new String[]{"kugou","netease"})network.execute(()->{JSONArray result=new JSONArray();String failure="";try{result=api.search(platform,query,0);}catch(Exception e){failure=name(platform)+"搜索失败";}JSONArray found=result;String error=failure;main.post(()->{if(!valid(ticket))return;for(int i=0;i<found.length();i++)rows.add(found.optJSONObject(i));adapter.notifyDataSetChanged();if(!error.isEmpty())errors.append(error).append("；");remaining[0]--;status.setText((remaining[0]>0?"正在搜索 · ":"搜索完成 · ")+rows.size()+" 首"+(errors.length()>0?"\n"+errors:""));});});
    }
    boolean isRoutePage(){return routePage;}
    boolean back(){if(back==null)return false;back.run();return true;}
    void play(JSONArray queue,int index){if(index<0||index>=queue.length())return;Toast.makeText(activity,"正在获取可播音源…",Toast.LENGTH_SHORT).show();network.execute(()->{try{File file=new File(activity.getCacheDir(),"play-queue-"+UUID.randomUUID()+".json");try(Writer writer=new OutputStreamWriter(new FileOutputStream(file),java.nio.charset.StandardCharsets.UTF_8)){writer.write(queue.toString());}main.post(()->{if(!closed){if(Build.VERSION.SDK_INT>=33&&activity.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED)activity.requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},40);activity.startForegroundService(new Intent(activity,PlaybackService.class).setAction("play").putExtra("queueFile",file.getAbsolutePath()).putExtra("index",index));}else file.delete();});}catch(Exception e){main.post(()->Toast.makeText(activity,"无法建立播放队列，请重试",Toast.LENGTH_LONG).show());}});}
    private interface Task{void run()throws Exception;}
    private void job(int ticket,TextView status,Task task){network.execute(()->{try{if(valid(ticket))task.run();}catch(Exception e){main.post(()->{if(valid(ticket))status.setText(OnlineApi.message(e));});}});}
    private int begin(){generation++;home=false;routePage=false;host.removeAllViews();return generation;}
    private boolean valid(int ticket){return !closed&&generation==ticket;}
    private LinearLayout page(String title,Runnable back){LinearLayout page=column();LinearLayout bar=new LinearLayout(activity);bar.addView(button("返回",back),new LinearLayout.LayoutParams(dp(64),dp(40)));TextView name=text(title,19,true);name.setMaxLines(2);bar.addView(name,new LinearLayout.LayoutParams(0,-2,1));page.addView(bar);host.addView(page,new FrameLayout.LayoutParams(-1,-1));return page;}
    private final class Rows extends BaseAdapter{
        final ArrayList<JSONObject> rows;final boolean playlists;Rows(ArrayList<JSONObject> rows,boolean playlists){this.rows=rows;this.playlists=playlists;}public int getCount(){return rows.size();}public Object getItem(int p){return rows.get(p);}public long getItemId(int p){return p;}
        public View getView(int pos,View recycled,ViewGroup parent){GlassRow row;if(recycled instanceof GlassRow)row=(GlassRow)recycled;else{row=new GlassRow(activity);row.setGravity(Gravity.CENTER_VERTICAL);row.setPadding(dp(8),dp(6),dp(10),dp(6));ImageView image=new ImageView(activity);image.setScaleType(ImageView.ScaleType.CENTER_CROP);row.addView(image,new LinearLayout.LayoutParams(dp(44),dp(44)));TextView label=text("",14,true);label.setMaxLines(2);label.setPadding(dp(10),0,0,0);row.addView(label,new LinearLayout.LayoutParams(0,-2,1));}JSONObject data=rows.get(pos);images.load((ImageView)row.getChildAt(0),data);((TextView)row.getChildAt(1)).setText(data.optString("title")+(playlists?"":"\n"+data.optString("artist"))+"\n"+(data.optString("platform").isEmpty()?"本地音乐":name(data.optString("platform")))+(data.optBoolean("unavailable")?" · 信息待补全":""));return row;}
    }
    private ListView list(LinearLayout page){ListView list=new ListView(activity);list.setDivider(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));list.setDividerHeight(dp(8));list.setOnScrollListener(new android.widget.AbsListView.OnScrollListener(){public void onScrollStateChanged(android.widget.AbsListView v,int state){}public void onScroll(android.widget.AbsListView v,int first,int count,int total){GlassRow.kick(.55f);}});page.addView(list,new LinearLayout.LayoutParams(-1,0,1));return list;}
    private ImageView cover(JSONObject song){ImageView view=new ImageView(activity);view.setScaleType(ImageView.ScaleType.CENTER_CROP);images.load(view,song);return view;}
    private ImageView cover(String url){ImageView view=new ImageView(activity);view.setScaleType(ImageView.ScaleType.CENTER_CROP);images.load(view,url);return view;}
    private LinearLayout column(){LinearLayout v=new LinearLayout(activity);v.setOrientation(1);return v;}
    private TextView text(String value,int size,boolean bold){TextView t=new TextView(activity);t.setText(value);t.setTextSize(size);t.setTextColor(0xFFE6FAFF);t.setPadding(dp(6),dp(8),dp(6),dp(8));if(bold)t.setTypeface(null,Typeface.BOLD);return t;}
    private Button button(String title,Runnable action){Button b=new AeroButton(activity);b.setText(title);b.setOnClickListener(v->action.run());return b;}
    private EditText searchBox(String hint){EditText e=new EditText(activity);e.setSingleLine();e.setTextColor(Color.WHITE);e.setHintTextColor(0xFF9CC4CE);e.setHint(hint);e.setTextSize(14);e.setPadding(dp(14),0,dp(14),0);e.setBackground(glass());return e;}
    private GradientDrawable glass(){GradientDrawable d=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{0x66327683,0x55133250});d.setCornerRadius(dp(24));d.setStroke(dp(1),0x557DCAD6);return d;}
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    private static String name(String platform){return platform.equals("kugou")?"酷狗音乐":"网易云音乐";}
    private static JSONArray json(List<JSONObject> rows){JSONArray out=new JSONArray();for(JSONObject row:rows)out.put(row);return out;}
    private static TextWatcher watcher(java.util.function.Consumer<String> action){return new TextWatcher(){public void beforeTextChanged(CharSequence s,int start,int count,int after){}public void onTextChanged(CharSequence s,int start,int before,int count){action.accept(s.toString());}public void afterTextChanged(Editable e){}};}
    void pause(){}void resume(){}
    public void close(){closed=true;generation++;main.removeCallbacksAndMessages(null);network.shutdownNow();images.close();api.close();}
}
