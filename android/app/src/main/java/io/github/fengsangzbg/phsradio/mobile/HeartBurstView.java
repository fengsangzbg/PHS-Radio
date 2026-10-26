package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.Gravity;
import android.widget.FrameLayout;

/**
 * A soft light bloom behind the water heart. No rainbow, no prism: just a pale
 * blue-white breath and a few slow bubbles, added above every other view.
 */
final class HeartBurstView extends FrameLayout {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private long started;

    HeartBurstView(Context context) {
        super(context);
        setWillNotDraw(false);
        setClickable(false);
        setFocusable(false);
        setBackground(null);
        CrystalHeartView heart = new CrystalHeartView(context);
        heart.celebrate();
        heart.setScaleX(.45f);
        heart.setScaleY(.45f);
        addView(heart, new LayoutParams((int) dp(250f), (int) dp(250f), Gravity.CENTER));
        heart.animate().scaleX(1f).scaleY(1f).setDuration(520)
                .setInterpolator(new android.view.animation.OvershootInterpolator(1.6f)).start();
    }

    void burst() {
        started = SystemClock.uptimeMillis();
        postInvalidateOnAnimation();
    }

    @Override protected void dispatchDraw(Canvas canvas) {
        final long now = SystemClock.uptimeMillis();
        if (started == 0) started = now;
        final float age = (now - started) / 1000f;
        final float open = Math.min(1f, age / .55f);
        final float ease = 1f - (float) Math.pow(1f - open, 3f);
        final float fade = age > 1.05f ? Math.max(0f, 1f - (age - 1.05f) / .55f) : 1f;
        final float cx = getWidth() * .5f, cy = getHeight() * .5f;
        final float radius = Math.min(getWidth(), getHeight()) * .42f;

        paint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.ADD));
        paint.setShader(new RadialGradient(cx, cy, radius * (.55f + .55f * ease),
                new int[]{0x66FFFFFF, 0x338FE8FF, 0x1A4FA8E8, 0x00000000},
                new float[]{0f, .35f, .68f, 1f}, Shader.TileMode.CLAMP));
        paint.setAlpha((int) (220 * fade));
        canvas.drawCircle(cx, cy, radius * (.55f + .55f * ease), paint);
        paint.setAlpha(255);
        paint.setShader(null);

        // A few soft bubbles drifting out, never a hard ray.
        for (int i = 0; i < 12; i++) {
            double a = i * 2.39996 + .4;
            float d = radius * (.28f + .62f * ((i * 37 % 100) / 100f)) * ease;
            float x = cx + (float) Math.cos(a) * d;
            float y = cy + (float) Math.sin(a) * d;
            float r = dp(3f + (i % 3) * 2.2f) * (.6f + .4f * ease);
            paint.setShader(new RadialGradient(x, y, r * 2.4f,
                    new int[]{0x55EFFFFF, 0x00EFFFFF}, null, Shader.TileMode.CLAMP));
            canvas.drawCircle(x, y, r * 2.4f, paint);
            paint.setShader(null);
            paint.setColor(0x40FFFFFF);
            paint.setStyle(Paint.Style.FILL);
            canvas.drawCircle(x, y, r * .55f, paint);
        }
        paint.setXfermode(null);

        super.dispatchDraw(canvas);
        if (age < 1.6f) postInvalidateOnAnimation();
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }
}
