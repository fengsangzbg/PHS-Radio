package io.github.fengsangzbg.phsradio.mobile;
import android.content.Context;
import android.graphics.*;
import android.os.SystemClock;
import android.view.View;

/** A staggered shower of curved water/fire meteors with tapered, receding trails. */
final class PlayMeteor extends View {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path trail=new Path(),centre=new Path();
    private final float[] xs=new float[25],ys=new float[25],widths=new float[25];
    private final Matrix shaderMatrix=new Matrix();
    private final Shader water=new LinearGradient(-1,0,0,0,new int[]{0x000075C4,0x4469DDE9,0xDDACF5FF,0xFFFFFFFF},new float[]{0,.35f,.82f,1},Shader.TileMode.CLAMP);
    private final Shader flame=new LinearGradient(-1,0,0,0,new int[]{0x00A82D17,0x55E96928,0xEEFFCA76,0xFFFFF3DC},new float[]{0,.35f,.82f,1},Shader.TileMode.CLAMP);
    private long start;
    private boolean fire;
    PlayMeteor(Context c){super(c);setClickable(false);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
    void flash(boolean fire){this.fire=fire;start=SystemClock.uptimeMillis();invalidate();}
    void stop(){start=0;invalidate();}
    private float x(float t,int i){return getWidth()*(.68f+(i%4)*.13f-.70f*t-.18f*t*t);}
    private float y(float t,int i){return getHeight()*(-.10f+(i%3)*.07f+.62f*t+.46f*t*t);}
    @Override protected void onDraw(Canvas canvas){
        if(start==0)return;float elapsed=(SystemClock.uptimeMillis()-start)/1000f;if(elapsed>2.65f){start=0;return;}float density=getResources().getDisplayMetrics().density;
        for(int i=0;i<9;i++){
            float age=elapsed-i*.125f,duration=1.15f+(i%3)*.12f;if(age<=0||age>=duration)continue;float progress=age/duration;
            float fade=Math.min(1,age/.12f)*Math.min(1,(duration-age)/.32f);
            float tail=Math.max(0,progress-.42f);float hx=x(progress,i),hy=y(progress,i),tx=x(tail,i),ty=y(tail,i);
            for(int n=0;n<=24;n++){float u=n/24f,t=tail+(progress-tail)*u;xs[n]=x(t,i);ys[n]=y(t,i);widths[n]=(float)Math.sin(u*Math.PI*.65)*density*(1.15f+(i%3)*.22f);}
            trail.reset();centre.reset();for(int n=0;n<=24;n++){int a=Math.max(0,n-1),b=Math.min(24,n+1);float dx=xs[b]-xs[a],dy=ys[b]-ys[a],len=(float)Math.hypot(dx,dy);float ox=len>0?-dy/len*widths[n]:0,oy=len>0?dx/len*widths[n]:0;if(n==0){trail.moveTo(xs[n]+ox,ys[n]+oy);centre.moveTo(xs[n],ys[n]);}else{trail.lineTo(xs[n]+ox,ys[n]+oy);centre.lineTo(xs[n],ys[n]);}}
            for(int n=24;n>=0;n--){int a=Math.max(0,n-1),b=Math.min(24,n+1);float dx=xs[b]-xs[a],dy=ys[b]-ys[a],len=(float)Math.hypot(dx,dy);trail.lineTo(xs[n]+(len>0?dy/len*widths[n]:0),ys[n]-(len>0?dx/len*widths[n]:0));}trail.close();
            shaderMatrix.reset();shaderMatrix.setScale((float)Math.hypot(hx-tx,hy-ty),1);shaderMatrix.postRotate((float)Math.toDegrees(Math.atan2(hy-ty,hx-tx)));shaderMatrix.postTranslate(hx,hy);Shader shader=fire?flame:water;shader.setLocalMatrix(shaderMatrix);paint.setShader(shader);paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(11*density);paint.setAlpha((int)(fade*23));canvas.drawPath(centre,paint);paint.setStrokeWidth(5*density);paint.setAlpha((int)(fade*65));canvas.drawPath(centre,paint);paint.setStyle(Paint.Style.FILL);paint.setAlpha((int)(fade*235));canvas.drawPath(trail,paint);paint.setShader(null);
            paint.setColor(Color.argb((int)(fade*30),fire?255:100,fire?167:225,fire?74:255));canvas.drawCircle(hx,hy,7*density,paint);paint.setColor(Color.argb((int)(fade*240),255,fire?245:255,fire?215:255));canvas.drawCircle(hx,hy,1.7f*density,paint);
            for(int n=0;n<5;n++){float u=Math.max(0,progress-.03f-n*.025f);float drift=(float)Math.sin(i*7+n*3.1f)*density*(3+n*1.4f);paint.setColor(Color.argb((int)(fade*(70-n*11)),fire?255:156,fire?190:239,fire?100:255));canvas.drawCircle(x(u,i)+drift,y(u,i)+n*density*2,(.8f-n*.09f)*density,paint);}
        }
        paint.setShader(null);paint.setAlpha(255);postInvalidateOnAnimation();
    }
}
