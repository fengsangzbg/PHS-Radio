package io.github.fengsangzbg.phsradio.mobile;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.zip.InflaterInputStream;
final class LyricCodec {
    static String decode(String encoded)throws IOException{
        byte[] bytes;try{bytes=Base64.getMimeDecoder().decode(encoded);}catch(IllegalArgumentException error){throw new IOException("歌词编码无法解析");}
        if(bytes.length>=4&&bytes[0]=='k'&&bytes[1]=='r'&&bytes[2]=='c'&&bytes[3]=='1'){
            int[] key={64,71,97,119,94,50,116,71,81,54,49,45,206,210,110,105};byte[] data=new byte[bytes.length-4];for(int i=0;i<data.length;i++)data[i]=(byte)(bytes[i+4]^key[i%key.length]);
            try(InputStream input=new InflaterInputStream(new ByteArrayInputStream(data))){ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int n;while((n=input.read(buffer))!=-1){if(out.size()+n>2*1024*1024)throw new IOException("歌词文件过大");out.write(buffer,0,n);}return out.toString("UTF-8");}
        }
        return new String(bytes,StandardCharsets.UTF_8);
    }
}
