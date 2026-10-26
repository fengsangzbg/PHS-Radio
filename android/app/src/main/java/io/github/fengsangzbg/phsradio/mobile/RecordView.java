package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.Build;
import android.view.View;

/**
 * The record, ported from the desktop GlassRecord.
 *
 * Desktop paints a dark glass plate first, adds nineteen groove rings and a
 * conical reflection, then rotates that finished plate as one texture with the
 * artwork in the middle. The phone used to be a pale GL disc on a black surface;
 * this version keeps the desktop's dark glass and its proportions, but draws
 * into a normal transparent View so the water behind it stays visible.
 *
 * A slow liquid sheen moves over the glass independently of the disc rotation.
 */
final class RecordView extends View {
    private static final int[] RIM_COLORS = {
            0x80FF6EC7, 0x8083E8FF, 0x8089FF9E, 0x80FFE066, 0x80FF6EC7
    };
    private static final float[] RIM_POSITIONS = {0f, .25f, .5f, .75f, 1f};

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint note = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint shaderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path disc = new Path();
    private final Path artClip = new Path();
    private final Matrix spinMatrix = new Matrix();
    private final RectF artRect = new RectF();
    private RuntimeShader recordShader;
    private boolean shaderTried;
    private Bitmap artBitmap;
    private BitmapShader artShader;

    private Bitmap cover;
    private boolean playing = true;
    private boolean active = true;
    private boolean warm;
    private float spin;
    private float time;
    private long lastFrame;

    RecordView(Context context) {
        super(context);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        note.setTextAlign(Paint.Align.CENTER);
        note.setTypeface(Typeface.DEFAULT_BOLD);
        note.setColor(0x99E6F4FF);
        setBackground(null);
    }

    void cover(Bitmap bitmap) {
        cover = bitmap;
        invalidate();
    }

    void playing(boolean value) {
        playing = value;
        invalidate();
    }

    void theme(boolean value) {
        warm = value;
        invalidate();
    }

    void active(boolean value) {
        active = value;
        lastFrame = 0;
        if (value) invalidate();
    }

    void onPause() { active = false; }
    void onResume() { active = true; lastFrame = 0; invalidate(); }

    @Override protected void onDraw(Canvas canvas) {
        final int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;

        long now = System.nanoTime();
        float dt = lastFrame == 0 ? 0f : Math.min(.05f, (now - lastFrame) / 1e9f);
        lastFrame = now;
        time += dt;
        if (playing) spin += dt * .92f;

        if (drawShader(canvas, w, h)) {
            if (active) postInvalidateOnAnimation();
            return;
        }

        final float cx = w * .5f, cy = h * .5f;
        final float radius = Math.min(w, h) * .455f;
        final float artRadius = radius * .58f;

        disc.reset();
        disc.addCircle(cx, cy, radius, Path.Direction.CW);
        artClip.reset();
        artClip.addCircle(cx, cy, artRadius, Path.Direction.CW);
        artRect.set(cx - artRadius, cy - artRadius, cx + artRadius, cy + artRadius);

        canvas.save();
        canvas.clipPath(disc);
        canvas.save();
        canvas.rotate((float) Math.toDegrees(spin), cx, cy);

        drawPlate(canvas, cx, cy, radius);
        drawGrooves(canvas, cx, cy, radius);
        drawReflection(canvas, cx, cy, radius);
        drawLiquid(canvas, cx, cy, radius);
        drawCover(canvas, cx, cy, artRadius);
        drawSpindle(canvas, cx, cy);

        canvas.restore();
        if (warm) {
            paint.setColor(0x12FFB07A);
            canvas.drawCircle(cx, cy, radius, paint);
        }
        canvas.restore();

        drawSpecular(canvas, cx, cy, radius);
        drawRim(canvas, cx, cy, radius);
        if (active) postInvalidateOnAnimation();
    }

