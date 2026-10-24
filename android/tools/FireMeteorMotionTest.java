package io.github.fengsangzbg.phsradio.mobile;
public final class FireMeteorMotionTest {
    public static void main(String[] args){
        for(int i=0;i<7;i++){
            FireMeteorMotion m=new FireMeteorMotion(i);
            m.sample(m.delay-.001);check(m.opacity==0,"Not started");
            m.sample(m.delay);check(m.opacity==0,"Starts dark");
            m.sample(m.delay+m.duration);check(m.opacity==0,"Ends dark");
            m.sample(m.delay+m.period-.001);check(m.opacity==0,"No flash before restart");
            double previous=0;
            for(int frame=0;frame<Math.ceil((m.period+.1)*120);frame++){
                m.sample(m.delay+frame/120.0);check(Double.isFinite(m.opacity)&&m.opacity>=0&&m.opacity<=1,"Bounded alpha");check(Math.abs(m.opacity-previous)<.12,"Continuous fade at 120Hz");previous=m.opacity;
            }
            m.sample(m.delay+.4);double x=m.x,y=m.y;
            m.sample(m.delay+.4+1.0/120);check(m.x<x&&m.y>y,"Upper-right to lower-left");
            m.sample(m.delay+m.period*100000+.4);check(Math.abs(m.x-x)<1e-8,"Long-session precision");
        }
        System.out.println("PASS: seven meteor tracks, lifecycle edges, 120Hz fade continuity, trajectory and long-session precision");
    }
    private static void check(boolean ok,String name){if(!ok)throw new AssertionError(name);}
}
