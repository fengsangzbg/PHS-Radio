package io.github.fengsangzbg.phsradio.mobile;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.*;
import org.json.JSONObject;
import java.util.*;

/**
 * The in-app collection, independent of both music platforms.
 *
 * It is a full-screen glass sheet over the player: tap a row to play, long-press
 * to take the song out. Opened from a long-press on the dock heart, or from the
 * settings panel, so the collection is reachable even when nothing is playing.
 */
final class FavoritesView extends FrameLayout {
    interface Host {
        List<JSONObject> tracks();
        void play(JSONObject track);
        void remove(JSONObject track);
        void close();
    }

    private final Activity activity;
    private final Host host;
    private final List<JSONObject> items = new ArrayList<>();
    private final ListView list;
    private final TextView count;
    private final TextView empty;
    private final BaseAdapter adapter;

    FavoritesView(Activity activity, Host host) {
        super(activity);
        this.activity = activity;
        this.host = host;
        setBackgroundColor(0x6600070D);
        setClickable(true);
        setOnClickListener(v -> host.close());

        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setClickable(true);
        panel.setPadding(dp(18), dp(14), dp(18), dp(10));
        GradientDrawable glass = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xF01B3D4E, 0xF0091A29});
        glass.setCornerRadius(dp(26));
        glass.setStroke(dp(1), 0x9986D8E4);
        panel.setBackground(glass);

        LinearLayout header = new LinearLayout(activity);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text("新大地收藏", 20, true);
        header.addView(title, new LinearLayout.LayoutParams(0, dp(44), 1));
        count = text("", 12, false);
        count.setTextColor(0xFF9FDDEA);
        header.addView(count, new LinearLayout.LayoutParams(dp(56), dp(44)));
        Button close = new AeroButton(activity);
        close.setText("×");
        close.setTextSize(17);
        close.setOnClickListener(v -> host.close());
        header.addView(close, new LinearLayout.LayoutParams(dp(44), dp(38)));
        panel.addView(header);

        empty = text("还没有收藏。\n在播放时点 Dock 上的爱心，歌曲会存到这里。", 13, false);
        empty.setGravity(Gravity.CENTER);
        empty.setTextColor(0xFF9FBCCB);
        empty.setVisibility(View.GONE);
        panel.addView(empty, new LinearLayout.LayoutParams(-1, 0, 1));

        list = new ListView(activity);
        list.setBackgroundColor(Color.TRANSPARENT);
        list.setDivider(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        list.setDividerHeight(dp(8));
        list.setClipToPadding(false);
        list.setPadding(0, dp(6), 0, 0);
        adapter = new BaseAdapter() {
            public int getCount() { return items.size(); }
            public Object getItem(int position) { return items.get(position); }
            public long getItemId(int position) { return position; }
            public View getView(int position, View recycled, ViewGroup parent) {
                GlassRow row;
                LinearLayout info;
                TextView name, meta, badge;
                if (recycled instanceof GlassRow) {
                    row = (GlassRow) recycled;
                    info = (LinearLayout) row.getChildAt(0);
                    name = (TextView) info.getChildAt(0);
                    meta = (TextView) info.getChildAt(1);
                    badge = (TextView) row.getChildAt(1);
                } else {
                    row = new GlassRow(activity);
                    row.setPadding(dp(14), dp(10), dp(14), dp(10));
                    info = new LinearLayout(activity);
                    info.setOrientation(LinearLayout.VERTICAL);
                    name = text("", 15, true);
                    name.setSingleLine();
                    name.setEllipsize(TextUtils.TruncateAt.END);
                    meta = text("", 12, false);
                    meta.setTextColor(0xFFA9CFDA);
                    meta.setSingleLine();
                    meta.setEllipsize(TextUtils.TruncateAt.END);
                    info.addView(name);
                    info.addView(meta);
                    row.addView(info, new LinearLayout.LayoutParams(0, -2, 1));
                    badge = text("", 11, false);
                    badge.setTextColor(0xFF9FDDEA);
                    badge.setGravity(Gravity.CENTER);
                    row.addView(badge, new LinearLayout.LayoutParams(dp(54), -1));
                }
                JSONObject track = items.get(position);
                String nameText = track.optString("title");
                if (nameText.isEmpty()) nameText = track.optString("artist");
                if (nameText.isEmpty()) nameText = "未命名歌曲";
                name.setText(nameText);
                String artist = track.optString("artist");
                meta.setText(artist.isEmpty() ? "新大地收藏" : artist);
                badge.setText(platform(track.optString("platform")));
                return row;
            }
        };
        list.setAdapter(adapter);
        list.setOnItemClickListener((parent, view, position, id) -> host.play(items.get(position)));
        list.setOnItemLongClickListener((parent, view, position, id) -> {
            host.remove(items.get(position));
            refresh();
            return true;
        });
        panel.addView(list, new LinearLayout.LayoutParams(-1, 0, 1));

        int width = (int) (getResources().getDisplayMetrics().widthPixels * .92f);
        int height = (int) (getResources().getDisplayMetrics().heightPixels * .70f);
        addView(panel, new FrameLayout.LayoutParams(width, height, Gravity.CENTER));
        refresh();
    }

    void refresh() {
        items.clear();
        items.addAll(host.tracks());
        if (adapter != null) adapter.notifyDataSetChanged();
        count.setText(items.size() + " 首");
        boolean isEmpty = items.isEmpty();
        empty.setVisibility(isEmpty ? View.VISIBLE : View.GONE);
        list.setVisibility(isEmpty ? View.GONE : View.VISIBLE);
    }

    private String platform(String value) {
        if ("local".equals(value)) return "本地";
        if ("kugou".equals(value)) return "酷狗";
        if ("netease".equals(value)) return "网易云";
        return "收藏";
    }

    private TextView text(String value, int size, boolean bold) {
        TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(0xFFF0FCFF);
        if (bold) view.setTypeface(Typeface.DEFAULT_BOLD);
        return view;
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
