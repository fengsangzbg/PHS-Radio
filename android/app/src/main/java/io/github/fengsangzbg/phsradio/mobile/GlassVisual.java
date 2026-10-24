package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.*;
import java.util.Random;

/** Native, display-synchronised drawing. No bitmap blur or per-frame image decoding. */
final class GlassVisual extends View {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
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
    GlassVisual(Context context,boolean sky){super(context);this.sky=sky;Random random=new Random(31);for(int i=0;i<stars.length;i++)stars[i]=random.nextFloat();}
    void theme(boolean fire){fireTheme=fire;invalidate();}
    void playing(boolean value){playing=value;last=0;invalidate();}
    void cover(Bitmap value){cover=value;invalidate();}
    void active(boolean value){active=value;last=0;skyTick=0;if(value)invalidate();}
    @Override protected void onSizeChanged(int w,int h,int ow,int oh){fireSky.resize(w,h);waterSky.resize(w,h);background=new LinearGradient(0,0,w,h,new int[]{0xFF070D29,0xFF1F1D44,0xFF513347},null,Shader.TileMode.CLAMP);glass=new LinearGradient(0,0,w,h,new int[]{0x6684E5F2,0x1531BFD8,0x887CCBE4,0x223F689A},null,Shader.TileMode.CLAMP);}
    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);float w=getWidth(),h=getHeight();long now=SystemClock.uptimeMillis();float seconds=now/1000f;
        paint.setShader(null);paint.setStyle(Paint.Style.FILL);
        if(sky){
            long tick=SystemClock.elapsedRealtimeNanos();
            if(active&&skyTick!=0)skySeconds+=(tick-skyTick)/1_000_000_000.0;
            skyTick=active?tick:0;
            if(fireTheme)fireSky.draw(canvas,getWidth(),getHeight(),skySeconds,getResources().getDisplayMetrics().density);else waterSky.draw(canvas,getWidth(),getHeight(),skySeconds,getResources().getDisplayMetrics().density);
        }else{
            if(last!=0&&playing)rotation+=(Math.min(50,now-last))*.012f;last=now;
            float cx=w/2,cy=h/2,r=Math.min(w,h)*.44f;
            paint.setColor(0x33000818);canvas.drawCircle(cx,cy+r*.06f,r*1.04f,paint);
            paint.setShader(glass);canvas.drawCircle(cx,cy,r,paint);paint.setShader(null);
            paint.setStyle(Paint.Style.STROKE);for(int i=0;i<12;i++){paint.setStrokeWidth(i==0?2:1);paint.setColor(i==0?0xBBD8FAFF:0x224ED7EC);canvas.drawCircle(cx,cy,r*(1-i*.048f),paint);}paint.setStyle(Paint.Style.FILL);
            canvas.save();canvas.rotate(rotation,cx,cy);Path clip=new Path();clip.addCircle(cx,cy,r*.54f,Path.Direction.CW);canvas.clipPath(clip);
            if(cover!=null){float side=Math.min(cover.getWidth(),cover.getHeight());Rect src=new Rect((int)((cover.getWidth()-side)/2),(int)((cover.getHeight()-side)/2),(int)((cover.getWidth()+side)/2),(int)((cover.getHeight()+side)/2));paint.setFilterBitmap(true);canvas.drawBitmap(cover,src,new RectF(cx-r*.54f,cy-r*.54f,cx+r*.54f,cy+r*.54f),paint);}
            else{paint.setColor(0xFF30889B);canvas.drawPaint(paint);paint.setColor(Color.WHITE);paint.setTextSize(r*.6f);paint.setTypeface(Typeface.create("sans-serif",Typeface.BOLD));paint.setTextAlign(Paint.Align.CENTER);canvas.drawText("P",cx,cy+r*.2f,paint);}canvas.restore();
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2);paint.setColor(0xAAE4FBFF);canvas.drawCircle(cx,cy,r*.54f,paint);paint.setStyle(Paint.Style.FILL);paint.setColor(0xFFB9EDF1);canvas.drawCircle(cx,cy,r*.045f,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(r*.06f);paint.setColor(0x448BEEFF);canvas.drawArc(cx-r*.84f,cy-r*.84f,cx+r*.84f,cy+r*.84f,208,65,false,paint);paint.setStyle(Paint.Style.FILL);
        }
        if(active&&(sky||playing)&&isShown())postInvalidateOnAnimation();
    }
}
