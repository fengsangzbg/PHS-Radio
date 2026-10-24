package io.github.fengsangzbg.phsradio.mobile;
import android.graphics.*;
/** Slow caustic reflections, independent of the short playback meteor shower. */
final class WaterSky {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path wave=new Path();
    private Shader base,glow;
    private final Matrix drift=new Matrix();
    void resize(int w,int h){base=new LinearGradient(0,0,w,h,new int[]{0xFF0A5364,0xFF0A344D,0xFF0D233F},null,Shader.TileMode.CLAMP);glow=new RadialGradient(w*.24f,h*.35f,Math.max(w,h)*.65f,new int[]{0x5522D2D4,0x0034CDE0},null,Shader.TileMode.CLAMP);}
    void draw(Canvas canvas,int w,int h,double seconds,float density){
        paint.setStyle(Paint.Style.FILL);paint.setAlpha(255);paint.setShader(base);canvas.drawRect(0,0,w,h,paint);drift.setTranslate((float)Math.sin(seconds*.13)*w*.06f,(float)Math.cos(seconds*.11)*h*.05f);glow.setLocalMatrix(drift);paint.setShader(glow);canvas.drawRect(0,0,w,h,paint);paint.setShader(null);paint.setStyle(Paint.Style.STROKE);
        for(int i=0;i<6;i++){float phase=(float)(seconds*(.10+i*.008)+i*1.7),y=h*(.10f+i*.17f)+(float)Math.sin(phase)*h*.04f;wave.reset();wave.moveTo(-w*.15f,y+h*.08f);wave.cubicTo(w*.2f,y-h*.12f,w*.65f,y+h*.12f,w*1.15f,y-h*.03f);paint.setStrokeWidth(6*density);paint.setColor(0x0636DDE2);canvas.drawPath(wave,paint);paint.setStrokeWidth(.7f*density);paint.setColor(i%2==0?0x2188F7F4:0x1344BDE4);canvas.drawPath(wave,paint);}paint.setStyle(Paint.Style.FILL);
    }
}
