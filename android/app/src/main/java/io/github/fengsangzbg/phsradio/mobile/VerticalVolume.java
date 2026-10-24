package io.github.fengsangzbg.phsradio.mobile;
import android.content.Context;
import android.graphics.*;
import android.view.*;
import android.view.accessibility.AccessibilityNodeInfo;
final class VerticalVolume extends View {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private float level=1;
    private final java.util.function.Consumer<Float> change;
    VerticalVolume(Context context,java.util.function.Consumer<Float> change){super(context);this.change=change;setContentDescription("音乐音量，上滑增大，下滑减小");setFocusable(true);}
    void level(float value){level=Math.max(0,Math.min(1,value));invalidate();}
    @Override protected void onDraw(Canvas c){float d=getResources().getDisplayMetrics().density,cx=getWidth()/2f,top=24*d,bottom=getHeight()-20*d,y=bottom-level*(bottom-top);paint.setStyle(Paint.Style.FILL);paint.setColor(0x44316C7C);c.drawRoundRect(cx-14*d,0,cx+14*d,getHeight(),14*d,14*d,paint);paint.setStrokeWidth(4*d);paint.setStrokeCap(Paint.Cap.ROUND);paint.setColor(0x4439B1C6);c.drawLine(cx,top,cx,bottom,paint);paint.setColor(0xCC70E9EB);c.drawLine(cx,y,cx,bottom,paint);paint.setColor(0x5533D4E8);c.drawCircle(cx,y,9*d,paint);paint.setColor(0xFFE1FCFF);c.drawCircle(cx,y,5*d,paint);paint.setTextSize(9*d);paint.setTextAlign(Paint.Align.CENTER);c.drawText(Math.round(level*100)+"",cx,15*d,paint);}
    @Override public boolean onTouchEvent(MotionEvent event){if(event.getActionMasked()==MotionEvent.ACTION_DOWN||event.getActionMasked()==MotionEvent.ACTION_MOVE){getParent().requestDisallowInterceptTouchEvent(true);float d=getResources().getDisplayMetrics().density;level((getHeight()-20*d-event.getY())/Math.max(1,getHeight()-44*d));change.accept(level);return true;}if(event.getActionMasked()==MotionEvent.ACTION_UP){performClick();return true;}return true;}
    @Override public boolean performClick(){super.performClick();return true;}
    @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info){super.onInitializeAccessibilityNodeInfo(info);info.setClassName("android.widget.SeekBar");info.setRangeInfo(AccessibilityNodeInfo.RangeInfo.obtain(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT,0,1,level));info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);}
    @Override public boolean performAccessibilityAction(int action,android.os.Bundle args){if(action==AccessibilityNodeInfo.ACTION_SCROLL_FORWARD||action==AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD){level(level+(action==AccessibilityNodeInfo.ACTION_SCROLL_FORWARD?.05f:-.05f));change.accept(level);return true;}return super.performAccessibilityAction(action,args);}
}
