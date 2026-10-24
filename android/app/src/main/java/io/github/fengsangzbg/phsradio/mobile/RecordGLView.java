package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.GLUtils;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * The record, drawn with a real fragment shader instead of Canvas gradients.
 *
 * This is the phone port of the desktop scene.frag approach: the disc body is a
 * lit sphere section, the rim gets fresnel and dispersion computed per pixel,
 * grooves are a radial wave, and the album art is sampled as a texture in the
 * centre so it stays crisp and can spin independently.
 *
 * Transparent outside the disc; the surrounding glass panel shows through.
 */
final class RecordGLView extends GLSurfaceView {
    private final Renderer renderer = new Renderer();

    RecordGLView(Context context) {
        super(context);
        setEGLContextClientVersion(2);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(false);
        setZOrderMediaOverlay(false);
        setRenderer(renderer);
        setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
    }

    void cover(Bitmap bitmap) { renderer.pendingCover = bitmap; }
    void playing(boolean value) { renderer.playing = value; }
    void theme(boolean value) { renderer.warm = value; }
    void active(boolean value) { renderer.active = value; }

    private static final String VERTEX = ""
            + "attribute vec2 aPos;\n"
            + "void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }\n";

    private static final String FRAGMENT = """
            precision mediump float;
            uniform vec2 uResolution;
            uniform float uRotation;
            uniform float uTime;
            uniform float uHasArt;
            uniform float uWarm;
            uniform sampler2D uArt;

            vec3 hue2rgb(float h){
              vec3 k = mod(vec3(5.0, 3.0, 1.0) + h * 6.0, 6.0);
              return clamp(min(k, 4.0 - k), 0.0, 1.0);
            }

            void main(){
              vec2 res = uResolution;
              vec2 p = (gl_FragCoord.xy * 2.0 - res) / min(res.x, res.y);
              float r = length(p);
              float R = 0.90;
              float edge = smoothstep(R, R - 0.010, r);
              if (edge <= 0.0) { gl_FragColor = vec4(0.0); return; }
              float rn = clamp(r / R, 0.0, 1.0);
              float a = atan(p.y, p.x);

              vec3 n = normalize(vec3(p, sqrt(max(0.0, 1.0 - rn * rn)) * 1.35));
              vec3 L = normalize(vec3(-0.45, 0.66, 0.62));
              float diff = max(0.0, dot(n, L));
              float fres = pow(1.0 - max(0.0, n.z), 3.0);

              vec3 deep = vec3(0.020, 0.075, 0.115);
              vec3 cool = vec3(0.075, 0.280, 0.360);
              vec3 col = mix(deep, cool, 0.35 + 0.45 * diff);
              col += vec3(0.30, 0.72, 0.85) * fres * 0.55;

              float band = pow(max(0.0, cos(a - 2.45)), 6.0);
              col += vec3(0.55, 0.85, 0.95) * band * smoothstep(0.22, 0.95, rn) * 0.38;

              float groove = 0.5 + 0.5 * sin(rn * 140.0);
              float gm = smoothstep(0.50, 0.60, rn) * (1.0 - smoothstep(0.94, 1.0, rn));
              col *= 1.0 - 0.045 * groove * gm;

              float disp = smoothstep(0.974, 1.0, rn);
              col += hue2rgb(fract(0.53 + a / 6.28318 + 0.05 * sin(uTime * 0.5))) * disp * 0.65;

              float alpha = mix(0.46, 0.94, smoothstep(0.35, 1.0, rn));
              alpha = max(alpha, fres * 0.92);

              float artR = 0.445 * R;
              if (uHasArt > 0.5 && r < artR * 1.02) {
                 float s = sin(-uRotation), c = cos(-uRotation);
                 vec2 q = mat2(c, -s, s, c) * p;
                 vec2 uv = q / (2.0 * artR) + 0.5;
                 uv.y = 1.0 - uv.y;
                 vec3 art = texture2D(uArt, clamp(uv, 0.002, 0.998)).rgb;
                 gl_FragColor = vec4(art, 1.0);
                 return;
              }

              float bezel = smoothstep(artR * 0.975, artR * 1.015, r) * smoothstep(artR * 1.09, artR * 1.02, r);
              col += vec3(0.85, 0.97, 1.0) * bezel * 0.95;
              alpha = max(alpha, bezel);

              float rim = smoothstep(0.982, 1.0, rn);
              col += vec3(0.90, 0.99, 1.0) * rim * 0.95;
              alpha = max(alpha, rim * 0.95);

              col = mix(col, col * vec3(1.06, 0.94, 0.88), uWarm * 0.5);
              gl_FragColor = vec4(col * alpha, alpha) * edge;
            }
            """;
    private final class Renderer implements GLSurfaceView.Renderer {
        private int program, position, resolution, rotation, time, hasArt, artSampler;
        private int warmUniform;
        private int texture;
        private FloatBuffer quad;
        private float spin;
        private long last;
        volatile Bitmap pendingCover;
        volatile boolean playing = true;
        volatile boolean active = true;
        volatile boolean warm;

