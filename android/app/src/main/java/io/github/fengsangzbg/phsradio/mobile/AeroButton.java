package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.view.MotionEvent;
import android.view.animation.OvershootInterpolator;
import android.widget.Button;

/**
 * The desktop JellyButton brought to Canvas: a capsule of glass with a lit top
 * rim, a shaded bottom, a highlight that follows the finger and pressure that
 * squashes the plate and whitens it before springing back.
 *
 * The button paints its own plate, so callers must not set a background - use
 * setActive() for the selected tab state instead.
 */
final class AeroButton extends Button {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint rim = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF plate = new RectF();
    private float press;
    private float pointerX = -1, pointerY = -1;
    private boolean active;
    private boolean lit = true;

    AeroButton(Context context) {
        super(context);
        setAllCaps(false);
        setTextColor(0xFFF0FCFF);
        setTextSize(14);
        setMinWidth(0);
        setMinimumWidth(0);
        setPadding((int) dp(12), 0, (int) dp(12), 0);
        setStateListAnimator(null);
        rim.setStyle(Paint.Style.STROKE);
    }

    void setActive(boolean value) {
        active = value;
        invalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        float w = getWidth(), h = getHeight();
        float radius = h * .5f;
        plate.set(0, 0, w, h);
        press += ((lit ? 0f : 1f) - press) * .35f;

        // Base: bright shoulder, cool body, shaded floor.
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(0, 0, 0, h,
                active ? new int[]{0xF2A8F0FF, 0xD93A86A6, 0xF21B4258}
                       : new int[]{0xCCE4FAFF, 0x993E7F99, 0xD92B4E66},
                new float[]{0f, .46f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(plate, radius, radius, paint);

        // Gloss band across the upper third: the Frutiger Aero signature.
        paint.setShader(new LinearGradient(0, 0, 0, h * .48f,
                new int[]{0x66FFFFFF, 0x1AFFFFFF, 0x00FFFFFF},
                new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(new RectF(plate.left + dp(2), dp(1), plate.right - dp(2), h * .48f),
                radius, radius, paint);

        // Highlight follows the finger, like the desktop hover light.
        if (pointerX >= 0) {
            paint.setShader(new RadialGradient(pointerX, pointerY, h * 1.6f,
                    new int[]{0x40FFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
            canvas.drawRoundRect(plate, radius, radius, paint);
        }
        paint.setShader(null);

        // Press whitens the glass and squashes it a hair.
        if (press > .01f) {
            paint.setColor(Color.argb((int) (58 * press), 255, 255, 255));
            canvas.drawRoundRect(plate, radius, radius, paint);
            paint.setColor(Color.argb((int) (26 * press), 255, 255, 255));
            canvas.drawRoundRect(new RectF(plate.left, plate.top + h * .5f, plate.right, plate.bottom),
                    radius, radius, paint);
        }

        // Lit top rim and shaded bottom edge give the plate thickness.
        rim.setStrokeWidth(dp(1.2f));
        rim.setColor(active ? 0xE6EAFEFF : 0x99CFF2FA);
        canvas.drawRoundRect(new RectF(dp(.6f), dp(.6f), w - dp(.6f), h - dp(.6f)), radius, radius, rim);
        rim.setStrokeWidth(dp(1f));
        rim.setColor(0x33102A3A);
        canvas.drawLine(dp(radius * .6f), h - dp(1.2f), w - dp(radius * .6f), h - dp(1.2f), rim);

        canvas.save();
        float squash = 1f - press * .04f;
        canvas.scale(1f + press * .015f, squash, w / 2f, h / 2f);
        super.onDraw(canvas);
        canvas.restore();
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                lit = false;
                pointerX = e.getX();
                pointerY = e.getY();
                invalidate();
                break;
            case MotionEvent.ACTION_MOVE:
                pointerX = e.getX();
                pointerY = e.getY();
                invalidate();
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                lit = true;
                pointerX = -1;
                animate().scaleX(1).scaleY(1).setInterpolator(new OvershootInterpolator(2.5f))
                        .setDuration(360).start();
                setScaleX(.96f);
                setScaleY(.93f);
                invalidate();
                break;
            default:
                break;
        }
        return super.onTouchEvent(e);
    }

    private float dp(float value) { return value * getResources().getDisplayMetrics().density; }
}
