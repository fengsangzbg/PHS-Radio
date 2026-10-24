package io.github.fengsangzbg.phsradio.mobile;
import android.graphics.*;
import android.os.*;
import android.util.LruCache;
import android.widget.ImageView;
import javax.net.ssl.HttpsURLConnection;
import java.io.*;
import java.net.*;
import java.util.HashMap;
import java.util.concurrent.*;
/**
 * Cover loader with a memory cache and a resolved-URL map. A recycled row asks
 * for the same cover again while scrolling or paging; if the bitmap is already
 * cached it is installed synchronously, so the artwork never flickers back to
 * the placeholder.
 */
final class CoverImages implements AutoCloseable {
    private final ExecutorService pool=Executors.newFixedThreadPool(4);
    private final Handler main=new Handler(Looper.getMainLooper());
    private final LruCache<String,Bitmap> cache=new LruCache<String,Bitmap>(24*1024*1024){protected int sizeOf(String key,Bitmap b){return b.getAllocationByteCount();}};
    private final HashMap<String,String> resolved=new HashMap<>();
    private volatile boolean closed;

    void load(ImageView image,org.json.JSONObject track){
        String uri=track.optString("uri");
        String url=track.optString("cover");
        if(!url.startsWith("https://")&&!uri.isEmpty()){
            String known=resolved.get(uri);
            url=known==null?"":known;
        }
        final String target=url;
        String key=target.startsWith("https://")?target:uri;
        image.setTag(key);
        Bitmap cached=target.startsWith("https://")?cache.get(target):null;
        if(cached!=null){image.setImageBitmap(cached);return;}
        image.setImageResource(R.drawable.ic_phs);
        if(key.isEmpty()||track.optBoolean("unavailable"))return;
        pool.execute(()->{
            if(closed)return;
            try(OnlineApi api=new OnlineApi(image.getContext())){
                String cover=target.startsWith("https://")?target:api.cover(track);
                if(cover==null||!cover.startsWith("https://"))return;
                if(!uri.isEmpty())resolved.put(uri,cover);
                Bitmap bitmap=cache.get(cover);
                if(bitmap==null){bitmap=fetch(cover,512);cache.put(cover,bitmap);}
                Bitmap result=bitmap;
                main.post(()->{
                    if(!closed&&(cover.equals(image.getTag())||key.equals(image.getTag())))image.setImageBitmap(result);
                });
            }catch(Exception ignored){}
        });
    }

    void load(ImageView image,String url){
        image.setTag(url);
        if(!url.startsWith("https://"))return;
        Bitmap cached=cache.get(url);
        if(cached!=null){image.setImageBitmap(cached);return;}
        image.setImageResource(R.drawable.ic_phs);
        pool.execute(()->{
            try{
                Bitmap bitmap=cache.get(url);
                if(bitmap==null){bitmap=fetch(url,512);cache.put(url,bitmap);}
                Bitmap result=bitmap;
                main.post(()->{if(!closed&&url.equals(image.getTag()))image.setImageBitmap(result);});
            }catch(Exception ignored){}
        });
    }

    static Bitmap fetch(String url)throws Exception{return fetch(url,2048);}
    private static Bitmap fetch(String url,int limit)throws Exception{
        if(!url.startsWith("https://"))throw new IOException();HttpsURLConnection c=(HttpsURLConnection)new URL(url).openConnection();c.setConnectTimeout(8000);c.setReadTimeout(10000);
        try(InputStream input=c.getInputStream()){ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[8192];int n;while((n=input.read(buffer))!=-1){if(out.size()+n>12*1024*1024)throw new IOException();out.write(buffer,0,n);}byte[] bytes=out.toByteArray();BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);options.inSampleSize=1;while(Math.max(options.outWidth,options.outHeight)/options.inSampleSize>limit)options.inSampleSize*=2;options.inJustDecodeBounds=false;Bitmap result=BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);if(result==null)throw new IOException();return result;}finally{c.disconnect();}
    }
    public void close(){closed=true;pool.shutdownNow();main.removeCallbacksAndMessages(null);cache.evictAll();resolved.clear();}
}
