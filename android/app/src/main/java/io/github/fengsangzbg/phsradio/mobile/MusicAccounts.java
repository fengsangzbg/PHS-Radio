package io.github.fengsangzbg.phsradio.mobile;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.provider.MediaStore;
import android.security.keystore.*;
import android.util.Base64;
import android.view.*;
import android.widget.*;
import com.google.zxing.*;
import com.google.zxing.common.BitMatrix;
import org.json.*;
import javax.crypto.*;
import javax.crypto.spec.*;
import javax.net.ssl.HttpsURLConnection;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;
import java.util.concurrent.*;

/** Independent platform sessions. Tokens are encrypted using the Android Keystore. */
final class MusicAccounts implements AutoCloseable {
    private final Activity activity;
    private final Context context;
    Runnable onChanged;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private volatile int generation;
    private volatile boolean closed;
    private volatile String qrCookie="";
    private Runnable qrTask;
    private boolean pollBusy;
    MusicAccounts(Context a){context=a;activity=a instanceof Activity?(Activity)a:null;}
    void show(){
        LinearLayout panel=panel();Dialog dialog=dialog(panel);
        label(panel,"连接音乐平台",24);
        label(panel,"酷狗和网易云可以同时登录。授权由官方 App 确认，本软件不收集密码。",14);
        for(String platform:new String[]{"kugou","netease"}){
            boolean saved=!context.getSharedPreferences("accounts",0).getString(platform,"").isEmpty();
            action(panel,name(platform)+(saved?" · 已保存授权":" · 扫码登录"),()->{dialog.dismiss();login(platform);});
            
            if(saved)action(panel,"退出"+name(platform),()->{context.getSharedPreferences("accounts",0).edit().remove(platform).apply();dialog.dismiss();if(onChanged!=null)onChanged.run();show();});
        }
        label(panel,"登录完成后返回首页，可打开平台歌单和每日推荐。",13);
    }
    JSONObject restore(String platform)throws Exception{
        String[] value=context.getSharedPreferences("accounts",0).getString(platform,"").split(":",2);
        if(value.length!=2)throw new IOException("请先登录");
        KeyStore keys=KeyStore.getInstance("AndroidKeyStore");keys.load(null);
        Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.DECRYPT_MODE,keys.getKey("phs.music.accounts",null),new GCMParameterSpec(128,Base64.decode(value[0],Base64.NO_WRAP)));
        return new JSONObject(new String(cipher.doFinal(Base64.decode(value[1],Base64.NO_WRAP)),StandardCharsets.UTF_8));
    }
    JSONObject neAuthorized(String path,JSONObject data,JSONObject session)throws Exception{data.put("_sessionCookie",session.getString("cookie"));return ne(path,data);}
    private void login(String platform){
        int ticket=++generation;qrCookie="";qrTask=null;pollBusy=false;LinearLayout panel=panel();Dialog dialog=dialog(panel);
        label(panel,name(platform)+"扫码登录",22);
        TextView status=label(panel,"正在请求官方登录二维码…",14);
        ImageView image=new ImageView(activity);image.setBackgroundColor(Color.WHITE);image.setAdjustViewBounds(true);
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(dp(240),dp(240));params.gravity=Gravity.CENTER_HORIZONTAL;params.topMargin=dp(16);panel.addView(image,params);
        final Bitmap[] bitmap={null};
        action(panel,"保存二维码到相册",()->{if(bitmap[0]!=null)saveQr(bitmap[0]);});
        label(panel,"用对应官方 App 的扫一扫，从相册选择二维码，再确认授权；也可以用另一台设备扫码。",13);
        action(panel,"已确认，检查登录状态",()->resumePolling());
        action(panel,"重新生成",()->{dialog.dismiss();login(platform);});
        dialog.setOnDismissListener(d->{if(generation==ticket){++generation;qrTask=null;}});
        submit(ticket,status,()->{
            JSONObject response=platform.equals("kugou")?kg("/v2/qrcode",new TreeMap<>(Map.of("appid","1001","type","1","plat","4","srcappid","2919","qrcode_txt","https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=1005&"))):ne("/api/login/qrcode/unikey",new JSONObject().put("type",3).put("_sessionCookie",qrCookie));
            if(platform.equals("netease")&&valid(ticket))qrCookie=NeteaseAuth.cookies(qrCookie,response);
            JSONObject data=response.optJSONObject("data");if(data==null&&!platform.equals("kugou"))data=response;
            String key=data==null?"":data.optString(platform.equals("kugou")?"qrcode":"unikey",data.optString("key",""));
            if(key.isEmpty())throw new IOException("平台未返回二维码，请重试（"+response.optInt("code",response.optInt("error_code"))+"）");
            String url=platform.equals("kugou")?"https://h5.kugou.com/apps/loginQRCode/html/index.html?qrcode="+enc(key):"https://music.163.com/login?codekey="+enc(key);
            BitMatrix matrix=new MultiFormatWriter().encode(url,BarcodeFormat.QR_CODE,720,720);
            int[] pixels=new int[720*720];for(int y=0;y<720;y++)for(int x=0;x<720;x++)pixels[y*720+x]=matrix.get(x,y)?Color.BLACK:Color.WHITE;
            Bitmap qr=Bitmap.createBitmap(pixels,720,720,Bitmap.Config.ARGB_8888);
            main.post(()->{if(valid(ticket)){bitmap[0]=qr;image.setImageBitmap(qr);status.setText("等待扫码，二维码仅用于本次授权");poll(platform,key,ticket,status,SystemClock.elapsedRealtime()+180000);}});
        });
    }
    void resumePolling(){if(qrTask!=null&&!pollBusy){main.removeCallbacks(qrTask);main.post(qrTask);}}
    private void poll(String platform,String key,int ticket,TextView status,long deadline){
        qrTask=()->{if(!valid(ticket)||pollBusy)return;if(SystemClock.elapsedRealtime()>deadline){status.setText("二维码已过期，请重新生成");qrTask=null;return;}pollBusy=true;
            worker.execute(()->{boolean again=true;String message="";try{
                JSONObject result=platform.equals("kugou")?kg("/v2/get_userinfo_qrcode",new TreeMap<>(Map.of("plat","4","appid","1005","srcappid","2919","qrcode",key))):ne("/api/login/qrcode/client/login",new JSONObject().put("key",key).put("type",3).put("_sessionCookie",qrCookie));if(!valid(ticket))return;
                if(platform.equals("netease"))qrCookie=NeteaseAuth.cookies(qrCookie,result);JSONObject data=result.optJSONObject("data");int code=platform.equals("kugou")?(data==null?-1:data.optInt("status",-1)):NeteaseAuth.code(result);
                if(code==4&&platform.equals("kugou")){if(data.optString("token").isEmpty()||data.optString("userid").isEmpty())throw new IOException("酷狗授权结果不完整，请重新扫码");store(platform,data,ticket);message="酷狗登录成功，授权已保存在此手机";again=false;}
                else if(code==803&&platform.equals("netease")){
                    if(!NeteaseAuth.authorized(qrCookie))throw new IOException("已确认授权，正在等待网易云返回登录凭据…");JSONObject session=new JSONObject().put("cookie",qrCookie);JSONObject account=neAuthorized("/api/w/nuser/account/get",new JSONObject(),session);JSONObject profile=account.optJSONObject("profile"),user=account.optJSONObject("account");String uid=profile!=null?profile.optString("userId"):user!=null?user.optString("id"):"";if(uid.isEmpty()||uid.equals("0"))throw new IOException("网易云授权正在验证，稍后重试…");session.put("userid",uid);store(platform,session,ticket);message="网易云登录成功，可以返回查看歌单";again=false;
                }else if(code==800||(platform.equals("kugou")&&code==0)){message="二维码已过期，请重新生成";again=false;}
                else if(code==802||code==2)message="已扫码，请在官方 App 确认登录；确认后返回本软件";
                else if(code==801||code==1)message="等待扫码，确认后返回本软件即可";
                else message="登录状态暂未更新（"+code+"），正在重试…";
            }catch(Exception error){message=error instanceof SocketTimeoutException?"读取授权超时，正在重试…":OnlineApi.message(error);}
            boolean repeat=again;String label=message;main.post(()->{if(!valid(ticket))return;pollBusy=false;status.setText(label);if(repeat){poll(platform,key,ticket,status,deadline);}else qrTask=null;});});
        };main.postDelayed(qrTask,2200);
    }
    private interface Work{void run()throws Exception;}
    private void submit(int ticket,TextView status,Work work){worker.execute(()->{try{if(valid(ticket))work.run();}catch(Exception error){main.post(()->{if(valid(ticket))status.setText(error instanceof SocketTimeoutException?"连接超时，请重新生成二维码":error instanceof IOException?error.getMessage():"无法完成登录，请重试");});}});}
    private boolean valid(int ticket){return !closed&&generation==ticket;}
    private void store(String platform,JSONObject session,int ticket)throws Exception{
        // Never persist the response or cookies in logs/plaintext preferences.
        KeyStore store=KeyStore.getInstance("AndroidKeyStore");store.load(null);
        String alias="phs.music.accounts";
        if(!store.containsAlias(alias)){KeyGenerator generator=KeyGenerator.getInstance("AES","AndroidKeyStore");generator.init(new KeyGenParameterSpec.Builder(alias,KeyProperties.PURPOSE_ENCRYPT|KeyProperties.PURPOSE_DECRYPT).setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());generator.generateKey();}
        Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.ENCRYPT_MODE,store.getKey(alias,null));
        String encoded=Base64.encodeToString(cipher.getIV(),Base64.NO_WRAP)+":"+Base64.encodeToString(cipher.doFinal(session.toString().getBytes(StandardCharsets.UTF_8)),Base64.NO_WRAP);
        synchronized(this){if(valid(ticket)&&!context.getSharedPreferences("accounts",0).edit().putString(platform,encoded).commit())throw new IOException("无法保存登录状态，请重试");}
        main.post(()->{if(valid(ticket)&&onChanged!=null)onChanged.run();});
    }
    private JSONObject kg(String path,TreeMap<String,String> values)throws Exception{
        String mid=context.getSharedPreferences("accounts",0).getString("mid","");if(mid.isEmpty()){mid=hex(MessageDigest.getInstance("MD5").digest(UUID.randomUUID().toString().getBytes(StandardCharsets.UTF_8)));context.getSharedPreferences("accounts",0).edit().putString("mid",mid).apply();}
        values.putIfAbsent("appid","1005");values.put("dfid","-");values.put("mid",mid);values.put("uuid","-");values.put("clientver","20489");values.put("clienttime",Long.toString(System.currentTimeMillis()/1000));
        String salt="NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt",sign=salt;for(Map.Entry<String,String> item:values.entrySet())sign+=item.getKey()+"="+item.getValue();values.put("signature",md5(sign+salt));
        StringJoiner query=new StringJoiner("&");for(Map.Entry<String,String> item:values.entrySet())query.add(enc(item.getKey())+"="+enc(item.getValue()));
        return request("https://login-user.kugou.com"+path+"?"+query,null);
    }
    private JSONObject ne(String path,JSONObject data)throws Exception{
        String device=context.getSharedPreferences("accounts",0).getString("neDevice","");if(device.isEmpty()){device=UUID.randomUUID().toString().replace("-","");context.getSharedPreferences("accounts",0).edit().putString("neDevice",device).apply();}JSONObject body=new JSONObject(data.toString());String cookie=body.optString("_sessionCookie");body.remove("_sessionCookie");return NeteaseTransport.request(path,body,cookie,device);
    }
    private JSONObject request(String address,String body)throws Exception{
        return request(address,body,Collections.emptyMap());
    }
    private JSONObject request(String address,String body,Map<String,String> headers)throws Exception{
        HttpsURLConnection connection=(HttpsURLConnection)new URL(address).openConnection();connection.setConnectTimeout(12000);connection.setReadTimeout(12000);connection.setInstanceFollowRedirects(false);connection.setRequestProperty("User-Agent","Mozilla/5.0 (Linux; Android 14) PHSRadio/0.1");
        try{
            if(body!=null){connection.setRequestMethod("POST");connection.setDoOutput(true);connection.setRequestProperty("Content-Type","application/x-www-form-urlencoded");for(Map.Entry<String,String> header:headers.entrySet())connection.setRequestProperty(header.getKey(),header.getValue());try(OutputStream output=connection.getOutputStream()){output.write(body.getBytes(StandardCharsets.UTF_8));}}
            int http=connection.getResponseCode();if(http<200||http>=300)throw new IOException("平台连接失败（HTTP "+http+"），请稍后重试");
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();try(InputStream input=connection.getInputStream()){byte[] buffer=new byte[4096];int n;while((n=input.read(buffer))!=-1){if(bytes.size()+n>1048576)throw new IOException("平台返回数据过大");bytes.write(buffer,0,n);}}
            JSONObject result=new JSONObject(bytes.toString("UTF-8"));StringJoiner cookies=new StringJoiner("; ");for(Map.Entry<String,List<String>> header:connection.getHeaderFields().entrySet())if("Set-Cookie".equalsIgnoreCase(header.getKey()))for(String value:header.getValue())cookies.add(value.split(";",2)[0]);result.put("_cookie",cookies.toString());return result;
        }finally{connection.disconnect();}
    }
    private void saveQr(Bitmap image){
        
        // Use MediaStore on Android 10+; no broad storage permission is needed.
        if(Build.VERSION.SDK_INT<29){Toast.makeText(activity,"请截图保存二维码，再从官方 App 相册扫码",Toast.LENGTH_LONG).show();return;}
        android.net.Uri uri=null;try{ContentValues values=new ContentValues();values.put(MediaStore.Images.Media.DISPLAY_NAME,"PHS-login-"+System.currentTimeMillis()+".png");values.put(MediaStore.Images.Media.MIME_TYPE,"image/png");values.put(MediaStore.Images.Media.RELATIVE_PATH,"Pictures/PHSRadio");values.put(MediaStore.Images.Media.IS_PENDING,1);uri=activity.getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,values);if(uri==null)throw new IOException();try(OutputStream output=activity.getContentResolver().openOutputStream(uri)){if(output==null||!image.compress(Bitmap.CompressFormat.PNG,100,output))throw new IOException();}values.clear();values.put(MediaStore.Images.Media.IS_PENDING,0);activity.getContentResolver().update(uri,values,null,null);Toast.makeText(activity,"已保存，请在官方 App 扫一扫中选择相册",Toast.LENGTH_LONG).show();}catch(Exception e){if(uri!=null)activity.getContentResolver().delete(uri,null,null);Toast.makeText(activity,"保存失败，可截图后扫码",Toast.LENGTH_LONG).show();}
    }
    private static String enc(String s)throws Exception{return URLEncoder.encode(s,"UTF-8");}
    private static String md5(String s)throws Exception{return hex(MessageDigest.getInstance("MD5").digest(s.getBytes(StandardCharsets.UTF_8)));}
    private static String hex(byte[] bytes){StringBuilder result=new StringBuilder();for(byte b:bytes)result.append(String.format(Locale.ROOT,"%02x",b&255));return result.toString();}
    private String name(String platform){return platform.equals("kugou")?"酷狗音乐":"网易云音乐";}
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    private LinearLayout panel(){LinearLayout panel=new LinearLayout(activity);panel.setOrientation(1);panel.setPadding(dp(22),dp(20),dp(22),dp(20));GradientDrawable glass=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{0xF0335968,0xF0102034});glass.setCornerRadius(dp(26));glass.setStroke(dp(1),0x888FE1E8);panel.setBackground(glass);return panel;}
    private Dialog dialog(LinearLayout panel){Dialog dialog=new Dialog(activity);ScrollView scroll=new ScrollView(activity);scroll.addView(panel);dialog.setContentView(scroll);dialog.getWindow().setBackgroundDrawableResource(android.R.color.transparent);dialog.show();dialog.getWindow().setLayout(Math.min(activity.getResources().getDisplayMetrics().widthPixels-dp(24),dp(440)),-2);return dialog;}
    private TextView label(LinearLayout panel,String text,int size){TextView view=new TextView(activity);view.setText(text);view.setTextSize(size);view.setTextColor(Color.WHITE);view.setPadding(0,dp(8),0,dp(8));panel.addView(view);return view;}
    private void action(LinearLayout panel,String text,Runnable callback){Button button=new AeroButton(activity);button.setText(text);button.setAllCaps(false);button.setOnClickListener(v->callback.run());panel.addView(button,new LinearLayout.LayoutParams(-1,dp(52)));}
    public synchronized void close(){closed=true;++generation;main.removeCallbacksAndMessages(null);worker.shutdownNow();}
}
