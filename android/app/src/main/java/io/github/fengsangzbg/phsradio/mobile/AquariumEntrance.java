package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.*;

/**
 * Port of the Windows aquarium entrance.
 *
 * The sky is opaque from the first frame, the humpback crosses head-first in the
 * same direction as the desktop scene, and the card is drawn as the desktop's
 * glass MP3 shell: an outer glass body with a rim light, a dim refracted screen
 * inside it, and a circular pearl badge carrying the P.
 *
 * The wind follows the original fragment shader (three strands at
 * a = (2*PI*k - time*3.8 + radius*24) / 3 inside a Gaussian annulus) but each
 * strand is drawn in tapered bands instead of one flat arc.
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

    private Bitmap skyImage, whale, arms, core;
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
            arms = BitmapFactory.decodeResource(getResources(), R.drawable.logo_arms);
            core = BitmapFactory.decodeResource(getResources(), R.drawable.logo_core);
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
        int whaleWidth = Math.round(w * .86f);
        if (whale != null && whaleWidth > 0) {
            int whaleHeight = Math.max(1, Math.round(whale.getHeight() * whaleWidth / (float) whale.getWidth()));
            whaleScaled = Bitmap.createScaledBitmap(whale, whaleWidth, whaleHeight, true);
        }
        float panelWidth = Math.min(w * .62f, dp(300));
        float panelHeight = Math.min(h * .62f, dp(460));
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

        float target = pressed ? FAST_SPIN : BASE_SPIN;
        spinVelocity += (target - spinVelocity) * Math.min(1f, dt * 6f);
        spin += spinVelocity * dt;
        float windTarget = pressed ? 1f : Math.max(0f, (spinVelocity - BASE_SPIN) / (FAST_SPIN - BASE_SPIN));
        wind += (windTarget - wind) * Math.min(1f, dt * 7f);

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
                new int[]{0x00000000, 0x26000818, 0x7A00070F}, new float[]{.42f, .82f, 1f}, Shader.TileMode.CLAMP));
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
            drawDevice(canvas, w, h, seconds);
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
            drawDevice(canvas, w, h, seconds);
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
        // The sprite is drawn head-left, so it has to cross right-to-left to swim
        // forwards; the earlier build moved it left-to-right and looked reversed.
        float cycle = 1f - (seconds % 18f) / 18f;
        float x = -whaleScaled.getWidth() * .55f + w * 1.25f * cycle;
        float bob = (float) Math.sin(seconds * .55f) * dp(7);
        float y = h * .30f + bob;
        canvas.save();
        canvas.translate(x, y);
        canvas.rotate((float) Math.sin(seconds * .35f) * 1.8f, whaleScaled.getWidth() * .5f, whaleScaled.getHeight() * .5f);
        paint.setAlpha(238);
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

    /**
     * The glass device: floor shadow, outer glass body with a lit rim, a dim
     * refracted screen inset inside it, then the pearl badge and the labels.
     */
    private void drawDevice(Canvas canvas, int w, int h, float seconds) {
        float radius = Math.min(panel.width(), panel.height()) * .22f;
        float press = 1f - pressAmount * .03f;
        canvas.save();
        canvas.scale(press, press, panel.centerX(), panel.centerY());

        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0x4D001018);
        RectF shadow = new RectF(panel);
        shadow.offset(0, dp(10));
        canvas.drawRoundRect(shadow, radius, radius, paint);

        // Outer glass shell: bright at the top edge, deep water at the bottom.
        paint.setShader(new LinearGradient(panel.left, panel.top, panel.right, panel.bottom,
                new int[]{0xF2A9E6F4, 0xE855A9C4, 0xE6235972, 0xF2163D50},
                new float[]{0f, .38f, .74f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(panel, radius, radius, paint);
        paint.setShader(null);

        // Inner screen: the sky again, slightly scaled and dimmed, which reads as
        // thick glass rather than a flat fill.
        RectF screen = new RectF(panel);
        screen.inset(dp(11), dp(11));
        float screenRadius = radius * .86f;
        canvas.save();
        Path clip = new Path();
        clip.addRoundRect(screen, screenRadius, screenRadius, Path.Direction.CW);
        canvas.clipPath(clip);
        if (skyScaled != null) {
            paint.setAlpha(150);
            Rect source = new Rect(0, 0, skyScaled.getWidth(), skyScaled.getHeight());
            RectF destination = new RectF(screen);
            destination.inset(-dp(26), -dp(18));
            canvas.drawBitmap(skyScaled, source, destination, paint);
            paint.setAlpha(255);
        }
        paint.setColor(0x8C06222F);
        canvas.drawRect(screen, paint);
        paint.setShader(new LinearGradient(screen.left, screen.top, screen.left, screen.bottom,
                new int[]{0x33000000, 0x00000000, 0x40000A10}, new float[]{0f, .45f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRect(screen, paint);
        paint.setShader(null);
        canvas.restore();

        stroke.setStrokeWidth(dp(1.4f));
        stroke.setColor(0x59D8F6FF);
        canvas.drawRoundRect(screen, screenRadius, screenRadius, stroke);

        // Edge light: a gradient along the body edge instead of a free-floating arc.
        stroke.setStrokeWidth(dp(1.0f));
        stroke.setColor(0x66BEEBF8);
        canvas.drawRoundRect(panel, radius, radius, stroke);

        paint.setShader(new RadialGradient(panel.left + panel.width() * .26f, panel.top + panel.height() * .10f,
                panel.width() * .85f, new int[]{0x40E8FCFF, 0x00E8FCFF}, null, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(panel, radius, radius, paint);
        paint.setShader(null);

        float logoSize = dp(104);
        float logoCx = panel.centerX();
        float logoCy = panel.bottom - dp(108);
        drawWind(canvas, logoCx, logoCy, logoSize);
        drawPearl(canvas, logoCx, logoCy, logoSize);

        textPaint.setColor(0xFFF2FDFF);
        textPaint.setTextSize(dp(13));
        textPaint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
        textPaint.setLetterSpacing(.32f);
        canvas.drawText("PHS RADIO", panel.centerX(), panel.top + dp(52), textPaint);

        textPaint.setLetterSpacing(0f);
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        textPaint.setTextSize(dp(21));
        canvas.drawText("点击软件图标", panel.centerX(), panel.top + dp(96), textPaint);
        canvas.drawText("进入登录界面", panel.centerX(), panel.top + dp(126), textPaint);

        textPaint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
        textPaint.setTextSize(dp(11.5f));
        textPaint.setColor(0x8CCFEFF7);
        float pulse = .58f + .42f * (float) Math.sin(seconds * 2.6f);
        textPaint.setColor(Color.argb((int) (255 * pulse * .8f), 199, 240, 250));
        canvas.drawText("轻触 P，让音乐开始流动", panel.centerX(), logoCy - logoSize * .78f, textPaint);
        canvas.restore();
    }

    /**
     * The pearl badge: a circular glass body so the mark reads round instead of
     * showing the artwork's pointed arm tips, with the P clipped inside it.
     */
    private void drawPearl(Canvas canvas, float cx, float cy, float size) {
        float radius = size * .5f;

        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new RadialGradient(cx, cy, radius * 1.06f,
                new int[]{0xC6E9FCFF, 0x8C7FD9EC, 0x33315F78, 0x00000000},
                new float[]{.62f, .82f, .94f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius * 1.06f, paint);
        paint.setShader(null);

        paint.setColor(0xFF0B2B3C);
        canvas.drawCircle(cx, cy, radius * .92f, paint);

        // Vinyl-like rings so the badge reads as a glass record, not a flat disc.
        stroke.setStrokeWidth(dp(.8f));
        for (int ring = 1; ring <= 3; ring++) {
            stroke.setColor(0x1FDFFAFF);
            canvas.drawCircle(cx, cy, radius * (.34f + ring * .17f), stroke);
        }

        // Only the typhoon arms spin; the pearl P itself stays upright.
        if (arms != null) {
            canvas.save();
            Path clip = new Path();
            clip.addCircle(cx, cy, radius * .92f, Path.Direction.CW);
            canvas.clipPath(clip);
            canvas.translate(cx, cy);
            canvas.rotate((float) Math.toDegrees(spin));
            float inner = radius * 1.45f;
            paint.setAlpha(255);
            canvas.drawBitmap(arms, null, new RectF(-inner, -inner, inner, inner), paint);
            canvas.restore();
        }
        if (core != null) {
            canvas.save();
            Path clip = new Path();
            clip.addCircle(cx, cy, radius * .92f, Path.Direction.CW);
            canvas.clipPath(clip);
            float inner = radius * 1.45f;
            canvas.drawBitmap(core, null, new RectF(cx - inner, cy - inner, cx + inner, cy + inner), paint);
            canvas.restore();
        }

        stroke.setStrokeWidth(dp(2.2f));
        stroke.setColor(0xD9EFFDFF);
        canvas.drawCircle(cx, cy, radius * .92f, stroke);
        stroke.setStrokeWidth(dp(1.2f));
        stroke.setColor(0x59BEEBF8);
        canvas.drawCircle(cx, cy, radius * 1.02f, stroke);

        paint.setShader(new RadialGradient(cx - radius * .34f, cy - radius * .40f, radius * .66f,
                new int[]{0x99FFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius * .92f, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.STROKE);
    }

    /**
     * Three spiral wind filaments. Drawn in radial bands so each strand tapers at
     * both ends instead of reading as a flat arc.
     */
    private void drawWind(Canvas canvas, float cx, float cy, float size) {
        if (wind <= .01f) return;
        float inner = size * .56f;
        float outer = size * 1.06f;
        float seconds = SystemClock.uptimeMillis() / 1000f;
        // Five radial bands per arm: enough to taper smoothly, cheap enough to
        // keep the whole scene at 60 fps (bands are paths, not per-segment lines).
        final int bands = 5;
        for (int arm = 0; arm < 3; arm++) {
            for (int band = 0; band < bands; band++) {
                float from = band / (float) bands;
                float to = (band + 1) / (float) bands;
                float mid = (from + to) / 2f;
                float taper = (float) Math.pow(Math.sin(Math.PI * mid), .65f);
                Path path = new Path();
                boolean started = false;
                for (int step = 0; step <= 14; step++) {
                    float f = from + (to - from) * step / 14f;
                    float r = inner + (outer - inner) * f;
                    double angle = (2 * Math.PI * arm - seconds * 3.8 + (r / size) * 24.0) / 3.0;
                    float x = cx + (float) Math.cos(angle) * r;
                    float y = cy + (float) Math.sin(angle) * r;
                    if (!started) {
                        path.moveTo(x, y);
                        started = true;
                    } else {
                        path.lineTo(x, y);
                    }
                }
                stroke.setStrokeWidth(dp(3.4f) + dp(4.6f) * taper);
                stroke.setColor(Color.argb((int) (30 * wind * taper), 120, 226, 250));
                canvas.drawPath(path, stroke);
                stroke.setStrokeWidth(dp(.8f) + dp(2.0f) * taper);
                stroke.setColor(Color.argb((int) (170 * wind * taper), 196, 248, 255));
                canvas.drawPath(path, stroke);
            }
        }
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new RadialGradient(cx, cy, outer,
                new int[]{0x00000000, Color.argb((int) (22 * wind), 150, 236, 255), 0x00000000},
                new float[]{.42f, .74f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, outer, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.STROKE);
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
