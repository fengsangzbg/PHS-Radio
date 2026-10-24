package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.widget.LinearLayout;

/**
 * A song row that carries the desktop glass feel: a rounded aqua pane, a slow
 * specular band that sweeps across it like light moving over real glass, a
 * faint inner caustic and a spring squash on touch.
 *
 * It stays a LinearLayout so list adapters can keep recycling plain rows; the
 * pane is painted in dispatchDraw behind the children.
 */
final class GlassRow extends LinearLayout {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF bounds = new RectF();
    private static float pulse;
    private static long pulseAt;
    private long clock;
    private float phase;
    private float press;
    private boolean pressed;

    /** Called while a list scrolls: rows squash like jelly and settle back. */
    static void kick(float amount) {
        pulse = Math.min(1f, amount);
        pulseAt = SystemClock.uptimeMillis();
    }

    GlassRow(Context context) {
        super(context);
        setWillNotDraw(false);
        setOrientation(HORIZONTAL);
        setGravity(android.view.Gravity.CENTER_VERTICAL);
        stroke.setStyle(Paint.Style.STROKE);
        clock = SystemClock.uptimeMillis();
    }

    /** Recommendation tiles stack vertically; rows stay horizontal. */
    void vertical() {
        setOrientation(VERTICAL);
        setGravity(android.view.Gravity.TOP);
    }

    @Override protected void dispatchDraw(Canvas canvas) {
        long now = SystemClock.uptimeMillis();
        float dt = Math.min(.05f, (now - clock) / 1000f);
        clock = now;
        // A slow drift, not a strobe: 0.22 of a cycle per second reads as light
        // moving across a surface rather than a blinking highlight.
        phase = (phase + dt * .22f) % 1f;
        press += ((pressed ? 1f : 0f) - press) * Math.min(1f, dt * 12f);

        float w = getWidth(), h = getHeight();
        bounds.set(0, 0, w, h);
        float radius = h * .32f;

        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(0, 0, w, h,
                new int[]{0x5A2F6B7C, 0x4A1B3A56, 0x6612293F},
                new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(bounds, radius, radius, paint);
        paint.setShader(null);

        // Specular band: a narrow bright wedge travelling along the row.
        paint.setShader(new LinearGradient(w * (phase * 1.6f - .3f), 0, w * (phase * 1.6f + .12f), h,
                new int[]{0x00FFFFFF, 0x2EFFFFFF, 0x00FFFFFF}, new float[]{0f, .5f, 1f},
                Shader.TileMode.CLAMP));
        canvas.drawRoundRect(bounds, radius, radius, paint);
        paint.setShader(null);

        // Inner caustic near the leading edge, echoing the aquarium scene.
        paint.setShader(new RadialGradient(w * .18f, h * .10f, w * .55f,
                new int[]{0x22B9F2FF, 0x00B9F2FF}, null, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(bounds, radius, radius, paint);
        paint.setShader(null);

        stroke.setStrokeWidth(dp(1f));
        stroke.setColor(0x5A8FD8E4);
        RectF rim = new RectF(bounds);
        rim.inset(dp(.5f), dp(.5f));
        canvas.drawRoundRect(rim, radius, radius, stroke);

        // Squash on press plus the scroll jelly: both are canvas transforms, so a
        // recycled row deforms without relayout.
        float jellyAge = (SystemClock.uptimeMillis() - pulseAt) / 260f;
        float jelly = pulse * Math.max(0f, 1f - jellyAge);
        canvas.save();
        float squash = 1f - press * .035f - jelly * .045f;
        canvas.scale(1f + press * .012f + jelly * .020f, squash, w / 2f, h / 2f);
        super.dispatchDraw(canvas);
        canvas.restore();
        if (jelly > .01f) postInvalidateOnAnimation();

        if (isShown()) postInvalidateOnAnimation();
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pressed = true;
                invalidate();
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                pressed = false;
                invalidate();
                break;
            default:
                break;
        }
        return super.onTouchEvent(event);
    }

    private float dp(float value) { return value * getResources().getDisplayMetrics().density; }
}
