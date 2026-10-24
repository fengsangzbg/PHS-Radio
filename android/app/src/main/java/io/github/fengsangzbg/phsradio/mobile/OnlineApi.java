package io.github.fengsangzbg.phsradio.mobile;

import android.content.Context;
import android.os.Build;
import android.util.Base64;
import org.json.*;
import javax.net.ssl.HttpsURLConnection;
import javax.crypto.*;
import javax.crypto.spec.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.security.spec.X509EncodedKeySpec;
import java.util.*;

/** Only account-authorized platform sources; every track is resolved just before playback. */
final class OnlineApi implements AutoCloseable {
    String quality="128";
    final Context context;
    private final MusicAccounts accounts;
    private final LinkedHashMap<String,JSONArray> trackCache=new LinkedHashMap<>(4,.75f,true);
    private final Map<String,Long> cacheTimes=new HashMap<>();
    OnlineApi(Context context){this.context=context;accounts=new MusicAccounts(context);}
    boolean logged(String platform){return !context.getSharedPreferences("accounts",0).getString(platform,"").isEmpty();}
    JSONObject session(String platform)throws Exception{return accounts.restore(platform);}
    JSONArray playlists(String platform,int page)throws Exception{
        JSONObject session=session(platform);
        if(platform.equals("netease")){
            JSONObject user=ne("/api/w/nuser/account/get",new JSONObject(),session);JSONObject profile=user.optJSONObject("profile"),account=user.optJSONObject("account");String uid=profile!=null?profile.optString("userId"):account!=null?account.optString("id"):"";
            if(uid.isEmpty())throw new IOException("网易云授权已过期，请重新登录");
            JSONObject result=ne("/api/user/playlist",new JSONObject().put("uid",uid).put("limit",100).put("offset",page*100).put("includeVideo",true),session);
            JSONArray rows=result.optJSONArray("playlist");if(rows==null)throw new IOException("网易云未返回歌单，请重新登录");return normalizeLists(rows,platform);
        }
        JSONObject result=kg("/v7/get_all_list",new TreeMap<>(Map.of("plat","1")),new JSONObject().put("userid",Long.parseLong(session.getString("userid"))).put("token",session.getString("token")).put("total_ver",979).put("type",2).put("page",page+1).put("pagesize",100).toString(),"cloudlist.service.kugou.com",session);
        return normalizeLists(array(result,"info","lists","list"),platform);
    }
    private JSONArray normalizeLists(JSONArray rows,String platform)throws Exception{
        JSONArray out=new JSONArray();for(int i=0;i<rows.length();i++){JSONObject r=rows.getJSONObject(i);out.put(new JSONObject().put("platform",platform).put("id",pick(r,"global_collection_id","global_specialid","id","listid","specialid")).put("title",pick(r,"name","listname","specialname")).put("cover",image(pick(r,"coverImgUrl","picUrl","pic","imgurl","cover","image"))));}return out;
    }
    JSONArray tracks(JSONObject playlist)throws Exception{
        return tracks(playlist,()->false);
    }
    JSONArray tracks(JSONObject playlist,java.util.function.BooleanSupplier cancelled)throws Exception{
        return tracks(playlist,cancelled,batch->{});
    }
    JSONArray tracks(JSONObject playlist,java.util.function.BooleanSupplier cancelled,java.util.function.Consumer<JSONArray> progress)throws Exception{
        if(cancelled.getAsBoolean())throw new InterruptedIOException();
        String platform=playlist.getString("platform");String cacheKey=platform+":"+playlist.getString("id")+":"+context.getSharedPreferences("accounts",0).getString(platform,"").hashCode();synchronized(trackCache){JSONArray cached=trackCache.get(cacheKey);if(cached!=null&&System.currentTimeMillis()-cacheTimes.getOrDefault(cacheKey,0L)<120000){progress.accept(cached);return cached;}}
        JSONObject session=session(platform);JSONArray out=new JSONArray();Set<String> seen=new HashSet<>();
        if(platform.equals("netease")){
            JSONObject response=ne("/api/v6/playlist/detail",new JSONObject().put("id",playlist.getString("id")).put("n",100000).put("s",0),session);
            JSONObject list=response.optJSONObject("playlist");if(list==null)throw new IOException("网易云未返回歌单详情");JSONArray ids=list.optJSONArray("trackIds");if(ids==null)throw new IOException("网易云未返回完整歌曲列表");
            for(int offset=0;offset<ids.length();offset+=200){if(cancelled.getAsBoolean())throw new InterruptedIOException();JSONArray query=new JSONArray();for(int n=offset;n<Math.min(ids.length(),offset+200);n++)query.put(new JSONObject().put("id",ids.getJSONObject(n).get("id")));
                JSONObject detail=ne("/api/v3/song/detail",new JSONObject().put("c",query.toString()),session);JSONArray songs=detail.optJSONArray("songs");if(songs==null)throw new IOException("部分歌曲信息读取失败，请重试");Map<String,JSONObject> byId=new HashMap<>();for(int n=0;n<songs.length();n++)byId.put(songs.getJSONObject(n).optString("id"),songs.getJSONObject(n));
                JSONArray batch=new JSONArray();for(int n=0;n<query.length();n++){String id=query.getJSONObject(n).optString("id");JSONObject raw=byId.get(id);if(raw==null)raw=new JSONObject().put("id",id).put("name","暂不可用的歌曲（"+id+"）");JSONObject track=song(raw,platform);out.put(track);batch.put(track);}progress.accept(batch);
            }remember(cacheKey,out);return out;
        }
        for(int page=0;page<1000;page++){
            if(cancelled.getAsBoolean())throw new InterruptedIOException();
            JSONObject result=kg("/pubsongs/v2/get_other_list_file_nofilt",new TreeMap<>(Map.of("area_code","1","begin_idx",Integer.toString(page*100),"plat","1","type","1","mode","1","personal_switch","1","pagesize","100","global_collection_id",playlist.getString("id"))),null,"",session);
            JSONArray rows=array(result,"info","songs","files","list");if(rows.length()>0&&!seen.add(md5(rows.toString())))throw new IOException("歌单分页重复，请稍后重试");JSONArray batch=new JSONArray();for(int i=0;i<rows.length();i++){JSONObject track=song(rows.getJSONObject(i),platform);out.put(track);batch.put(track);}progress.accept(batch);
            if(rows.length()<100){remember(cacheKey,out);return out;}
        }throw new IOException("歌单过大，尚未完整读取");
    }
    private void remember(String key,JSONArray rows){synchronized(trackCache){trackCache.put(key,rows);cacheTimes.put(key,System.currentTimeMillis());while(trackCache.size()>4){String oldest=trackCache.keySet().iterator().next();trackCache.remove(oldest);cacheTimes.remove(oldest);}}}
    JSONArray recommendations(String platform)throws Exception{
        JSONObject session=session(platform);JSONObject response=platform.equals("netease")?ne("/api/v3/discovery/recommend/songs",new JSONObject(),session):kg("/everyday_song_recommend",new TreeMap<>(Map.of("platform","android")),"","everydayrec.service.kugou.com",session);
        JSONArray rows=array(response,"dailySongs","song_list","songs","recommend","list","info");JSONArray out=new JSONArray();for(int i=0;i<rows.length();i++)out.put(song(rows.getJSONObject(i),platform));return out;
    }
    JSONArray recommendedPlaylists(String platform)throws Exception{
        JSONObject session=session(platform);JSONObject result;
        if(platform.equals("netease")){result=ne("/api/personalized/playlist",new JSONObject().put("limit",12).put("total",true).put("n",1000),session);return normalizeLists(array(result,"result"),platform);}
        String now=Long.toString(System.currentTimeMillis()/1000);
        JSONObject special=new JSONObject().put("withtag",1).put("withsong",1).put("sort",1).put("ugc",1).put("is_selected",0).put("withrecommend",1).put("area_code",1).put("categoryid",0);
        JSONObject data=new JSONObject().put("appid",1005).put("mid",context.getSharedPreferences("accounts",0).getString("mid","")).put("clientver",20489).put("platform","android").put("clienttime",now).put("userid",Long.parseLong(session.getString("userid"))).put("module_id",1).put("page",1).put("pagesize",12).put("key",md5("1005OIlwieks28dk2k092lksi2UIkp20489"+now)).put("special_recommend",special).put("req_multi",1).put("retrun_min",5).put("return_special_falg",1);
        result=kg("/v2/special_recommend",new TreeMap<>(),data.toString(),"specialrec.service.kugou.com",session);return normalizeLists(array(result,"special_list","specials","list","info"),platform);
    }
    JSONArray search(String platform,String query,int page)throws Exception{
        JSONObject session=logged(platform)?session(platform):new JSONObject().put("userid","0").put("token","").put("cookie","");
        if(context.getSharedPreferences("accounts",0).getString("mid","").isEmpty())context.getSharedPreferences("accounts",0).edit().putString("mid",md5(UUID.randomUUID().toString())).apply();
        JSONObject result=platform.equals("netease")?ne("/api/cloudsearch/pc",new JSONObject().put("s",query).put("type",1).put("limit",50).put("offset",page*50).put("total",true),session):kg("/v3/search/song",new TreeMap<>(Map.of("albumhide","0","iscorrection","1","keyword",query,"nocollect","0","page",Integer.toString(page+1),"pagesize","50","platform","AndroidFilter")),null,"complexsearch.kugou.com",session);
        if(result.optJSONObject("result")!=null)result=result.getJSONObject("result");JSONArray rows=array(result,"songs","lists","info","list");JSONArray out=new JSONArray();for(int i=0;i<rows.length();i++)out.put(song(rows.getJSONObject(i),platform));return out;
    }
    JSONObject song(JSONObject source,String platform)throws Exception{return SongFields.parse(source,platform);}
    String resolve(JSONObject track)throws Exception{
        if(track.optBoolean("unavailable"))throw new IOException("这首歌缺少有效播放标识，其他歌曲仍可正常选择");String platform=track.getString("platform");JSONObject session=session(platform);
        if(platform.equals("netease")){
            JSONObject result=ne("/api/song/enhance/player/url/v1",new JSONObject().put("ids","["+track.getString("id")+"]").put("level","standard").put("encodeType","mp3"),session);
            JSONArray data=result.optJSONArray("data");if(data==null||data.length()==0)throw new IOException("网易云未返回可播音源");return playable(data.getJSONObject(0).optString("url"));
        }
        String dfid=context.getSharedPreferences("devices",0).getString("dfid-"+session.getString("userid"),"");if(dfid.isEmpty())dfid=register(session);
        session.put("dfid",dfid);
        TreeMap<String,String> p=new TreeMap<>(Map.of("hash",track.optString("hash",track.getString("id")).toLowerCase(Locale.ROOT),"album_id",number(track.optString("album_id")),"album_audio_id",number(track.optString("album_audio_id")),"area_code","1","ssa_flag","is_fromtrack","version","11430","page_id","151369488","quality",quality,"behavior","play"));
        p.putAll(Map.of("pid","2","cmd","26","pidversion","3001","IsFreePart","0","ppage_id","463467626,350369493,788954147","cdnBackup","1","module","","clientver","11430"));
        JSONObject response=kg("/v5/url",p,null,"trackercdn.kugou.com",session);if(response.optInt("error_code")==20010){context.getSharedPreferences("devices",0).edit().remove("dfid-"+session.getString("userid")).apply();throw new IOException("酷狗设备授权已失效，请再点一次播放重新验证");}JSONArray urls=response.optJSONArray("url");if(urls==null&&response.optJSONObject("data")!=null)urls=response.getJSONObject("data").optJSONArray("url");
        if(urls==null){String direct=pick(response,"play_url","url");if(direct.isEmpty()&&response.optJSONObject("data")!=null)direct=pick(response.getJSONObject("data"),"play_url","url");if(!direct.isEmpty()&&!direct.startsWith("["))return playable(direct);}
        if(urls==null||urls.length()==0)throw new IOException("酷狗未提供可播音源（"+response.optInt("error_code")+"），可能受会员、购买或地区权限限制");return playable(urls.optString(0));
    }
    String lyrics(JSONObject track)throws Exception{
        if(!track.optString("platform").equals("netease")){
            JSONObject found=lyricRequest("/v1/search",Map.of("album_audio_id",number(track.optString("album_audio_id")),"appid","1005","clientver","20489","duration","0","hash",track.optString("hash",track.getString("id")),"keyword",track.optString("title")+" "+track.optString("artist"),"lrctxt","1","man","no"));if(found.optInt("status",200)!=200&&found.optInt("status")!=1)throw new IOException("酷狗歌词查询失败，请重试");JSONArray candidates=found.optJSONArray("candidates");if(candidates==null&&found.optJSONObject("data")!=null)candidates=found.getJSONObject("data").optJSONArray("candidates");if(candidates==null||candidates.length()==0)return "";JSONObject candidate=candidates.getJSONObject(0);JSONObject result=lyricRequest("/download",Map.of("ver","1","client","android","id",candidate.getString("id"),"accesskey",candidate.getString("accesskey"),"fmt","lrc","charset","utf8"));if(result.optInt("status",200)!=200&&result.optInt("status")!=1)throw new IOException("酷狗歌词下载失败，请重试");JSONObject body=result.optJSONObject("data")==null?result:result.getJSONObject("data");String decoded=body.optString("decodeContent");if(!decoded.isEmpty())return decoded;String encoded=body.optString("content");return encoded.isEmpty()?"":LyricCodec.decode(encoded);
        }
        JSONObject response=ne("/api/song/lyric",new JSONObject().put("id",track.getString("id")).put("lv",-1).put("kv",-1).put("tv",-1),session("netease"));if(response.optInt("code",200)!=200)throw new IOException("网易云歌词请求失败（"+response.optInt("code")+"），请重试");JSONObject lrc=response.optJSONObject("lrc");return lrc==null?"":lrc.optString("lyric");
    }
    private JSONObject lyricRequest(String path,Map<String,String> params)throws Exception{TreeMap<String,String> values=new TreeMap<>(params);if(path.equals("/download")){values.put("appid","1005");values.put("clientver","20489");values.put("clienttime",Long.toString(System.currentTimeMillis()/1000));values.put("dfid","-");values.put("uuid","-");values.put("mid",context.getSharedPreferences("accounts",0).getString("mid",""));}String salt="OIlwieks28dk2k092lksi2UIkp",sign=salt;for(Map.Entry<String,String> item:values.entrySet())sign+=item.getKey()+"="+item.getValue();values.put("signature",md5(sign+salt));return publicJson("https://lyrics.kugou.com",path,values);}
    private JSONObject publicJson(String host,String path,Map<String,String> params)throws Exception{
        StringJoiner query=new StringJoiner("&");for(Map.Entry<String,String> item:params.entrySet())query.add(URLEncoder.encode(item.getKey(),"UTF-8")+"="+URLEncoder.encode(item.getValue(),"UTF-8"));HttpsURLConnection c=(HttpsURLConnection)new URL(host+path+"?"+query).openConnection();c.setConnectTimeout(8000);c.setReadTimeout(8000);c.setRequestProperty("User-Agent","Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi");c.setRequestProperty("Accept","application/json");try(InputStream input=c.getInputStream()){ByteArrayOutputStream output=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int n;while((n=input.read(buffer))!=-1){if(output.size()+n>1024*1024)throw new IOException();output.write(buffer,0,n);}byte[] bytes=output.toByteArray();if(bytes.length>=2&&(bytes[0]&255)==31&&(bytes[1]&255)==139){try(InputStream gzip=new java.util.zip.GZIPInputStream(new ByteArrayInputStream(bytes))){ByteArrayOutputStream decoded=new ByteArrayOutputStream();while((n=gzip.read(buffer))!=-1){if(decoded.size()+n>2*1024*1024)throw new IOException("平台返回数据过大");decoded.write(buffer,0,n);}bytes=decoded.toByteArray();}}String raw=new String(bytes,StandardCharsets.UTF_8).trim();if(!raw.startsWith("{"))throw new IOException("平台返回了无法解析的数据，请重试");return new JSONObject(raw);}finally{c.disconnect();}
    }
    private JSONObject ne(String path,JSONObject data,JSONObject session)throws Exception{return accounts.neAuthorized(path,data,session);}
    String cover(JSONObject track)throws Exception{
        String provided=image(track.optString("cover"));if(provided.startsWith("https://"))return provided;if(!track.optString("platform").equals("kugou")||track.optBoolean("unavailable"))return "";
        String cacheKey=track.optString("uri",track.optString("id"));String cached=context.getSharedPreferences("covers",0).getString(cacheKey,"");if(!cached.isEmpty())return cached;
        JSONObject file=new JSONObject().put("hash",track.optString("hash",track.optString("id"))).put("album_id",number(track.optString("album_id"))).put("album_audio_id",number(track.optString("album_audio_id")));
        TreeMap<String,String> params=new TreeMap<>();params.put("album_image_type","-3");params.put("appid","1005");params.put("clientver","20489");params.put("author_image_type","3,4,5");params.put("count","5");params.put("data",new JSONArray().put(file).toString());params.put("isCdn","1");params.put("publish_time","1");String salt="OIlwieks28dk2k092lksi2UIkp",sign=salt;for(Map.Entry<String,String> item:params.entrySet())sign+=item.getKey()+"="+item.getValue();params.put("signature",md5(sign+salt));
        JSONObject response=publicJson("https://expendablekmr.kugou.com","/container/v2/image",params);String url=coverUrl(response,0);if(!url.isEmpty())context.getSharedPreferences("covers",0).edit().putString(cacheKey,url).apply();return url;
    }
    private static String coverUrl(Object value,int depth){if(depth>8)return "";if(value instanceof JSONObject){JSONObject object=(JSONObject)value;for(String key:new String[]{"sizable_cover","album_img","imgurl","picUrl","cover","url","image"}){Object item=object.opt(key);if(item instanceof String){String url=image((String)item);if(url.startsWith("https://"))return url;}}
        for(String key:new String[]{"album","albuminfo","album_image","album_images","data","info","list"}){String found=coverUrl(object.opt(key),depth+1);if(!found.isEmpty())return found;}
        Iterator<String> keys=object.keys();while(keys.hasNext()){String found=coverUrl(object.opt(keys.next()),depth+1);if(!found.isEmpty())return found;}
        }else if(value instanceof JSONArray){JSONArray array=(JSONArray)value;for(int i=0;i<array.length();i++){String found=coverUrl(array.opt(i),depth+1);if(!found.isEmpty())return found;}}return "";}
    private JSONObject kg(String path,TreeMap<String,String> params,String body,String router,JSONObject session)throws Exception{
        return new JSONObject(new String(kgBytes("https://gateway.kugou.com",path,params,body,router,session),StandardCharsets.UTF_8));
    }
    private byte[] kgBytes(String host,String path,TreeMap<String,String> params,String body,String router,JSONObject session)throws Exception{
        String mid=context.getSharedPreferences("accounts",0).getString("mid","");if(mid.isEmpty())throw new IOException("请重新登录酷狗以初始化设备");
        params.putIfAbsent("appid","1005");params.putIfAbsent("clientver","20489");params.put("clienttime",Long.toString(System.currentTimeMillis()/1000));params.put("mid",mid);params.put("uuid","-");params.put("dfid",session.optString("dfid","-"));params.put("userid",session.getString("userid"));params.put("token",session.getString("token"));
        if(path.equals("/v5/url"))params.put("key",md5(params.get("hash")+"57ae12eb6890223e355ccfcb74edf70d"+params.get("appid")+mid+params.get("userid")));
        String salt="OIlwieks28dk2k092lksi2UIkp",sign=salt;for(Map.Entry<String,String> item:params.entrySet())sign+=item.getKey()+"="+item.getValue();params.put("signature",md5(sign+(body==null?"":body)+salt));StringJoiner query=new StringJoiner("&");for(Map.Entry<String,String> item:params.entrySet())query.add(URLEncoder.encode(item.getKey(),"UTF-8")+"="+URLEncoder.encode(item.getValue(),"UTF-8"));
        HttpsURLConnection c=(HttpsURLConnection)new URL(host+path+"?"+query).openConnection();c.setConnectTimeout(12000);c.setReadTimeout(15000);c.setRequestProperty("User-Agent","Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi");c.setRequestProperty("mid",mid);c.setRequestProperty("dfid",params.get("dfid"));c.setRequestProperty("clienttime",params.get("clienttime"));if(!router.isEmpty())c.setRequestProperty("x-router",router);
        try{if(body!=null){c.setRequestMethod("POST");c.setDoOutput(true);c.setRequestProperty("Content-Type","application/json");try(OutputStream output=c.getOutputStream()){output.write(body.getBytes(StandardCharsets.UTF_8));}}if(c.getResponseCode()!=200)throw new IOException("酷狗请求失败（HTTP "+c.getResponseCode()+"）");try(InputStream input=c.getInputStream()){ByteArrayOutputStream output=new ByteArrayOutputStream();byte[] buffer=new byte[8192];int n;while((n=input.read(buffer))!=-1){if(output.size()+n>8*1024*1024)throw new IOException("平台数据超出限制");output.write(buffer,0,n);}return output.toByteArray();}}finally{c.disconnect();}
    }
    private String register(JSONObject session)throws Exception{
        String key=UUID.randomUUID().toString().replace("-","").substring(0,6),digest=md5(key);
        JSONObject device=new JSONObject().put("brand",Build.BRAND).put("manufacturer",Build.MANUFACTURER).put("device",Build.DEVICE).put("uuid",context.getSharedPreferences("accounts",0).getString("mid","")).put("buildSerial","unknown");
        device.put("imei",device.getString("uuid")).put("imsi","").put("basebandVer","").put("availableRamSize",Runtime.getRuntime().maxMemory()).put("availableRomSize",context.getFilesDir().getUsableSpace()).put("availableSDSize",context.getFilesDir().getUsableSpace()).put("batteryLevel",100).put("batteryStatus",3);
        for(String sensor:new String[]{"accelerometer","gravity","gyroscope","light","magnetic","orientation","pressure","step_counter","temperature"})device.put(sensor,false).put(sensor+"Value","");
        Cipher aes=Cipher.getInstance("AES/CBC/PKCS5Padding");SecretKeySpec secret=new SecretKeySpec(digest.substring(0,16).getBytes(StandardCharsets.UTF_8),"AES");IvParameterSpec iv=new IvParameterSpec(digest.substring(16).getBytes(StandardCharsets.UTF_8));aes.init(Cipher.ENCRYPT_MODE,secret,iv);String body=Base64.encodeToString(aes.doFinal(device.toString().getBytes(StandardCharsets.UTF_8)),Base64.NO_WRAP);
        String pub="MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDIAG7QOELSYoIJvTFJhMpe1s/gbjDJX51HBNnEl5HXqTW6lQ7LC8jr9fWZTwusknp+sVGzwd40MwP6U5yDE27M/X1+UR4tvOGOqp94TJtQ1EPnWGWXngpeIW5GxoQGao1rmYWAu6oi1z9XkChrsUdC6DJE5E221wf/4WLFxwAtRQIDAQAB";
        Cipher rsa=Cipher.getInstance("RSA/ECB/PKCS1Padding");rsa.init(Cipher.ENCRYPT_MODE,KeyFactory.getInstance("RSA").generatePublic(new X509EncodedKeySpec(Base64.decode(pub,Base64.DEFAULT))));String p=hex(rsa.doFinal(new JSONObject().put("aes",key).put("uid",Long.parseLong(session.getString("userid"))).put("token",session.getString("token")).toString().getBytes(StandardCharsets.UTF_8)));
        byte[] encrypted=kgBytes("https://userservice.kugou.com","/risk/v2/r_register_dev",new TreeMap<>(Map.of("part","1","platid","1","p",p)),body,"",session);aes.init(Cipher.DECRYPT_MODE,secret,iv);JSONObject result=new JSONObject(new String(aes.doFinal(encrypted),StandardCharsets.UTF_8));JSONObject data=result.optJSONObject("data");String id=data==null?"":data.optString("dfid");if(id.isEmpty())throw new IOException("酷狗设备验证未通过，请重新登录后重试");context.getSharedPreferences("devices",0).edit().putString("dfid-"+session.getString("userid"),id).apply();return id;
    }
    static JSONArray array(JSONObject object,String... keys)throws IOException{
        for(String key:keys){JSONArray rows=object.optJSONArray(key);if(rows!=null)return rows;}
        JSONArray direct=object.optJSONArray("data");if(direct!=null)return direct;JSONObject data=object.optJSONObject("data");if(data!=null)return array(data,keys);throw new IOException("平台未返回完整数据，请重试");
    }
    static String pick(JSONObject o,String... keys){for(String key:keys){String s=o.optString(key,"");if(!s.isEmpty()&&!s.equals("null"))return s;}return "";}
    static String image(String value){return SongFields.image(value);}
    private static String number(String s){return s.matches("[0-9]+")?s:"0";}
    private static String playable(String url)throws IOException{if(url.startsWith("http://"))url="https://"+url.substring(7);if(!url.startsWith("https://"))throw new IOException("平台未提供可播音源，可能需要会员、购买或当前地区无版权");return url;}
    static String message(Throwable error){String text=error.getMessage();return text!=null&&!text.isEmpty()&&text.charAt(0)>='\u3400'&&text.charAt(0)<='\u9fff'&&!text.contains("token")&&!text.contains("https://")?text:"平台请求失败或连接超时，请重试；如持续失败请重新登录";}
    private static String md5(String s)throws Exception{return hex(MessageDigest.getInstance("MD5").digest(s.getBytes(StandardCharsets.UTF_8)));}
    private static String hex(byte[] bytes){StringBuilder b=new StringBuilder();for(byte n:bytes)b.append(String.format(Locale.ROOT,"%02x",n&255));return b.toString();}
    public void close(){accounts.close();}
}
