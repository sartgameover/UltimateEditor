#include "Effects.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <cmath>

const char* kVertexSource = R"GLSL(#version 330 core
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vUV = p;
    gl_Position = uMVP * vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

QString effectFragmentSource(const EffectDef& d) {
    return QString(R"GLSL(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform sampler2D uPrev;
uniform vec2 uRes;
uniform float uTime;
uniform float uAudio;
uniform float uMotion;
uniform float p0; uniform float p1; uniform float p2; uniform float p3; uniform float p4;
uniform float p5; uniform float p6; uniform float p7; uniform float p8;
float hash(float n) { return fract(sin(n * 127.1) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
float vnoise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);
    float a = hash2(i); float b = hash2(i + vec2(1.0, 0.0)); float c = hash2(i + vec2(0.0, 1.0)); float d = hash2(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm(vec2 p) { float v = 0.0; float a = 0.5; for (int k = 0; k < 5; k++) { v += a * vnoise(p); p *= 2.0; a *= 0.5; } return v; }
vec4 over(vec4 c, vec4 g, float k) { float a = g.a * k; return vec4(mix(c.rgb, g.rgb, a), max(c.a, a)); }
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
)GLSL") + d.body + QString("\nvoid main() { oColor = fx(vUV); }\n");
}

static QList<EffectDef> build() {
    QList<EffectDef> L;
    auto add = [&](const char* name, const char* cat, QList<ParamDef> params, const char* body) {
        EffectDef d; d.name = name; d.category = cat; d.params = params; d.body = body; L.append(d);
    };
    // ---- цвет ----
    add("Grayscale", "Цвет", {{"amount", 1, 0, 1}},
        "vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float g=dot(c.rgb,vec3(0.299,0.587,0.114)); return vec4(mix(c.rgb,vec3(g),p0),c.a); }");
    add("Invert", "Цвет", {{"amount", 1, 0, 1}},
        "vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(mix(c.rgb,1.0-c.rgb,p0),c.a); }");
    add("Sepia", "Цвет", {{"amount", 1, 0, 1}},
        "vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 s=vec3(dot(c.rgb,vec3(0.393,0.769,0.189)),dot(c.rgb,vec3(0.349,0.686,0.168)),dot(c.rgb,vec3(0.272,0.534,0.131))); return vec4(mix(c.rgb,min(s,vec3(1.0)),p0),c.a); }");
    add("Brightness & Contrast", "Цвет", {{"brightness", 0, -1, 1}, {"contrast", 1, 0, 3}},
        "vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 v=(c.rgb-0.5)*p1+0.5+p0; return vec4(clamp(v,0.0,1.0),c.a); }");
    add("Saturation", "Цвет", {{"saturation", 1, 0, 3}},
        "vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float g=dot(c.rgb,vec3(0.299,0.587,0.114)); return vec4(clamp(mix(vec3(g),c.rgb,p0),0.0,1.0),c.a); }");
    add("Color Wheels", "Цвет",
        {{"lift_r",0,-0.3,0.3},{"lift_g",0,-0.3,0.3},{"lift_b",0,-0.3,0.3},
         {"gamma_r",1,0.4,2.5},{"gamma_g",1,0.4,2.5},{"gamma_b",1,0.4,2.5},
         {"gain_r",1,0,2},{"gain_g",1,0,2},{"gain_b",1,0,2}},
        "vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 v=c.rgb*vec3(p6,p7,p8)+vec3(p0,p1,p2); v=pow(max(v,vec3(0.0)),1.0/max(vec3(p3,p4,p5),vec3(0.01))); return vec4(clamp(v,0.0,1.0),c.a); }");
    // ---- стилизация ----
    add("Gaussian Blur", "Размытие", {{"radius", 8, 0, 40}},
        "vec4 fx(vec2 uv){ vec4 acc=vec4(0.0); for(int i=0;i<32;i++){ float r=sqrt((float(i)+0.5)/32.0)*p0; float a=float(i)*2.39996; acc+=texture(uTex,uv+vec2(cos(a),sin(a))*r/uRes);} return acc/32.0; }");
    add("Pixelate", "Стилизация", {{"size", 16, 2, 128}},
        "vec4 fx(vec2 uv){ vec2 g=uRes/max(p0,1.0); return texture(uTex,(floor(uv*g)+0.5)/g); }");
    add("Vignette", "Стилизация", {{"strength", 0.6, 0, 1}},
        "vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float d=distance(uv,vec2(0.5))*1.4142; float v=1.0-p0*smoothstep(0.3,1.0,d); return vec4(c.rgb*v,c.a); }");
    add("Mirror", "Стилизация", {},
        "vec4 fx(vec2 uv){ if(uv.x>0.5) uv.x=1.0-uv.x; return texture(uTex,uv); }");
    // ---- динамические / «умные» ----
    add("Chromatic Aberration", "Искажение", {{"shift", 6, 0, 40}, {"audio_react", 1, 0, 1}},
        "vec4 fx(vec2 uv){ float s=p0*(1.0+p1*uAudio*3.0)/uRes.x; vec4 c=texture(uTex,uv); c.r=texture(uTex,uv+vec2(s,0.0)).r; c.b=texture(uTex,uv-vec2(s,0.0)).b; return c; }");
    add("Glitch", "Искажение", {{"intensity", 0.6, 0, 1}, {"audio_react", 1, 0, 1}, {"motion_react", 1, 0, 1}},
        "vec4 fx(vec2 uv){ float k=p0*(0.15+p1*uAudio+p2*uMotion); float tt=floor(uTime*24.0); float band=floor(uv.y*24.0);"
        " float on=step(hash(band+tt*13.0),k*0.6); float sh=(hash(band*7.0+tt)-0.5)*0.25*min(k,1.0)*on;"
        " vec2 u=vec2(fract(uv.x+sh),uv.y); float s=k*0.02*on+k*0.004; vec4 c=texture(uTex,u);"
        " c.r=texture(uTex,u+vec2(s,0.0)).r; c.b=texture(uTex,u-vec2(s,0.0)).b; return c; }");
    add("VHS", "Искажение", {{"noise", 0.15, 0, 1}, {"bleed", 4, 0, 20}},
        "vec4 fx(vec2 uv){ float wob=sin(uv.y*40.0+uTime*8.0)*0.0015+(hash(floor(uTime*30.0)+floor(uv.y*60.0))-0.5)*0.003;"
        " vec2 u=vec2(uv.x+wob,uv.y); float b=p1/uRes.x; vec4 c=texture(uTex,u); c.r=texture(uTex,u+vec2(b,0.0)).r; c.b=texture(uTex,u-vec2(b,0.0)).b;"
        " float line=0.85+0.15*sin(uv.y*uRes.y*3.14159); float n=(hash(dot(uv,vec2(12.9898,78.233))+uTime)-0.5)*p0;"
        " return vec4(clamp(c.rgb*line+n,0.0,1.0),c.a); }");
    add("Datamosh", "Искажение", {{"smear", 0.7, 0, 1}, {"block", 24, 4, 64}},
        "vec4 fx(vec2 uv){ vec2 g=uRes/max(p1,4.0); vec2 cc=(floor(uv*g)+0.5)/g;"
        " float d=length(texture(uTex,cc).rgb-texture(uPrev,cc).rgb); float thr=0.12*(1.2-p0);"
        " return d>thr ? texture(uPrev,uv) : texture(uTex,uv); }");

    // =============== эффекты в стиле Kdenlive (MLT / frei0r) ===============
    // ---- Размытие и скрытие ----
    add("Fast Box Blur", "Размытие", {{"radius", 6, 0, 30}}, R"G(vec4 fx(vec2 uv){ vec4 acc=vec4(0.0); for(int i=-3;i<=3;i++){ for(int j=-3;j<=3;j++){ acc+=texture(uTex,uv+vec2(float(i),float(j))*p0/3.0/uRes); } } return acc/49.0; })G");
    add("Directional Blur", "Размытие", {{"angle", 0, -180, 180}, {"length", 20, 0, 100}}, R"G(vec4 fx(vec2 uv){ vec2 d=vec2(cos(radians(p0)),sin(radians(p0)))*p1/uRes; vec4 acc=vec4(0.0); for(int i=-8;i<=8;i++){ acc+=texture(uTex,uv+d*float(i)/8.0); } return acc/17.0; })G");
    add("CC Radial Fast Blur", "Размытие", {{"strength", 0.4, 0, 1}}, R"G(vec4 fx(vec2 uv){ vec2 d=(uv-vec2(0.5))*p0*0.2; vec4 acc=vec4(0.0); for(int i=0;i<16;i++){ acc+=texture(uTex,uv-d*float(i)/15.0); } return acc/16.0; })G");
    add("Obscure (Mosaic Region)", "Размытие", {{"x", 0.4, 0, 1}, {"y", 0.4, 0, 1}, {"width", 0.2, 0, 1}, {"height", 0.2, 0, 1}, {"size", 16, 2, 80}},
        R"G(vec4 fx(vec2 uv){ if(uv.x>p0&&uv.x<p0+p2&&uv.y>p1&&uv.y<p1+p3){ vec2 g=uRes/max(p4,2.0); return texture(uTex,(floor(uv*g)+0.5)/g); } return texture(uTex,uv); })G");
    add("Tilt Shift", "Размытие", {{"focus", 0.5, 0, 1}, {"width", 0.1, 0.02, 0.5}, {"blur", 10, 0, 40}},
        R"G(vec4 fx(vec2 uv){ float d=smoothstep(p1,p1+0.25,abs(uv.y-p0)); float r=p2*d; vec4 acc=vec4(0.0); for(int i=0;i<16;i++){ float a=float(i)*2.39996; float rr=sqrt((float(i)+0.5)/16.0)*r; acc+=texture(uTex,uv+vec2(cos(a),sin(a))*rr/uRes); } return acc/16.0; })G");
    add("Sharpen", "Размытие", {{"amount", 1, 0, 3}},
        R"G(vec4 fx(vec2 uv){ vec2 t=1.0/uRes; vec4 c=texture(uTex,uv); vec3 b=(texture(uTex,uv+vec2(t.x,0.0)).rgb+texture(uTex,uv-vec2(t.x,0.0)).rgb+texture(uTex,uv+vec2(0.0,t.y)).rgb+texture(uTex,uv-vec2(0.0,t.y)).rgb)*0.25; return vec4(clamp(c.rgb+(c.rgb-b)*p0,0.0,1.0),c.a); })G");
    // ---- Цвет и коррекция ----
    add("Gamma", "Цвет", {{"gamma", 1, 0.2, 3}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(pow(c.rgb,vec3(1.0/p0)),c.a); })G");
    add("Intensity (Brightness)", "Цвет", {{"level", 1, 0, 4}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(clamp(c.rgb*p0,0.0,1.0),c.a); })G");
    add("Exposure", "Цвет", {{"ev", 0, -3, 3}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(clamp(c.rgb*exp2(p0),0.0,1.0),c.a); })G");
    add("Hue Shift", "Цвет", {{"degrees", 0, -180, 180}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 h=rgb2hsv(c.rgb); h.x=fract(h.x+p0/360.0); return vec4(hsv2rgb(h),c.a); })G");
    add("Color Temperature", "Цвет", {{"warmth", 0, -1, 1}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); c.r*=1.0+p0*0.3; c.b*=1.0-p0*0.3; return vec4(clamp(c.rgb,0.0,1.0),c.a); })G");
    add("Levels", "Цвет", {{"in_black", 0, 0, 1}, {"in_white", 1, 0, 1}, {"gamma", 1, 0.2, 3}, {"out_black", 0, 0, 1}, {"out_white", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 v=clamp((c.rgb-p0)/max(p1-p0,0.001),0.0,1.0); v=pow(v,vec3(1.0/p2)); v=mix(vec3(p3),vec3(p4),v); return vec4(v,c.a); })G");
    add("Threshold", "Цвет", {{"threshold", 0.5, 0, 1}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(vec3(step(p0,luma(c.rgb))),c.a); })G");
    add("Posterize", "Цвет", {{"levels", 5, 2, 32}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(floor(c.rgb*p0+0.5)/p0,c.a); })G");
    add("Solarize", "Цвет", {{"threshold", 0.5, 0, 1}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(mix(c.rgb,1.0-c.rgb,step(vec3(p0),c.rgb)),c.a); })G");
    add("Vibrance", "Цвет", {{"vibrance", 0.5, -1, 2}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float mx=max(c.r,max(c.g,c.b)); float mn=min(c.r,min(c.g,c.b)); float g=luma(c.rgb); vec3 v=mix(vec3(g),c.rgb,1.0+p0*(1.0-(mx-mn))); return vec4(clamp(v,0.0,1.0),c.a); })G");
    add("Shadow/Highlight", "Цвет", {{"shadows", 0.3, -1, 1}, {"highlights", -0.2, -1, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float l=luma(c.rgb); float sh=1.0-smoothstep(0.0,0.5,l); float hi=smoothstep(0.5,1.0,l); return vec4(clamp(c.rgb+p0*sh*0.4+p1*hi*0.4,0.0,1.0),c.a); })G");
    add("Channel Mixer", "Цвет", {{"rr", 1, -2, 2}, {"rg", 0, -2, 2}, {"rb", 0, -2, 2}, {"gr", 0, -2, 2}, {"gg", 1, -2, 2}, {"gb", 0, -2, 2}, {"br", 0, -2, 2}, {"bg", 0, -2, 2}, {"bb", 1, -2, 2}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 v=vec3(dot(c.rgb,vec3(p0,p1,p2)),dot(c.rgb,vec3(p3,p4,p5)),dot(c.rgb,vec3(p6,p7,p8))); return vec4(clamp(v,0.0,1.0),c.a); })G");
    add("Colorize", "Цвет", {{"hue", 0.6, 0, 1}, {"saturation", 0.7, 0, 1}, {"mix", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 t=hsv2rgb(vec3(p0,p1,luma(c.rgb))); return vec4(mix(c.rgb,t,p2),c.a); })G");
    add("Duotone", "Цвет", {{"shadow_r", 0.05, 0, 1}, {"shadow_g", 0, 0, 1}, {"shadow_b", 0.2, 0, 1}, {"light_r", 1, 0, 1}, {"light_g", 0.8, 0, 1}, {"light_b", 0.3, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(mix(vec3(p0,p1,p2),vec3(p3,p4,p5),luma(c.rgb)),c.a); })G");
    // ---- Обрезка, трансформ, искажение ----
    add("Edge Crop", "Обрезка и трансформ", {{"top", 0, 0, 0.5}, {"left", 0, 0, 0.5}, {"bottom", 0, 0, 0.5}, {"right", 0, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float m=step(p1,uv.x)*step(uv.x,1.0-p3)*step(p0,uv.y)*step(uv.y,1.0-p2); return vec4(c.rgb,c.a*m); })G");
    add("Flip", "Обрезка и трансформ", {{"horizontal", 1, 0, 1}, {"vertical", 0, 0, 1}},
        R"G(vec4 fx(vec2 uv){ if(p0>0.5) uv.x=1.0-uv.x; if(p1>0.5) uv.y=1.0-uv.y; return texture(uTex,uv); })G");
    add("Letterbox (Cinema Bars)", "Обрезка и трансформ", {{"bar", 0.12, 0, 0.3}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float m=step(p0,uv.y)*step(uv.y,1.0-p0); return vec4(c.rgb*m,c.a); })G");
    add("Border", "Обрезка и трансформ", {{"width", 0.02, 0, 0.1}, {"r", 1, 0, 1}, {"g", 1, 0, 1}, {"b", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float e=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y)); return e<p0 ? vec4(p1,p2,p3,c.a) : c; })G");
    add("Twirl", "Искажение", {{"angle", 2.5, -6, 6}, {"radius", 0.5, 0.1, 1}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(0.5); float r=length(d); float a=p0*smoothstep(p1,0.0,r); float s=sin(a); float co=cos(a); d=vec2(co*d.x-s*d.y,s*d.x+co*d.y); return texture(uTex,vec2(0.5)+d); })G");
    add("Optics Compensation", "Искажение", {{"amount", 0.6, -1, 1.5}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(0.5); float r=length(d)*2.0; float f=1.0+p0*r*r; return texture(uTex,vec2(0.5)+d/f); })G");
    add("Wave Warp", "Искажение", {{"amplitude", 0.02, 0, 0.1}, {"frequency", 14, 1, 60}, {"speed", 3, 0, 20}},
        R"G(vec4 fx(vec2 uv){ uv.x+=sin(uv.y*p1+uTime*p2)*p0; return texture(uTex,uv); })G");
    add("Ripple", "Искажение", {{"amplitude", 0.015, 0, 0.1}, {"frequency", 40, 1, 120}, {"speed", 6, 0, 30}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(0.5); float r=length(d); uv+=normalize(d+vec2(1e-6))*sin(r*p1-uTime*p2)*p0; return texture(uTex,uv); })G");
    add("CC Kaleida", "Искажение", {{"segments", 6, 2, 16}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(0.5); float a=atan(d.y,d.x); float r=length(d); float seg=6.2831853/p0; a=abs(mod(a,seg)-seg*0.5); return texture(uTex,vec2(cos(a),sin(a))*r+vec2(0.5)); })G");
    // ---- Стилизация и «плёнка» ----
    add("Edge Glow (Sobel)", "Стилизация", {{"strength", 1.5, 0, 4}},
        R"G(vec4 fx(vec2 uv){ vec2 t=1.0/uRes; float tl=luma(texture(uTex,uv+vec2(-t.x,-t.y)).rgb); float tc=luma(texture(uTex,uv+vec2(0.0,-t.y)).rgb); float tr=luma(texture(uTex,uv+vec2(t.x,-t.y)).rgb);
 float ml=luma(texture(uTex,uv+vec2(-t.x,0.0)).rgb); float mr=luma(texture(uTex,uv+vec2(t.x,0.0)).rgb); float bl=luma(texture(uTex,uv+vec2(-t.x,t.y)).rgb); float bc=luma(texture(uTex,uv+vec2(0.0,t.y)).rgb); float br=luma(texture(uTex,uv+vec2(t.x,t.y)).rgb);
 float gx=-tl-2.0*ml-bl+tr+2.0*mr+br; float gy=-tl-2.0*tc-tr+bl+2.0*bc+br; float e=length(vec2(gx,gy))*p0; vec4 c=texture(uTex,uv); return vec4(clamp(c.rgb+vec3(e),0.0,1.0),c.a); })G");
    add("Emboss", "Стилизация", {{"strength", 2, 0, 6}},
        R"G(vec4 fx(vec2 uv){ vec2 t=1.0/uRes; vec4 c=texture(uTex,uv); float a=luma(texture(uTex,uv+t).rgb); float b=luma(texture(uTex,uv-t).rgb); return vec4(vec3(0.5+(a-b)*p0),c.a); })G");
    add("Cartoon", "Стилизация", {{"levels", 5, 2, 12}, {"edges", 2, 0, 6}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 t=1.0/uRes; float a=luma(texture(uTex,uv+vec2(t.x,0.0)).rgb)-luma(texture(uTex,uv-vec2(t.x,0.0)).rgb); float b=luma(texture(uTex,uv+vec2(0.0,t.y)).rgb)-luma(texture(uTex,uv-vec2(0.0,t.y)).rgb); float e=length(vec2(a,b))*p1; vec3 v=floor(c.rgb*p0+0.5)/p0; v*=1.0-clamp(e,0.0,1.0); return vec4(v,c.a); })G");
    add("Halftone", "Стилизация", {{"size", 10, 3, 40}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 g=uv*uRes/p0; vec2 f=fract(g)-0.5; float r=sqrt(luma(c.rgb))*0.7; float m=smoothstep(r,r-0.1,length(f)); return vec4(c.rgb*m,c.a); })G");
    add("Oil Paint", "Стилизация", {{"radius", 3, 1, 4}},
        R"G(vec4 fx(vec2 uv){ vec2 t=1.0/uRes; int R=int(p0); vec3 m[4]; vec3 s[4]; for(int k=0;k<4;k++){ m[k]=vec3(0.0); s[k]=vec3(0.0); } float n=float((R+1)*(R+1));
 for(int j=-4;j<=0;j++){ for(int i=-4;i<=0;i++){ if(-i>R||-j>R) continue;
  vec3 c0=texture(uTex,uv+vec2(float(i),float(j))*t).rgb; m[0]+=c0; s[0]+=c0*c0;
  vec3 c1=texture(uTex,uv+vec2(float(-i),float(j))*t).rgb; m[1]+=c1; s[1]+=c1*c1;
  vec3 c2=texture(uTex,uv+vec2(float(i),float(-j))*t).rgb; m[2]+=c2; s[2]+=c2*c2;
  vec3 c3=texture(uTex,uv+vec2(float(-i),float(-j))*t).rgb; m[3]+=c3; s[3]+=c3*c3; } }
 float minv=1e9; vec4 src=texture(uTex,uv); vec3 res=src.rgb;
 for(int k=0;k<4;k++){ m[k]/=n; s[k]=abs(s[k]/n-m[k]*m[k]); float v=s[k].r+s[k].g+s[k].b; if(v<minv){ minv=v; res=m[k]; } }
 return vec4(res,src.a); })G");
    add("Old Film", "Стилизация", {{"dust", 0.4, 0, 1}, {"scratches", 0.4, 0, 1}, {"flicker", 0.4, 0, 1}, {"vignette", 0.5, 0, 1}, {"grain", 0.4, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float tt=floor(uTime*24.0); float fl=1.0+(hash(tt)-0.5)*p2*0.4; vec3 v=c.rgb*fl;
 float sc=step(1.0-p1*0.02,hash(floor(uv.x*300.0)+tt*7.0)); v+=vec3(sc*0.6)*step(0.3,hash(tt+uv.x));
 vec2 g=floor(uv*uRes/3.0); float dust=step(1.0-p0*0.004,hash2(g+tt*3.1)); v=mix(v,vec3(0.9),dust);
 v+=(hash2(uv*uRes+tt)-0.5)*p4*0.25; float d=distance(uv,vec2(0.5)); v*=1.0-p3*smoothstep(0.35,0.85,d);
 float l=luma(v); v=mix(vec3(l),v*vec3(1.0,0.92,0.78),0.8); return vec4(clamp(v,0.0,1.0),c.a); })G");
    add("Film Grain", "Стилизация", {{"amount", 0.4, 0, 1}, {"size", 1.5, 1, 4}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 g=floor(uv*uRes/p1); float n=hash2(g+floor(uTime*24.0))-0.5; return vec4(clamp(c.rgb+n*p0*0.5,0.0,1.0),c.a); })G");
    add("Noise", "Стилизация", {{"amount", 0.2, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 n=vec3(hash2(uv*uRes+uTime),hash2(uv*uRes+uTime+1.7),hash2(uv*uRes+uTime+3.1))-0.5; return vec4(clamp(c.rgb+n*p0,0.0,1.0),c.a); })G");
    add("Glow", "Стилизация", {{"threshold", 0.6, 0, 1}, {"radius", 18, 2, 60}, {"intensity", 1, 0, 3}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 acc=vec3(0.0); for(int i=0;i<24;i++){ float a=float(i)*2.39996; float r=sqrt((float(i)+0.5)/24.0)*p1; vec3 s=texture(uTex,uv+vec2(cos(a),sin(a))*r/uRes).rgb; acc+=max(s-vec3(p0),vec3(0.0)); } return vec4(clamp(c.rgb+acc/24.0*p2*3.0,0.0,1.0),c.a); })G");
    add("CRT Scanlines", "Стилизация", {{"lines", 0.6, 0, 1}, {"curvature", 0.4, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(0.5); uv=vec2(0.5)+d*(1.0+dot(d,d)*p1); if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0) return vec4(0.0,0.0,0.0,1.0); vec4 c=texture(uTex,uv); float s=0.5+0.5*sin(uv.y*uRes.y*3.14159); return vec4(c.rgb*mix(1.0,s,p0*0.5),c.a); })G");
    // ---- Альфа и ключи ----
    add("Chroma Key (Select Color)", "Альфа / Ключи", {{"key_r", 0, 0, 1}, {"key_g", 1, 0, 1}, {"key_b", 0, 0, 1}, {"similarity", 0.15, 0, 0.8}, {"smoothness", 0.1, 0, 0.5}, {"spill", 0.5, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 k=vec3(p0,p1,p2);
 vec2 cc=vec2(dot(c.rgb,vec3(-0.169,-0.331,0.5)),dot(c.rgb,vec3(0.5,-0.419,-0.081))); vec2 kk=vec2(dot(k,vec3(-0.169,-0.331,0.5)),dot(k,vec3(0.5,-0.419,-0.081)));
 float d=distance(cc,kk); float a=smoothstep(p3,p3+p4+0.001,d); vec3 rgb=c.rgb; float g=rgb.g; rgb.g=mix(g,min(g,(rgb.r+rgb.b)*0.5),p5*(1.0-a)); return vec4(rgb,c.a*a); })G");
    add("Luma Key", "Альфа / Ключи", {{"threshold", 0.5, 0, 1}, {"softness", 0.1, 0, 0.5}, {"invert", 0, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float l=luma(c.rgb); float a=smoothstep(p0-p1,p0+p1+0.001,l); if(p2>0.5) a=1.0-a; return vec4(c.rgb,c.a*a); })G");
    // =============== After Effects: размытие и резкость ===============
    add("Bilateral Blur", "Размытие", {{"radius", 6, 1, 20}, {"threshold", 0.15, 0.01, 1}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 acc=vec3(0.0); float ws=0.0; for(int i=0;i<24;i++){ float a=float(i)*2.39996; float r=sqrt((float(i)+0.5)/24.0)*p0; vec3 s=texture(uTex,uv+vec2(cos(a),sin(a))*r/uRes).rgb; float w=exp(-distance(s,c.rgb)/max(p1,0.001)); acc+=s*w; ws+=w; } return vec4(acc/ws,c.a); })G");
    add("Smart Blur", "Размытие", {{"radius", 5, 1, 20}, {"threshold", 25, 1, 100}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 acc=vec3(0.0); float ws=0.0; for(int i=0;i<24;i++){ float a=float(i)*2.39996; float r=sqrt((float(i)+0.5)/24.0)*p0; vec3 s=texture(uTex,uv+vec2(cos(a),sin(a))*r/uRes).rgb; float w=exp(-distance(s,c.rgb)/max(p1/100.0,0.001)); acc+=s*w; ws+=w; } return vec4(acc/ws,c.a); })G");
    add("Camera Lens Blur", "Размытие", {{"radius", 12, 0, 40}, {"highlights", 1.5, 0, 4}}, R"G(vec4 fx(vec2 uv){ vec3 acc=vec3(0.0); float ws=0.0; for(int i=0;i<32;i++){ float a=float(i)*2.39996; float r=sqrt((float(i)+0.5)/32.0)*p0; vec3 s=texture(uTex,uv+vec2(cos(a),sin(a))*r/uRes).rgb; float w=1.0+max(luma(s)-0.7,0.0)*p1*4.0; acc+=s*w; ws+=w; } return vec4(acc/ws,texture(uTex,uv).a); })G");
    add("CC Cross Blur", "Размытие", {{"horizontal", 8, 0, 40}, {"vertical", 2, 0, 40}}, R"G(vec4 fx(vec2 uv){ vec4 acc=vec4(0.0); for(int i=-6;i<=6;i++){ acc+=texture(uTex,uv+vec2(float(i)*p0/6.0/uRes.x,0.0)); acc+=texture(uTex,uv+vec2(0.0,float(i)*p1/6.0/uRes.y)); } return acc/26.0; })G");
    add("CC Radial Blur", "Размытие", {{"angle", 12, 0, 90}}, R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(0.5); vec4 acc=vec4(0.0); for(int i=0;i<16;i++){ float a=radians(p0)*(float(i)/15.0-0.5); float s=sin(a); float co=cos(a); acc+=texture(uTex,vec2(0.5)+vec2(co*d.x-s*d.y,s*d.x+co*d.y)); } return acc/16.0; })G");
    add("Channel Blur", "Размытие", {{"red", 6, 0, 30}, {"green", 0, 0, 30}, {"blue", 0, 0, 30}, {"alpha", 0, 0, 30}},
        R"G(vec4 fx(vec2 uv){ vec4 acc=vec4(0.0); for(int i=0;i<12;i++){ float a=float(i)*2.39996; float k=sqrt((float(i)+0.5)/12.0); vec2 d=vec2(cos(a),sin(a))*k/uRes; acc.r+=texture(uTex,uv+d*p0).r; acc.g+=texture(uTex,uv+d*p1).g; acc.b+=texture(uTex,uv+d*p2).b; acc.a+=texture(uTex,uv+d*p3).a; } return acc/12.0; })G");
    add("Unsharp Mask", "Размытие", {{"amount", 1.5, 0, 5}, {"radius", 3, 1, 20}, {"threshold", 0.02, 0, 0.3}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 b=vec3(0.0); for(int i=0;i<12;i++){ float a=float(i)*2.39996; float r=sqrt((float(i)+0.5)/12.0)*p1; b+=texture(uTex,uv+vec2(cos(a),sin(a))*r/uRes).rgb; } b/=12.0; vec3 d=c.rgb-b; d*=step(p2,length(d)); return vec4(clamp(c.rgb+d*p0,0.0,1.0),c.a); })G");
    // =============== канал ===============
    add("Invert", "Канал", {{"channel", 0, 0, 4}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); int m=int(p0+0.5); if(m==0) c.rgb=1.0-c.rgb; else if(m==1) c.r=1.0-c.r; else if(m==2) c.g=1.0-c.g; else if(m==3) c.b=1.0-c.b; else c.a=1.0-c.a; return c; })G");
    add("Minimax", "Канал", {{"radius", 2, 1, 4}, {"operation", 0, 0, 1}}, R"G(vec4 fx(vec2 uv){ vec4 acc=texture(uTex,uv); for(int i=-4;i<=4;i++){ for(int j=-4;j<=4;j++){ if(abs(float(i))>p0||abs(float(j))>p0) continue; vec4 s=texture(uTex,uv+vec2(float(i),float(j))/uRes); if(p1>0.5) acc=max(acc,s); else acc=min(acc,s); } } return acc; })G");
    add("Shift Channels", "Канал", {{"take_red", 0, 0, 6}, {"take_green", 1, 0, 6}, {"take_blue", 2, 0, 6}, {"take_alpha", 3, 0, 6}},
        R"G(float pick(vec4 c,float m){ int k=int(m+0.5); if(k==0) return c.r; if(k==1) return c.g; if(k==2) return c.b; if(k==3) return c.a; if(k==4) return luma(c.rgb); if(k==5) return 1.0; return 0.0; }
vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(pick(c,p0),pick(c,p1),pick(c,p2),pick(c,p3)); })G");
    add("Solid Composite", "Канал", {{"r", 0, 0, 1}, {"g", 0, 0, 1}, {"b", 0, 0, 1}, {"opacity", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 o=mix(vec3(p0,p1,p2),c.rgb,c.a); return vec4(mix(c.rgb,o,p3),mix(c.a,1.0,p3)); })G");
    add("Arithmetic", "Канал", {{"operator", 0, 0, 3}, {"red", 0.2, 0, 1}, {"green", 0.2, 0, 1}, {"blue", 0.2, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 v=vec3(p1,p2,p3); int m=int(p0+0.5); vec3 r; if(m==0) r=c.rgb+v; else if(m==1) r=c.rgb-v; else if(m==2) r=c.rgb*(v*2.0); else r=abs(c.rgb-v); return vec4(clamp(r,0.0,1.0),c.a); })G");
    // =============== коррекция цвета ===============
    add("Hue/Saturation", "Цвет", {{"hue", 0, -180, 180}, {"saturation", 0, -1, 1}, {"lightness", 0, -1, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 h=rgb2hsv(c.rgb); h.x=fract(h.x+p0/360.0); h.y=clamp(h.y*(1.0+p1),0.0,1.0); vec3 r=hsv2rgb(h); r=(p2>=0.0) ? mix(r,vec3(1.0),p2) : mix(r,vec3(0.0),-p2); return vec4(r,c.a); })G");
    add("Color Balance", "Цвет", {{"shadow_r", 0, -1, 1}, {"shadow_g", 0, -1, 1}, {"shadow_b", 0, -1, 1}, {"mid_r", 0, -1, 1}, {"mid_g", 0, -1, 1}, {"mid_b", 0, -1, 1}, {"high_r", 0, -1, 1}, {"high_g", 0, -1, 1}, {"high_b", 0, -1, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float l=luma(c.rgb); float sh=1.0-smoothstep(0.0,0.5,l); float hi=smoothstep(0.5,1.0,l); float mi=1.0-sh-hi; vec3 v=c.rgb+vec3(p0,p1,p2)*sh*0.3+vec3(p3,p4,p5)*mi*0.3+vec3(p6,p7,p8)*hi*0.3; return vec4(clamp(v,0.0,1.0),c.a); })G");
    add("Gamma/Pedestal/Gain", "Цвет", {{"gamma", 1, 0.2, 3}, {"pedestal", 0, -0.5, 0.5}, {"gain", 1, 0, 3}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 v=pow(max(c.rgb*p2+p1,vec3(0.0)),vec3(1.0/p0)); return vec4(clamp(v,0.0,1.0),c.a); })G");
    add("Photo Filter", "Цвет", {{"r", 1, 0, 1}, {"g", 0.6, 0, 1}, {"b", 0.1, 0, 1}, {"density", 0.25, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 v=mix(c.rgb,c.rgb*vec3(p0,p1,p2)*1.6,p3); return vec4(clamp(v,0.0,1.0),c.a); })G");
    add("Tint", "Цвет", {{"black_r", 0, 0, 1}, {"black_g", 0, 0, 1}, {"black_b", 0, 0, 1}, {"white_r", 1, 0, 1}, {"white_g", 0.85, 0, 1}, {"white_b", 0.6, 0, 1}, {"amount", 0.8, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 t=mix(vec3(p0,p1,p2),vec3(p3,p4,p5),luma(c.rgb)); return vec4(mix(c.rgb,t,p6),c.a); })G");
    add("Tritone", "Цвет", {{"shadow_r", 0.05, 0, 1}, {"shadow_g", 0, 0, 1}, {"shadow_b", 0.25, 0, 1}, {"mid_r", 0.8, 0, 1}, {"mid_g", 0.3, 0, 1}, {"mid_b", 0.4, 0, 1}, {"high_r", 1, 0, 1}, {"high_g", 0.9, 0, 1}, {"high_b", 0.6, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float l=luma(c.rgb); vec3 s=vec3(p0,p1,p2); vec3 m=vec3(p3,p4,p5); vec3 h=vec3(p6,p7,p8); vec3 t=(l<0.5) ? mix(s,m,l*2.0) : mix(m,h,(l-0.5)*2.0); return vec4(t,c.a); })G");
    add("Change to Color", "Цвет", {{"from_r", 0.2, 0, 1}, {"from_g", 0.6, 0, 1}, {"from_b", 0.2, 0, 1}, {"to_r", 0.9, 0, 1}, {"to_g", 0.2, 0, 1}, {"to_b", 0.2, 0, 1}, {"tolerance", 0.1, 0, 0.5}, {"softness", 0.1, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 fh=rgb2hsv(vec3(p0,p1,p2)); vec3 th=rgb2hsv(vec3(p3,p4,p5)); vec3 h=rgb2hsv(c.rgb); float d=abs(h.x-fh.x); d=min(d,1.0-d); float m=1.0-smoothstep(p6,p6+p7+0.001,d); h.x=fract(h.x+(th.x-fh.x)*m); h.y=mix(h.y,h.y*th.y/max(fh.y,0.01),m*0.5); return vec4(hsv2rgb(h),c.a); })G");
    add("Leave Color", "Цвет", {{"r", 1, 0, 1}, {"g", 0, 0, 1}, {"b", 0, 0, 1}, {"tolerance", 0.1, 0, 0.5}, {"softness", 0.1, 0, 0.5}, {"amount", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 kh=rgb2hsv(vec3(p0,p1,p2)); vec3 h=rgb2hsv(c.rgb); float d=abs(h.x-kh.x); d=min(d,1.0-d); float m=1.0-smoothstep(p3,p3+p4+0.001,d); float g=luma(c.rgb); return vec4(mix(vec3(g),c.rgb,mix(1.0,m,p5)),c.a); })G");
    add("Curves", "Цвет", {{"p0", 0, 0, 1}, {"p25", 0.25, 0, 1}, {"p50", 0.5, 0, 1}, {"p75", 0.75, 0, 1}, {"p100", 1, 0, 1}},
        R"G(float curve(float x){ x=clamp(x,0.0,1.0); float t=x*4.0; int k=int(min(floor(t),3.0)); float f=t-float(k); float a; float b; if(k==0){a=p0;b=p1;} else if(k==1){a=p1;b=p2;} else if(k==2){a=p2;b=p3;} else {a=p3;b=p4;} f=f*f*(3.0-2.0*f); return mix(a,b,f); }
vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(curve(c.r),curve(c.g),curve(c.b),c.a); })G");
    add("Video Limiter", "Цвет", {{"min", 0.0627, 0, 0.5}, {"max", 0.92, 0.5, 1}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(clamp(c.rgb,vec3(p0),vec3(p1)),c.a); })G");
    add("Colorama", "Цвет", {{"phase", 0, 0, 1}, {"cycles", 1, 0.2, 4}, {"saturation", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float l=luma(c.rgb); vec3 col=hsv2rgb(vec3(fract(l*p1+p0),p2,1.0)); return vec4(col*mix(0.4,1.0,l),c.a); })G");
    // =============== искажение ===============
    add("Bulge", "Искажение", {{"center_x", 0.5, 0, 1}, {"center_y", 0.5, 0, 1}, {"radius", 0.4, 0.05, 1}, {"amount", 0.6, -1, 1}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(p0,p1); float r=length(d)/max(p2,0.01); if(r<1.0){ d*=1.0-p3*(1.0-r*r); } return texture(uTex,vec2(p0,p1)+d); })G");
    add("Spherize", "Искажение", {{"center_x", 0.5, 0, 1}, {"center_y", 0.5, 0, 1}, {"radius", 0.45, 0.05, 1}, {"amount", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(p0,p1); float r=length(d)/max(p2,0.01); if(r<1.0 && r>0.0001){ float k=asin(r)/(1.5708*r); d*=mix(1.0,k,p3); } return texture(uTex,vec2(p0,p1)+d); })G");
    add("Magnify", "Искажение", {{"center_x", 0.5, 0, 1}, {"center_y", 0.5, 0, 1}, {"size", 0.2, 0.02, 0.6}, {"magnification", 2, 1, 6}, {"feather", 0.02, 0, 0.2}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(p0,p1); float r=length(d*vec2(uRes.x/uRes.y,1.0)); float m=1.0-smoothstep(p2-p4,p2,r); return texture(uTex,vec2(p0,p1)+d/mix(1.0,p3,m)); })G");
    add("Offset", "Искажение", {{"shift_x", 0.1, -1, 1}, {"shift_y", 0, -1, 1}}, R"G(vec4 fx(vec2 uv){ return texture(uTex,fract(uv-vec2(p0,p1))); })G");
    add("Polar Coordinates", "Искажение", {{"interpolation", 1, 0, 1}, {"mode", 0, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec2 d=uv-vec2(0.5); vec2 u; if(p1<0.5){ u=vec2(fract(atan(d.y,d.x)/6.2831853+0.5),clamp(length(d)*2.0,0.0,1.0)); } else { float ang=(uv.x-0.5)*6.2831853; u=vec2(0.5)+0.5*uv.y*vec2(cos(ang),sin(ang)); } return texture(uTex,mix(uv,u,p0)); })G");
    add("CC Tiler", "Искажение", {{"tiles", 2, 1, 8}}, R"G(vec4 fx(vec2 uv){ return texture(uTex,fract(uv*p0)); })G");
    add("CC Slant", "Искажение", {{"slant", 0.3, -1, 1}}, R"G(vec4 fx(vec2 uv){ uv.x+=(uv.y-0.5)*p0; return texture(uTex,uv); })G");
    add("Turbulent Displace", "Искажение", {{"amount", 0.03, 0, 0.2}, {"size", 4, 1, 20}, {"speed", 0.5, 0, 5}},
        R"G(vec4 fx(vec2 uv){ vec2 n=vec2(fbm(uv*p1+uTime*p2),fbm(uv*p1+17.3+uTime*p2))-0.5; return texture(uTex,uv+n*p0*2.0); })G");
    // =============== генерация ===============
    add("Fill", "Генерация", {{"r", 1, 0, 1}, {"g", 0, 0, 1}, {"b", 0, 0, 1}, {"opacity", 1, 0, 1}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); return vec4(mix(c.rgb,vec3(p0,p1,p2),p3),c.a); })G");
    add("Gradient Ramp", "Генерация", {{"r1", 1, 0, 1}, {"g1", 0.3, 0, 1}, {"b1", 0.2, 0, 1}, {"r2", 0.1, 0, 1}, {"g2", 0.2, 0, 1}, {"b2", 0.9, 0, 1}, {"shape", 0, 0, 1}, {"angle", 90, 0, 360}, {"blend", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float t=(p6<0.5) ? dot(uv-vec2(0.5),vec2(cos(radians(p7)),sin(radians(p7))))+0.5 : length(uv-vec2(0.5))*1.414; vec3 g=mix(vec3(p0,p1,p2),vec3(p3,p4,p5),clamp(t,0.0,1.0)); return over(c,vec4(g,1.0),p8); })G");
    add("Checkerboard", "Генерация", {{"size", 40, 4, 200}, {"r", 1, 0, 1}, {"g", 1, 0, 1}, {"b", 1, 0, 1}, {"opacity", 0.5, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 g=floor(uv*uRes/p0); float k=mod(g.x+g.y,2.0); return over(c,vec4(vec3(p1,p2,p3),k),p4); })G");
    add("Grid", "Генерация", {{"size", 60, 8, 300}, {"border", 2, 1, 20}, {"r", 1, 0, 1}, {"g", 1, 0, 1}, {"b", 1, 0, 1}, {"opacity", 0.8, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 q=uv*uRes/p0; vec2 m=min(fract(q),1.0-fract(q))*p0; float d=min(m.x,m.y); float a=1.0-smoothstep(p1*0.5,p1*0.5+1.0,d); return over(c,vec4(vec3(p2,p3,p4),a),p5); })G");
    add("Circle", "Генерация", {{"center_x", 0.5, 0, 1}, {"center_y", 0.5, 0, 1}, {"radius", 0.2, 0.01, 1}, {"feather", 0.02, 0, 0.5}, {"r", 1, 0, 1}, {"g", 1, 0, 1}, {"b", 1, 0, 1}, {"opacity", 1, 0, 1}, {"invert", 0, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 d=(uv-vec2(p0,p1))*vec2(uRes.x/uRes.y,1.0); float a=1.0-smoothstep(p2-p3,p2+0.0001,length(d)); if(p8>0.5) a=1.0-a; return over(c,vec4(vec3(p4,p5,p6),a),p7); })G");
    add("Ellipse", "Генерация", {{"center_x", 0.5, 0, 1}, {"center_y", 0.5, 0, 1}, {"width", 0.5, 0.05, 1}, {"height", 0.3, 0.05, 1}, {"thickness", 0.02, 0.002, 0.2}, {"r", 1, 0, 1}, {"g", 1, 0, 1}, {"b", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 d=(uv-vec2(p0,p1))/vec2(p2*0.5,p3*0.5); float r=length(d); float a=1.0-smoothstep(p4*0.5,p4*0.5+0.01,abs(r-1.0)*min(p2,p3)*0.5); return over(c,vec4(vec3(p5,p6,p7),a),1.0); })G");
    add("Cell Pattern", "Генерация", {{"size", 8, 1, 40}, {"contrast", 1, 0.3, 4}, {"speed", 0.5, 0, 4}},
        R"G(vec2 h2(vec2 p){ return vec2(hash2(p),hash2(p+vec2(31.7,17.3))); }
vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 g=uv*vec2(uRes.x/uRes.y,1.0)*p0; vec2 ip=floor(g); vec2 f=fract(g); float md=8.0; for(int y=-1;y<=1;y++){ for(int x=-1;x<=1;x++){ vec2 o=vec2(float(x),float(y)); vec2 pt=0.5+0.5*sin(uTime*p2+6.2831*h2(ip+o)); md=min(md,length(o+pt-f)); } } return vec4(vec3(pow(clamp(md,0.0,1.0),p1)),1.0); })G");
    add("Fractal Noise", "Генерация", {{"scale", 4, 0.5, 30}, {"contrast", 1.2, 0.2, 4}, {"brightness", 0, -0.5, 0.5}, {"speed", 0.3, 0, 3}, {"blend", 1, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float n=fbm(uv*p0+vec2(uTime*p3,0.0)); n=clamp((n-0.5)*p1+0.5+p2,0.0,1.0); return over(c,vec4(vec3(n),1.0),p4); })G");
    add("Lens Flare", "Генерация", {{"x", 0.3, 0, 1}, {"y", 0.3, 0, 1}, {"brightness", 1, 0, 3}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 asp=vec2(uRes.x/uRes.y,1.0); vec2 pos=vec2(p0,p1); float r=length((uv-pos)*asp); vec3 f=vec3(1.0,0.9,0.7)*exp(-r*18.0)*p2*1.5; f+=vec3(1.0,0.8,0.5)*exp(-r*5.0)*0.25*p2; for(int k=1;k<4;k++){ vec2 gp=mix(pos,vec2(1.0)-pos,float(k)*0.35); float gr=length((uv-gp)*asp); f+=vec3(0.4,0.6,1.0)*smoothstep(0.06,0.0,gr)*0.15*p2; } return vec4(clamp(c.rgb+f,0.0,1.0),c.a); })G");
    add("Radio Waves", "Генерация", {{"x", 0.5, 0, 1}, {"y", 0.5, 0, 1}, {"frequency", 12, 1, 60}, {"speed", 2, 0, 10}, {"width", 0.3, 0.05, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float r=length((uv-vec2(p0,p1))*vec2(uRes.x/uRes.y,1.0)); float w=0.5+0.5*sin(r*p2*6.2831-uTime*p3); float a=smoothstep(1.0-p4,1.0,w); return over(c,vec4(1.0,1.0,1.0,a),1.0); })G");
    add("CC Light Sweep", "Генерация", {{"direction", 45, 0, 360}, {"width", 0.15, 0.02, 0.6}, {"speed", 0.5, 0.05, 3}, {"intensity", 0.6, 0, 2}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float s=dot(uv-vec2(0.5),vec2(cos(radians(p0)),sin(radians(p0)))); float pos=fract(uTime*p2)*1.6-0.8; float q=(s-pos)/p1; float a=exp(-q*q); return vec4(clamp(c.rgb+vec3(a*p3),0.0,1.0),c.a); })G");
    // =============== кейинг и маски ===============
    add("Color Range", "Альфа / Ключи", {{"hue_min", 0.2, 0, 1}, {"hue_max", 0.45, 0, 1}, {"sat_min", 0.3, 0, 1}, {"val_min", 0.2, 0, 1}, {"softness", 0.04, 0, 0.3}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 h=rgb2hsv(c.rgb); float hm=smoothstep(p0-p4,p0,h.x)*(1.0-smoothstep(p1,p1+p4,h.x)); float a=1.0-hm*step(p2,h.y)*step(p3,h.z); return vec4(c.rgb,c.a*a); })G");
    add("Extract", "Альфа / Ключи", {{"black", 0.2, 0, 1}, {"white", 0.8, 0, 1}, {"softness", 0.1, 0, 0.5}, {"invert", 0, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float l=luma(c.rgb); float a=smoothstep(p0-p2,p0+0.0001,l)*(1.0-smoothstep(p1,p1+p2+0.0001,l)); if(p3>0.5) a=1.0-a; return vec4(c.rgb,c.a*a); })G");
    add("Linear Color Key", "Альфа / Ключи", {{"key_r", 0, 0, 1}, {"key_g", 1, 0, 1}, {"key_b", 0, 0, 1}, {"tolerance", 0.25, 0, 1}, {"softness", 0.1, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float d=distance(c.rgb,vec3(p0,p1,p2)); return vec4(c.rgb,c.a*smoothstep(p3,p3+p4+0.001,d)); })G");
    add("Advanced Spill Suppressor", "Альфа / Ключи", {{"key_r", 0, 0, 1}, {"key_g", 1, 0, 1}, {"key_b", 0, 0, 1}, {"amount", 0.8, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float m=max(max(p0,p1),p2); vec3 v=c.rgb; if(p1>=m){ v.g=mix(v.g,min(v.g,(v.r+v.b)*0.5),p3); } else if(p2>=m){ v.b=mix(v.b,min(v.b,(v.r+v.g)*0.5),p3); } else { v.r=mix(v.r,min(v.r,(v.g+v.b)*0.5),p3); } return vec4(v,c.a); })G");
    add("Simple Choker", "Матирование", {{"choke", 2, -8, 8}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float a=c.a; float r=abs(p0); for(int i=0;i<8;i++){ float ang=float(i)*0.785398; float s=texture(uTex,uv+vec2(cos(ang),sin(ang))*r/uRes).a; if(p0>0.0) a=min(a,s); else a=max(a,s); } return vec4(c.rgb,a); })G");
    // =============== шум, перспектива, стилизация, время ===============
    add("Noise Alpha", "Шум и зерно", {{"amount", 0.5, 0, 1}}, R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float n=hash2(uv*uRes+uTime); return vec4(c.rgb,clamp(c.a*(1.0-p0*n),0.0,1.0)); })G");
    add("Noise HLS", "Шум и зерно", {{"hue", 0.05, 0, 0.5}, {"lightness", 0.1, 0, 0.5}, {"saturation", 0.1, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec3 h=rgb2hsv(c.rgb); float n1=hash2(uv*uRes+uTime); float n2=hash2(uv*uRes+uTime+5.1); float n3=hash2(uv*uRes+uTime+9.7); h.x=fract(h.x+(n1-0.5)*p0); h.z=clamp(h.z+(n3-0.5)*p1,0.0,1.0); h.y=clamp(h.y+(n2-0.5)*p2,0.0,1.0); return vec4(hsv2rgb(h),c.a); })G");
    add("Drop Shadow", "Перспектива", {{"r", 0, 0, 1}, {"g", 0, 0, 1}, {"b", 0, 0, 1}, {"opacity", 0.6, 0, 1}, {"direction", 135, 0, 360}, {"distance", 12, 0, 80}, {"softness", 8, 0, 40}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 off=vec2(cos(radians(p4)),-sin(radians(p4)))*p5/uRes; float a=0.0; for(int i=0;i<16;i++){ float ang=float(i)*2.39996; float r=sqrt((float(i)+0.5)/16.0)*p6; a+=texture(uTex,uv-off+vec2(cos(ang),sin(ang))*r/uRes).a; } a/=16.0; float sa=a*p3; float oa=c.a+sa*(1.0-c.a); vec3 rgb=(c.rgb*c.a+vec3(p0,p1,p2)*sa*(1.0-c.a))/max(oa,0.001); return vec4(rgb,oa); })G");
    add("Bevel Alpha", "Перспектива", {{"thickness", 3, 1, 12}, {"light_angle", 120, 0, 360}, {"intensity", 0.6, 0, 2}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 t=p0/uRes; float ax=texture(uTex,uv+vec2(t.x,0.0)).a-texture(uTex,uv-vec2(t.x,0.0)).a; float ay=texture(uTex,uv+vec2(0.0,t.y)).a-texture(uTex,uv-vec2(0.0,t.y)).a; float b=dot(vec2(ax,ay),vec2(cos(radians(p1)),-sin(radians(p1))))*p2; return vec4(clamp(c.rgb+b,0.0,1.0),c.a); })G");
    add("3D Glasses", "Перспектива", {{"offset", 6, 0, 40}}, R"G(vec4 fx(vec2 uv){ vec4 l=texture(uTex,uv-vec2(p0/uRes.x,0.0)); vec4 r=texture(uTex,uv+vec2(p0/uRes.x,0.0)); return vec4(l.r,r.g,r.b,max(l.a,r.a)); })G");
    add("Color Emboss", "Стилизация", {{"strength", 2, 0, 6}}, R"G(vec4 fx(vec2 uv){ vec2 t=1.0/uRes; vec4 c=texture(uTex,uv); vec3 d=texture(uTex,uv+t).rgb-texture(uTex,uv-t).rgb; return vec4(clamp(c.rgb*0.5+d*p0*0.5+0.25,0.0,1.0),c.a); })G");
    add("Find Edges", "Стилизация", {{"invert", 1, 0, 1}, {"strength", 1.5, 0, 5}},
        R"G(vec4 fx(vec2 uv){ vec2 t=1.0/uRes; float tl=luma(texture(uTex,uv+vec2(-t.x,-t.y)).rgb); float tc=luma(texture(uTex,uv+vec2(0.0,-t.y)).rgb); float tr=luma(texture(uTex,uv+vec2(t.x,-t.y)).rgb);
 float ml=luma(texture(uTex,uv+vec2(-t.x,0.0)).rgb); float mr=luma(texture(uTex,uv+vec2(t.x,0.0)).rgb); float bl=luma(texture(uTex,uv+vec2(-t.x,t.y)).rgb); float bc=luma(texture(uTex,uv+vec2(0.0,t.y)).rgb); float br=luma(texture(uTex,uv+vec2(t.x,t.y)).rgb);
 float gx=-tl-2.0*ml-bl+tr+2.0*mr+br; float gy=-tl-2.0*tc-tr+bl+2.0*bc+br; float e=clamp(length(vec2(gx,gy))*p1,0.0,1.0); float v=(p0>0.5) ? 1.0-e : e; return vec4(vec3(v),texture(uTex,uv).a); })G");
    add("Mosaic", "Стилизация", {{"horizontal", 40, 2, 300}, {"vertical", 24, 2, 300}}, R"G(vec4 fx(vec2 uv){ vec2 g=vec2(p0,p1); return texture(uTex,(floor(uv*g)+0.5)/g); })G");
    add("Motion Tile", "Стилизация", {{"tiles_x", 2, 1, 8}, {"tiles_y", 2, 1, 8}, {"mirror", 0, 0, 1}, {"phase", 0, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec2 u=uv*vec2(p0,p1)+vec2(p3,0.0); if(p2>0.5){ u=abs(mod(u,2.0)-1.0); } else { u=fract(u); } return texture(uTex,u); })G");
    add("Roughen Edges", "Стилизация", {{"border", 6, 0, 40}, {"scale", 20, 2, 80}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 d=(vec2(fbm(uv*p1),fbm(uv*p1+9.1))-0.5)*2.0*p0/uRes; return vec4(c.rgb,texture(uTex,uv+d).a); })G");
    add("Scatter", "Стилизация", {{"amount", 20, 0, 100}}, R"G(vec4 fx(vec2 uv){ vec2 n=vec2(hash2(uv*uRes+uTime),hash2(uv*uRes+uTime+3.3))-0.5; return texture(uTex,uv+n*p0/uRes); })G");
    add("Strobe Light", "Стилизация", {{"r", 1, 0, 1}, {"g", 1, 0, 1}, {"b", 1, 0, 1}, {"duration", 0.05, 0.01, 1}, {"period", 1, 0.1, 5}, {"blend", 0.8, 0, 1}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float ph=mod(uTime,max(p4,0.01)); float f=step(ph,p3); return vec4(mix(c.rgb,vec3(p0,p1,p2),f*p5),c.a); })G");
    add("Posterize Time", "Время", {{"fps", 12, 1, 60}}, R"G(vec4 fx(vec2 uv){ return texture(uTex,uv); })G");
    // =============== переходы (анимируй «completion» ключами) ===============
    add("Linear Wipe", "Переходы", {{"completion", 0.5, 0, 1}, {"angle", 90, 0, 360}, {"feather", 0.05, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float s=dot(uv-vec2(0.5),vec2(cos(radians(p1)),sin(radians(p1))))+0.5; float k=p0*(1.0+p2)-p2*0.5; return vec4(c.rgb,c.a*smoothstep(k-p2,k+p2+0.0001,s)); })G");
    add("Radial Wipe", "Переходы", {{"completion", 0.5, 0, 1}, {"start_angle", 0, 0, 360}, {"feather", 0.05, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 d=uv-vec2(0.5); float a=fract(atan(d.y,d.x)/6.2831853+0.5-p1/360.0); float k=p0*(1.0+p2)-p2*0.5; return vec4(c.rgb,c.a*smoothstep(k-p2,k+p2+0.0001,a)); })G");
    add("Iris Wipe", "Переходы", {{"completion", 0.5, 0, 1}, {"feather", 0.05, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float r=length((uv-vec2(0.5))*vec2(uRes.x/uRes.y,1.0))/0.9; float rad=(1.0-p0)*(1.1+p1)-p1*0.5; return vec4(c.rgb,c.a*(1.0-smoothstep(rad-p1,rad+p1+0.0001,r))); })G");
    add("Venetian Blinds", "Переходы", {{"completion", 0.5, 0, 1}, {"angle", 0, 0, 360}, {"width", 40, 4, 300}, {"feather", 0.05, 0, 0.5}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); float s=dot(uv*uRes,vec2(cos(radians(p1)),sin(radians(p1)))); float f=fract(s/p2); float k=1.0-p0; return vec4(c.rgb,c.a*(1.0-smoothstep(k-p3,k+p3+0.0001,f))); })G");
    add("Block Dissolve", "Переходы", {{"completion", 0.5, 0, 1}, {"block_width", 32, 2, 300}, {"block_height", 32, 2, 300}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 g=floor(uv*uRes/vec2(p1,p2)); return vec4(c.rgb,c.a*step(p0,hash2(g))); })G");
    add("CC Grid Wipe", "Переходы", {{"completion", 0.5, 0, 1}, {"size", 40, 4, 300}},
        R"G(vec4 fx(vec2 uv){ vec4 c=texture(uTex,uv); vec2 g=fract(uv*uRes/p1)-0.5; float d=max(abs(g.x),abs(g.y)); float rad=(1.0-p0)*0.75; return vec4(c.rgb,c.a*(1.0-smoothstep(rad,rad+0.02,d))); })G");
    return L;
}

const QList<EffectDef>& effectDefs() { static QList<EffectDef> L = build(); return L; }
const EffectDef* findEffect(const QString& name) {
    for (const EffectDef& d : effectDefs()) if (d.name == name) return &d;
    return nullptr;
}

// ---------- пресеты ----------
static QString presetFile() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/presets.json";
}
static QJsonObject readPresets() {
    QFile f(presetFile());
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}
QStringList presetNames() { return readPresets().keys(); }
std::vector<EffectInst> loadPreset(const QString& name) {
    std::vector<EffectInst> r;
    for (const QJsonValue& v : readPresets()[name].toArray()) r.push_back(effectFromJson(v.toObject()));
    return r;
}
void savePreset(const QString& name, const std::vector<EffectInst>& fx) {
    QJsonObject all = readPresets();
    QJsonArray a; for (const EffectInst& e : fx) a.append(effectToJson(e));
    all[name] = a;
    QFile f(presetFile());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(all).toJson());
}

// ---------- анимации ----------
static double easeStep(double u) { u = qBound(0.0, u, 1.0); return u * u * (3 - 2 * u); }

QStringList animationInNames() {
    return {"Fade In", "Slide In Left", "Slide In Right", "Slide In Up", "Slide In Down", "Zoom In", "Pop", "Spin In"};
}
QStringList animationOutNames() {
    return {"Fade Out", "Slide Out Left", "Slide Out Right", "Slide Out Up", "Slide Out Down", "Zoom Out", "Spin Out"};
}
QStringList animationLoopNames() { return {"Ken Burns In", "Ken Burns Out", "Shake", "Pulse"}; }

void setAnimation(Clip& c, const QString& n) {
    if (animationInNames().contains(n)) c.animIn = n;
    else if (animationOutNames().contains(n)) c.animOut = n;
    else if (animationLoopNames().contains(n)) c.animLoop = n;
}

AnimMod animationMod(const Clip& c, double local) {
    AnimMod m;
    if (!c.animIn.isEmpty() && c.animInDur > 1e-6 && local < c.animInDur) {
        double u = local / c.animInDur, p = easeStep(u);
        const QString& n = c.animIn;
        if (n == "Fade In") m.opacity *= p;
        else if (n == "Slide In Left") m.dx -= (1 - p);
        else if (n == "Slide In Right") m.dx += (1 - p);
        else if (n == "Slide In Up") m.dy += (1 - p);
        else if (n == "Slide In Down") m.dy -= (1 - p);
        else if (n == "Zoom In") m.scale *= p;
        else if (n == "Pop") m.scale *= (u < 0.6) ? (u / 0.6) * 1.15 : 1.15 - 0.15 * ((u - 0.6) / 0.4);
        else if (n == "Spin In") { m.rot -= 360.0 * (1 - p); m.scale *= p; }
    }
    if (!c.animOut.isEmpty() && c.animOutDur > 1e-6) {
        double st = c.dur - c.animOutDur;
        if (local > st) {
            double q = easeStep((local - st) / c.animOutDur);
            const QString& n = c.animOut;
            if (n == "Fade Out") m.opacity *= (1 - q);
            else if (n == "Slide Out Left") m.dx -= q;
            else if (n == "Slide Out Right") m.dx += q;
            else if (n == "Slide Out Up") m.dy -= q;
            else if (n == "Slide Out Down") m.dy += q;
            else if (n == "Zoom Out") m.scale *= (1 - q);
            else if (n == "Spin Out") { m.rot += 360.0 * q; m.scale *= (1 - q); }
        }
    }
    if (!c.animLoop.isEmpty()) {
        const QString& n = c.animLoop;
        if (n == "Ken Burns In") m.scale *= 1.0 + 0.25 * (local / qMax(c.dur, 0.01));
        else if (n == "Ken Burns Out") m.scale *= 1.25 - 0.25 * (local / qMax(c.dur, 0.01));
        else if (n == "Shake") { m.dx += 0.010 * std::sin(local * 55.0); m.dy += 0.008 * std::sin(local * 47.0 + 1.0); }
        else if (n == "Pulse") m.scale *= 1.0 + 0.05 * std::sin(local * 6.2832 * 2.0);
    }
    return m;
}
