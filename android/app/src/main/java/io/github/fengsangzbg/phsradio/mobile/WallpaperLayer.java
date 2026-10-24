package io.github.fengsangzbg.phsradio.mobile;

import android.app.Activity;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.media.MediaPlayer;
import android.net.Uri;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.concurrent.*;

/** System live wallpaper is rendered by Android; imported video is always silent. */
final class WallpaperLayer implements TextureView.SurfaceTextureListener {
    private final Activity activity;
    private final FrameLayout host;
    private final ImageView image;
    private final TextureView video;
    private final GlassVisual fire;
    private final ExecutorService io=Executors.newSingleThreadExecutor();
    private MediaPlayer decoder;
    private Surface surface;
    private String mode="water",source="";
    private int generation;
    private boolean resumed,prepared;
    WallpaperLayer(Activity activity){
        this.activity=activity;host=new FrameLayout(activity);
        image=new ImageView(activity);image.setScaleType(ImageView.ScaleType.CENTER_CROP);
        video=new TextureView(activity);video.setSurfaceTextureListener(this);
        host.addView(image,new FrameLayout.LayoutParams(-1,-1));host.addView(video,new FrameLayout.LayoutParams(-1,-1));
        fire=new GlassVisual(activity,true);host.addView(fire,new FrameLayout.LayoutParams(-1,-1));
        SharedPreferences saved=activity.getSharedPreferences("mobile",0);
        set(saved.getString("backgroundMode","water"),saved.getString("backgroundUri",""));
    }
    View view(){return host;}
    String mode(){return mode;}
    private void releaseVideo(){prepared=false;if(decoder!=null){decoder.release();decoder=null;}}
    void set(String selected,String uri){
        String previous=source;
        generation++;mode=selected;source=uri;releaseVideo();image.setImageDrawable(null);
        if(!previous.equals(uri)&&previous.startsWith("file:")){
            try{File old=new File(Uri.parse(previous).getPath());if(old.getParentFile().equals(activity.getFilesDir())&&old.getName().startsWith("background-import-"))old.delete();}catch(Exception ignored){}
        }
        image.setVisibility(View.GONE);video.setVisibility(View.GONE);fire.theme("fire".equals(mode));fire.setVisibility(("fire".equals(mode)||"water".equals(mode))?View.VISIBLE:View.GONE);fire.active(resumed&&("fire".equals(mode)||"water".equals(mode)));
        boolean system="system".equals(mode);
        if(system){activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SHOW_WALLPAPER);host.setBackgroundColor(Color.TRANSPARENT);}
        else{
            activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_SHOW_WALLPAPER);
            host.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{0xFF186C7F,0xFF0A2C47,0xFF0B142B}));
        }
        activity.getSharedPreferences("mobile",0).edit().putString("backgroundMode",mode).putString("backgroundUri",source).apply();
        if("video".equals(mode)){video.setVisibility(View.VISIBLE);if(surface!=null)prepareVideo();}
        else if("image".equals(mode)&&!source.isEmpty()){
            final int current=generation;final Uri selectedUri=Uri.parse(source);
            io.execute(()->{
                try{
                    Bitmap bitmap=ImageDecoder.decodeBitmap(ImageDecoder.createSource(activity.getContentResolver(),selectedUri),(decoder,info,src)->{
                        int w=info.getSize().getWidth(),h=info.getSize().getHeight();
                        double ratio=Math.min(1,Math.sqrt(12000000.0/((double)w*h)));
                        if(ratio<1)decoder.setTargetSize(Math.max(1,(int)(w*ratio)),Math.max(1,(int)(h*ratio)));
                    });
                    activity.runOnUiThread(()->{if(current==generation&&!activity.isDestroyed()){image.setImageBitmap(bitmap);image.setVisibility(View.VISIBLE);}});
                }catch(Exception e){activity.runOnUiThread(()->{if(current==generation)failed();});}
            });
        }
    }
    void importShared(Uri uri,String mime){
        final int current=++generation;
        Toast.makeText(activity,"正在导入壁纸…",Toast.LENGTH_SHORT).show();
        io.execute(()->{
            File destination=new File(activity.getFilesDir(),"background-import-"+System.nanoTime()+(mime.startsWith("video/")?".mp4":".image"));
            try(InputStream input=activity.getContentResolver().openInputStream(uri);OutputStream output=new FileOutputStream(destination)){
                if(input==null)throw new IOException();byte[] buffer=new byte[1024*1024];long total=0;int n;
                while((n=input.read(buffer))>=0){total+=n;if(total>1024L*1024*1024||Thread.currentThread().isInterrupted())throw new IOException();output.write(buffer,0,n);}
                activity.runOnUiThread(()->{if(current==generation&&!activity.isDestroyed())set(mime.startsWith("video/")?"video":"image",Uri.fromFile(destination).toString());else destination.delete();});
            }catch(Exception e){destination.delete();activity.runOnUiThread(()->Toast.makeText(activity,"无法导入该壁纸，请使用系统文件选择器重试",Toast.LENGTH_LONG).show());}
        });
    }
    private void prepareVideo(){
        if(!"video".equals(mode)||source.isEmpty()||surface==null)return;
        releaseVideo();decoder=new MediaPlayer();MediaPlayer expected=decoder;
        decoder.setSurface(surface);decoder.setVolume(0,0);decoder.setLooping(true);
        decoder.setOnPreparedListener(p->{if(decoder==expected){prepared=true;p.setVolume(0,0);fit(p.getVideoWidth(),p.getVideoHeight());if(resumed)p.start();}});
        decoder.setOnVideoSizeChangedListener((p,w,h)->fit(w,h));
        decoder.setOnErrorListener((p,w,e)->{if(decoder==expected)failed();return true;});
        try{decoder.setDataSource(activity,Uri.parse(source));decoder.prepareAsync();}catch(Exception e){failed();}
    }
    private void fit(int w,int h){
        if(w<=0||h<=0||video.getWidth()==0||video.getHeight()==0)return;
        float viewRatio=video.getWidth()/(float)video.getHeight(),ratio=w/(float)h;
        Matrix matrix=new Matrix();matrix.setScale(ratio>viewRatio?ratio/viewRatio:1,ratio<viewRatio?viewRatio/ratio:1,video.getWidth()/2f,video.getHeight()/2f);video.setTransform(matrix);
    }
    private void failed(){set("water","");Toast.makeText(activity,"壁纸无法打开，已恢复水光背景",Toast.LENGTH_LONG).show();}
    void resume(){resumed=true;fire.active("fire".equals(mode)||"water".equals(mode));if(decoder!=null&&prepared){try{decoder.start();}catch(IllegalStateException ignored){}}}
    void pause(){resumed=false;fire.active(false);if(decoder!=null){try{if(decoder.isPlaying())decoder.pause();}catch(IllegalStateException ignored){}}}
    void close(){fire.active(false);generation++;releaseVideo();if(surface!=null){surface.release();surface=null;}io.shutdownNow();}
    public void onSurfaceTextureAvailable(SurfaceTexture texture,int w,int h){surface=new Surface(texture);prepareVideo();}
    public void onSurfaceTextureSizeChanged(SurfaceTexture texture,int w,int h){if(decoder!=null){try{fit(decoder.getVideoWidth(),decoder.getVideoHeight());}catch(IllegalStateException ignored){}}}
    public boolean onSurfaceTextureDestroyed(SurfaceTexture texture){releaseVideo();if(surface!=null){surface.release();surface=null;}return true;}
    public void onSurfaceTextureUpdated(SurfaceTexture texture){}
}
