package io.github.fengsangzbg.phsradio.mobile;
import java.util.*;
import java.util.regex.*;
/** Same time conventions as Windows lyrics.cpp, including KRC line timings and plain text. */
final class LrcTimeline {
    static final class Line{final long time;final String text;Line(long time,String text){this.time=time;this.text=text;}}
    final ArrayList<Line> lines=new ArrayList<>();
    String plain="";
    private static final Pattern STAMP=Pattern.compile("\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]");
    private static final Pattern KRC=Pattern.compile("^\\[(\\d+),(\\d+)\\]");
    private static final Pattern OFFSET=Pattern.compile("\\[offset:([+-]?\\d+)\\]",Pattern.CASE_INSENSITIVE);
    private static final Pattern META=Pattern.compile("^\\[(?:ar|al|ti|au|by|re|ve|length|language|offset):",Pattern.CASE_INSENSITIVE);
    static LrcTimeline parse(String source){
        LrcTimeline result=new LrcTimeline();if(source==null)return result;source=source.replace("\uFEFF","");long offset=0;Matcher adjustment=OFFSET.matcher(source);if(adjustment.find()){try{offset=Long.parseLong(adjustment.group(1));}catch(NumberFormatException ignored){}}
        StringJoiner plain=new StringJoiner("\n");
        for(String raw:source.split("\\r?\\n")){String row=raw.trim();if(row.isEmpty()||META.matcher(row).find())continue;ArrayList<Long> times=new ArrayList<>();int end=0;Matcher matcher=STAMP.matcher(row);
            while(matcher.find(end)&&matcher.start()==end){int seconds=Integer.parseInt(matcher.group(2));if(seconds>=60)break;String fraction=matcher.group(3);long millis=fraction==null?0:Long.parseLong((fraction+"00").substring(0,3));times.add(Math.max(0,Long.parseLong(matcher.group(1))*60000+seconds*1000+millis-offset));end=matcher.end();}
            if(times.isEmpty()){Matcher krc=KRC.matcher(row);if(krc.find()){try{times.add(Math.max(0,Long.parseLong(krc.group(1))-offset));end=krc.end();}catch(NumberFormatException ignored){}}}
            String text=row.substring(end).replaceAll("<\\d+,\\d+,\\d+>","").trim();if(!text.isEmpty())plain.add(text);for(long time:times)result.lines.add(new Line(time,text));
        }
        result.lines.sort(Comparator.comparingLong(line->line.time));ArrayList<Line> merged=new ArrayList<>();for(Line line:result.lines){if(!merged.isEmpty()&&merged.get(merged.size()-1).time==line.time){Line previous=merged.remove(merged.size()-1);merged.add(new Line(line.time,previous.text.equals(line.text)?line.text:previous.text+"\n"+line.text));}else merged.add(line);}result.lines.clear();result.lines.addAll(merged);result.plain=plain.toString();return result;
    }
    int at(long position){int low=0,high=lines.size()-1,result=-1;while(low<=high){int middle=(low+high)>>>1;if(lines.get(middle).time<=position){result=middle;low=middle+1;}else high=middle-1;}return result;}
}
