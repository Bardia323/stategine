// WGSL executors for the same Look/pass inputs. No runtime GLSL translation.
globalThis.sgWGSL={};
sgWGSL.frame=`
diagnostic(off, derivative_uniformity);
struct Light { position:vec4f, colour:vec4f, direction:vec4f };
struct Door { at:vec4f, axis:vec4f, inward:vec4f, sky:vec4f, ground:vec4f };
struct Frame { vp:mat4x4f, eye:vec4f, clock:vec4f, look:array<vec4f,8>, clips:array<vec4f,8>, lights:array<Light,24>, shadow:mat4x4f, extra:array<vec4f,24>, controls:array<vec4f,24>, gates:array<vec4f,24>, axes:array<vec4f,24>, shadows:array<mat4x4f,8>, bias:array<vec4f,8>, caster:array<vec4f,8>, doors:array<Door,4> };
@group(0) @binding(0) var<uniform> f:Frame;
`;
sgWGSL.scene=sgWGSL.frame+`
@group(0) @binding(1) var linearSampler:sampler;
@group(0) @binding(2) var picture:texture_2d<f32>;
@group(0) @binding(3) var shadow:texture_depth_2d_array;
@group(0) @binding(4) var shadowSampler:sampler_comparison;
struct Vertex { @location(0) position:vec3f, @location(1) normal:vec3f, @location(2) uv:vec2f,
 @location(3) m0:vec4f,@location(4) m1:vec4f,@location(5) m2:vec4f,@location(6) m3:vec4f,@location(7) colour:vec4f,@location(8) material:vec4f,@location(9) texture:vec4f,@location(10) rect:vec4f };
struct Vary { @builtin(position) position:vec4f,@location(0) world:vec3f,@location(1) normal:vec3f,@location(2) uv:vec2f,@location(3) colour:vec4f,@location(4) material:vec4f,@location(5) local:vec3f,@location(6) object:vec3f,@location(7) objectNormal:vec3f,@location(8) texture:vec4f,@location(9) rect:vec4f };
@vertex fn vs_main(v:Vertex)->Vary {var o:Vary;let m=mat4x4f(v.m0,v.m1,v.m2,v.m3);let p=m*vec4f(v.position,1);o.position=f.vp*p;o.world=p.xyz;
 o.normal=normalize(mat3x3f(m[0].xyz,m[1].xyz,m[2].xyz)*v.normal);o.uv=v.uv;o.colour=v.colour;o.material=v.material;o.local=v.position;o.object=v.position*vec3f(length(m[0].xyz),length(m[1].xyz),length(m[2].xyz));o.objectNormal=v.normal;o.texture=v.texture;o.rect=v.rect;return o;}
struct ShadowVertex { @builtin(position) position:vec4f,@location(0) side:f32 };
@vertex fn vs_shadow(v:Vertex)->ShadowVertex {let layer=u32(f.extra[12].w);let world=mat4x4f(v.m0,v.m1,v.m2,v.m3)*vec4f(v.position,1);var out:ShadowVertex;out.position=f.shadows[layer]*world;out.side=dot(f.caster[layer],world);return out;}
@fragment fn fs_shadow(v:ShadowVertex){if(v.side<0){discard;}}

fn hash(p:vec2f)->f32{return fract(sin(dot(p,vec2f(41.3,289.1)))*43758.5453);}
fn noise(p:vec2f)->f32{let i=floor(p);var q=fract(p);q=q*q*(3-2*q);return mix(mix(hash(i),hash(i+vec2f(1,0)),q.x),mix(hash(i+vec2f(0,1)),hash(i+1),q.x),q.y);}
fn sky(d:vec3f)->vec3f{
 let t=clamp(d.y,-1,1);var c=mix(f.extra[3].xyz,f.extra[2].xyz,pow(max(t,0),.45));c=mix(c,f.extra[3].xyz*.8,clamp(-t*5,0,1));
 let sd=normalize(f.extra[6].xyz+vec3f(0,.0001,0));let sun=max(dot(d,sd),0);c+=f.extra[7].xyz*(pow(sun,1200)*40+pow(sun,24)*.35+pow(sun,4)*.1);
 if(f.extra[3].w>0&&d.y>0){let p=d.xz/(d.y+.06)*.9+f.clock.x*vec2f(.006,.002);let n=noise(p*1.1)*.5+noise(p*2.3+3.7)*.25+noise(p*4.9+9.1)*.15+noise(p*10.3)*.1;
 let cover=smoothstep(1-f.extra[3].w,1-f.extra[3].w+.28,n);let thick=smoothstep(1-f.extra[3].w+.1,1,n);let toward=max(dot(d,sd),0);
 var cloud=mix(f.extra[4].xyz,f.extra[5].xyz,thick*.8);cloud+=f.extra[7].xyz*(pow(toward,6)*.8+pow(toward,40)*1.2)*(1-thick*.6);c=mix(c,cloud,cover*smoothstep(0,.12,d.y));}
 if(f.extra[2].w>0&&d.y>0){let q=d*420;let h=fract(sin(dot(floor(q),vec3f(12.9898,78.233,37.719)))*43758.5453);if(h>.996){let v=length(fract(q)-.5);let twinkle=.7+.3*sin(f.clock.x*(2+h*40)+h*100);c+=vec3f(.8,.85,1)*(1-smoothstep(0,.35,v))*(.4+(h-.996)*1500)*twinkle*f.extra[2].w*smoothstep(0,.25,d.y);}}
 return c;
}
struct Material { colour:vec3f, rough:f32 };
fn material(v:Vary)->Material {
 let s=v.material.x;let a=v.colour.rgb;let p=select(select(v.world.xy,v.world.zy,abs(v.normal.x)>abs(v.normal.z)),v.world.xz,abs(v.normal.y)>.7);var rough=0.0;var c=a;
 if(s<.5){return Material(c,rough);}
 if(s<1.5){let q=v.world.xz*.5;let cell=fract(q);let grout=smoothstep(0,.035,min(cell.x,cell.y))*smoothstep(0,.035,min(1-cell.x,1-cell.y));rough=mix(-.25,.05,grout);c*=mix(.55,1,hash(floor(q))*.35+.65)*mix(.45,1,grout)*(.94+.12*noise(v.world.xz*8));}
 else if(s<2.5){c*=(.92+.16*noise(v.world.xz*6+v.world.y*3))*mix(.82,1.06,clamp(v.world.y/4,0,1));}
 else if(s<3.5){let edge=max(max(abs(v.local.x),abs(v.local.y)),abs(v.local.z));c*=(.88+.12*sin(v.local.y*42+hash(v.local.xz)*3))*mix(1,.55,smoothstep(.42,.5,edge));}
 else if(s<4.5){let along=select(v.object.y,v.object.z,abs(normalize(v.objectNormal).y)>.5);let grain=noise(vec2f(v.object.x*2+v.object.z*2,along*55));let rings=.5+.5*sin((v.object.x+v.object.z)*9+grain*6);rough=-.05*rings;c*=(.82+.14*rings+.1*grain)*mix(1,.8,smoothstep(.46,.5,max(max(abs(v.local.x),abs(v.local.y)),abs(v.local.z))));}
 else if(s<5.5){let brush=noise(vec2f(v.object.x*400,v.object.y*6+v.object.z*6));rough=-.2+.1*brush;c*=.9+.12*brush;}
 else if(s<6.5){c*=(.96+.06*noise(v.object.xz*180+v.object.y*90))*mix(1,.78,smoothstep(.45,.5,max(max(abs(v.local.x),abs(v.local.y)),abs(v.local.z))));}
 else if(s<7.5){let q=(v.object.xz+v.object.yy)*260;rough=.2;c*=(.85+.2*(.5+.25*(sin(q.x)+sin(q.y))))*mix(1,.85,smoothstep(.46,.5,max(max(abs(v.local.x),abs(v.local.y)),abs(v.local.z))));}
 else if(s<8.5){let q=v.world.xz;let arg=dot(q,vec2f(.8,.6))*21+noise(q*.35)*6;let fade=clamp(1.5-fwidth(arg)*.6,0,1);var sand=a*(.93+.09*(.5+.5*sin(arg)-.5)*fade+.07*(noise(q*37)-.5)*fade)*(.9+.2*noise(q*.04));let rock=vec3f(.5,.3,.2)*(.72+.32*(.5+.5*sin(v.world.y*2.6+noise(q*.15)*2.5)))*(.85+.2*noise(q*2+v.world.y));let w=q*vec2f(.35,1.2)+f.clock.x*vec2f(1.7,.3);let drift=smoothstep(.55,.9,noise(w)*.7+noise(w*3.1+7)*.3)*f.extra[1].w;sand=mix(sand,a*1.12+.02,drift*.35*fade);rough=.25;c=mix(sand,rock,smoothstep(.3,.5,1-normalize(v.normal).y));}
 else if(s<9.5){c=sky(normalize(v.world-f.eye.xyz));}
 else if(s<10.5){var q=p*vec2f(.6,6);q.x+=hash(vec2f(floor(q.y),3))*7;let cell=fract(q);let seam=smoothstep(0,.04,min(cell.y,1-cell.y))*smoothstep(0,.01,min(cell.x,1-cell.x));rough=-.1;c*=(.78+.3*hash(floor(q)))*(.85+.2*noise(q*vec2f(30,3)))*mix(.5,1,seam);}
 else if(s<11.5){rough=.15;let n=noise(p*1.3)*.6+noise(p*7)*.3+noise(p*40)*.1;let line=smoothstep(0,.02,abs(fract(p.y*.8)-.5)-.48);c*=(.8+.3*n)*(1-.08*line);}
 else if(s<12.5){let q=floor(p*2);c*=mix(.35,1,(q.x+q.y)-floor((q.x+q.y)/2)*2)*(.96+.06*noise(p*9));}
 else if(s<13.5){var q=p*vec2f(4,12);q.x+=(floor(q.y)-floor(q.y/2)*2)*.5;let cell=fract(q);let mortar=smoothstep(0,.06,min(cell.y,1-cell.y))*smoothstep(0,.03,min(cell.x,1-cell.x));rough=.1;c=mix(vec3f(.62,.6,.56),a*(.75+.35*hash(floor(q)))*(.9+.15*noise(p*20)),mortar);}
 else if(s<14.5){rough=.3;c*=.82+.3*(noise(p*90)*.5+noise(p*23)*.5);}
 else if(s<15.5){let cell=fract(p*.8);let seam=smoothstep(0,.015,min(min(cell.x,1-cell.x),min(cell.y,1-cell.y)));let d=fract(vec2f(p.x+p.y,p.x-p.y)*12);let tread=smoothstep(.35,.5,1-abs(d.x-.5)*2)*.12;rough=-.25;c*=(.85+tread+.08*noise(p*vec2f(300,4)))*mix(.45,1,seam);}
 else if(s>16.5&&s<17.5){let q=p*.9;let slab=floor(p*.8);let cell=fract(p*.8);let seam=smoothstep(0,.006,min(min(cell.x,1-cell.x),min(cell.y,1-cell.y)));let warp=noise(q*1.7+slab*3.1)*3+noise(q*4.3)*1.2;let vein=1-smoothstep(0,.12,abs(sin((q.x+q.y*.6)*2.2+warp)));let fine=1-smoothstep(0,.06,abs(sin((q.x*.4-q.y)*5+warp*1.7)));rough=-.1;c=mix(mix(a*(.96+.05*hash(slab)),a*vec3f(.78,.58,.7),vein*.55+fine*.25),a*.8,1-seam);}
 else if(s>17.5&&s<18.5){rough=-.2;c*=.9+.1*noise(p*.3+f.clock.x*.05);}
 else{rough=.3;c*=mix(vec3f(.75,.8,.5),vec3f(1.1,1.15,.9),noise(p*3)*.5+noise(p*17)*.3+noise(p*80)*.2);}
 return Material(c,rough);
}
fn through_gate(i:u32,p:vec3f,way:vec3f,parallel:bool)->f32{
 let g=f.axes[i];if(g.w<.5){return 1;}let at=f.gates[i];let a=vec3f(g.x,0,g.y);let n=vec3f(-a.z,0,a.x);let dn=dot(way,n);if(abs(dn)<.00001){return 0;}let t=dot(at.xyz-p,n)/dn;if(t<0||(!parallel&&t>1)){return 0;}let q=p+way*t;let soft=.02+.04*t*length(way);return (1-smoothstep(at.w-soft,at.w+soft,abs(dot(q-at.xyz,a))))*(1-smoothstep(g.z-soft,g.z+soft,abs(q.y-at.y)));
}
fn shadow_factor(i:u32,v:Vary,n:vec3f,l:vec3f,fl:f32)->f32{
 let p=f.shadows[i]*vec4f(v.world,1);let q=p.xyz/max(p.w,.00001);let uv=vec2f(q.x*.5+.5,.5-q.y*.5);if(q.z>1||any(uv<vec2f(0))||any(uv>vec2f(1))){return 1;}
 let spread=max(f.extra[0].w,.5);let bias=max(.0016*(1-dot(n,l)),.0006)*f.bias[i].x*(.6+.4*spread);let turn=fract(52.9829189*fract(dot(v.position.xy,vec2f(.06711056,.00583715))))*6.2831853;var sum=0.0;
 for(var k=0;k<16;k++){let a=f32(k)*2.3999632+turn;let off=vec2f(cos(a),sin(a))*sqrt((f32(k)+.5)/16)*2.4*spread/vec2f(textureDimensions(shadow));sum+=textureSampleCompareLevel(shadow,shadowSampler,uv+off,i32(i),q.z-bias);}
 let edge=smoothstep(.82,.98,max(abs(q.x),abs(q.y)));return mix(mix(fl,1,sum/16),1,edge);
}
fn crt(v:Vary,uv:vec2f)->vec3f{
 let flat=v.texture.y;let corner=mix(.09,.012,flat);let g=(uv*2-1)/mix(vec2f(.975,.965),vec2f(1),flat);let d=abs(g)-vec2f(1-corner);let sd=length(max(d,vec2f(0)))+min(max(d.x,d.y),0)-corner;let px=max(fwidth(sd),.00001);let glass=1-smoothstep(-px,px,sd);
 let r2=dot(g,g);let w=g*(1+.045*(1-flat)*r2)/mix(.975,1,flat);let ps=max(abs(w.x),abs(w.y))-1;let pp=max(fwidth(ps),.00001);let visible=1-smoothstep(-pp,pp,ps);let uvp=clamp(w*.5+.5,vec2f(0),vec2f(1));var c=textureSampleLevel(picture,linearSampler,uvp,0).rgb;let size=vec2f(textureDimensions(picture));
 if(v.texture.w>0){var near=vec3f(0);var far=vec3f(0);for(var k=0;k<8;k++){let a=f32(k)*.7853982+.39;let off=vec2f(cos(a),sin(a))/size;near+=textureSampleLevel(picture,linearSampler,uvp+off*2,0).rgb;far+=textureSampleLevel(picture,linearSampler,uvp+off*6,0).rgb;}c+=v.texture.w*(near*.045+far*.03);}
 let scan=.22*clamp(1.6-fwidth(uvp.y*size.y),0,1);let mask=.18*clamp(1.6-fwidth(uvp.x*size.x),0,1);let line=1-scan+scan*cos(uvp.y*size.y*3.14159265);let stripe=u32(floor(uvp.x*size.x))%3u;let grille=vec3f(select(1-mask,1,stripe==0u),select(1-mask,1,stripe==1u),select(1-mask,1,stripe==2u));
 var screen=mix(vec3f(.012,.013,.013)*(1-flat),c*line*grille*(1-mix(.16,.05,flat)*r2)+.012*(1-flat),visible);screen+=vec3f(.018)*(1-flat)*smoothstep(.1,.9,-g.y)*(1-.6*r2);let lip=1-clamp(sd*14,0,1);let bezel=(vec3f(.022,.021,.02)*(1+lip)+.01*(1-uv.y))*(1-flat);return mix(bezel,screen,glass);
}
struct Fragment { @location(0) hdr:vec4f,@location(1) normal:vec4f };
@fragment fn fs_main(v:Vary)->Fragment {
 let skyPixel=v.material.x>8.5&&v.material.x<9.5;
 if(!skyPixel){for(var i=0u;i<u32(f.clock.y);i++){if(dot(f.clips[i],vec4f(v.world,1))<0){discard;}}}
 let m=material(v);var base=m.colour;var n=normalize(v.normal);var indirect=0.0;
 if(v.texture.x>.5){var uv=v.uv;if(v.texture.x>2.5&&v.texture.x<3.5){uv=v.position.xy/vec2f(textureDimensions(picture));}
 if(v.texture.x>3.5&&v.texture.x<4.5){let a=abs(v.objectNormal);let p=v.local;var cell=0.0;var u=0.0;var y=0.0;
 if(a.x>=a.y&&a.x>=a.z){cell=select(1.0,0.0,v.objectNormal.x>0);u=select(p.z+.5,.5-p.z,v.objectNormal.x>0);y=p.y+.5;}
 else if(a.z>=a.y){cell=select(3.0,2.0,v.objectNormal.z>0);u=select(.5-p.x,p.x+.5,v.objectNormal.z>0);y=p.y+.5;}
 else{cell=select(5.0,4.0,v.objectNormal.y>0);u=p.x+.5;y=select(p.z+.5,.5-p.z,v.objectNormal.y>0);}
 uv=vec2f((cell-floor(cell/3)*3+clamp(u,.002,.998))/3,(floor(cell/3)+1-clamp(y,.002,.998))/2);}
 if(v.texture.x>4.5&&v.texture.x<5.5){let a=abs(cross(dpdx(v.world),dpdy(v.world)));let p=v.world/max(v.rect.x,.001);uv=select(select(vec2f(p.x,-p.y),vec2f(p.z,-p.y),a.x>=a.z),p.xz,a.y>=a.x&&a.y>=a.z);}
 if(v.rect.z>0){uv=v.rect.xy+uv*v.rect.zw;}
 var texel=textureSampleLevel(picture,linearSampler,uv,0);if(texel.a<.5){discard;}var tex=texel.rgb;if(v.texture.x>1.5&&v.texture.x<2.5){tex=crt(v,uv);}if(v.texture.z>.5){let y=min(tex,vec3f(.985));let a=2.43*y-2.51;let b=.59*y-.03;let c=.14*y;tex=max((-b-sqrt(max(b*b-4*a*c,vec3f(0))))/(2*a),vec3f(0));}base=tex;}
 var colour=base;
 if(!skyPixel&&f.clock.w<.5&&!(v.texture.x>2.5&&v.texture.x<3.5)){
   let roughness=clamp(v.colour.w+m.rough,.05,1);let metal=select(0.0,.35,v.material.x>4.5&&v.material.x<5.5);let f0=mix(vec3f(.04),base,metal);let diffuse=base*(1-metal);let view=normalize(f.eye.xyz-v.world);
   if(v.material.x>17.5&&v.material.x<18.5&&n.y>.5){let dirs=array<vec2f,6>(vec2f(.8,.6),vec2f(-.45,.89),vec2f(.96,-.28),vec2f(.2,.98),vec2f(-.87,.5),vec2f(.6,-.8));var slope=vec2f(0);let far=length(v.world-f.eye.xyz);for(var i=0;i<6;i++){let k=.35*pow(1.9,f32(i));let a=.09/(1+f32(i)*.8);let phase=dot(dirs[i],v.world.xz)*k-f.clock.x*sqrt(9.8*k)+f32(i)*1.7;slope+=dirs[i]*cos(phase)*a*exp(-far*k*.004)*1.6;}n=normalize(n-vec3f(slope.x,0,slope.y));}
   let ndv=clamp(dot(n,view),.001,1);let dx=dpdx(n);let dy=dpdy(n);let a2=clamp(pow(roughness,4)+min(.5*(dot(dx,dx)+dot(dy,dy)),.25),0,1);var direct=vec3f(0);var bounced=vec3f(0);
   for(var i=0u;i<u32(f.clock.z);i++){let light=f.lights[i];let ctl=f.controls[i];let delta=light.position.xyz-v.world;let distance=max(length(delta),.0001);let l=select(delta/distance,normalize(-light.direction.xyz),light.position.w>.5);var cone=1.0;var atten=light.colour.w;
     if(light.position.w<.5){cone=clamp((dot(-l,normalize(light.direction.xyz))-light.direction.w)/max(ctl.x-light.direction.w,.0001),0,1);cone*=cone;atten*=mix(1/(1+.22*distance+.14*distance*distance),1/(1+2*distance*distance),ctl.z)*through_gate(i,v.world,delta,false);}else{atten*=through_gate(i,v.world,l,true);}
     let ndl=max(dot(n,l),0);var visibility=1.0;if(ndl>0&&i<u32(f.extra[12].z)){visibility=shadow_factor(i,v,n,l,select(ctl.y,f.look[1].w,ctl.y<0));}
     let h=normalize(l+view);let ndh=max(dot(n,h),0);let vdh=clamp(dot(view,h),0,1);let denom=ndh*ndh*(a2-1)+1;let distribution=a2/(denom*denom+.0000001);let vis=.5/max(mix(2*ndl*ndv,ndl+ndv,sqrt(a2)),.000001);let fresnel=f0+(1-f0)*pow(1-vdh,5);let lobe=diffuse*(1-fresnel)+distribution*vis*fresnel;
     if(ctl.w>.5){bounced+=diffuse*ndl*light.colour.xyz*atten*cone*visibility;}else{direct+=lobe*ndl*light.colour.xyz*atten*cone*visibility;}
   }
   let r=roughness*vec4f(-1,-.0275,-.572,.022)+vec4f(1,.0425,1.04,-.04);let a004=min(r.x*r.x,exp2(-9.28*ndv))*r.x+r.y;let ab=vec2f(-1.04,1.04)*a004+r.zw;let reflected=f0*ab.x+ab.y;let reflection=reflect(-view,n);let up=mix(reflection.y,n.y,roughness*roughness);var ambientSky=f.extra[0].xyz*f.look[0].w;var ambientGround=f.extra[1].xyz*f.look[0].w;
   for(var i=0u;i<u32(f.extra[13].x);i++){let door=f.doors[i];let d=v.world-door.at.xyz;let side=dot(d.xz,door.inward.xy);let u=abs(dot(d.xz,door.axis.xy))-door.at.w;let y=abs(d.y)-door.axis.z;let there=(1-smoothstep(-.12,-.02,u))*(1-smoothstep(-.12,-.02,y))*(1-smoothstep(-.35,.35,side));ambientSky=mix(ambientSky,door.sky.xyz,there);ambientGround=mix(ambientGround,door.ground.xyz,there);}let around=mix(ambientGround,ambientSky,n.y*.5+.5);var mirrored=mix(ambientGround,ambientSky,smoothstep(-.35,.35,up));if(v.material.w>0){mirrored=mix(mirrored,sky(normalize(vec3f(reflection.x,abs(reflection.y),reflection.z))),v.material.w*(1-roughness));}
   let ambient=diffuse*around*(1-reflected)+mirrored*reflected+bounced;colour=ambient+direct+base*v.material.y;indirect=clamp(dot(ambient,vec3f(.2126,.7152,.0722))/max(dot(colour,vec3f(.2126,.7152,.0722)),.00001),0,1);
   let fog=clamp(1-exp(-length(v.world-f.eye.xyz)*f.look[1].x),0,.85);let toward=pow(max(dot(normalize(v.world-f.eye.xyz),normalize(f.extra[6].xyz+vec3f(0,.0001,0))),0),6);colour=mix(colour,f.look[0].xyz+f.extra[7].xyz*toward*.25,fog);indirect*=1-fog;
 }
 var out:Fragment;out.hdr=vec4f(colour*(1-f.look[5].x*(1-select(0.0,v.material.z,v.texture.x>2.5&&v.texture.x<3.5))),indirect);out.normal=vec4f(n*.5+.5,1);return out;
}`;
sgWGSL.post=sgWGSL.frame+`
@group(0) @binding(1) var linearSampler:sampler;
@group(0) @binding(2) var scene:texture_2d<f32>;
@group(0) @binding(3) var bloom:texture_2d<f32>;
@group(0) @binding(4) var normal:texture_2d<f32>;
@group(0) @binding(5) var depth:texture_depth_2d;
struct Full { @builtin(position) position:vec4f,@location(0) uv:vec2f };
@vertex fn vs_main(@builtin(vertex_index) i:u32)->Full {let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));var o:Full;o.position=vec4f(p[i],0,1);o.uv=vec2f(p[i].x*0.5+0.5,0.5-p[i].y*0.5);return o;}

fn read(uv:vec2f)->vec3f{return textureSampleLevel(scene,linearSampler,uv,0).rgb;}
fn aces(x:vec3f)->vec3f{return clamp(x*(2.51*x+.03)/(x*(2.43*x+.59)+.14),vec3f(0),vec3f(1));}
fn tonemap(hdr:vec3f)->vec3f{let x=max(hdr,vec3f(0));let l=max(dot(x,vec3f(.2126,.7152,.0722)),.000001);let lt=aces(vec3f(l)).x;var c=x*(lt/l);let m=max(c.x,max(c.y,c.z));if(m>.75){let knee=.75+.25*(1-exp(-(m-.75)/.25));c=vec3f(lt)+clamp((knee-lt)/max(m-lt,.000001),0,1)*(c-lt);}return clamp(mix(aces(x),c,.5),vec3f(0),vec3f(1));}
fn film_hash(p:vec2f)->f32{var q=fract(p.xyx*.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);}
fn film_noise(p:vec2f)->f32{let i=floor(p);var q=fract(p);q=q*q*(3-2*q);return mix(mix(film_hash(i),film_hash(i+vec2f(1,0)),q.x),mix(film_hash(i+vec2f(0,1)),film_hash(i+1),q.x),q.y);}
fn film(c:vec3f,px:vec2f)->vec3f{
 var e=pow(max(c,vec3f(0)),vec3f(1.0/2.2));let jump=vec2f(film_hash(vec2f(floor(f.clock.x*24),7)),film_hash(vec2f(floor(f.clock.x*24),13)))*911;let y=dot(e,vec3f(.2126,.7152,.0722));let grain=film_noise((px+jump)/1.5)*.6+film_hash(px+jump)*.4-.5;let amount=f.look[4].y*2.4*mix(.45,1,smoothstep(0,.3,y))*(1-.7*smoothstep(.55,1,y));let chroma=vec3f(film_hash(px+jump+3.1),film_hash(px+jump+5.7),film_hash(px+jump+9.3))-.5;e+=(vec3f(grain)+chroma*.15)*amount;e+=(film_hash(px+jump*1.37)-film_hash(px+17+jump))/255;return e;
}
fn rays(uv:vec2f)->vec3f{
 if(f.extra[9].w<=0){return vec3f(0);}let forward=normalize(f.extra[8].xyz);let right=normalize(cross(forward,select(vec3f(0,1,0),vec3f(0,0,1),abs(forward.y)>.999)));let up=cross(right,forward);let direction=normalize(f.extra[9].xyz);let z=dot(direction,forward);if(z<=.02){return vec3f(0);}let aspect=f.extra[11].z;let src=vec2f(dot(direction,right)/(z*f.extra[8].w*aspect),-dot(direction,up)/(z*f.extra[8].w))*.5+.5;let seen=smoothstep(.05,.35,z)*(1-smoothstep(.7,1.6,length(src-.5)));if(seen<=0){return vec3f(0);}
 let step=(src-uv)/56*.92;var p=uv+step*film_hash(uv*911);var weight=1.0;var sum=vec3f(0);for(var i=0;i<56;i++){p+=step;let s=read(clamp(p,vec2f(0),vec2f(1)));let off=(p-src)*vec2f(aspect,1);let near=exp(-dot(off,off)/max(f.extra[10].w*f.extra[10].w,.000001));let l=dot(s,vec3f(.299,.587,.114));sum+=s*(max(l-f.extra[11].w,0)/max(l,.0001))*near*weight;weight*=.965;}return sum/56*f.extra[9].w*f.extra[10].xyz*seen;
}
fn smooth_edges(uv:vec2f,c:vec3f)->vec3f{let texel=1/vec2f(textureDimensions(scene));let n=read(uv+vec2f(0,texel.y));let s=read(uv-vec2f(0,texel.y));let e=read(uv+vec2f(texel.x,0));let w=read(uv-vec2f(texel.x,0));let edge=abs(dot(n+s+e+w,vec3f(.299,.587,.114))*f.look[2].z-4*dot(c,vec3f(.299,.587,.114)));if(edge<=.12){return c;}return mix(c,tonemap((n+s+e+w)*.25*f.look[2].z),clamp(edge*2,0,.6));}
fn linear(d:f32)->f32{return f.extra[11].x*f.extra[11].y/(f.extra[11].y-d*(f.extra[11].y-f.extra[11].x));}
fn depth_at(uv:vec2f)->f32{let size=vec2i(textureDimensions(depth));return textureLoad(depth,clamp(vec2i(uv*vec2f(size)),vec2i(0),size-1),0);}
fn view_at(uv:vec2f)->vec3f{let z=linear(depth_at(uv));let ndc=uv*2-1;return vec3f(ndc.x*f.extra[8].w*f.extra[11].z*z,-ndc.y*f.extra[8].w*z,-z);}
@fragment fn ao(v:Full)->@location(0) vec4f{
 if(depth_at(v.uv)>=1){return vec4f(1);}let p=view_at(v.uv);let texel=1/vec2f(textureDimensions(depth));let px1=view_at(v.uv+vec2f(texel.x,0))-p;let px0=p-view_at(v.uv-vec2f(texel.x,0));let py1=view_at(v.uv+vec2f(0,texel.y))-p;let py0=p-view_at(v.uv-vec2f(0,texel.y));let dx=select(px0,px1,abs(px1.z)<abs(px0.z));let dy=select(py0,py1,abs(py1.z)<abs(py0.z));let n=normalize(cross(dy,dx));let tangent=normalize(select(cross(n,vec3f(1,0,0)),cross(n,vec3f(0,1,0)),abs(n.y)<.9));let bitangent=cross(n,tangent);let cell=vec2f(vec2u(v.position.xy)%vec2u(4));let turn=(cell.x*4+cell.y)/16*6.2831853;var occ=0.0;
 for(var i=0;i<16;i++){let fi=f32(i);let a=fi*2.3999632+turn;let h=fract(fi*.618034+.13);let r=pow(mix(.12,1,(fi+.5)/16),2);let direction=normalize(cos(a)*sqrt(1-h*h)*tangent+sin(a)*sqrt(1-h*h)*bitangent+h*n);let s=p+direction*r*f.extra[12].x;let uv=vec2f(s.x/(-s.z*f.extra[8].w*f.extra[11].z),-s.y/(-s.z*f.extra[8].w))*.5+.5;if(any(uv<vec2f(0))||any(uv>vec2f(1))){continue;}let there=linear(depth_at(uv));let front=select(0.0,1.0,there<=-s.z-.02);occ+=front*smoothstep(0,1,f.extra[12].x/max(abs(-p.z-there),.0001));}return vec4f(vec3f(1-occ/16),1);
}
@fragment fn ao_blur(v:Full)->@location(0) vec4f{let texel=1/vec2f(textureDimensions(depth));let zc=linear(depth_at(v.uv));var sum=0.0;var weight=0.0;for(var y=-2;y<2;y++){for(var x=-2;x<2;x++){let uv=v.uv+vec2f(f32(x),f32(y))*texel;let z=linear(depth_at(uv));let w=select(0.0,1.0,abs(z-zc)<zc*.04);sum+=read(uv).x*w;weight+=w;}}return vec4f(vec3f(sum/max(weight,.0001)),1);}
@fragment fn ao_apply(v:Full)->@location(0) vec4f{let s=textureSampleLevel(scene,linearSampler,v.uv,0);var a=textureSampleLevel(bloom,linearSampler,v.uv,0).x;let zc=linear(depth_at(v.uv));let texel=1/vec2f(textureDimensions(depth));var edge=0.0;for(var i=0;i<4;i++){let d=array<vec2f,4>(vec2f(1,0),vec2f(-1,0),vec2f(0,1),vec2f(0,-1));edge=max(edge,abs(linear(depth_at(v.uv+d[i]*texel))-zc)/max(zc,.0001));}if(edge>.02){for(var y=-1;y<=1;y++){for(var x=-1;x<=1;x++){a=max(a,textureSampleLevel(bloom,linearSampler,v.uv+vec2f(f32(x),f32(y))*texel,0).x);}}}return vec4f(s.xyz*mix(1,pow(a,1.6),f.look[3].w*s.w),1);}
@fragment fn bright(v:Full)->@location(0) vec4f{let c=read(v.uv);let l=dot(c,vec3f(.2126,.7152,.0722));return vec4f(c*max(l-f.look[2].x,0)/max(l,.0001),1);}
@fragment fn blur(v:Full)->@location(0) vec4f{let d=f.look[7].xy/vec2f(textureDimensions(scene));var c=read(v.uv)*0.227027;c+=(read(v.uv+d*1.384615)+read(v.uv-d*1.384615))*0.316216;c+=(read(v.uv+d*3.230769)+read(v.uv-d*3.230769))*0.070270;return vec4f(c,1);}
fn bloom_weight(c:vec3f)->f32{return 1/(1+dot(c,vec3f(.2126,.7152,.0722)));}
@fragment fn bloom_down(v:Full)->@location(0) vec4f {
 let t=1/vec2f(textureDimensions(scene));let uv=v.uv;
 let a=read(uv+t*vec2f(-2,2));let b=read(uv+t*vec2f(0,2));let c=read(uv+t*vec2f(2,2));let d=read(uv+t*vec2f(-2,0));let e=read(uv);let g=read(uv+t*vec2f(-2,-2));let h=read(uv+t*vec2f(0,-2));let i=read(uv+t*vec2f(2,-2));let right=read(uv+t*vec2f(2,0));
 let g0=(read(uv+t*vec2f(-1,1))+read(uv+t*vec2f(1,1))+read(uv+t*vec2f(-1,-1))+read(uv+t*vec2f(1,-1)))*.25;
 let g1=(a+b+d+e)*.25;let g2=(b+c+e+right)*.25;let g3=(d+e+g+h)*.25;let g4=(e+right+h+i)*.25;
 if(f.look[7].x>.5){let w0=bloom_weight(g0)*.5;let w1=bloom_weight(g1)*.125;let w2=bloom_weight(g2)*.125;let w3=bloom_weight(g3)*.125;let w4=bloom_weight(g4)*.125;return vec4f((g0*w0+g1*w1+g2*w2+g3*w3+g4*w4)/(w0+w1+w2+w3+w4),1);}
 return vec4f(g0*.5+(g1+g2+g3+g4)*.125,1);
}
fn upsample(uv:vec2f)->vec3f{let t=1/vec2f(textureDimensions(scene));var s=read(uv)*4;s+=(read(uv+vec2f(t.x,0))+read(uv-vec2f(t.x,0))+read(uv+vec2f(0,t.y))+read(uv-vec2f(0,t.y)))*2;s+=read(uv+t)+read(uv-t)+read(uv+vec2f(t.x,-t.y))+read(uv+vec2f(-t.x,t.y));return s/16;}
@fragment fn bloom_up(v:Full)->@location(0) vec4f{return vec4f(upsample(v.uv),1);}
@fragment fn bloom_mix(v:Full)->@location(0) vec4f {
 let t=1/vec2f(textureDimensions(bloom));var s=textureSampleLevel(bloom,linearSampler,v.uv,0).rgb*4;
 for(var i=0;i<8;i++){let offsets=array<vec2f,8>(vec2f(1,0),vec2f(-1,0),vec2f(0,1),vec2f(0,-1),vec2f(1,1),vec2f(-1,-1),vec2f(1,-1),vec2f(-1,1));s+=textureSampleLevel(bloom,linearSampler,v.uv+offsets[i]*t,0).rgb*select(1.0,2.0,i<4);}
 return vec4f(mix(read(v.uv),s/16*f.look[7].y,f.look[7].x),1);
}
@fragment fn composite(v:Full)->@location(0) vec4f{
 var light=read(v.uv)+textureSampleLevel(bloom,linearSampler,v.uv,0).rgb*f.look[2].y+rays(v.uv);var c=smooth_edges(v.uv,tonemap(light*f.look[2].z));let grey=dot(c,vec3f(.299,.587,.114));c=mix(vec3f(grey),c,f.look[2].w)*f.look[3].xyz;let d=v.uv-.5;c*=1-dot(d,d)*f.look[4].x;return vec4f(film(c,vec2f(v.position.x,f32(textureDimensions(scene).y)-v.position.y)),1);
}`;
