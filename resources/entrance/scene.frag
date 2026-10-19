#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D skyTex, whaleTex, armsTex, letterTex, screenTex, typeTex;
uniform vec2 viewport, deviceCenter, pointer;
uniform float time, spin, hover, press, light, loginBlend, deviceAngle;
uniform float zoomProgress, whiteProgress, typeProgress;
uniform int glyphCount;
uniform float typeAspect, glyphEnds[32];
const float PI = 3.14159265359;

mat2 turn(float a) { return mat2(cos(a),-sin(a),sin(a),cos(a)); }
float box(vec2 p,vec2 b,float r) { vec2 d=abs(p)-b+r;return length(max(d,0.))+min(max(d.x,d.y),0.)-r; }
float edge(float d) { return 1.-smoothstep(-fwidth(d),fwidth(d),d); }
float ease(float t) { return t*t*(3.-2.*t); }
vec3 layer(vec3 base,vec4 over,float opacity) { return mix(base,over.rgb,clamp(over.a*opacity,0.,1.)); }

vec2 backgroundUV(vec2 uv) {
    float aspect=viewport.x/viewport.y, source=1672./941.;
    vec2 scale=vec2(min(1.,aspect/source),min(1.,source/aspect));
    return (uv-.5)*scale+.5;
}
vec3 sky(vec2 uv) {
    vec2 tc=backgroundUV(uv);
    // Sub-pixel refraction, never CPU-resize or repeatedly blur the full image.
    tc+=vec2(sin(tc.y*24.+time*.31),cos(tc.x*20.-time*.24))*.0007;
    return texture(skyTex,clamp(tc,0.,1.)).rgb;
}
vec4 whale(vec2 world, vec2 center, float width, float phase, float direction) {
    vec2 tc=(world-center)/vec2(width,width*.375)+.5;
    if(direction>0.) tc.x=1.-tc.x;
    tc.y+=pow(clamp(tc.x,0.,1.),2.7)*sin(phase*1.8-tc.x*6.)*.058;
    tc.y+=sin(phase*.65)*.012;
    if(any(lessThan(tc,vec2(0.)))||any(greaterThan(tc,vec2(1.)))) return vec4(0.);
    vec4 c=texture(whaleTex,tc);
    c.rgb=mix(c.rgb,vec3(.16,.67,.84),.12);
    return c;
}
vec3 environment(vec2 world) {
    float aspect=viewport.x/viewport.y;
    vec2 uv=world/vec2(aspect,1.)+.5;
    vec3 c=sky(uv);
    float travel=mod(time*.018+0.38,3.6);
    float x=travel<1.8?-.9+travel:.9-(travel-1.8);
    c=layer(c,whale(world,vec2(x,-.16+.035*sin(time*.24)),.83,time,travel<1.8?1.:-1.),.83);
    float farTravel=mod(time*.010+1.35,3.2);
    float fx=farTravel<1.6?-.8+farTravel:.8-(farTravel-1.6);
    c=layer(c,whale(world,vec2(fx,-.32+.018*cos(time*.17)),.42,time+.9,farTravel<1.6?1.:-1.),.36);
    return c;
}

