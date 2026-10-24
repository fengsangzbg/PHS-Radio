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

    private float dp(float value){return value*getResources().getDisplayMetrics().density;}

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
        float cr=r*.46f;
        paint.setStyle(Paint.Style.FILL);

        // Outer bloom so the disc sits in light instead of on a flat panel.
        paint.setShader(new RadialGradient(cx,cy,r*1.16f,
                new int[]{0x004FE9FF,0x2240D6F5,0x0000B8E0},
                new float[]{.62f,.90f,1f},Shader.TileMode.CLAMP));
        canvas.drawCircle(cx,cy,r*1.16f,paint);

        // Clear glass body: pale aqua with a bright upper-left shoulder. This is
        // the Aero read: light passes through, it does not paint a dark vinyl.
        paint.setShader(new RadialGradient(cx-r*.30f,cy-r*.34f,r*1.25f,
                new int[]{0xFFDFF9FF,0xFFB7E9F6,0xFF7FC8DE,0xCC3E93AE},
                new float[]{0f,.30f,.62f,1f},Shader.TileMode.CLAMP));
        canvas.drawCircle(cx,cy,r,paint);
        paint.setShader(null);

        // Grooves: fine, cool, and fading outward so they never fight the art.
        paint.setStyle(Paint.Style.STROKE);
        for(int i=1;i<=20;i++){
            float rr=r*(1f-i*.031f);
            if(rr<=cr+dp(3))break;
            paint.setStrokeWidth(1f);
            paint.setColor(i%4==0?0x4AFFFFFF:0x22BAF1FF);
            canvas.drawCircle(cx,cy,rr,paint);
        }

        // Iridescent dispersion at the rim: four hue arcs, the Aero rainbow edge.
        int[] hues={186,205,268,320};
        for(int i=0;i<hues.length;i++){
            paint.setStrokeWidth(dp(2.2f));
            paint.setColor(Color.HSVToColor(new float[]{hues[i],.55f,1f,}));
            paint.setAlpha(46);
            float inset=dp(2.2f)+i*dp(1.1f);
            canvas.drawArc(cx-r+inset,cy-r+inset,cx+r-inset,cy+r-inset,200+i*12,60,false,paint);
        }

        // Crisp bright rim plus a dark seat line under it for thickness.
        paint.setStrokeWidth(dp(2.4f));
        paint.setColor(0xF2F4FEFF);
        canvas.drawCircle(cx,cy,r-dp(1.2f),paint);
        paint.setStrokeWidth(dp(1f));
        paint.setColor(0x4A1A5E78);
        canvas.drawCircle(cx,cy,r-dp(4f),paint);

        // Artwork: opaque seat, then the picture, then a glass bezel.
        clip.reset();
        clip.addCircle(cx,cy,cr,Path.Direction.CW);
        canvas.save();
        canvas.clipPath(clip);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xFF07202E);
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

        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(2.2f));
        paint.setColor(0xEEFFFFFF);
        canvas.drawCircle(cx,cy,cr,paint);
        paint.setStrokeWidth(dp(1f));
        paint.setColor(0x66103A4E);
        canvas.drawCircle(cx,cy,cr+dp(2f),paint);
        if(cover==null){
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0xFFFFFFFF);
            canvas.drawCircle(cx,cy,r*.045f,paint);
        }

        // Glass sheen on the annulus only: the art stays untouched and sharp.
        annulus.reset();
        annulus.setFillType(Path.FillType.EVEN_ODD);
        annulus.addCircle(cx,cy,r-dp(2f),Path.Direction.CW);
        annulus.addCircle(cx,cy,cr+dp(3f),Path.Direction.CCW);
        canvas.save();
        canvas.clipPath(annulus);
        paint.setStyle(Paint.Style.FILL);
        paint.setShader(new LinearGradient(cx-r*.8f,cy-r, cx+r*.1f,cy+r*.2f,
                new int[]{0x88FFFFFF,0x2EFFFFFF,0x00FFFFFF},
                new float[]{0f,.28f,1f},Shader.TileMode.CLAMP));
        canvas.drawCircle(cx,cy,r,paint);
        paint.setShader(null);
        canvas.restore();

        // Two specular streaks: the long upper arc is what sells "crystal".
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(5f));
        paint.setColor(0x40FFFFFF);
        canvas.drawArc(cx-r*.86f,cy-r*.86f,cx+r*.86f,cy+r*.86f,192,74,false,paint);
        paint.setStrokeWidth(dp(1.8f));
        paint.setColor(0xCCFFFFFF);
        canvas.drawArc(cx-r*.84f,cy-r*.84f,cx+r*.84f,cy+r*.84f,196,58,false,paint);
        paint.setStrokeWidth(dp(2.4f));
        paint.setColor(0x66FFFFFF);
        canvas.drawArc(cx-r*.80f,cy-r*.80f,cx+r*.80f,cy+r*.80f,26,40,false,paint);
    }
}