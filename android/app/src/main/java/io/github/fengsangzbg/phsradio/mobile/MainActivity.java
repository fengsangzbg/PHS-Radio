package io.github.fengsangzbg.phsradio.mobile;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.*;
import android.graphics.drawable.*;
import android.net.Uri;
import android.os.*;
import android.provider.OpenableColumns;
import android.text.*;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.util.*;

public final class MainActivity extends Activity {
    private static final int MUSIC=10,VIDEO=11,IMAGE=12;
    private final List<String> uris=new ArrayList<>(),titles=new ArrayList<>();
    private final List<Integer> visible=new ArrayList<>();
    private WallpaperLayer wallpaper;
    private MusicAccounts accounts;
    private SongPage songPage;
    private PlayMeteor meteor;
    private OnlinePages pages;
    private final HomeRoute route=new HomeRoute();
    private float swipeX,swipeY;
    private boolean swipeBlocked,swipeConsumed;
    private FrameLayout onlineHost;
    private LinearLayout localScreen;
    private final Handler searchHandler=new Handler(Looper.getMainLooper());
    private Runnable searchPending;
    private boolean isLocal;
    private boolean wasPlaying;
    private boolean lyricRequested;
    private VerticalVolume sideVolume;
    private FrameLayout root;
    private LinearLayout content;
    private TextView now,summary,time;
    private Button play,mode;
    private SeekBar progress;
    private BaseAdapter adapter;
    private AquariumEntrance entrance;
    private long duration;
    private boolean seeking,registered;
    private final BroadcastReceiver state=new BroadcastReceiver(){public void onReceive(Context c,Intent i){
        now.setText(i.getStringExtra("title"));
        play.setText(i.getBooleanExtra("playing",false)?"Ⅱ":"▶");
        mode.setText(new String[]{"顺序","随机","单曲循环"}[Math.floorMod(i.getIntExtra("playMode",0),3)]);
        boolean playing=i.getBooleanExtra("playing",false);if(playing&&!wasPlaying&&getSharedPreferences("mobile",0).getBoolean("meteors",true))meteor.flash("fire".equals(wallpaper.mode()));wasPlaying=playing;
        songPage.online(i.getStringExtra("track"),i.getStringExtra("lyrics"));
        songPage.lyricStatus(i.getStringExtra("lyricState"),i.getStringExtra("lyricError"));if(sideVolume!=null)sideVolume.level(i.getFloatExtra("volume",1));
        songPage.state(i.getStringExtra("uri")==null?"":i.getStringExtra("uri"),i.getStringExtra("title"),i.getLongExtra("position",0),playing);
        duration=i.getLongExtra("duration",0);long position=i.getLongExtra("position",0);
        if(!seeking)progress.setProgress(duration>0?(int)(position*1000/duration):0);
        time.setText(format(position)+" / "+format(duration));
        String error=i.getStringExtra("error");if(error!=null&&!error.isEmpty())Toast.makeText(MainActivity.this,error,Toast.LENGTH_LONG).show();
    }};

    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        accounts=new MusicAccounts(this);accounts.onChanged=()->renderRoute();songPage=new SongPage(this);
        getWindow().setStatusBarColor(Color.TRANSPARENT);getWindow().setNavigationBarColor(Color.TRANSPARENT);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        root=new FrameLayout(this);wallpaper=new WallpaperLayer(this);root.addView(wallpaper.view(),new FrameLayout.LayoutParams(-1,-1));
        content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);content.setPadding(dp(16),dp(36),dp(16),dp(24));root.addView(content,new FrameLayout.LayoutParams(-1,-1));
        root.setOnApplyWindowInsetsListener((v,insets)->{
            if(Build.VERSION.SDK_INT>=30){android.graphics.Insets safe=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());content.setPadding(dp(16)+safe.left,dp(10)+safe.top,dp(16)+safe.right,dp(10)+safe.bottom);}
            else content.setPadding(dp(16),dp(12)+insets.getSystemWindowInsetTop(),dp(16),dp(12)+insets.getSystemWindowInsetBottom());return insets;
        });
        meteor=new PlayMeteor(this);root.addView(meteor,new FrameLayout.LayoutParams(-1,-1));
        setContentView(root);
        LinearLayout header=row();header.setGravity(Gravity.CENTER_VERTICAL);
        ImageView logo=new ImageView(this);logo.setImageResource(R.drawable.logo_phs);header.addView(logo,new LinearLayout.LayoutParams(dp(40),dp(40)));logo.setOnClickListener(v->v.animate().rotationBy(360).scaleX(.85f).scaleY(.85f).setDuration(250).withEndAction(()->v.animate().scaleX(1).scaleY(1).setInterpolator(new android.view.animation.OvershootInterpolator(3)).setDuration(400).start()).start());
        TextView brand=text("PHS Radio",19,true);brand.setOnClickListener(v->{v.animate().rotationBy(360).scaleX(1.08f).scaleY(.94f).setDuration(330).withEndAction(()->v.animate().scaleX(1).scaleY(1).setInterpolator(new android.view.animation.OvershootInterpolator()).setDuration(300).start()).start();});header.addView(brand,new LinearLayout.LayoutParams(0,dp(54),1));
        Button account=button("账号",()->accounts.show());header.addView(account,new LinearLayout.LayoutParams(dp(68),dp(46)));
        Button theme=button("主题",()->showThemes());header.addView(theme,new LinearLayout.LayoutParams(dp(76),dp(46)));content.addView(header);
        LinearLayout actions=row();actions.setPadding(0,dp(10),0,dp(8));
        EditText search=new EditText(this);search.setSingleLine();search.setTextColor(Color.WHITE);search.setHintTextColor(0xFFA8CACF);search.setHint("搜索本地、酷狗、网易云歌曲或歌手");search.setTextSize(14);search.setPadding(dp(16),0,dp(12),0);search.setBackground(glass(24));search.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH);actions.addView(search,new LinearLayout.LayoutParams(0,dp(46),1));content.addView(actions);
        LinearLayout nav=row();content.addView(nav);
        LinearLayout bodyRow=row();content.addView(bodyRow,new LinearLayout.LayoutParams(-1,0,1));FrameLayout body=new FrameLayout(this);bodyRow.addView(body,new LinearLayout.LayoutParams(0,-1,1));sideVolume=new VerticalVolume(this,value->command(new Intent(this,PlaybackService.class).setAction("volume").putExtra("volume",value),false));sideVolume.level(getSharedPreferences("mobile",0).getFloat("volume",1));LinearLayout.LayoutParams rail=new LinearLayout.LayoutParams(dp(32),dp(168));rail.gravity=Gravity.CENTER_VERTICAL;rail.leftMargin=dp(6);bodyRow.addView(sideVolume,rail);onlineHost=new FrameLayout(this);body.addView(onlineHost,new FrameLayout.LayoutParams(-1,-1));
        localScreen=new LinearLayout(this);localScreen.setOrientation(1);localScreen.setVisibility(View.GONE);body.addView(localScreen,new FrameLayout.LayoutParams(-1,-1));
        pages=new OnlinePages(this,onlineHost,()->queue());pages.onCentre=this::selectCentre;pages.onConnect=()->accounts.show();pages.onRouteSwipe=dx->{if(!route.swipe(dx))return false;lyricRequested=false;renderRoute();onlineHost.setTranslationX(dx<0?dp(38):-dp(38));onlineHost.setAlpha(.6f);onlineHost.animate().translationX(0).alpha(1).setDuration(240).start();return true;};
        String[] tabs={"酷狗歌单","酷狗推荐","唱片歌词","网易推荐","网易歌单"};for(int i=0;i<tabs.length;i++){final int selected=i;Button tab=button(tabs[i],()->{searchHandler.removeCallbacksAndMessages(null);lyricRequested=false;route.select(selected);renderRoute();});tab.setTextSize(11);nav.addView(tab,new LinearLayout.LayoutParams(0,dp(42),1));}
        summary=text("本地音乐",13,false);localScreen.addView(summary);localScreen.addView(button("导入本地音乐",()->choose(MUSIC,"audio/*")),new LinearLayout.LayoutParams(-1,dp(44)));
        ListView list=new ListView(this);list.setBackgroundColor(Color.TRANSPARENT);list.setDivider(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));list.setDividerHeight(dp(9));
        adapter=new BaseAdapter(){public int getCount(){return visible.size();}public Object getItem(int n){return titles.get(visible.get(n));}public long getItemId(int n){return visible.get(n);}public View getView(int n,View recycled,ViewGroup parent){TextView label=recycled instanceof TextView?(TextView)recycled:text("",16,false);label.setSingleLine();label.setEllipsize(TextUtils.TruncateAt.END);label.setGravity(Gravity.CENTER_VERTICAL);label.setPadding(dp(20),dp(12),dp(20),dp(12));label.setMinHeight(dp(72));label.setBackground(glass(22));label.setText(titles.get(visible.get(n)));return label;}};
        list.setAdapter(adapter);list.setOnItemClickListener((parent,view,position,id)->start(visible.get(position)));localScreen.addView(list,new LinearLayout.LayoutParams(-1,0,1));
        search.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int start,int count,int after){}public void onTextChanged(CharSequence value,int start,int before,int count){if(searchPending!=null)searchHandler.removeCallbacks(searchPending);String query=value.toString();searchPending=()->{songPage.back();showOnline();pages.search(query);};searchHandler.postDelayed(searchPending,550);}public void afterTextChanged(Editable e){}});
        search.setOnEditorActionListener((v,action,event)->{if(action==android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH){searchHandler.removeCallbacksAndMessages(null);songPage.back();showOnline();pages.search(search.getText().toString());((android.view.inputmethod.InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(search.getWindowToken(),0);return true;}return false;});
        renderRoute();
        createDock();songPage.home(content);restoreQueue();handleShare(getIntent());
        // The aquarium scene sits on top of the finished UI, exactly like the
        // Windows build: tap to zoom in, then drop the view entirely.
        entrance=new AquariumEntrance(this);
        entrance.onEntered=()->{root.removeView(entrance);entrance=null;};
        root.addView(entrance,new FrameLayout.LayoutParams(-1,-1));
    }
    @Override public boolean dispatchTouchEvent(MotionEvent e){
        if(e.getActionMasked()==MotionEvent.ACTION_DOWN){swipeX=e.getX();swipeY=e.getY();swipeConsumed=false;swipeBlocked=isLocal||content==null||content.getVisibility()!=View.VISIBLE||inHorizontalScroll(getWindow().getDecorView(),e.getRawX(),e.getRawY());}
        if(e.getActionMasked()==MotionEvent.ACTION_MOVE&&!swipeBlocked&&!swipeConsumed&&pages!=null){float dx=e.getX()-swipeX,dy=e.getY()-swipeY;if(Math.abs(dx)>dp(84)&&Math.abs(dy)<Math.abs(dx)*.45f){if(pages.swipe(dx)){MotionEvent cancel=MotionEvent.obtain(e);cancel.setAction(MotionEvent.ACTION_CANCEL);super.dispatchTouchEvent(cancel);cancel.recycle();swipeConsumed=true;return true;}}}
        if(swipeConsumed){if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL)swipeConsumed=false;return true;}return super.dispatchTouchEvent(e);
    }
    private boolean inHorizontalScroll(View view,float x,float y){if(view.getVisibility()!=View.VISIBLE)return false;int[] at=new int[2];view.getLocationOnScreen(at);if(x<at[0]||x>at[0]+view.getWidth()||y<at[1]||y>at[1]+view.getHeight())return false;if(view instanceof HorizontalScrollView||view instanceof SeekBar||view instanceof EditText||view instanceof VerticalVolume)return true;if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=group.getChildCount()-1;i>=0;i--)if(inHorizontalScroll(group.getChildAt(i),x,y))return true;}return false;}
    private void selectCentre(){lyricRequested=false;route.select(HomeRoute.CENTRE);renderRoute();}
    private void selectLyrics(){lyricRequested=true;route.select(HomeRoute.CENTRE);renderRoute();}
    private void renderRoute(){songPage.mode(lyricRequested);showOnline();songPage.back();if(route.index()==0)pages.playlists("kugou");else if(route.index()==1)pages.home("kugou");else if(route.index()==2)pages.songPage(songPage);else if(route.index()==3)pages.home("netease");else pages.playlists("netease");}
    private void showOnline(){isLocal=false;onlineHost.setVisibility(View.VISIBLE);localScreen.setVisibility(View.GONE);}
    @Override public void onBackPressed(){if(songPage.showingLyrics()){selectCentre();return;}if(isLocal){selectCentre();}else if(pages.isRoutePage()){if(route.index()!=2)selectCentre();else super.onBackPressed();}else if(!pages.back())selectCentre();}
    private void createDock(){
        LinearLayout dock=new LinearLayout(this);dock.setOrientation(1);dock.setPadding(dp(10),dp(6),dp(10),dp(4));dock.setBackground(glass(26));
        now=text("选择一首音乐",15,true);now.setSingleLine();now.setEllipsize(TextUtils.TruncateAt.END);now.setOnClickListener(v->selectCentre());dock.addView(now,new LinearLayout.LayoutParams(-1,dp(26)));
        LinearLayout transport=row();transport.setGravity(Gravity.CENTER_VERTICAL);mode=button("顺序",()->send("shuffle"));mode.setTextSize(12);transport.addView(mode,new LinearLayout.LayoutParams(0,dp(40),1.3f));transport.addView(button("❮",()->send("previous")),new LinearLayout.LayoutParams(0,dp(40),.8f));play=button("▶",()->send("toggle"));transport.addView(play,new LinearLayout.LayoutParams(0,dp(44),1));transport.addView(button("❯",()->send("next")),new LinearLayout.LayoutParams(0,dp(40),.8f));transport.addView(button("停止",()->send("stop")),new LinearLayout.LayoutParams(0,dp(40),1));transport.addView(button("歌词",()->selectLyrics()),new LinearLayout.LayoutParams(0,dp(40),1));dock.addView(transport);
        LinearLayout progressRow=row();progressRow.setGravity(Gravity.CENTER_VERTICAL);time=text("0:00 / 0:00",10,false);time.setTextColor(0xFFB2DBDE);progressRow.addView(time,new LinearLayout.LayoutParams(dp(85),dp(28)));progress=new SeekBar(this);progress.setMax(1000);progressRow.addView(progress,new LinearLayout.LayoutParams(0,dp(30),1));dock.addView(progressRow);
        progress.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int p,boolean user){}public void onStartTrackingTouch(SeekBar s){seeking=true;}public void onStopTrackingTouch(SeekBar s){seeking=false;command(new Intent(MainActivity.this,PlaybackService.class).setAction("seek").putExtra("position",duration*s.getProgress()/1000),false);}});
        LinearLayout.LayoutParams size=new LinearLayout.LayoutParams(-1,-2);size.topMargin=dp(6);content.addView(dock,size);
    }
    private void choose(int request,String mime){Intent pick=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType(mime).addCategory(Intent.CATEGORY_OPENABLE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);if(request==MUSIC)pick.putExtra(Intent.EXTRA_ALLOW_MULTIPLE,true);startActivityForResult(pick,request);}
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(result!=RESULT_OK||data==null)return;
        if(request==13){if(data.getData()!=null)songPage.importLyrics(data.getData());return;}
        List<Uri> chosen=new ArrayList<>();if(data.getClipData()!=null){for(int n=0;n<data.getClipData().getItemCount();n++)chosen.add(data.getClipData().getItemAt(n).getUri());}else if(data.getData()!=null)chosen.add(data.getData());
        for(Uri uri:chosen){try{getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(SecurityException ignored){}
            if(request==MUSIC){if(!uris.contains(uri.toString())){uris.add(uri.toString());titles.add(displayName(uri));}}
            else wallpaper.set(request==VIDEO?"video":"image",uri.toString());
        }
        if(request==MUSIC){getSharedPreferences("mobile",0).edit().putString("queue",queue().toString()).apply();filter("");}
    }
    private String displayName(Uri uri){try(Cursor cursor=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(cursor!=null&&cursor.moveToFirst())return cursor.getString(0);}catch(Exception ignored){}return "音乐";}
    private JSONArray queue(){JSONArray queue=new JSONArray();for(int n=0;n<uris.size();n++){JSONObject item=new JSONObject();try{item.put("uri",uris.get(n));item.put("title",titles.get(n));queue.put(item);}catch(JSONException ignored){}}return queue;}
    private void restoreQueue(){try{JSONArray queue=new JSONArray(getSharedPreferences("mobile",0).getString("queue","[]"));for(int n=0;n<queue.length();n++){JSONObject song=queue.getJSONObject(n);uris.add(song.getString("uri"));titles.add(song.getString("title"));}}catch(Exception ignored){}filter("");}
    private void filter(String query){visible.clear();String key=query.trim().toLowerCase(Locale.ROOT);for(int n=0;n<titles.size();n++)if(titles.get(n).toLowerCase(Locale.ROOT).contains(key))visible.add(n);adapter.notifyDataSetChanged();summary.setText(uris.isEmpty()?"尚未导入本地音乐":"音乐库 "+uris.size()+" 首 · 显示 "+visible.size()+" 首");}
    private void start(int index){
        if(Build.VERSION.SDK_INT>=33&&checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},40);
        command(new Intent(this,PlaybackService.class).setAction("play").putExtra("queue",queue().toString()).putExtra("index",index),true);
    }
    private void send(String action){if("toggle".equals(action)&&play.getText().equals("▶")&&now.getText().equals("选择一首音乐")&&!uris.isEmpty())start(0);else command(new Intent(this,PlaybackService.class).setAction(action),false);}
    private void command(Intent intent,boolean foreground){if(foreground)startForegroundService(intent);else startService(intent);}
    private void showThemes(){
        Dialog dialog=new Dialog(this);LinearLayout panel=new LinearLayout(this);panel.setOrientation(LinearLayout.VERTICAL);panel.setPadding(dp(22),dp(22),dp(22),dp(18));panel.setBackground(glass(30));
        TextView title=text("你的背景，你的节奏",22,true);panel.addView(title);TextView hint=text("在星空视频壁纸中设好桌面动态壁纸后，可在这里跟随显示。系统壁纸的声音由壁纸应用管理；导入视频在本软件内始终静音。",14,false);hint.setTextColor(0xFFC0DEE1);hint.setPadding(0,dp(14),0,dp(16));panel.addView(hint);
        addTheme(panel,"显示桌面动态壁纸",()->{wallpaper.set("system","");dialog.dismiss();});
        addTheme(panel,"打开系统动态壁纸选择器",()->{try{startActivity(new Intent(android.app.WallpaperManager.ACTION_LIVE_WALLPAPER_CHOOSER));}catch(ActivityNotFoundException e){Toast.makeText(this,"请在星空视频壁纸中设置桌面壁纸",Toast.LENGTH_LONG).show();}dialog.dismiss();});
        addTheme(panel,"导入视频背景 · 静音",()->{choose(VIDEO,"video/*");dialog.dismiss();});
        addTheme(panel,"导入图片",()->{choose(IMAGE,"image/*");dialog.dismiss();});
        addTheme(panel,"火流星 · 盛夏夜空",()->{wallpaper.set("fire","");dialog.dismiss();});
        addTheme(panel,"播放流星：开启 / 关闭",()->{boolean enabled=!getSharedPreferences("mobile",0).getBoolean("meteors",true);getSharedPreferences("mobile",0).edit().putBoolean("meteors",enabled).apply();if(!enabled)meteor.stop();Toast.makeText(this,enabled?"流星已开启":"流星已关闭",Toast.LENGTH_SHORT).show();});
        addTheme(panel,"默认水光",()->{wallpaper.set("water","");dialog.dismiss();});
        dialog.setContentView(panel);dialog.getWindow().setBackgroundDrawableResource(android.R.color.transparent);dialog.show();dialog.getWindow().setLayout(Math.min(getResources().getDisplayMetrics().widthPixels-dp(32),dp(480)),-2);
    }
    private void addTheme(LinearLayout panel,String label,Runnable callback){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,dp(50));p.topMargin=dp(8);panel.addView(button(label,callback),p);}
    @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);handleShare(intent);}
    private void handleShare(Intent intent){if(Intent.ACTION_SEND.equals(intent.getAction())){Uri uri=intent.getParcelableExtra(Intent.EXTRA_STREAM);String mime=intent.getType();if(uri!=null&&mime!=null&&(mime.startsWith("video/")||mime.startsWith("image/")))wallpaper.importShared(uri,mime);}}
    @Override protected void onStart(){super.onStart();if(Build.VERSION.SDK_INT>=33)registerReceiver(state,new IntentFilter(PlaybackService.STATE),Context.RECEIVER_NOT_EXPORTED);else registerReceiver(state,new IntentFilter(PlaybackService.STATE));registered=true;send("status");}
    @Override protected void onResume(){super.onResume();accounts.resumePolling();wallpaper.resume();songPage.resume();pages.resume();}
    @Override protected void onPause(){pages.pause();meteor.stop();songPage.pause();wallpaper.pause();super.onPause();}
    @Override protected void onStop(){if(registered){unregisterReceiver(state);registered=false;}super.onStop();}
    @Override protected void onDestroy(){searchHandler.removeCallbacksAndMessages(null);pages.close();songPage.close();accounts.close();wallpaper.close();super.onDestroy();}
    private LinearLayout row(){LinearLayout view=new LinearLayout(this);view.setOrientation(LinearLayout.HORIZONTAL);return view;}
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private TextView text(String label,int size,boolean bold){TextView view=new TextView(this);view.setText(label);view.setTextColor(0xFFF0FCFF);view.setTextSize(size);if(bold)view.setTypeface(null,android.graphics.Typeface.BOLD);return view;}
    private GradientDrawable glass(int radius){GradientDrawable shape=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{0x553C727A,0x5020304A,0x88202D43});shape.setCornerRadius(dp(radius));shape.setStroke(dp(1),0x558BDFE3);return shape;}
    private Button button(String label,Runnable callback){Button view=new AeroButton(this);view.setText(label);view.setTextColor(0xFFF0FCFF);view.setTextSize(14);view.setAllCaps(false);view.setMinWidth(0);view.setMinimumWidth(0);view.setPadding(dp(8),0,dp(8),0);view.setBackground(glass(22));view.setOnClickListener(v->callback.run());return view;}
    private static String format(long ms){long seconds=ms/1000;return String.format(Locale.ROOT,"%d:%02d",seconds/60,seconds%60);}
}