void main() {
    vec2 uv=vec2(vUV.x,1.-vUV.y);
    float aspect=viewport.x/viewport.y;
    vec2 raw=(uv-.5)*vec2(aspect,1.);
    vec2 anchor=deviceCenter+turn(-deviceAngle)*vec2(0.,-.132);
    float zoom=exp(ease(zoomProgress)*3.0);
    vec2 world=(raw-anchor)/zoom+anchor;
    vec3 c=environment(world);
    // Sunlight motes in the water; a fixed, small number of analytic particles.
    for(int i=0;i<7;i++) {
        float f=float(i);
        vec2 bp=vec2(sin(f*7.17+time*.027)*aspect*.49,fract(f*.173-time*.014)-.5);
        float d=length(world-bp), radius=.0017+fract(f*.71)*.0012;
        c+=vec3(.13,.22,.23)*exp(-d*d/(radius*radius*3.));
    }
    vec2 p=turn(deviceAngle)*(world-deviceCenter);
    p/=vec2(1.+press*.065,1.-press*.065);
    float deviceVisible=1.-ease(loginBlend);
    vec2 halfBody=vec2(.183,.318);
    float body=box(p,halfBody,.058);
    float shadow=box(p-vec2(.012,.025),halfBody+vec2(.009),.065);
    c=mix(c,vec3(.015,.23,.31),exp(-max(shadow,0.)*82.)*.22*deviceVisible);
    if(body<.018) {
        vec2 normal=normalize(vec2(sign(p.x)*pow(abs(p.x)/halfBody.x,5.),sign(p.y)*pow(abs(p.y)/halfBody.y,5.))+.00001);
        float thickness=clamp(-body/.026,0.,1.);
        vec3 refracted=environment(world+normal*(.009+.012*(1.-thickness)));
        vec3 glass=mix(refracted,vec3(.12,.66,.90),.22);
        float fresnel=pow(1.-thickness,2.);
        glass+=fresnel*vec3(.33,.66,.73)*(.60+.4*normal.y);
        float shine=exp(-pow((p.x+p.y*.40+.105)/.022,2.));
        glass+=shine*.23*vec3(.65,.94,1.);
        float rim=exp(-abs(body+.002)*750.);
        glass+=rim*vec3(.53,.86,.95);
        float inner=box(p+vec2(0.,.001),halfBody-vec2(.011),.05);
        glass+=exp(-abs(inner)*900.)*.28;
        c=mix(c,glass,edge(body)*deviceVisible);
    }
    vec2 screenPos=p-vec2(0.,-.132);
    vec2 screenSize=vec2(.151,.119);
    float screen=box(screenPos,screenSize,.024);
    if(screen<.012) {
        vec2 n=normalize(screenPos+.000001);
        vec3 ref=environment(world+n*.005);
        vec3 screenColor=mix(ref,vec3(.055,.24,.35),.73-.29*light);
        float glow=exp(-dot(screenPos/screenSize,screenPos/screenSize)*.8);
        screenColor+=light*glow*vec3(.46,.67,.71);
        screenColor+=exp(-abs(screen+.001)*900.)*vec3(.38,.65,.74);
        float reflection=smoothstep(.006,-.006,screenPos.y+screenPos.x*.33+.045);
        screenColor=mix(screenColor,vec3(.70,.93,.98),reflection*.09);
        vec2 tc=screenPos/(screenSize*2.)+.5;
        vec4 text=texture(screenTex,clamp(tc,0.,1.));
        screenColor=layer(screenColor,text,1.);
        c=mix(c,screenColor,edge(screen)*deviceVisible);
    }
    // White light really spills out from the display onto the water and glass.
    c+=light*exp(-dot(screenPos,screenPos)/.045)*vec3(.13,.22,.26)*deviceVisible*(1.-edge(screen));
    vec2 buttonPos=p-vec2(0.,.164);
    float radius=.097*(1.-press*.055);
    float button=length(buttonPos)-radius;
    if(button<.022) {
        float lens=clamp(1.-length(buttonPos)/radius,0.,1.);
        vec3 buttonColor=mix(environment(world+buttonPos*.09),vec3(.12,.56,.72),.43);
        buttonColor+=pow(1.-lens,4.)*vec3(.49,.82,.87)*.8;
        buttonColor+=exp(-abs(button+.001)*900.)*.55;
        buttonColor+=exp(-dot(buttonPos-vec2(-.028,-.039),buttonPos-vec2(-.028,-.039))/.0013)*.35;
        vec2 logo=buttonPos/(radius*1.8);
        vec2 armUV=turn(spin)*logo+.5;
        vec2 letterUV=logo+.5;
        if(all(greaterThanEqual(armUV,vec2(0.)))&&all(lessThanEqual(armUV,vec2(1.)))) buttonColor=layer(buttonColor,texture(armsTex,armUV),1.);
        if(all(greaterThanEqual(letterUV,vec2(0.)))&&all(lessThanEqual(letterUV,vec2(1.)))) buttonColor=layer(buttonColor,texture(letterTex,letterUV),1.);
        c=mix(c,buttonColor,edge(button)*deviceVisible);
        c+=exp(-max(button,0.)*170.)*hover*.15*vec3(.28,.76,.94)*deviceVisible;
    }
    // Hovering P produces spiralling wind filaments around the device.
    if(hover>.005) {
        vec2 wind=p-vec2(0.,.164);
        float a=atan(wind.y,wind.x), r=length(wind);
        float strands=pow(max(0.,cos(a*3.+time*3.8-r*24.)),35.);
        float annulus=exp(-pow((r-.155)/.047,2.));
        c+=strands*annulus*hover*.21*vec3(.74,.96,1.)*deviceVisible;
    }
    if(loginBlend>.001) {
        vec2 panel=raw;
        float w=min(aspect*.5-.035,.49);
        float d=box(panel,vec2(w,.405),.030);
        vec3 fog=(sky(uv+vec2(.009,0.))+sky(uv-vec2(.009,0.))+sky(uv+vec2(0.,.009))+sky(uv-vec2(0.,.009)))*.25;
        vec3 panelColor=mix(fog,vec3(.025,.115,.16),.78);
        panelColor+=exp(-abs(d)*850.)*.23;
        c=mix(c,panelColor,edge(d)*ease(loginBlend));
    }
    c=mix(c,vec3(.986,.995,1.),whiteProgress);
    if(typeProgress>0.) {
        float textScale=clamp(viewport.y/1080.,.65,2.5);
        vec2 textPixels=vec2(typeAspect,1.)*72.*textScale;
        vec2 origin=(viewport-textPixels)*.5;
        vec2 tp=(uv*viewport-origin)/textPixels;
        float index=0.;
        for(int i=0;i<glyphCount;i++){if(tp.x>=glyphEnds[i])index+=1.;}
        float age=typeProgress*float(glyphCount)-index;
        if(index>=0.&&index<float(glyphCount)&&age>0.) {
            tp.y+=exp(-age*5.)*sin(age*5.)*.14;
            if(tp.x>=0.&&tp.x<=1.&&tp.y>=0.&&tp.y<=1.) {
                float ink=texture(typeTex,tp).a;
                c=mix(c,vec3(.035,.22,.28),ink*clamp(age*3.,0.,1.));
            }
        }
    }
    fragColor=vec4(c,1.);
}
