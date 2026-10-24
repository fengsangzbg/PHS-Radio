package io.github.fengsangzbg.phsradio.mobile;
import android.graphics.*;
import android.os.*;
import android.util.LruCache;
import android.widget.ImageView;
import javax.net.ssl.HttpsURLConnection;
import java.io.*;
import java.net.*;
import java.util.concurrent.*;
final class CoverImages implements AutoCloseable {
    private final ExecutorService pool=Executors.newFixedThreadPool(2);
    private final Handler main=new Handler(Looper.getMainLooper());
    private final LruCache<String,Bitmap> cache=new LruCache<String,Bitmap>(16*1024*1024){protected int sizeOf(String key,Bitmap b){return b.getAllocationByteCount();}};
    private volatile boolean closed;
    void load(ImageView image,org.json.JSONObject track){String url=track.optString("cover");if(url.startsWith("https://")){load(image,url);return;}String key=track.optString("uri");image.setTag(key);image.setImageResource(R.drawable.ic_phs);if(key.isEmpty()||track.optBoolean("unavailable"))return;pool.execute(()->{if(closed)return;try(OnlineApi api=new OnlineApi(image.getContext())){String cover=api.cover(track);if(!cover.isEmpty()){Bitmap bitmap=cache.get(cover);if(bitmap==null){bitmap=fetch(cover,512);cache.put(cover,bitmap);}Bitmap result=bitmap;main.post(()->{if(!closed&&key.equals(image.getTag()))image.setImageBitmap(result);});}}catch(Exception ignored){}});}
    void load(ImageView image,String url){image.setTag(url);image.setImageResource(R.drawable.ic_phs);if(!url.startsWith("https://"))return;Bitmap cached=cache.get(url);if(cached!=null){image.setImageBitmap(cached);return;}pool.execute(()->{try{Bitmap bitmap=fetch(url,512);cache.put(url,bitmap);main.post(()->{if(!closed&&url.equals(image.getTag()))image.setImageBitmap(bitmap);});}catch(Exception ignored){}});}
    static Bitmap fetch(String url)throws Exception{return fetch(url,2048);}
    private static Bitmap fetch(String url,int limit)throws Exception{
        if(!url.startsWith("https://"))throw new IOException();HttpsURLConnection c=(HttpsURLConnection)new URL(url).openConnection();c.setConnectTimeout(8000);c.setReadTimeout(10000);
        try(InputStream input=c.getInputStream()){ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[8192];int n;while((n=input.read(buffer))!=-1){if(out.size()+n>12*1024*1024)throw new IOException();out.write(buffer,0,n);}byte[] bytes=out.toByteArray();BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);options.inSampleSize=1;while(Math.max(options.outWidth,options.outHeight)/options.inSampleSize>limit)options.inSampleSize*=2;options.inJustDecodeBounds=false;Bitmap result=BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);if(result==null)throw new IOException();return result;}finally{c.disconnect();}
    }
    public void close(){closed=true;pool.shutdownNow();main.removeCallbacksAndMessages(null);cache.evictAll();}
}
