const fs=require('fs'),p=__dirname+'/app/src/main/java/io/github/fengsangzbg/phsradio/mobile/MainActivity.java';let s=fs.readFileSync(p,'utf8');s=s.replace('private OnlinePages pages;','private OnlinePages pages;\n    private PlaylistDrawer drawer;\n    private float swipeX,swipeY;\n    private boolean swipeBlocked,swipeConsumed;');
s=s.replace('pages=new OnlinePages(this,onlineHost,()->queue());','pages=new OnlinePages(this,onlineHost,()->queue());drawer=new PlaylistDrawer(this,root,()->queue(),playlist->{showOnline();pages.tracks(playlist);});pages.onDrawer=platform->drawer.show(platform);');
s=s.replace('if(entry[1].equals("home"))pages.home();else pages.playlists(entry[1]);','if(entry[1].equals("home"))pages.home("kugou");else if(entry[1].equals("netease"))pages.home("netease");else drawer.show("kugou");');
s=s.replace('onBackPressed(){if(songPage.back())','onBackPressed(){if(drawer.back())return;if(songPage.back())');
s=s.replace('pages.close();songPage.close();','drawer.close();pages.close();songPage.close();');
s=s.replace('    private void showOnline(){',`    @Override public boolean dispatchTouchEvent(MotionEvent e){
        if(e.getActionMasked()==MotionEvent.ACTION_DOWN){swipeX=e.getX();swipeY=e.getY();swipeConsumed=false;swipeBlocked=isLocal||content==null||content.getVisibility()!=View.VISIBLE||inHorizontalScroll(getWindow().getDecorView(),e.getRawX(),e.getRawY());}
        if(e.getActionMasked()==MotionEvent.ACTION_MOVE&&!swipeBlocked&&!swipeConsumed&&pages!=null){float dx=e.getX()-swipeX,dy=e.getY()-swipeY;if(Math.abs(dx)>dp(84)&&Math.abs(dy)<Math.abs(dx)*.45f){if(pages.swipe(dx)){MotionEvent cancel=MotionEvent.obtain(e);cancel.setAction(MotionEvent.ACTION_CANCEL);super.dispatchTouchEvent(cancel);cancel.recycle();swipeConsumed=true;return true;}}}
        if(swipeConsumed){if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL)swipeConsumed=false;return true;}return super.dispatchTouchEvent(e);
    }
    private boolean inHorizontalScroll(View view,float x,float y){if(view.getVisibility()!=View.VISIBLE)return false;int[] at=new int[2];view.getLocationOnScreen(at);if(x<at[0]||x>at[0]+view.getWidth()||y<at[1]||y>at[1]+view.getHeight())return false;if(view instanceof HorizontalScrollView||view instanceof SeekBar||view instanceof EditText)return true;if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=group.getChildCount()-1;i>=0;i--)if(inHorizontalScroll(group.getChildAt(i),x,y))return true;}return false;}
    private void showOnline(){`);
fs.writeFileSync(p,s);
