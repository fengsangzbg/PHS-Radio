package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.View;

/**
 * A cut-crystal heart: twelve facets around a shared centre, each face shaded
 * like a gemstone, a white specular wedge on the upper left lobe, and - once
 * lit - a soft rainbow bloom behind it that breathes instead of drawing lines.
 *
 * Used at 42 dp as the dock button and at 160 dp as the celebration overlay.
 */
final class CrystalHeartView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path heart = new Path();
    private final Path facet = new Path();
    private final Path glowPath = new Path();
    private boolean lit;
    private boolean celebratory;
    private long litAt;

    CrystalHeartView(Context context) {
        super(context);
        setLayerType(LAYER_TYPE_HARDWARE, null);
        paint.setStyle(Paint.Style.FILL);
    }

    void setLit(boolean value) {
        if (value && !lit) litAt = SystemClock.uptimeMillis();
        lit = value;
        invalidate();
    }

    boolean isLit() { return lit; }

    /** Bigger bloom and a slow swell, for the tap celebration. */
    void celebrate() {
        celebratory = true;
        litAt = SystemClock.uptimeMillis();
        invalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        long now = SystemClock.uptimeMillis();
        float w = getWidth(), h = getHeight();
        float cx = w / 2f, cy = h / 2f;
        float size = Math.min(w, h) * .34f;
        float age = (now - litAt) / 1000f;

        if (lit) {
            // Rainbow bloom: three additive radial washes whose hue drifts with
            // time. Additive layering is what reads as light rather than paint.
            float breathe = celebratory
                    ? Math.min(1f, age * 2.2f) * (1f + .18f * (float) Math.sin(age * 6f))
                    : .78f + .22f * (float) Math.sin(age * 1.8f);
            int blend = paint.getAlpha();
            paint.setAlpha(blend);
            paint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.ADD));
            float[] hues = {190f, 265f, 320f};
            for (int i = 0; i < hues.length; i++) {
                float radius = size * (2.0f + i * .22f) * breathe;
                int color = Color.HSVToColor(new float[]{hues[i], .62f, 1f});
                int inner = Color.argb((int) (70 * breathe), Color.red(color), Color.green(color), Color.blue(color));
                int outer = Color.argb(0, Color.red(color), Color.green(color), Color.blue(color));
                paint.setShader(new RadialGradient(cx, cy, radius, inner, outer, Shader.TileMode.CLAMP));
                canvas.drawCircle(cx, cy, radius, paint);
            }
            paint.setShader(null);
            paint.setXfermode(null);
            paint.setAlpha(255);
        }

        buildHeart(cx, cy, size);
        // Translucent glass body first, so the heart reads as crystal and not paint.
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(cx, cy - size, cx, cy + size,
                new int[]{0xE6FFFFFF, 0xB3D8F8FF, 0x8C86D4F0},
                new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
        paint.setAlpha(lit ? 205 : 160);
        canvas.drawPath(heart, paint);
        paint.setShader(null);
        paint.setAlpha(255);

        // Facets: triangles from the centre to the outline, shaded by direction.
        float[] ring = {
                -.98f, -.30f, -.62f, -.86f, -.10f, -.58f, .18f, -.92f, .62f, -.74f,
                .98f, -.22f, .58f, .28f, .20f, .52f, -.30f, .50f, -.72f, .18f
        };
        for (int i = 0; i < ring.length; i += 2) {
            float x = cx + ring[i] * size;
            float y = cy + ring[i + 1] * size;
            float angle = (float) Math.atan2(ring[i + 1], ring[i]);
            // Light from the upper left: facets facing it are brighter and cooler.
            float lit01 = (float) ((Math.cos(angle + Math.PI * .75f) + 1f) / 2f);
            float hue = lit ? (200f + 90f * lit01 + age * 22f) % 360f : 200f;
            float value = .62f + .38f * lit01;
            float sat = lit ? .55f - .28f * lit01 : .10f;
            int color = Color.HSVToColor(new float[]{hue, sat, value});
            paint.setColor(color);
            paint.setAlpha(lit ? 105 : 78);
            facet.reset();
            facet.moveTo(cx, cy - size * .12f);
            facet.lineTo(x, y);
            int next = (i + 2) % ring.length;
            facet.lineTo(cx + ring[next] * size, cy + ring[next + 1] * size);
            facet.close();
            canvas.drawPath(facet, paint);
        }

        // Facet edges: hairline ridges, the "cut" that makes it read as diamond.
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(.8f));
        paint.setColor(0x66FFFFFF);
        for (int i = 0; i < ring.length; i += 2) {
            canvas.drawLine(cx, cy - size * .12f, cx + ring[i] * size, cy + ring[i + 1] * size, paint);
        }

        // Prismatic edge: a rainbow gradient stroked over the silhouette.
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(2.6f));
        paint.setShader(new LinearGradient(cx - size, cy - size, cx + size, cy + size,
                new int[]{0x8CFF6EC7, 0x8C83E8FF, 0x8C89FF9E, 0x8CFFE066},
                null, Shader.TileMode.CLAMP));
        paint.setAlpha(lit ? 220 : 150);
        canvas.drawPath(heart, paint);
        paint.setShader(null);
        paint.setAlpha(255);
        // Rim.
        paint.setStrokeWidth(dp(1.6f));
        paint.setColor(0xCCFFFFFF);
        canvas.drawPath(heart, paint);

        // Specular wedge on the left lobe.
        Path gleam = new Path();
        gleam.moveTo(cx - size * .58f, cy - size * .34f);
        gleam.lineTo(cx - size * .30f, cy - size * .72f);
        gleam.lineTo(cx - size * .12f, cy - size * .50f);
        gleam.close();
        paint.setColor(0xE6FFFFFF);
        paint.setAlpha(lit ? 235 : 170);
        paint.setStyle(Paint.Style.FILL);
        canvas.drawPath(gleam, paint);

        paint.setAlpha(255);
        paint.setStyle(Paint.Style.FILL);

        if (lit && (celebratory ? age < 1.1f : true)) postInvalidateOnAnimation();
    }

    private void buildHeart(float cx, float cy, float size) {
        heart.reset();
        heart.moveTo(cx, cy + size * .86f);
        heart.cubicTo(cx - size * 1.30f, cy - size * .10f, cx - size * .60f, cy - size * 1.02f, cx, cy - size * .40f);
        heart.cubicTo(cx + size * .60f, cy - size * 1.02f, cx + size * 1.30f, cy - size * .10f, cx, cy + size * .86f);
        heart.close();
    }

    private float dp(float value) { return value * getResources().getDisplayMetrics().density; }
}
