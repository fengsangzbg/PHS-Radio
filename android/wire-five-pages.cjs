const fs=require('fs'),base=__dirname+'/app/src/main/java/io/github/fengsangzbg/phsradio/mobile/';let p=base+'SongPage.java',s=fs.readFileSync(p,'utf8');
s=s.replace('void show(){','void show(){showIn((ViewGroup)activity.findViewById(android.R.id.content));}\n    void showIn(ViewGroup target){');
s=s.replace('overlay=(ViewGroup)activity.findViewById(android.R.id.content);','overlay=target;');
s=s.replace('pagePanel.setPadding(dp(16),dp(44),dp(16),dp(28));','pagePanel.setPadding(dp(4),dp(4),dp(4),dp(4));');
s=s.replace(/        pagePanel\.setOnApplyWindowInsetsListener\(.*?\);\r?\n/,'');
s=s.replace(/        Button back=new AeroButton\(activity\);.*?pagePanel.addView\(back,new LinearLayout.LayoutParams\(dp\(78\),dp\(42\)\)\);\r?\n/,'');
s=s.replace(/        LinearLayout controls=new LinearLayout\(activity\);.*?pagePanel.addView\(controls\);\r?\n/,'');
s=s.replace('if(home!=null)home.setVisibility(View.INVISIBLE);','');s=s.replace('if(home!=null)home.setVisibility(View.VISIBLE);','');
fs.writeFileSync(p,s);
p=base+'OnlinePages.java';s=fs.readFileSync(p,'utf8');s=s.replace('    void home(String platform)', '    void songPage(SongPage song){begin();routePage=true;back=null;song.showIn(host);}\n    void home(String platform)');fs.writeFileSync(p,s);
p=base+'MainActivity.java';s=fs.readFileSync(p,'utf8');s=s.replace('private PlaylistDrawer drawer;','private final HomeRoute route=new HomeRoute();');s=s.replace('drawer=new PlaylistDrawer(this,root,()->queue(),playlist->{showOnline();pages.tracks(playlist);});pages.onDrawer=platform->drawer.show(platform);','pages.onRouteSwipe=dx->{if(!route.swipe(dx))return false;renderRoute();onlineHost.setTranslationX(dx<0?dp(38):-dp(38));onlineHost.setAlpha(.6f);onlineHost.animate().translationX(0).alpha(1).setDuration(240).start();return true;};');
const start=s.indexOf('        for(String[] entry:new String[][]'),end=s.indexOf('\n',start);
s=s.slice(0,start)+`        String[] tabs={"酷狗歌单","酷狗推荐","唱片歌词","网易推荐","网易歌单"};for(int i=0;i<tabs.length;i++){final int selected=i;Button tab=button(tabs[i],()->{searchHandler.removeCallbacksAndMessages(null);route.select(selected);renderRoute();});tab.setTextSize(11);nav.addView(tab,new LinearLayout.LayoutParams(0,dp(42),1));}
`+s.slice(end+1);
s=s.replace('        pages.home();','        renderRoute();');
s=s.replace('()->songPage.show()','()->selectCentre()');
let a=s.indexOf('    @Override public void onBackPressed(){'),b=s.indexOf('\n',a);s=s.slice(0,a)+'    @Override public void onBackPressed(){if(isLocal){selectCentre();}else if(!pages.back()){if(route.index()!=2)selectCentre();else super.onBackPressed();}}'+s.slice(b);
s=s.replace('    private void showOnline(){',`    private void selectCentre(){route.select(HomeRoute.CENTRE);renderRoute();}
    private void renderRoute(){showOnline();songPage.back();if(route.index()==0)pages.playlists("kugou");else if(route.index()==1)pages.home("kugou");else if(route.index()==2)pages.songPage(songPage);else if(route.index()==3)pages.home("netease");else pages.playlists("netease");}
    private void showOnline(){`);
s=s.replace('drawer.close();pages.close();','pages.close();');
// Remove the embedded song view before a global search replaces its host.
s=s.replace('showOnline();pages.search(query);','songPage.back();showOnline();pages.search(query);').replace('showOnline();pages.search(search.getText()','songPage.back();showOnline();pages.search(search.getText()');
fs.writeFileSync(p,s);