    private void drawPlate(Canvas canvas, float cx, float cy, float radius) {
        // Desktop glass body: a dark, low-opacity pane rather than a white disc.
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(0, cy - radius, 0, cy + radius,
                new int[]{0x300A1C26, 0x40041119, 0x5A040E16},
                new float[]{0f, .55f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius, paint);

        paint.setShader(new LinearGradient(0, cy - radius, 0, cy + radius * .08f,
                new int[]{0x24FFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius, paint);

        paint.setShader(new RadialGradient(cx, cy, radius,
                new int[]{0x0F73D5E3, 0x00000000},
                new float[]{0f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius, paint);

        // Darker floor and edge so the plate has thickness.
        paint.setShader(new RadialGradient(cx, cy, radius,
                new int[]{0x00000000, 0x00000000, 0x3D000A12},
                new float[]{0f, .82f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius, paint);
        paint.setShader(null);
    }

    private void drawGrooves(Canvas canvas, float cx, float cy, float radius) {
        // Desktop: 19 rings, every third one brighter.
        for (int ring = 0; ring < 19; ring++) {
            float r = radius * (.95f - .016f * ring);
            stroke.setStrokeWidth(dp(.8f));
            stroke.setColor(ring % 3 == 0 ? 0x1ADFF8FF : 0x0AFFFFFF);
            canvas.drawCircle(cx, cy, r, stroke);
        }
    }

    private void drawReflection(Canvas canvas, float cx, float cy, float radius) {
        // The conical reflection from the desktop plate: two narrow white bands.
        spinMatrix.setRotate(36f, cx, cy);
        SweepGradient reflection = new SweepGradient(cx, cy,
                new int[]{0x00FFFFFF, 0x30E7F7FF, 0x00FFFFFF, 0x00FFFFFF,
                        0x1DE0F5FF, 0x00FFFFFF, 0x00FFFFFF},
                new float[]{0f, .09f, .17f, .56f, .66f, .77f, 1f});
        reflection.setLocalMatrix(spinMatrix);
        paint.setShader(reflection);
        canvas.drawCircle(cx, cy, radius, paint);
        paint.setShader(null);
    }

    private void drawLiquid(Canvas canvas, float cx, float cy, float radius) {
        // A slow sheen that is not part of the spinning plate.
        spinMatrix.setRotate((float) Math.toDegrees(time * .22f), cx, cy);
        SweepGradient cool = new SweepGradient(cx, cy,
                new int[]{0x00FFFFFF, 0x1AD8FBFF, 0x00FFFFFF, 0x12FFFFFF, 0x00FFFFFF},
                new float[]{0f, .2f, .45f, .7f, 1f});
        cool.setLocalMatrix(spinMatrix);
        paint.setShader(cool);
        canvas.drawCircle(cx, cy, radius, paint);

        spinMatrix.setRotate(-(float) Math.toDegrees(time * .14f) + 50f, cx, cy);
        SweepGradient warmSweep = new SweepGradient(cx, cy,
                new int[]{0x00FFFFFF, 0x10B9F2FF, 0x00FFFFFF, 0x0CFFE7C9, 0x00FFFFFF},
                new float[]{0f, .3f, .55f, .8f, 1f});
        warmSweep.setLocalMatrix(spinMatrix);
        paint.setShader(warmSweep);
        canvas.drawCircle(cx, cy, radius, paint);

        float hx = cx + (float) Math.cos(time * .31f) * radius * .3f;
        float hy = cy + (float) Math.sin(time * .26f) * radius * .3f;
        paint.setShader(new RadialGradient(hx, hy, radius * .82f,
                new int[]{0x1CFFFFFF, 0x00FFFFFF}, null, Shader.TileMode.CLAMP));
        canvas.drawCircle(cx, cy, radius, paint);
        paint.setShader(null);
    }

    private void drawCover(Canvas canvas, float cx, float cy, float artRadius) {
        if (cover != null) {
            canvas.save();
            canvas.clipPath(artClip);
            float side = Math.min(cover.getWidth(), cover.getHeight());
            Rect src = new Rect((int) ((cover.getWidth() - side) * .5f),
                    (int) ((cover.getHeight() - side) * .5f),
                    (int) ((cover.getWidth() + side) * .5f),
                    (int) ((cover.getHeight() + side) * .5f));
            paint.setAlpha(255);
            canvas.drawBitmap(cover, src, artRect, paint);
            canvas.restore();
        } else {
            // Desktop shows a dark label with a note, never a white plate.
            paint.setShader(new RadialGradient(cx, cy, artRadius,
                    new int[]{0xCC123040, 0xE60A1822},
                    new float[]{0f, 1f}, Shader.TileMode.CLAMP));
            canvas.drawCircle(cx, cy, artRadius, paint);
            paint.setShader(null);
            note.setTextSize(artRadius * .6f);
            Paint.FontMetrics fm = note.getFontMetrics();
            canvas.drawText("♪", cx, cy - (fm.ascent + fm.descent) * .5f, note);
        }
        stroke.setStrokeWidth(dp(1f));
        stroke.setColor(0x80F5FAFF);
        canvas.drawCircle(cx, cy, artRadius, stroke);
        stroke.setStrokeWidth(dp(6f));
        stroke.setColor(0x22BFF6FF);
        canvas.drawCircle(cx, cy, artRadius + dp(2f), stroke);
    }

    private void drawSpindle(Canvas canvas, float cx, float cy) {
        paint.setColor(0xCC0F1620);
        canvas.drawCircle(cx, cy, dp(4f), paint);
        stroke.setStrokeWidth(dp(1f));
        stroke.setColor(0x8CFFFFFF);
        canvas.drawCircle(cx, cy, dp(4f), stroke);
    }

    private void drawSpecular(Canvas canvas, float cx, float cy, float radius) {
        RectF oval = new RectF(cx - radius * .88f, cy - radius * .88f,
                cx + radius * .88f, cy + radius * .88f);
        stroke.setStrokeWidth(dp(10f));
        stroke.setColor(0x14FFFFFF);
        canvas.drawArc(oval, 198f, 72f, false, stroke);
        stroke.setStrokeWidth(dp(3.5f));
        stroke.setColor(0x2EFFFFFF);
        canvas.drawArc(oval, 204f, 44f, false, stroke);
        stroke.setStrokeWidth(dp(2f));
        stroke.setColor(0x1FFFFFFF);
        canvas.drawArc(oval, 24f, 52f, false, stroke);
    }

    private void drawRim(Canvas canvas, float cx, float cy, float radius) {
        for (int i = 2; i >= 0; i--) {
            stroke.setStrokeWidth(dp(5f + i * 5f));
            stroke.setColor(0x0DA8F0FF);
            canvas.drawCircle(cx, cy, radius + dp(2f + i * 2.5f), stroke);
        }

        stroke.setStrokeWidth(dp(1.2f));
        stroke.setColor(0xD9E8FDFF);
        canvas.drawCircle(cx, cy, radius - dp(.6f), stroke);
        stroke.setStrokeWidth(dp(3f));
        stroke.setColor(0x33001826);
        canvas.drawCircle(cx, cy, radius - dp(3.2f), stroke);

        spinMatrix.setRotate((float) Math.toDegrees(time * .1f), cx, cy);
        SweepGradient rainbow = new SweepGradient(cx, cy, RIM_COLORS, RIM_POSITIONS);
        rainbow.setLocalMatrix(spinMatrix);
        stroke.setShader(rainbow);
        stroke.setStrokeWidth(dp(2.4f));
        canvas.drawCircle(cx, cy, radius - dp(1.7f), stroke);
        stroke.setShader(null);
    }

    private static final String AGSL = """
            uniform float2 uSize;
            uniform float uTime;
            uniform float uSpin;
            uniform float uHasArt;
            uniform float uWarm;
            uniform shader uArt;

            float3 hue(float h) {
                float3 k = mod(float3(5.0, 3.0, 1.0) + h * 6.0, 6.0);
                return clamp(min(k, 4.0 - k), 0.0, 1.0);
            }

            half4 main(float2 fragCoord) {
                float2 res = uSize;
                float2 p = (fragCoord - res * 0.5) / (min(res.x, res.y) * 0.5);
                float r = length(p);
                float R = 0.90;
                float inside = 1.0 - smoothstep(R - 0.006, R, r);
                if (inside <= 0.0) return half4(0.0);
                float rn = clamp(r / R, 0.0, 1.0);
                float a = atan(p.y, p.x);

                // Thin glass plate: the wallpaper must read straight through it.
                float3 col = float3(0.14, 0.26, 0.31);
                float3 n = normalize(float3(p, sqrt(max(0.0, 1.0 - rn * rn)) * 1.4));
                float diff = max(0.0, dot(n, normalize(float3(-0.45, 0.65, 0.62))));
                col += float3(0.16, 0.28, 0.34) * (0.45 + 0.55 * diff) * 0.30;

                // Fine grooves: light etched lines, not a dark plate.
                float groove = 0.5 + 0.5 * sin(rn * 230.0);
                groove += 0.45 * (0.5 + 0.5 * sin(rn * 470.0));
                float gm = smoothstep(0.44, 0.54, rn) * (1.0 - smoothstep(0.94, 1.0, rn));
                col += float3(0.45, 0.70, 0.80) * groove * gm * 0.10;

                // Conical reflection, baked to the rotating plate.
                float band = pow(max(0.0, cos(a + uSpin - 2.4)), 18.0);
                col += float3(0.55, 0.85, 1.0) * band * smoothstep(0.25, 0.95, rn) * 0.05;
                float band2 = pow(max(0.0, cos(a + uSpin + 0.55)), 80.0);
                col += float3(0.90, 0.98, 1.0) * band2 * smoothstep(0.50, 1.0, rn) * 0.03;

                // A slow liquid sheen under the glass.
                float ca = sin(p.x * 7.0 + uTime * 0.45) * sin(p.y * 6.0 - uTime * 0.35);
                col += float3(0.04, 0.16, 0.20) * max(ca, 0.0) * 0.20;

                // Fresnel and the Aero dispersion in the last percent of the rim.
                float fres = pow(1.0 - sqrt(max(0.0, 1.0 - rn * rn)), 3.0);
                col += float3(0.40, 0.70, 0.92) * fres * 0.20;
                float rim = 1.0 - smoothstep(0.0, 0.008, abs(R - r));
                col += float3(0.90, 0.99, 1.0) * rim * 0.35;

                float alpha = mix(0.035, 0.14, smoothstep(0.15, 1.0, rn));
                alpha += 0.045 * groove * gm;
                alpha = max(alpha, fres * 0.25);
                alpha = max(alpha, rim * 0.35);

                // Album art, rotated with the plate.
                float artR = 0.58;
                if (uHasArt > 0.5 && r < artR * 1.01) {
                    float s = sin(-uSpin), c = cos(-uSpin);
                    float2 q = float2(c * p.x - s * p.y, s * p.x + c * p.y);
                    float2 uv = q / (2.0 * artR) + 0.5;
                    uv.y = 1.0 - uv.y;
                    half4 art = uArt.eval(uv * res);
                    col = float3(art.rgb);
                    alpha = 1.0;
                } else if (uHasArt <= 0.5 && r < artR) {
                    col = float3(0.025, 0.085, 0.115);
                    alpha = 0.06;
                }

                float bezel = smoothstep(artR * 0.985, artR * 0.998, r)
                        * (1.0 - smoothstep(artR * 1.010, artR * 1.030, r));
                col += float3(0.90, 0.98, 1.0) * bezel * 0.22;
                alpha = max(alpha, bezel * 0.20);

                float spindle = 1.0 - smoothstep(0.010, 0.014, r);
                col = mix(col, float3(0.02, 0.05, 0.07), spindle);
                alpha = max(alpha, spindle * 0.50);

                if (uWarm > 0.5) col *= float3(1.04, 0.96, 0.92);
                return half4(half3(col * alpha), half(alpha * inside));
            }
            """;

    private RuntimeShader shader() {
        if (!shaderTried) {
            shaderTried = true;
            if (Build.VERSION.SDK_INT >= 33) {
                try {
                    recordShader = new RuntimeShader(AGSL);
                    Bitmap empty = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888);
                    artShader = new BitmapShader(empty, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP);
                    recordShader.setInputShader("uArt", artShader);
                } catch (Throwable ignored) {
                    recordShader = null;
                }
            }
        }
        return recordShader;
    }

    private boolean drawShader(Canvas canvas, int w, int h) {
        RuntimeShader stone = shader();
        if (stone == null) return false;
        if (cover != null && cover != artBitmap) {
            artBitmap = cover;
            artShader = new BitmapShader(cover, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP);
            stone.setInputShader("uArt", artShader);
        }
        stone.setFloatUniform("uSize", (float) w, (float) h);
        stone.setFloatUniform("uTime", time);
        stone.setFloatUniform("uSpin", spin);
        stone.setFloatUniform("uHasArt", cover != null ? 1f : 0f);
        stone.setFloatUniform("uWarm", warm ? 1f : 0f);
        shaderPaint.setShader(stone);
        canvas.drawRect(0, 0, w, h, shaderPaint);
        shaderPaint.setShader(null);
        return true;
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }
}