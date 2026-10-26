package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.Build;
import android.view.View;

/**
 * The water heart, entirely in AGSL on Android 13+.
 *
 * A transparent cutout of the approved crystal artwork is the shader input; the
 * shader derives the soft edge, the empty-state outline and the translucent
 * body from its alpha. Canvas only carries the shader to the screen (and holds
 * the text-free fallback on older devices).
 */
final class CrystalHeartView extends View {
    private static final String AGSL = """
            uniform float uLit;
            uniform float uCelebrate;
            uniform float uArtScale;
            uniform float2 uArtOffset;
            uniform shader uArt;

            half4 main(float2 fragCoord) {
                float2 tex = (fragCoord - uArtOffset) / uArtScale;
                half4 art = uArt.eval(tex);
                float a = art.a;
                if (a < 0.01) return half4(0.0);

                if (uLit < 0.5 && uCelebrate < 0.5) {
                    // Soft heart outline before the song is collected.
                    float edge = smoothstep(0.04, 0.28, a)
                            * (1.0 - smoothstep(0.55, 0.95, a));
                    return half4(half3(0.86, 0.97, 1.0), half(edge * 0.55));
                }

                float3 col = float3(art.rgb);
                col += float3(0.10, 0.30, 0.45)
                        * (1.0 - smoothstep(0.35, 0.95, a)) * 0.35;
                return half4(half3(col), half(a * 0.62));
            }
            """;

    private static Bitmap artwork;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Matrix artMatrix = new Matrix();
    private BitmapShader artShader;
    private RuntimeShader shader;
    private boolean shaderTried;
    private boolean lit;
    private boolean celebratory;
    private float artScale = 1f, artOffsetX, artOffsetY;

    CrystalHeartView(Context context) {
        super(context);
        setLayerType(LAYER_TYPE_HARDWARE, null);
        if (artwork == null) {
            artwork = BitmapFactory.decodeResource(context.getResources(), R.drawable.heart_cutout);
        }
    }

    static Bitmap artwork() { return artwork; }

    void setLit(boolean value) {
        lit = value;
        invalidate();
    }

    boolean isLit() { return lit; }

    void celebrate() {
        celebratory = true;
        invalidate();
    }

    private RuntimeShader shader() {
        if (!shaderTried) {
            shaderTried = true;
            if (Build.VERSION.SDK_INT >= 33 && artwork != null) {
                try {
                    shader = new RuntimeShader(AGSL);
                    artShader = new BitmapShader(artwork, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP);
                    shader.setInputShader("uArt", artShader);
                } catch (Throwable ignored) {
                    shader = null;
                }
            }
        }
        return shader;
    }

    private void updateMatrix(int w, int h) {
        if (artwork == null) return;
        artScale = Math.min(w / (float) artwork.getWidth(), h / (float) artwork.getHeight());
        artOffsetX = (w - artwork.getWidth() * artScale) * .5f;
        artOffsetY = (h - artwork.getHeight() * artScale) * .5f;
        artMatrix.setScale(artScale, artScale);
        artMatrix.postTranslate(artOffsetX, artOffsetY);
    }

    @Override protected void onDraw(Canvas canvas) {
        final int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0 || artwork == null) return;

        RuntimeShader stone = shader();
        if (stone != null) {
            updateMatrix(w, h);
            stone.setFloatUniform("uLit", lit ? 1f : 0f);
            stone.setFloatUniform("uCelebrate", celebratory ? 1f : 0f);
            stone.setFloatUniform("uArtScale", artScale);
            stone.setFloatUniform("uArtOffset", artOffsetX, artOffsetY);
            paint.setShader(stone);
            canvas.drawRect(0, 0, w, h, paint);
            paint.setShader(null);
            return;
        }

        updateMatrix(w, h);
        paint.setAlpha(lit || celebratory ? 160 : 90);
        canvas.drawBitmap(artwork, artMatrix, paint);
        paint.setAlpha(255);
    }
}
