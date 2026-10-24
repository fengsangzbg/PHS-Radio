package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.*;
import java.util.Random;

/**
 * Native, display-synchronised drawing. No bitmap blur or per-frame image decoding.
 *
 * The disc is the desktop record recreated on Canvas: a deep navy body, fine
 * grooves, a lit rim, and the album art in a crisp opaque circle at the centre.
 * The glass sheen only covers the annulus around the art, so the artwork itself
 * is never veiled.
 */
final class GlassVisual extends View {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    private final float[] stars=new float[240];
    private final boolean sky;
    private final FireSky fireSky=new FireSky();
    private final WaterSky waterSky=new WaterSky();
    private boolean fireTheme=true;
    private double skySeconds;
    private long skyTick;
    private Shader background,glass;
    private Bitmap cover;
    private boolean active=true,playing;
    private long last;
    private float rotation;
    private final Path clip=new Path();
    private final Path annulus=new Path();
    private final RectF coverRect=new RectF();
    private final Rect square=new Rect();

    GlassVisual(Context context,boolean sky){
        super(context);
        this.sky=sky;
        Random random=new Random(31);
        for(int i=0;i<stars.length;i++)stars[i]=random.nextFloat();
    }

    void theme(boolean fire){fireTheme=fire;invalidate();}
    void playing(boolean value){playing=value;last=0;invalidate();}
    void cover(Bitmap value){cover=value;invalidate();}
    void active(boolean value){active=value;last=0;skyTick=0;if(value)invalidate();}

    @Override protected void onSizeChanged(int w,int h,int ow,int oh){
        fireSky.resize(w,h);
        waterSky.resize(w,h);
        background=new LinearGradient(0,0,w,h,new int[]{0xFF070D29,0xFF1F1D44,0xFF513347},null,Shader.TileMode.CLAMP);
        glass=new LinearGradient(0,0,w,h,new int[]{0x6684E5F2,0x1531BFD8,0x887CCBE4,0x223F689A},null,Shader.TileMode.CLAMP);
    }

    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);
        float w=getWidth(),h=getHeight();
        long now=SystemClock.uptimeMillis();
        if(sky){
            long tick=SystemClock.elapsedRealtimeNanos();
            if(active&&skyTick!=0)skySeconds+=(tick-skyTick)/1_000_000_000.0;
            skyTick=active?tick:0;
            if(fireTheme)fireSky.draw(canvas,getWidth(),getHeight(),skySeconds,getResources().getDisplayMetrics().density);
            else waterSky.draw(canvas,getWidth(),getHeight(),skySeconds,getResources().getDisplayMetrics().density);
        }else{
            drawRecord(canvas,w,h,now);
        }
        if(active&&(sky||playing)&&isShown())postInvalidateOnAnimation();
    }

    private void drawRecord(Canvas canvas,float w,float h,long now){
        if(last!=0&&playing)rotation+=(Math.min(50,now-last))*.012f;
        last=now;
        float cx=w/2f,cy=h/2f,r=Math.min(w,h)*.44f;

        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new RadialGradient(cx,cy+r*.05f,r*1.10f,
                new int[]{0x66000610,0x00000610},null,Shader.TileMode.CLAMP));
        canvas.drawCircle(cx,cy+r*.05f,r*1.10f,paint);

        // Body: lit from the upper left, sinking into deep water at the rim.
        paint.setShader(new RadialGradient(cx-r*.26f,cy-r*.30f,r*1.18f,
                new int[]{0xFF24506F,0xFF153952,0xFF081C2C},
                new float[]{0f,.52f,1f},Shader.TileMode.CLAMP));
        canvas.drawCircle(cx,cy,r,paint);
        paint.setShader(null);

        // Grooves.
        paint.setStyle(Paint.Style.STROKE);
        for(int i=1;i<=18;i++){
            float rr=r*(1f-i*.034f);
            if(rr<=r*.50f)break;
            paint.setStrokeWidth(1f);
            paint.setColor(i%3==0?0x3A5EE8FF:0x1A2E7A99);
            canvas.drawCircle(cx,cy,rr,paint);
        }
        // Rim light.
        paint.setStrokeWidth(2.6f);
        paint.setColor(0xCCE8FCFF);
        canvas.drawCircle(cx,cy,r-1.5f,paint);
        paint.setStrokeWidth(1f);
        paint.setColor(0x55BEEBF8);
        canvas.drawCircle(cx,cy,r-5f,paint);

        // Artwork disc: opaque backing first so nothing shows through a PNG cover.
        float cr=r*.46f;
        clip.reset();
        clip.addCircle(cx,cy,cr,Path.Direction.CW);
        canvas.save();
        canvas.clipPath(clip);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xFF05121C);
        canvas.drawCircle(cx,cy,cr,paint);
        if(cover!=null){
            float side=Math.min(cover.getWidth(),cover.getHeight());
            float ox=(cover.getWidth()-side)/2f,oy=(cover.getHeight()-side)/2f;
            square.set((int)ox,(int)oy,(int)(ox+side),(int)(oy+side));
            coverRect.set(cx-cr,cy-cr,cx+cr,cy+cr);
            paint.setAlpha(255);
            paint.setFilterBitmap(true);
            canvas.save();
            canvas.rotate(rotation,cx,cy);
            canvas.drawBitmap(cover,square,coverRect,paint);
            canvas.restore();
        }
        canvas.restore();

        // Crisp rim around the artwork, then the spindle when there is no art.
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2f);
        paint.setColor(0xDDDFF7FF);
        canvas.drawCircle(cx,cy,cr,paint);
        paint.setStrokeWidth(1f);
        paint.setColor(0x66234A63);
        canvas.drawCircle(cx,cy,cr+2f,paint);
        if(cover==null){
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0xFFB9EDF1);
            canvas.drawCircle(cx,cy,r*.045f,paint);
        }

        // Glass sheen across the grooves only: the artwork stays unmistakably clear.
        annulus.reset();
        annulus.setFillType(Path.FillType.EVEN_ODD);
        annulus.addCircle(cx,cy,r-1f,Path.Direction.CW);
        annulus.addCircle(cx,cy,cr+3f,Path.Direction.CCW);
        canvas.save();
        canvas.clipPath(annulus);
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(cx-r,cy-r,cx+r*.30f,cy+r*.55f,
                new int[]{0x59FFFFFF,0x14FFFFFF,0x00FFFFFF},
                new float[]{0f,.35f,1f},Shader.TileMode.CLAMP));
        canvas.drawCircle(cx,cy,r,paint);
        paint.setShader(null);
        canvas.restore();

        // A short specular arc on the upper rim, like the desktop glass ring.
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3f);
        paint.setColor(0x88EAFEFF);
        canvas.drawArc(cx-r+2,cy-r+2,cx+r-2,cy+r-2,196,86,false,paint);
    }
}
