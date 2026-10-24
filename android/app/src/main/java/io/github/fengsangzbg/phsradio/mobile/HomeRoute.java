package io.github.fengsangzbg.phsradio.mobile;
final class HomeRoute {
    static final int KUGOU_LISTS=0,KUGOU_HOME=1,CENTRE=2,NETEASE_HOME=3,NETEASE_LISTS=4;
    private int index=CENTRE;
    int index(){return index;}
    void select(int index){this.index=Math.max(0,Math.min(4,index));}
    boolean swipe(float dx){int next=index+(dx<0?1:-1);if(next<0||next>4)return false;index=next;return true;}
}
