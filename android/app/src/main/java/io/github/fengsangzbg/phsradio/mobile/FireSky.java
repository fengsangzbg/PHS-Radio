package io.github.fengsangzbg.phsradio.mobile;

import android.graphics.*;
import java.util.Random;

/** A steady night sky with small, staggered meteors; shaders and geometry are reused. */
final class FireSky {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float[] stars=new float[600];
    private final FireMeteorMotion[] meteors=new FireMeteorMotion[7];
    private final Path trail=new Path();
    private final Shader flame=new LinearGradient(-1,0,0,0,new int[]{0x00C44F26,0x44DE642D,0xDFFFAC58,0xFFFFF5D5},new float[]{0,.28f,.8f,1},Shader.TileMode.CLAMP);
    private Shader night,horizon;
    FireSky(){Random random=new Random(3101);for(int i=0;i<stars.length;i++)stars[i]=random.nextFloat();for(int i=0;i<meteors.length;i++)meteors[i]=new FireMeteorMotion(i);
        trail.moveTo(-1,0);trail.cubicTo(-.7f,-.16f,-.25f,-.65f,0,-.25f);trail.quadTo(.025f,0,0,.25f);trail.cubicTo(-.25f,.65f,-.7f,.16f,-1,0);trail.close();
    }
    void resize(int w,int h){night=new LinearGradient(0,0,0,h,new int[]{0xFF050B20,0xFF101A36,0xFF27314A},null,Shader.TileMode.CLAMP);horizon=new RadialGradient(w*.65f,h*.96f,Math.max(1,w*.95f),new int[]{0x22395C88,0x00395C88},null,Shader.TileMode.CLAMP);}
    void draw(Canvas canvas,int w,int h,double time,float density){
        paint.setAlpha(255);paint.setShader(night);canvas.drawRect(0,0,w,h,paint);paint.setShader(horizon);canvas.drawRect(0,0,w,h,paint);paint.setShader(null);
        for(int i=0;i<150;i++){int n=i*4;paint.setColor(Color.argb(55+(int)(stars[n+2]*130),207,224,247));canvas.drawCircle(stars[n]*w,stars[n+1]*h*.94f,(.35f+stars[n+3]*.55f)*density,paint);}
        for(FireMeteorMotion meteor:meteors){meteor.sample(time);if(meteor.opacity<.001)continue;float alpha=(float)meteor.opacity;
            canvas.save();canvas.translate((float)meteor.x*w,(float)meteor.y*h);canvas.rotate((float)Math.toDegrees(Math.atan2(meteor.travelY*h,meteor.travelX*w)));
            float length=(float)meteor.length*w;paint.setShader(flame);
            for(int layer=0;layer<3;layer++){canvas.save();canvas.scale(length,(layer==0?8:layer==1?2.6f:1)*density);paint.setAlpha((int)(alpha*(layer==0?24:layer==1?115:240)));canvas.drawPath(trail,paint);canvas.restore();}
            paint.setShader(null);paint.setColor(Color.argb((int)(alpha*38),255,141,58));canvas.drawCircle(0,0,4*density,paint);paint.setColor(Color.argb((int)(alpha*245),255,248,220));canvas.drawCircle(0,0,1.2f*density,paint);
            canvas.restore();
        }
        paint.setAlpha(255);paint.setShader(null);
    }
}
