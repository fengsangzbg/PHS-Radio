package io.github.fengsangzbg.phsradio.mobile;
import org.json.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import javax.crypto.*;
import javax.crypto.spec.SecretKeySpec;
import javax.net.ssl.HttpsURLConnection;

/** EAPI request transport with a stable per-installation device identity. No process-wide account cookie. */
final class NeteaseTransport {
    static JSONObject request(String path,JSONObject data,String cookie,String device)throws Exception{
        JSONObject body=new JSONObject(data.toString());JSONObject header=new JSONObject().put("os","android").put("osver","14").put("appver","8.20.20.231215173437").put("deviceId",device).put("requestId",System.currentTimeMillis()+"_"+new java.security.SecureRandom().nextInt(10000));
        for(String item:cookie.split(";")){String[] pair=item.trim().split("=",2);if(pair.length==2&&(pair[0].equals("MUSIC_U")||pair[0].equals("MUSIC_A")||pair[0].equals("__csrf")||pair[0].equals("NMTID")))header.put(pair[0],pair[1]);}body.put("header",header).put("e_r",false);String text=body.toString();String digest=hex(MessageDigest.getInstance("MD5").digest(("nobody"+path+"use"+text+"md5forencrypt").getBytes(StandardCharsets.UTF_8)));
        Cipher cipher=Cipher.getInstance("AES/ECB/PKCS5Padding");cipher.init(Cipher.ENCRYPT_MODE,new SecretKeySpec("e82ckenh8dichen8".getBytes(StandardCharsets.UTF_8),"AES"));String encrypted=hex(cipher.doFinal((path+"-36cd479b6b5-"+text+"-36cd479b6b5-"+digest).getBytes(StandardCharsets.UTF_8)));
        HttpsURLConnection connection=(HttpsURLConnection)new URL("https://interfacepc.music.163.com/eapi/"+path.substring(5)).openConnection();connection.setUseCaches(false);connection.setInstanceFollowRedirects(false);connection.setConnectTimeout(10000);connection.setReadTimeout(12000);connection.setRequestMethod("POST");connection.setDoOutput(true);connection.setRequestProperty("Content-Type","application/x-www-form-urlencoded");connection.setRequestProperty("User-Agent","NeteaseMusic/8.20.20.231215173437 (Android; 14)");connection.setRequestProperty("Cookie",NeteaseAuth.merge("os=android; appver=8.20.20.231215173437; deviceId="+device,cookie));
        try{try(OutputStream out=connection.getOutputStream()){out.write(("params="+encrypted).getBytes(StandardCharsets.UTF_8));}int status=connection.getResponseCode();if(status!=200)throw new IOException("网易云请求失败（HTTP "+status+"），请重试");ByteArrayOutputStream bytes=new ByteArrayOutputStream();try(InputStream input=connection.getInputStream()){byte[] buffer=new byte[8192];int n;while((n=input.read(buffer))!=-1){if(bytes.size()+n>8*1024*1024)throw new IOException("网易云返回数据过大");bytes.write(buffer,0,n);}}JSONObject result=new JSONObject(bytes.toString("UTF-8"));StringJoiner cookies=new StringJoiner("; ");for(Map.Entry<String,List<String>> entry:connection.getHeaderFields().entrySet())if("Set-Cookie".equalsIgnoreCase(entry.getKey()))for(String value:entry.getValue())cookies.add(value.split(";",2)[0]);result.put("_cookie",cookies.toString());return result;}finally{connection.disconnect();}
    }
    private static String hex(byte[] bytes){StringBuilder out=new StringBuilder();for(byte b:bytes)out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}
}
