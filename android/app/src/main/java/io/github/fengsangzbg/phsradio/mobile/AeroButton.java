package io.github.fengsangzbg.phsradio.mobile;

import android.animation.TimeInterpolator;
import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.*;
import android.view.MotionEvent;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.OvershootInterpolator;
import android.widget.Button;

/**
 * The desktop JellyButton, reduced to the parts a finger can use.
 *
 * The glass sits inset inside the widget: a dark body, an accent tint, a short
 * gloss and a rim that fades from white at the top to the accent at the bottom.
 * Pressing squashes the plate and drops a soft light under the finger; releasing
 * springs back. The button paints everything itself, so the system background
 * must stay off.
 */
final class AeroButton extends Button {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint rim = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF glass = new RectF();
    private final ValueAnimator pressure;
    private float press;
    private float pointerX = -1, pointerY = -1;
    private boolean active;

    AeroButton(Context context) {
        super(context);
        setBackground(null);
        setForeground(null);
        setAllCaps(false);
        setTextColor(0xFFEAF3FA);
        setTextSize(14);
        setMinWidth(0);
        setMinimumWidth(0);
        setPadding((int) dp(8f), 0, (int) dp(8f), 0);
        setStateListAnimator(null);
        getPaint().setShadowLayer(dp(3f), 0, dp(1f), 0x99000812);
        rim.setStyle(Paint.Style.STROKE);
        pressure = ValueAnimator.ofFloat(0f, 1f);
        pressure.setInterpolator(new DecelerateInterpolator());
        pressure.addUpdateListener(animation -> {
            press = (float) animation.getAnimatedValue();
            invalidate();
        });
    }

    void setActive(boolean value) {
        active = value;
        invalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        final float w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;

        final float mx = Math.max(dp(6f), w * .065f);
        final float my = Math.min(dp(6f), h * .18f);
        glass.set(mx, my, w - mx, h - my);
        final float radius = glass.height() * .46f;
        final float squash = Math.max(-.15f, press);

        canvas.save();
        canvas.scale(1f + squash * .10f, 1f - squash * .17f, w * .5f, h * .5f);

        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(0, glass.top, 0, glass.bottom,
                new int[]{0x1A0E2732, 0x0D07141D, 0x1A040E17},
                new float[]{0f, .5f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(glass, radius, radius, paint);

        paint.setShader(null);
        paint.setColor(active ? 0x1A73D5E3 : 0x0A73D5E3);
        canvas.drawRoundRect(glass, radius, radius, paint);

        paint.setShader(new LinearGradient(0, glass.top, 0, glass.top + glass.height() * .46f,
                new int[]{0x26FFFFFF, 0x08FFFFFF, 0x00FFFFFF},
                new float[]{0f, .6f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(glass, radius, radius, paint);
        paint.setShader(null);

        if (pointerX >= 0 && glass.contains(pointerX, pointerY)) {
            paint.setShader(new RadialGradient(pointerX, pointerY, glass.height() * 1.1f,
                    new int[]{0x26FFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
            canvas.drawRoundRect(glass, radius, radius, paint);
            paint.setShader(null);
        }

        if (press > .01f) {
            paint.setColor(Color.argb((int) (22 * press), 255, 255, 255));
            canvas.drawRoundRect(glass, radius, radius, paint);
        }

        rim.setStrokeWidth(dp(.9f));
        rim.setShader(new LinearGradient(0, glass.top, 0, glass.bottom,
                new int[]{active ? 0xB3FFFFFF : 0x8CFFFFFF, 0x14FFFFFF,
                        active ? 0x6673D5E3 : 0x3D73D5E3},
                new float[]{0f, .52f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(glass, radius, radius, rim);
        rim.setShader(null);

        // A short gleam on the upper-left shoulder, the desktop edge catch.
        rim.setStrokeWidth(dp(.9f));
        rim.setColor(0x33FFFFFF);
        RectF gleam = new RectF(glass.left + dp(1.5f), glass.top + dp(1.5f),
                glass.right - dp(1.5f), glass.bottom - dp(1.5f));
        canvas.drawArc(gleam, 190f, 55f, false, rim);

        super.onDraw(canvas);
        canvas.restore();
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointerX = event.getX();
                pointerY = event.getY();
                animatePressure(1f, 90, new DecelerateInterpolator());
                break;
            case MotionEvent.ACTION_MOVE:
                pointerX = event.getX();
                pointerY = event.getY();
                invalidate();
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                pointerX = -1;
                pointerY = -1;
                animatePressure(0f, 420, new OvershootInterpolator(2.2f));
                break;
            default:
                break;
        }
        return super.onTouchEvent(event);
    }

    private void animatePressure(float target, long duration, TimeInterpolator curve) {
        pressure.cancel();
        pressure.setFloatValues(press, target);
        pressure.setDuration(duration);
        pressure.setInterpolator(curve);
        pressure.start();
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }
}
