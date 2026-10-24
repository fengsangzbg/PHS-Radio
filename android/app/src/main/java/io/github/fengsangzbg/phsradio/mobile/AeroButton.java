package io.github.fengsangzbg.phsradio.mobile;
import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.view.*;
import android.view.animation.OvershootInterpolator;
import android.widget.Button;

final class AeroButton extends Button {
    private final Paint reflection=new Paint(Paint.ANTI_ALIAS_FLAG);
    AeroButton(Context context){super(context);setAllCaps(false);setTextColor(0xFFF0FCFF);setTextSize(14);setMinWidth(0);setMinimumWidth(0);setPadding(12,0,12,0);GradientDrawable bg=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{0x668AE7F1,0x183A8EA4,0x664578A0});bg.setCornerRadius(80);bg.setStroke(1,0x998CDDE5);setBackground(bg);setStateListAnimator(null);}
    @Override protected void onDraw(Canvas canvas){super.onDraw(canvas);reflection.setColor(0x448FF6FF);reflection.setStyle(Paint.Style.STROKE);reflection.setStrokeWidth(1.5f);canvas.drawArc(6,5,getWidth()-6,getHeight()-5,200,110,false,reflection);reflection.setColor(0x13FFFFFF);reflection.setStyle(Paint.Style.FILL);canvas.drawRoundRect(8,5,getWidth()-8,getHeight()*.42f,30,30,reflection);}
    @Override public boolean onTouchEvent(android.view.MotionEvent e){if(e.getActionMasked()==MotionEvent.ACTION_DOWN)animate().cancel();if(e.getActionMasked()==MotionEvent.ACTION_DOWN)animate().scaleX(.94f).scaleY(.88f).setDuration(90).start();else if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL)animate().scaleX(1).scaleY(1).setInterpolator(new OvershootInterpolator(2.5f)).setDuration(380).start();return super.onTouchEvent(e);}
}
