package io.github.fengsangzbg.phsradio.mobile;

/** Continuous motion in local elapsed seconds, independent of Android and uptime precision. */
final class FireMeteorMotion {
    final double period,delay,duration,startX,startY,travelX,travelY,length;
    double x,y,opacity;
    FireMeteorMotion(int index){
        period=9.7+index*1.371;delay=index*1.73;
        duration=1.65+(index%3)*.24;startX=.70+(index%4)*.16;
        startY=.03+(index%5)*.085;travelX=-.95;travelY=.57;
        length=.18+(index%3)*.04;
    }
    void sample(double seconds){
        double age=seconds-delay;
        if(age<0){opacity=0;return;}
        age=age%period;
        if(age>=duration){opacity=0;return;}
        double progress=age/duration;
        x=startX+travelX*progress;y=startY+travelY*progress;
        opacity=smooth(Math.min(1,age/.18))*smooth(Math.min(1,(duration-age)/.48));
    }
    private static double smooth(double t){return t*t*(3-2*t);}
}
