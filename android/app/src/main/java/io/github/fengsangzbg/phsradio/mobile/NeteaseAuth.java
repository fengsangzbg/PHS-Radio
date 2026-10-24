package io.github.fengsangzbg.phsradio.mobile;
import org.json.*;
import java.util.*;
final class NeteaseAuth {
    static int code(JSONObject result){JSONObject data=result.optJSONObject("data");int root=result.optInt("code",-1);return (root==200||root==-1)&&data!=null?data.optInt("code",root):root;}
    static String cookies(String previous,JSONObject result){JSONObject data=result.optJSONObject("data");return merge(previous,result.optString("_cookie"),result.optString("cookie"),data==null?"":data.optString("cookie"));}
    static boolean authorized(String cookies){for(String part:cookies.split(";")){String[] pair=part.trim().split("=",2);if(pair.length==2&&pair[0].equals("MUSIC_U")&&!pair[1].isEmpty())return true;}return false;}
    static String merge(String... sources){LinkedHashMap<String,String> values=new LinkedHashMap<>();for(String source:sources)for(String part:source.split(";")){String[] pair=part.trim().split("=",2);if(pair.length!=2||pair[0].isEmpty())continue;String name=pair[0];if(Arrays.asList("path","expires","max-age","domain","samesite","secure","httponly").contains(name.toLowerCase(Locale.ROOT)))continue;if(pair[1].isEmpty())values.remove(name);else values.put(name,pair[1]);}StringJoiner result=new StringJoiner("; ");for(Map.Entry<String,String> item:values.entrySet())result.add(item.getKey()+"="+item.getValue());return result.toString();}
}