        Renderer() {
            float[] verts = {-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f};
            quad = ByteBuffer.allocateDirect(verts.length * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
            quad.put(verts).position(0);
        }

        @Override public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            GLES20.glClearColor(0f, 0f, 0f, 0f);
            int vs = compile(GLES20.GL_VERTEX_SHADER, VERTEX);
            int fs = compile(GLES20.GL_FRAGMENT_SHADER, FRAGMENT);
            program = GLES20.glCreateProgram();
            GLES20.glAttachShader(program, vs);
            GLES20.glAttachShader(program, fs);
            GLES20.glLinkProgram(program);
            GLES20.glUseProgram(program);
            position = GLES20.glGetAttribLocation(program, "aPos");
            resolution = GLES20.glGetUniformLocation(program, "uResolution");
            rotation = GLES20.glGetUniformLocation(program, "uRotation");
            time = GLES20.glGetUniformLocation(program, "uTime");
            hasArt = GLES20.glGetUniformLocation(program, "uHasArt");
            warmUniform = GLES20.glGetUniformLocation(program, "uWarm");
            artSampler = GLES20.glGetUniformLocation(program, "uArt");
            texture = 0;
            last = 0;
        }

        @Override public void onSurfaceChanged(GL10 unused, int width, int height) {
            GLES20.glViewport(0, 0, width, height);
            GLES20.glUseProgram(program);
            GLES20.glUniform2f(resolution, width, height);
        }

        @Override public void onDrawFrame(GL10 unused) {
            long now = System.nanoTime();
            float dt = last == 0 ? 0f : Math.min(.05f, (now - last) / 1e9f);
            last = now;
            if (active && playing) spin += dt * 1.15f;

            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
            GLES20.glUseProgram(program);

            Bitmap pending = pendingCover;
            if (pending != null) {
                pendingCover = null;
                if (texture != 0) GLES20.glDeleteTextures(1, new int[]{texture}, 0);
                int[] ids = new int[1];
                GLES20.glGenTextures(1, ids, 0);
                texture = ids[0];
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
                GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, pending, 0);
            }

            if (texture != 0) {
                GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
                GLES20.glUniform1i(artSampler, 0);
                GLES20.glUniform1f(hasArt, 1f);
            } else {
                GLES20.glUniform1f(hasArt, 0f);
            }
            GLES20.glUniform1f(rotation, spin);
            GLES20.glUniform1f(time, now / 1e9f);
            GLES20.glUniform1f(warmUniform, warm ? 1f : 0f);

            GLES20.glEnable(GLES20.GL_BLEND);
            GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA);
            quad.position(0);
            GLES20.glEnableVertexAttribArray(position);
            GLES20.glVertexAttribPointer(position, 2, GLES20.GL_FLOAT, false, 0, quad);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
            GLES20.glDisableVertexAttribArray(position);
        }

        private int compile(int type, String source) {
            int shader = GLES20.glCreateShader(type);
            GLES20.glShaderSource(shader, source);
            GLES20.glCompileShader(shader);
            return shader;
        }
    }
}
