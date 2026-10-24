const fs=require('fs'),base=__dirname+'/app/src/main/java/io/github/fengsangzbg/phsradio/mobile/';let p=base+'MusicAccounts.java',s=fs.readFileSync(p,'utf8');s=s.replace('private volatile boolean closed;','private volatile boolean closed;\n    private volatile String qrCookie="";\n    private Runnable qrTask;\n    private boolean pollBusy;');
s=s.replace('int ticket=++generation;LinearLayout panel=panel();Dialog dialog=dialog(panel);','int ticket=++generation;qrCookie="";qrTask=null;pollBusy=false;LinearLayout panel=panel();Dialog dialog=dialog(panel);');
s=s.replace('dialog.setOnDismissListener(d->{if(generation==ticket)++generation;});','dialog.setOnDismissListener(d->{if(generation==ticket){++generation;qrTask=null;}});');
s=s.replace('ne("/api/login/qrcode/unikey",new JSONObject().put("type",3))','ne("/api/login/qrcode/unikey",new JSONObject().put("type",3).put("_sessionCookie",qrCookie))');
s=s.replace('JSONObject data=response.optJSONObject("data");if(data==null','if(platform.equals("netease")&&valid(ticket))qrCookie=NeteaseAuth.cookies(qrCookie,response);\n            JSONObject data=response.optJSONObject("data");if(data==null');
let start=s.indexOf('    private void poll('),end=s.indexOf('    private interface Work',start);if(start<0||end<0)throw Error('Poll missing');
s=s.slice(0,start)+`    void resumePolling(){if(qrTask!=null&&!pollBusy){main.removeCallbacks(qrTask);main.post(qrTask);}}
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
`+s.slice(end);
start=s.indexOf('    private JSONObject ne(');end=s.indexOf('    private JSONObject request(',start);s=s.slice(0,start)+`    private JSONObject ne(String path,JSONObject data)throws Exception{
        String device=context.getSharedPreferences("accounts",0).getString("neDevice","");if(device.isEmpty()){device=UUID.randomUUID().toString().replace("-","");context.getSharedPreferences("accounts",0).edit().putString("neDevice",device).apply();}JSONObject body=new JSONObject(data.toString());String cookie=body.optString("_sessionCookie");body.remove("_sessionCookie");return NeteaseTransport.request(path,body,cookie,device);
    }
`+s.slice(end);
s=s.replace('if(valid(ticket))context.getSharedPreferences("accounts",0).edit().putString(platform,encoded).commit();','if(valid(ticket)&&!context.getSharedPreferences("accounts",0).edit().putString(platform,encoded).commit())throw new IOException("无法保存登录状态，请重试");');fs.writeFileSync(p,s);
p=base+'OnlineApi.java';s=fs.readFileSync(p,'utf8').replaceAll('"/api/nuser/account/get"','"/api/w/nuser/account/get"');fs.writeFileSync(p,s);
p=base+'MainActivity.java';s=fs.readFileSync(p,'utf8').replace('super.onResume();wallpaper.resume();','super.onResume();accounts.resumePolling();wallpaper.resume();');fs.writeFileSync(p,s);
