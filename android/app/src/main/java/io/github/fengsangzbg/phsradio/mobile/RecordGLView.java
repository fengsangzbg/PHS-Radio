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

    private static final String FRAGMENT = ""
            + "precision mediump float;\n"
            + "uniform vec2 uResolution;\n"
            + "uniform float uRotation;\n"
            + "uniform float uTime;\n"
            + "uniform float uHasArt;\n"
            + "uniform float uWarm;\n"
            + "uniform sampler2D uArt;\n"
            + "\n"
            + "vec3 hue2rgb(float h){\n"
            + "  vec3 k = mod(vec3(5.0, 3.0, 1.0) + h * 6.0, 6.0);\n"
            + "  return clamp(min(k, 4.0 - k), 0.0, 1.0);\n"
            + "}\n"
            + "\n"
            + "void main(){\n"
            + "  vec2 res = uResolution;\n"
            + "  vec2 p = (gl_FragCoord.xy * 2.0 - res) / min(res.x, res.y);\n"
            + "  float r = length(p);\n"
            + "  float R = 0.90;\n"
            + "  float edge = smoothstep(R, R - 0.012, r);\n"
            + "  if (edge <= 0.0) { gl_FragColor = vec4(0.0); return; }\n"
            + "  float rn = clamp(r / R, 0.0, 1.0);\n"
            + "  float a = atan(p.y, p.x);\n"
            + "\n"
            + "  // Sphere section: the disc is a shallow dome lit from the upper left.\n"
            + "  vec3 n = normalize(vec3(p, sqrt(max(0.0, 1.0 - rn * rn)) * 1.6));\n"
            + "  vec3 L = normalize(vec3(-0.42, 0.62, 0.66));\n"
            + "  float diff = max(0.0, dot(n, L));\n"
            + "  float fres = pow(1.0 - max(0.0, n.z), 3.0);\n"
            + "\n"
            + "  vec3 baseA = vec3(0.87, 0.97, 1.00);\n"
            + "  vec3 baseB = vec3(0.28, 0.60, 0.73);\n"
            + "  vec3 body = mix(baseA, baseB, smoothstep(0.05, 1.05, rn));\n"
            + "  body += vec3(0.30) * diff;\n"
            + "  body *= 0.92 + 0.16 * fres;\n"
            + "\n"
            + "  // Grooves: a radial wave, fading before the artwork.\n"
            + "  float groove = 0.5 + 0.5 * sin(rn * 210.0);\n"
            + "  float grooveMask = smoothstep(0.50, 0.62, rn) * (1.0 - smoothstep(0.93, 1.0, rn));\n"
            + "  body *= 1.0 - 0.10 * groove * grooveMask;\n"
            + "\n"
            + "  // Dispersion ring: the Aero rainbow lives in the last few percent of the rim.\n"
            + "  float disp = smoothstep(0.955, 1.0, rn);\n"
            + "  float hue = fract(0.52 + a / 6.28318 + 0.06 * sin(uTime * 0.6));\n"
            + "  body += hue2rgb(hue) * disp * 0.55;\n"
            + "\n"
            + "  // Two specular arcs, fixed in world space like studio softboxes.\n"
            + "  float arcA = pow(max(0.0, cos(a - 2.35)), 26.0) * smoothstep(0.55, 0.92, rn);\n"
            + "  float arcB = pow(max(0.0, cos(a + 0.55)), 90.0) * smoothstep(0.60, 0.95, rn);\n"
            + "  body += vec3(1.0) * arcA * 0.55 + vec3(0.85, 0.97, 1.0) * arcB * 0.45;\n"
            + "\n"
            + "  // Album art: a texture in the middle, rotated by the spin uniform.\n"
            + "  float artR = 0.455 * R;\n"
            + "  if (uHasArt > 0.5 && r < artR * 1.01) {\n"
            + "     float s = sin(-uRotation), c = cos(-uRotation);\n"
            + "     vec2 q = mat2(c, -s, s, c) * p;\n"
            + "     vec2 uv = q / (2.0 * artR) + 0.5;\n"
            + "     uv.y = 1.0 - uv.y;\n"
            + "     vec3 art = texture2D(uArt, clamp(uv, 0.001, 0.999)).rgb;\n"
            + "     gl_FragColor = vec4(art, 1.0);\n"
            + "     return;\n"
            + "  }\n"
            + "\n"
            + "  // Bezel: a bright ring hugging the artwork.\n"
            + "  float bezel = smoothstep(artR * 0.985, artR * 1.02, r) * smoothstep(artR * 1.10, artR * 1.03, r);\n"
            + "  body += vec3(1.0) * bezel * 0.85;\n"
            + "  body = mix(body, body * vec3(1.04, 0.98, 0.94), uWarm * 0.5);\n"
            + "\n"
            + "  gl_FragColor = vec4(body * edge, edge);\n"
            + "}\n";

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
