package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.View;

/**
 * A crystal heart that lights up with rainbow light when a song is liked.
 * Drawn as a real heart path filled with a specular glass gradient, wrapped in
 * twelve spectral rays, and removed by the caller when it finishes.
 */
final class CrystalHeart extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path heart = new Path();
    private long started;
    private final Runnable onDone;

    CrystalHeart(Context context, Runnable onDone) {
        super(context);
        this.onDone = onDone;
        setLayerType(LAYER_TYPE_HARDWARE, null);
        started = SystemClock.uptimeMillis();
        stroke.setStyle(Paint.Style.STROKE);
    }

    @Override protected void onDraw(Canvas canvas) {
        long now = SystemClock.uptimeMillis();
        float t = Math.min(1f, (now - started) / 1100f);
        float w = getWidth(), h = getHeight();
        float cx = w / 2f, cy = h / 2f;
        float size = Math.min(w, h) * .30f;
        // Overshoot on the way in, then a slow fade.
        float grow = (float) (1f - Math.pow(1f - Math.min(1f, t * 2.2f), 3));
        float scale = grow * (1f + .12f * (float) Math.sin(Math.min(1f, t * 3f) * Math.PI));
        float alpha = t < .72f ? 1f : 1f - (t - .72f) / .28f;

        heart.reset();
        heart.moveTo(cx, cy + size * .78f);
        heart.cubicTo(cx - size * 1.25f, cy - size * .10f, cx - size * .58f, cy - size * .95f, cx, cy - size * .38f);
        heart.cubicTo(cx + size * .58f, cy - size * .95f, cx + size * 1.25f, cy - size * .10f, cx, cy + size * .78f);
        heart.close();

        canvas.save();
        canvas.scale(scale, scale, cx, cy);

        // Spectral rays.
        for (int i = 0; i < 12; i++) {
            float angle = i * 30f + (now - started) * .05f;
            float hue = (i * 30f + (now - started) * .12f) % 360f;
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(2.2f));
            paint.setColor(Color.HSVToColor(new float[]{hue, .55f, 1f}));
            paint.setAlpha((int) (150 * alpha * (1f - t * .35f)));
            float inner = size * 1.05f, outer = size * (1.35f + .30f * t);
            canvas.drawLine(
                    cx + (float) Math.cos(Math.toRadians(angle)) * inner,
                    cy + (float) Math.sin(Math.toRadians(angle)) * inner,
                    cx + (float) Math.cos(Math.toRadians(angle)) * outer,
                    cy + (float) Math.sin(Math.toRadians(angle)) * outer,
                    paint);
        }

        // Glass body: white core, aqua shoulders, a whisper of violet at the tip.
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new RadialGradient(cx - size * .18f, cy - size * .30f, size * 1.35f,
                new int[]{0xFFFFFFFF, 0xFFBFF3FF, 0xFF7FD6EE, 0xCC8E7BFF},
                new float[]{0f, .34f, .68f, 1f}, Shader.TileMode.CLAMP));
        paint.setAlpha((int) (255 * alpha));
        canvas.drawPath(heart, paint);
        paint.setShader(null);

        // Specular streak across the upper left lobe.
        stroke.setStrokeWidth(dp(3.4f));
        stroke.setColor(0xE6FFFFFF);
        stroke.setAlpha((int) (230 * alpha));
        Path gleam = new Path();
        gleam.moveTo(cx - size * .58f, cy - size * .34f);
        gleam.cubicTo(cx - size * .46f, cy - size * .70f, cx - size * .10f, cy - size * .72f,
                cx - size * .04f, cy - size * .48f);
        canvas.drawPath(gleam, stroke);

        // Rim.
        stroke.setStrokeWidth(dp(2f));
        stroke.setColor(0xFFFFFFFF);
        stroke.setAlpha((int) (200 * alpha));
        canvas.drawPath(heart, stroke);

        canvas.restore();

        if (t >= 1f) {
            if (onDone != null) onDone.run();
            return;
        }
        postInvalidateOnAnimation();
    }

    private float dp(float value) { return value * getResources().getDisplayMetrics().density; }
}
