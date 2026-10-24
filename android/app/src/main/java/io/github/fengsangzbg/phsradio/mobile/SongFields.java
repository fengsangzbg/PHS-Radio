package io.github.fengsangzbg.phsradio.mobile;
import org.json.*;
import java.util.*;
final class SongFields {
    static JSONObject parse(JSONObject source,String platform)throws JSONException{
        JSONObject r=flatten(source,0);boolean ne=platform.equals("netease");
        String hash=pick(r,"hash","FileHash","filehash","Hash","file_hash"),audio=id(r,"album_audio_id","mixsongid","MixSongID","audio_id","albumAudioId");
        String identifier=ne?id(r,"id","songId","songid","song_id"):!hash.isEmpty()?hash:audio.isEmpty()?"":"audio:"+audio;
        boolean missing=identifier.isEmpty();if(missing)identifier="unavailable:"+Integer.toHexString(source.toString().hashCode());
        String title=pick(r,"songname","song_name","audio_name","SongName","filename","FileName","name","title");if(title.isEmpty())title="未命名歌曲";
        String artist=pick(r,"author_name","singername","SingerName","artist","singer_name");JSONArray artists=r.optJSONArray("ar");if(artists==null)artists=r.optJSONArray("artists");if(artists==null)artists=r.optJSONArray("singerinfo");if(artists!=null){StringJoiner names=new StringJoiner(" / ");for(int i=0;i<artists.length();i++){JSONObject a=artists.optJSONObject(i);if(a!=null){String name=pick(a,"name","singername","singer_name");if(!name.isEmpty())names.add(name);}}if(names.length()>0)artist=names.toString();}
        String cover=pick(r,"cover","imgurl","image","album_img","union_cover","sizable_cover","cover_url","picUrl","coverImgUrl","album_cover");
        for(String key:new String[]{"al","album","albuminfo","album_info"}){JSONObject album=r.optJSONObject(key);if(album!=null){String url=pick(album,"picUrl","imgurl","sizable_cover","cover","image","img");if(!url.isEmpty()){cover=url;break;}}}
        return new JSONObject().put("id",identifier).put("hash",hash).put("platform",platform).put("uri","phs://"+platform+"/"+identifier).put("title",title).put("artist",artist).put("cover",image(cover)).put("album_id",pick(r,"album_id","albumid","AlbumID")).put("album_audio_id",audio).put("unavailable",missing);
    }
    private static JSONObject flatten(JSONObject input,int depth)throws JSONException{JSONObject out=new JSONObject(input.toString());if(depth>4)return out;for(String wrapper:new String[]{"file","song","song_info","songinfo","audio_info","base_info","base","data"}){JSONObject child=input.optJSONObject(wrapper);if(child==null)continue;JSONObject flat=flatten(child,depth+1);Iterator<String> keys=flat.keys();while(keys.hasNext()){String key=keys.next();if(!out.has(key)||out.isNull(key)||out.optString(key).isEmpty())out.put(key,flat.get(key));}}return out;}
    private static String id(JSONObject r,String... keys){for(String key:keys){String value=pick(r,key);if(!value.isEmpty()&&!value.equals("0")&&!value.equals("-1"))return value;}return "";}
    static String pick(JSONObject r,String... keys){for(String key:keys){Object value=r.opt(key);if(value==null||value==JSONObject.NULL||value instanceof JSONObject||value instanceof JSONArray)continue;String text=value.toString().trim();if(!text.isEmpty()&&!text.equals("null"))return text;}return "";}
    static String image(String value){if(value.startsWith("//"))value="https:"+value;return value.replace("{size}","1000").replaceFirst("^http:","https:");}
}
