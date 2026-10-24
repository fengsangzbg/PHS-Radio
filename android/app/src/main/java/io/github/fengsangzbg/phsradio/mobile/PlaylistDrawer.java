package io.github.fengsangzbg.phsradio.mobile;
import android.app.Activity;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.util.function.*;
final class PlaylistDrawer implements AutoCloseable {
    private final Activity activity;private final FrameLayout root;private final Supplier<JSONArray> local;private final Consumer<JSONObject> selected;
    private FrameLayout shade;private OnlinePages pages;
    PlaylistDrawer(Activity a,FrameLayout root,Supplier<JSONArray> local,Consumer<JSONObject> selected){activity=a;this.root=root;this.local=local;this.selected=selected;}
    void show(String platform){close();shade=new FrameLayout(activity);shade.setBackgroundColor(0x55101B2A);shade.setOnClickListener(v->close());root.addView(shade,new FrameLayout.LayoutParams(-1,-1));LinearLayout panel=new LinearLayout(activity);panel.setOrientation(1);panel.setClickable(true);int padding=dp(14);panel.setPadding(padding,dp(40),padding,dp(20));GradientDrawable glass=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{0xF0246474,0xEE102D49});glass.setCornerRadius(dp(28));glass.setStroke(dp(1),0x8884D3DF);panel.setBackground(glass);FrameLayout.LayoutParams layout=new FrameLayout.LayoutParams((int)(root.getWidth()*.9),-1,Gravity.END);shade.addView(panel,layout);
        LinearLayout tabs=new LinearLayout(activity);panel.addView(tabs);FrameLayout host=new FrameLayout(activity);panel.addView(host,new LinearLayout.LayoutParams(-1,0,1));pages=new OnlinePages(activity,host,local);pages.onPlaylist=playlist->{close();selected.accept(playlist);};
        for(String[] item:new String[][]{{"酷狗","kugou"},{"网易云","netease"},{"收起","close"}}){Button b=new AeroButton(activity);b.setText(item[0]);b.setOnClickListener(v->{if(item[1].equals("close"))close();else pages.playlists(item[1]);});tabs.addView(b,new LinearLayout.LayoutParams(0,dp(44),1));}pages.playlists(platform);panel.setTranslationX(layout.width);panel.animate().translationX(0).setDuration(300).setInterpolator(new android.view.animation.DecelerateInterpolator(1.7f)).start();
    }
    boolean back(){if(shade==null)return false;close();return true;}
    public void close(){if(pages!=null){pages.close();pages=null;}if(shade!=null){root.removeView(shade);shade=null;}}
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
}
