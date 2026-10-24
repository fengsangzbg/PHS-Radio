package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.os.SystemClock;
import android.view.*;

/**
 * Port of the Windows aquarium entrance: the sky photo, a drifting humpback, caustic
 * light on the glass floor, rising bubbles and the pearl P on a glass card. Tapping
 * the card zooms it away and hands control to the player.
 *
 * The scene is drawn with Canvas rather than OpenGL: every layer is one bitmap or a
 * handful of thin paths, so it holds 60 fps on phones without a shader pipeline.
 */
final class AquariumEntrance extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF panel = new RectF();
    private final float[] bubbles = new float[48];
    private final Matrix skyMatrix = new Matrix();

    private Bitmap sky, whale, logo;
    private Bitmap skyScaled, whaleScaled;
    private long clock;
    private long startedAt;
    private boolean leaving;
    private long leaveStart;
    private boolean pressed;
    private float pressAmount;

    Runnable onEntered;

    AquariumEntrance(Context context) {
        super(context);
        setLayerType(LAYER_TYPE_HARDWARE, null);
        // Decode the two photographs no wider than a phone screen: the sources are
        // 1672 and 2048 px wide, and keeping both at full size costs ~13 MB of heap.
        sky = decode(context, R.drawable.aquarium_sky, 1620);
        whale = decode(context, R.drawable.humpback, 2048);
        logo = BitmapFactory.decodeResource(context.getResources(), R.drawable.logo_phs);
        java.util.Random random = new java.util.Random(7);
        for (int i = 0; i < bubbles.length; i += 4) {
            bubbles[i] = random.nextFloat();          // x, 0..1
            bubbles[i + 1] = random.nextFloat();      // phase
            bubbles[i + 2] = .004f + random.nextFloat() * .010f; // speed
            bubbles[i + 3] = 1.4f + random.nextFloat() * 3.4f;   // radius in dp
        }
        stroke.setStyle(Paint.Style.STROKE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
        startedAt = SystemClock.uptimeMillis();
        clock = startedAt;
    }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        skyScaled = cover(sky, w, h);
        final int whaleWidth = Math.round(w * .78f);
        if (whale != null && whaleWidth > 0) {
            final int whaleHeight = Math.max(1, Math.round(whale.getHeight() * whaleWidth / (float) whale.getWidth()));
            whaleScaled = Bitmap.createScaledBitmap(whale, whaleWidth, whaleHeight, true);
        }
        final float panelWidth = Math.min(w * .82f, dp(360));
        final float panelHeight = Math.min(h * .58f, dp(430));
        panel.set((w - panelWidth) / 2f, (h - panelHeight) / 2f, (w + panelWidth) / 2f, (h + panelHeight) / 2f);
    }

    private static Bitmap decode(Context context, int resource, int maxWidth) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeResource(context.getResources(), resource, bounds);
        int sample = 1;
        while (bounds.outWidth / (sample * 2) >= maxWidth) sample *= 2;
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = sample;
        return BitmapFactory.decodeResource(context.getResources(), resource, options);
    }

    private static Bitmap cover(Bitmap source, int width, int height) {
        if (source == null || width <= 0 || height <= 0) return null;
        final float scale = Math.max(width / (float) source.getWidth(), height / (float) source.getHeight());
        final int w = Math.max(1, Math.round(source.getWidth() * scale));
        final int h = Math.max(1, Math.round(source.getHeight() * scale));
        Bitmap scaled = Bitmap.createScaledBitmap(source, w, h, true);
        final int x = Math.max(0, (w - width) / 2), y = Math.max(0, (h - height) / 2);
        return Bitmap.createBitmap(scaled, x, y, Math.min(width, w), Math.min(height, h));
    }

    @Override protected void onDraw(Canvas canvas) {
        final long now = SystemClock.uptimeMillis();
        final float seconds = (now - startedAt) / 1000f;
        final int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;

        // Sky: a slow breath in and out so the frame never feels like a still photo.
        final float zoom = 1.02f + .012f * (float) Math.sin(seconds * .16);
        canvas.save();
        canvas.translate(w / 2f, h / 2f);
        canvas.scale(zoom, zoom);
        canvas.translate(-w / 2f, -h / 2f);
        if (skyScaled != null) canvas.drawBitmap(skyScaled, 0, 0, paint);
        canvas.restore();

        drawCaustics(canvas, w, h, seconds);
        drawWhale(canvas, w, h, seconds);
        drawBubbles(canvas, w, h, seconds);

        // Vignette keeps the middle of the screen readable under bright sky.
        paint.setShader(new RadialGradient(w * .5f, h * .5f, Math.max(w, h) * .72f,
                new int[]{0x00000000, 0x33000818, 0x8C00070F}, new float[]{.42f, .82f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, w, h, paint);
        paint.setShader(null);

        drawPanel(canvas, w, h, seconds);

        pressAmount += ((pressed ? 1f : 0f) - pressAmount) * .25f;
        if (leaving) {
            final float progress = Math.min(1f, (now - leaveStart) / 420f);
            final float fade = 1f - progress;
            final float scale = 1f + progress * .12f;
            canvas.save();
            canvas.translate(w / 2f, h / 2f);
            canvas.scale(scale, scale);
            canvas.translate(-w / 2f, -h / 2f);
            paint.setAlpha((int) (fade * 255));
            canvas.drawColor(Color.argb((int) (fade * 120), 0, 0, 0));
            canvas.restore();
            if (progress >= 1f && onEntered != null) {
                final Runnable callback = onEntered;
                onEntered = null;
                callback.run();
            }
        }
        if (getVisibility() == VISIBLE) postInvalidateOnAnimation();
    }

    private void drawCaustics(Canvas canvas, int w, int h, float seconds) {
        stroke.setStrokeWidth(dp(1.1f));
        for (int i = 0; i < 7; i++) {
            final float phase = seconds * (.22f + i * .03f) + i * 1.7f;
            final float y = h * (.10f + i * .125f) + (float) Math.sin(phase) * h * .035f;
            final Path wave = new Path();
            wave.moveTo(-w * .1f, y);
            wave.cubicTo(w * .28f, y - h * .045f, w * .66f, y + h * .05f, w * 1.1f, y - h * .01f);
            stroke.setColor(i % 2 == 0 ? 0x2E9BF6F2 : 0x1A58CBE8);
            canvas.drawPath(wave, stroke);
        }
    }

    private void drawWhale(Canvas canvas, int w, int h, float seconds) {
        if (whaleScaled == null) return;
        // A nine second crossing with a long ease, plus a shallow bob.
        final float cycle = (seconds % 18f) / 18f;
        final float travel = w * 1.15f;
        final float x = -whaleScaled.getWidth() * .55f + travel * cycle;
        final float bob = (float) Math.sin(seconds * .55f) * dp(6);
        final float y = h * .40f + bob - whaleScaled.getHeight() * .35f;
        canvas.save();
        canvas.translate(x, y);
        canvas.rotate((float) Math.sin(seconds * .35f) * 1.6f, whaleScaled.getWidth() * .5f, whaleScaled.getHeight() * .5f);
        paint.setAlpha(235);
        canvas.drawBitmap(whaleScaled, 0, 0, paint);
        paint.setAlpha(255);
        canvas.restore();
    }

    private void drawBubbles(Canvas canvas, int w, int h, float seconds) {
        for (int i = 0; i < bubbles.length; i += 4) {
            final float phase = (seconds * bubbles[i + 2] + bubbles[i + 1]) % 1f;
            final float y = h * (1.05f - phase * 1.12f);
            final float x = bubbles[i] * w + (float) Math.sin((phase + bubbles[i]) * 6.3f) * dp(9);
            final float radius = dp(bubbles[i + 3]) * (1f - phase * .35f);
            final int alpha = (int) (70 * Math.sin(phase * Math.PI));
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(alpha / 3, 220, 248, 255));
            canvas.drawCircle(x, y, radius, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(.9f));
            paint.setColor(Color.argb(alpha, 216, 246, 255));
            canvas.drawCircle(x, y, radius, paint);
            paint.setStyle(Paint.Style.FILL);
        }
    }

    private void drawPanel(Canvas canvas, int w, int h, float seconds) {
        final float centerX = panel.centerX();
        final float press = 1f - pressAmount * .035f;
        canvas.save();
        canvas.scale(press, press, centerX, panel.centerY());

        final float radius = dp(30);
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(panel.left, panel.top, panel.right, panel.bottom,
                new int[]{0xE63A6E7C, 0xE62A3D5C, 0xE61B2740}, new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(panel, radius, radius, paint);
        paint.setShader(new RadialGradient(panel.left + panel.width() * .22f, panel.top + panel.height() * .12f,
                panel.width() * .9f, new int[]{0x3CB9EFFF, 0x00B9EFFF}, null, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(panel, radius, radius, paint);
        paint.setShader(null);

        stroke.setStrokeWidth(dp(1.2f));
        stroke.setColor(0x66A7E9F0);
        canvas.drawRoundRect(panel, radius, radius, stroke);

        // Logo, title, subtitle and the pulse hint, stacked from the top edge.
        final float logoSize = dp(104);
        final float logoTop = panel.top + dp(40);
        if (logo != null)
            canvas.drawBitmap(logo, null, new RectF(centerX - logoSize / 2f, logoTop, centerX + logoSize / 2f, logoTop + logoSize), paint);

        textPaint.setColor(0xFFF2FDFF);
        textPaint.setTextSize(dp(24));
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        canvas.drawText("PHS Radio", centerX, logoTop + logoSize + dp(38), textPaint);

        textPaint.setTextSize(dp(13));
        textPaint.setColor(0xCCBFE9F2);
        textPaint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
        canvas.drawText("新大地播放器", centerX, logoTop + logoSize + dp(62), textPaint);

        final float hintY = panel.bottom - dp(34);
        final float pulse = .62f + .38f * (float) Math.sin(seconds * 2.6f);
        textPaint.setTextSize(dp(14));
        textPaint.setColor(Color.argb((int) (255 * pulse), 214, 247, 255));
        canvas.drawText("点击进入", centerX, hintY, textPaint);

        textPaint.setTextSize(dp(10.5f));
        textPaint.setColor(0x8CCFEFF7);
        canvas.drawText("账号可在播放器内连接", centerX, hintY + dp(18), textPaint);
        canvas.restore();
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (leaving) return true;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                if (panel.contains(event.getX(), event.getY())) { pressed = true; invalidate(); }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (pressed || panel.contains(event.getX(), event.getY())) enter();
                pressed = false;
                invalidate();
                return true;
            default:
                return true;
        }
    }

    void enter() {
        if (leaving) return;
        leaving = true;
        leaveStart = SystemClock.uptimeMillis();
        postInvalidateOnAnimation();
    }

    private float dp(float value) { return value * getResources().getDisplayMetrics().density; }
}
