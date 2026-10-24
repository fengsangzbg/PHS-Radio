package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.*;

/**
 * Port of the Windows aquarium entrance. The sky photo is opaque from the first
 * frame (the player behind must never show through), the humpback crosses with a
 * slow bob, caustics and bubbles drift, and the pearl P sits on a glass card
 * with the shader's three spiral wind filaments wrapped around it.
 *
 * The wind follows the original fragment shader: three strands at
 * a = (2*PI*k - time*3.8 + radius*24) / 3 inside a Gaussian annulus, with the
 * spin velocity accelerating while the card is pressed.
 */
final class AquariumEntrance extends View {
    private static final float BASE_SPIN = .55f;
    private static final float FAST_SPIN = 7.0f;
    private static final long ENTER_MS = 900;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF panel = new RectF();
    private final float[] bubbles = new float[48];

    private Bitmap skyImage, whale, logo;
    private Bitmap skyScaled, whaleScaled;
    private long clock, startedAt;
    private float spin, spinVelocity = BASE_SPIN;
    private float wind;
    private boolean pressed;
    private float pressAmount;
    private boolean leaving;
    private long leaveStart;

    Runnable onEntered;

    AquariumEntrance(Context context) {
        super(context);
        setLayerType(LAYER_TYPE_HARDWARE, null);
        try {
            skyImage = decode(context, R.drawable.aquarium_sky, 1620);
            whale = decode(context, R.drawable.humpback, 2048);
            logo = BitmapFactory.decodeResource(context.getResources(), R.drawable.logo_phs);
        } catch (Throwable ignored) {
            // A missing or undecodable asset must not stop the player opening.
        }
        java.util.Random random = new java.util.Random(7);
        for (int i = 0; i < bubbles.length; i += 4) {
            bubbles[i] = random.nextFloat();
            bubbles[i + 1] = random.nextFloat();
            bubbles[i + 2] = .004f + random.nextFloat() * .010f;
            bubbles[i + 3] = 1.4f + random.nextFloat() * 3.4f;
        }
        stroke.setStyle(Paint.Style.STROKE);
        textPaint.setTextAlign(Paint.Align.CENTER);
        startedAt = SystemClock.uptimeMillis();
        clock = startedAt;
    }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        skyScaled = cover(skyImage, w, h);
        int whaleWidth = Math.round(w * .78f);
        if (whale != null && whaleWidth > 0) {
            int whaleHeight = Math.max(1, Math.round(whale.getHeight() * whaleWidth / (float) whale.getWidth()));
            whaleScaled = Bitmap.createScaledBitmap(whale, whaleWidth, whaleHeight, true);
        }
        float panelWidth = Math.min(w * .82f, dp(360));
        float panelHeight = Math.min(h * .58f, dp(430));
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
        float scale = Math.max(width / (float) source.getWidth(), height / (float) source.getHeight());
        int w = Math.max(1, Math.round(source.getWidth() * scale));
        int h = Math.max(1, Math.round(source.getHeight() * scale));
        Bitmap scaled = Bitmap.createScaledBitmap(source, w, h, true);
        int x = Math.max(0, (w - width) / 2), y = Math.max(0, (h - height) / 2);
        return Bitmap.createBitmap(scaled, x, y, Math.min(width, w), Math.min(height, h));
    }

    @Override protected void onDraw(Canvas canvas) {
        long now = SystemClock.uptimeMillis();
        float dt = Math.min(.05f, (now - clock) / 1000f);
        clock = now;
        float seconds = (now - startedAt) / 1000f;
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;

        // Spin physics: idle drift, accelerate hard while held, decay afterwards.
        float target = pressed ? FAST_SPIN : BASE_SPIN;
        spinVelocity += (target - spinVelocity) * Math.min(1f, dt * 6f);
        spin += spinVelocity * dt;
        float windTarget = pressed ? 1f : Math.max(0f, (spinVelocity - BASE_SPIN) / (FAST_SPIN - BASE_SPIN));
        wind += (windTarget - wind) * Math.min(1f, dt * 7f);

        // Opaque base first: the player UI must never bleed through this scene.
        canvas.drawColor(0xFF0B2536);

        float zoom = 1.02f + .012f * (float) Math.sin(seconds * .16f);
        canvas.save();
        canvas.translate(w / 2f, h / 2f);
        canvas.scale(zoom, zoom);
        canvas.translate(-w / 2f, -h / 2f);
        drawSky(canvas, w, h);
        canvas.restore();

        drawCaustics(canvas, w, h, seconds);
        drawWhale(canvas, w, h, seconds);
        drawBubbles(canvas, w, h, seconds);

        paint.setShader(new RadialGradient(w * .5f, h * .5f, Math.max(w, h) * .72f,
                new int[]{0x00000000, 0x2E000818, 0x8000070F}, new float[]{.42f, .82f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, w, h, paint);
        paint.setShader(null);

        pressAmount += ((pressed ? 1f : 0f) - pressAmount) * .25f;

        if (leaving) {
            float progress = Math.min(1f, (now - leaveStart) / (float) ENTER_MS);
            float scale = (float) Math.exp(ease(progress) * 2.4);
            canvas.save();
            canvas.translate(w / 2f, h / 2f);
            canvas.scale(scale, scale);
            canvas.translate(-w / 2f, -h / 2f);
            drawSky(canvas, w, h);
            drawCaustics(canvas, w, h, seconds);
            drawWhale(canvas, w, h, seconds);
            drawPanel(canvas, w, h, seconds);
            canvas.restore();
            float white = Math.max(0f, (progress - .72f) / .28f);
            if (white > 0f) {
                paint.setColor(Color.argb((int) (255 * Math.min(1f, white)), 250, 253, 255));
                canvas.drawRect(0, 0, w, h, paint);
            }
            if (progress >= 1f && onEntered != null) {
                Runnable callback = onEntered;
                onEntered = null;
                callback.run();
            }
        } else {
            drawPanel(canvas, w, h, seconds);
        }
        postInvalidateOnAnimation();
    }

    private void drawSky(Canvas canvas, int w, int h) {
        if (skyScaled != null) {
            paint.setAlpha(255);
            canvas.drawBitmap(skyScaled, 0, 0, paint);
            return;
        }
        paint.setShader(new LinearGradient(0, 0, 0, h,
                new int[]{0xFF0E4E6B, 0xFF116D86, 0xFF0B3A52}, null, Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, w, h, paint);
        paint.setShader(null);
    }

    private static float ease(float t) {
        t = Math.max(0f, Math.min(1f, t));
        return t * t * (3f - 2f * t);
    }

    private void drawCaustics(Canvas canvas, int w, int h, float seconds) {
        stroke.setStrokeWidth(dp(1.1f));
        for (int i = 0; i < 7; i++) {
            float phase = seconds * (.22f + i * .03f) + i * 1.7f;
            float y = h * (.10f + i * .125f) + (float) Math.sin(phase) * h * .035f;
            Path wave = new Path();
            wave.moveTo(-w * .1f, y);
            wave.cubicTo(w * .28f, y - h * .045f, w * .66f, y + h * .05f, w * 1.1f, y - h * .01f);
            stroke.setColor(i % 2 == 0 ? 0x2E9BF6F2 : 0x1A58CBE8);
            canvas.drawPath(wave, stroke);
        }
    }

    private void drawWhale(Canvas canvas, int w, int h, float seconds) {
        if (whaleScaled == null) return;
        float cycle = (seconds % 18f) / 18f;
        float x = -whaleScaled.getWidth() * .55f + w * 1.15f * cycle;
        float bob = (float) Math.sin(seconds * .55f) * dp(6);
        float y = h * .40f + bob - whaleScaled.getHeight() * .35f;
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
            float phase = (seconds * bubbles[i + 2] + bubbles[i + 1]) % 1f;
            float y = h * (1.05f - phase * 1.12f);
            float x = bubbles[i] * w + (float) Math.sin((phase + bubbles[i]) * 6.3f) * dp(9);
            float radius = dp(bubbles[i + 3]) * (1f - phase * .35f);
            int alpha = (int) (70 * Math.sin(phase * Math.PI));
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
        float centerX = panel.centerX();
        float press = 1f - pressAmount * .035f;
        canvas.save();
        canvas.scale(press, press, centerX, panel.centerY());

        float radius = dp(30);
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(panel.left, panel.top, panel.right, panel.bottom,
                new int[]{0xF23A6E7C, 0xF22A3D5C, 0xF21B2740}, new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(panel, radius, radius, paint);
        paint.setShader(new RadialGradient(panel.left + panel.width() * .22f, panel.top + panel.height() * .12f,
                panel.width() * .9f, new int[]{0x3CB9EFFF, 0x00B9EFFF}, null, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(panel, radius, radius, paint);
        paint.setShader(null);
        stroke.setStrokeWidth(dp(1.2f));
        stroke.setColor(0x66A7E9F0);
        canvas.drawRoundRect(panel, radius, radius, stroke);

        float logoSize = dp(112);
        float logoCx = centerX;
        float logoCy = panel.top + dp(44) + logoSize / 2f;
        drawWind(canvas, logoCx, logoCy, logoSize);
        drawLogo(canvas, logoCx, logoCy, logoSize);

        textPaint.setColor(0xFFF2FDFF);
        textPaint.setTextSize(dp(24));
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        canvas.drawText("PHS Radio", centerX, logoCy + logoSize / 2f + dp(34), textPaint);

        textPaint.setTextSize(dp(13));
        textPaint.setColor(0xCCBFE9F2);
        textPaint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
        canvas.drawText("新大地播放器", centerX, logoCy + logoSize / 2f + dp(58), textPaint);

        float hintY = panel.bottom - dp(34);
        float pulse = .62f + .38f * (float) Math.sin(seconds * 2.6f);
        textPaint.setTextSize(dp(14));
        textPaint.setColor(Color.argb((int) (255 * pulse), 214, 247, 255));
        canvas.drawText("点击进入", centerX, hintY, textPaint);

        textPaint.setTextSize(dp(10.5f));
        textPaint.setColor(0x8CCFEFF7);
        canvas.drawText("账号可在播放器内连接", centerX, hintY + dp(18), textPaint);
        canvas.restore();
    }

    private void drawLogo(Canvas canvas, float cx, float cy, float size) {
        if (logo == null) return;
        canvas.save();
        canvas.rotate((float) Math.toDegrees(spin), cx, cy);
        paint.setAlpha(255);
        canvas.drawBitmap(logo, null, new RectF(cx - size / 2f, cy - size / 2f, cx + size / 2f, cy + size / 2f), paint);
        canvas.restore();
    }

    /**
     * Three spiral filaments wrapped around the logo, derived from the desktop
     * shader: strands = pow(cos(angle*3 + time*3.8 - radius*24), 35) inside a
     * Gaussian annulus, scaled by the wind intensity.
     */
    private void drawWind(Canvas canvas, float cx, float cy, float size) {
        if (wind <= .01f) return;
        float inner = size * .60f;
        float outer = size * 1.00f;
        float seconds = SystemClock.uptimeMillis() / 1000f;
        stroke.setStyle(Paint.Style.STROKE);
        for (int arm = 0; arm < 3; arm++) {
            Path path = new Path();
            for (int step = 0; step <= 48; step++) {
                float t = step / 48f;
                float r = inner + (outer - inner) * t;
                double angle = (2 * Math.PI * arm - seconds * 3.8 + (r / size) * 24.0) / 3.0;
                float x = cx + (float) Math.cos(angle) * r;
                float y = cy + (float) Math.sin(angle) * r;
                if (step == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            stroke.setStrokeWidth(dp(3.4f));
            stroke.setColor(Color.argb((int) (46 * wind), 120, 226, 250));
            canvas.drawPath(path, stroke);
            stroke.setStrokeWidth(dp(1.6f));
            stroke.setColor(Color.argb((int) (150 * wind), 189, 246, 255));
            canvas.drawPath(path, stroke);
        }
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (leaving) return true;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pressed = true;
                invalidate();
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                pressed = false;
                enter();
                invalidate();
                return true;
            default:
                return true;
        }
    }

    void enter() {
        if (leaving) return;
        leaving = true;
        wind = 1f;
        leaveStart = SystemClock.uptimeMillis();
        postInvalidateOnAnimation();
    }

    private float dp(float value) { return value * getResources().getDisplayMetrics().density; }
}
