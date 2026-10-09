// WebGPU resources execute flat draw data. Deleting this entire object leaves
// the graph untouched; device-loss callbacks only dispose execution resources.
globalThis.sgGPU={create(selector){
  const canvas=document.querySelector(selector);
  if(!canvas)throw Error('WebGPU canvas absent');
  let device=null,context=null,closed=false,pending=0,generation=0;
  const errors=[],meshes=new Map(),textures=new Map(),targets=new Map(),custom=new Map();
  let sceneLayout,postLayout,shadowLayout,scenePipeline,builtinScene,shadowPipeline,postPipelines,sampler,pixelSampler,mipSampler,compareSampler,white,dummyShadow,packs=false;
  const sceneBuffers=[{arrayStride:32,attributes:[{shaderLocation:0,offset:0,format:'float32x3'},{shaderLocation:1,offset:12,format:'float32x3'},{shaderLocation:2,offset:24,format:'float32x2'}]},
    {arrayStride:128,stepMode:'instance',attributes:Array.from({length:8},(_,i)=>({shaderLocation:3+i,offset:16*i,format:'float32x4'}))}];
  const texture=(w,h,format,usage=GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.TEXTURE_BINDING)=>device.createTexture({size:[w,h],format,usage,...(format==='rgba8unorm'?{viewFormats:['rgba8unorm-srgb']}:{} )});
  const buffer=(data,usage)=>{const size=Math.max(4,Math.ceil(data.byteLength/4)*4);const b=device.createBuffer({size,usage:usage|GPUBufferUsage.COPY_DST});
    if(data.byteLength%4){const padded=new Uint8Array(size);padded.set(new Uint8Array(data.buffer,data.byteOffset,data.byteLength));device.queue.writeBuffer(b,0,padded);}else device.queue.writeBuffer(b,0,data);return b;};
  const createScene=(module,shadow=false)=>device.createRenderPipeline({layout:device.createPipelineLayout({bindGroupLayouts:[shadow?shadowLayout:sceneLayout]}),
    vertex:{module,entryPoint:shadow?'vs_shadow':'vs_main',buffers:sceneBuffers},
    ...(shadow?{fragment:{module,entryPoint:'fs_shadow',targets:[]}}:{fragment:{module,entryPoint:'fs_main',targets:[{format:'rgba16float'},{format:'rgba16float'}]}}),
    primitive:{topology:'triangle-list',cullMode:'none'},depthStencil:{format:'depth32float',depthWriteEnabled:true,depthCompare:'less'}});
  const createPost=(module,entry,format)=>device.createRenderPipeline({layout:device.createPipelineLayout({bindGroupLayouts:[postLayout]}),vertex:{module,entryPoint:'vs_main'},fragment:{module,entryPoint:entry,targets:[{format,...(entry==='composite'||entry==='fs_main'||entry==='bloom_up'?{blend:{color:entry==='bloom_up'?{srcFactor:'constant',dstFactor:'constant',operation:'add'}:{srcFactor:'constant',dstFactor:'one-minus-constant'},alpha:{srcFactor:'one',dstFactor:'zero'}}}:{})}]},primitive:{topology:'triangle-list'}});
  const destroyTarget=t=>{for(const value of Object.values(t))for(const v of Array.isArray(value)?value:[value])v?.destroy?.();};
  const dispose=()=>{
    for(const m of meshes.values()){m.buffer.destroy();m.index.destroy();}for(const t of textures.values())t.texture.destroy();
    for(const t of targets.values())destroyTarget(t);
    meshes.clear();textures.clear();targets.clear();custom.clear();white?.destroy();dummyShadow?.destroy();
    device=null;context=null;
  };
  const init=async()=>{
    const mine=++generation;pending++;
    try{
      if(!navigator.gpu)throw Error('WebGPU is unavailable');
      const adapter=await navigator.gpu.requestAdapter();if(!adapter)throw Error('WebGPU adapter unavailable');
      const bc=adapter.features.has('texture-compression-bc');
      const next=await adapter.requestDevice(bc?{requiredFeatures:['texture-compression-bc']}:{});if(closed||mine!==generation){next.destroy();return;}packs=bc;
      device=next;device.lost.then(info=>{if(!closed&&mine===generation){errors.push('WebGPU device lost: '+info.message);dispose();init();}});
      device.onuncapturederror=e=>errors.push(e.error.message);
      context=canvas.getContext('webgpu');context.configure({device,format:navigator.gpu.getPreferredCanvasFormat(),alphaMode:'opaque'});
      const lookUniform={binding:6,visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT,buffer:{type:'uniform'}};
      const uniform={binding:0,visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT,buffer:{type:'uniform'}};
      sceneLayout=device.createBindGroupLayout({entries:[uniform,lookUniform,{binding:1,visibility:GPUShaderStage.FRAGMENT,sampler:{type:'filtering'}},{binding:2,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'float'}},{binding:3,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'depth',viewDimension:'2d-array'}},{binding:4,visibility:GPUShaderStage.FRAGMENT,sampler:{type:'comparison'}}]});
      shadowLayout=device.createBindGroupLayout({entries:[uniform,lookUniform]});
      postLayout=device.createBindGroupLayout({entries:[uniform,lookUniform,{binding:1,visibility:GPUShaderStage.FRAGMENT,sampler:{type:'filtering'}},...Array.from({length:3},(_,i)=>({binding:2+i,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'float'}})),{binding:5,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'depth'}}]});
      sampler=device.createSampler({minFilter:'linear',magFilter:'linear'});mipSampler=device.createSampler({minFilter:'linear',magFilter:'linear',mipmapFilter:'linear',maxAnisotropy:8});pixelSampler=device.createSampler({minFilter:'nearest',magFilter:'nearest',addressModeU:'repeat',addressModeV:'repeat'});compareSampler=device.createSampler({compare:'less-equal',minFilter:'linear',magFilter:'linear'});
      white=texture(1,1,'rgba8unorm',GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST);device.queue.writeTexture({texture:white},new Uint8Array([255,255,255,255]),{bytesPerRow:4},[1,1]);
      dummyShadow=device.createTexture({size:[1,1,8],format:'depth32float',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.TEXTURE_BINDING});
      const scene=device.createShaderModule({code:sgWGSL.scene}),post=device.createShaderModule({code:sgWGSL.post});
      const reports=await Promise.all([scene.getCompilationInfo(),post.getCompilationInfo()]);
      const failed=reports.flatMap(r=>r.messages.filter(m=>m.type==='error').map(m=>m.message));if(failed.length)throw Error(failed.join('\n'));
      scenePipeline=createScene(scene);builtinScene=scenePipeline;shadowPipeline=createScene(scene,true);
      postPipelines={bright:createPost(post,'bright','rgba16float'),blur:createPost(post,'blur','rgba16float'),composite:createPost(post,'composite',navigator.gpu.getPreferredCanvasFormat()),feed:createPost(post,'composite','rgba8unorm'),ao:createPost(post,'ao','rgba16float'),ao_blur:createPost(post,'ao_blur','rgba16float'),ao_apply:createPost(post,'ao_apply','rgba16float'),bloom_down:createPost(post,'bloom_down','rgba16float'),bloom_up:createPost(post,'bloom_up','rgba16float'),bloom_mix:createPost(post,'bloom_mix','rgba16float')};
    }catch(e){errors.push(String(e));dispose();}finally{pending--;}
  };
  const target=(id,w,h)=>{
    const old=targets.get(id);if(old&&old.w===w&&old.h===h)return old;
    if(old)destroyTarget(old);
    const t={w,h,hdr:texture(w,h,'rgba16float'),normal:texture(w,h,'rgba16float'),depth:texture(w,h,'depth32float'),shadow:device.createTexture({size:[1024,1024,8],format:'depth32float',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.TEXTURE_BINDING}),ao:texture(w,h,'rgba16float'),aoBlur:texture(w,h,'rgba16float'),lit:texture(w,h,'rgba16float'),bright:texture(w,h,'rgba16float'),blur:texture(w,h,'rgba16float'),out:texture(w,h,'rgba8unorm'),previous:texture(w,h,'rgba8unorm')};t.chain=[];for(let cw=Math.floor(w/2),ch=Math.floor(h/2);t.chain.length<6&&cw>=8&&ch>=8;cw=Math.floor(cw/2),ch=Math.floor(ch/2))t.chain.push(texture(cw,ch,'rgba16float'));targets.set(id,t);return t;
  };
  const picture=t=>{
    let old=textures.get(t.key);
    if(t.bytes){const packed=!!t.levels;
      if(!old||old.w!==t.w||old.h!==t.h||old.packed!==packed){old?.texture.destroy();
        old={w:t.w,h:t.h,packed,revision:-1,texture:packed?device.createTexture({size:[t.w,t.h],mipLevelCount:t.levels.length,format:t.srgb?'bc7-rgba-unorm-srgb':'bc7-rgba-unorm',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST})
          :texture(t.w,t.h,t.srgb?'rgba8unorm-srgb':'rgba8unorm',GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST)};textures.set(t.key,old);}
      if(old.revision!==t.revision){
        if(packed)t.levels.forEach((l,i)=>{const bw=Math.ceil(l.w/4),bh=Math.ceil(l.h/4);device.queue.writeTexture({texture:old.texture,mipLevel:i},t.bytes.subarray(l.at,l.at+l.size),{bytesPerRow:bw*16,rowsPerImage:bh},[bw*4,bh*4]);});
        else device.queue.writeTexture({texture:old.texture},t.bytes,{bytesPerRow:t.w*4},[t.w,t.h]);
        old.revision=t.revision;}
    }
    if(!old){errors.push('picture not held: '+t.key);return null;}return old;
  };
  const mesh=m=>{let old=meshes.get(m.key);if(!old&&m.vertices){old={buffer:buffer(m.vertices,GPUBufferUsage.VERTEX),index:buffer(m.indices,GPUBufferUsage.INDEX),format:m.indices instanceof Uint16Array?'uint16':'uint32',count:m.indices.length};meshes.set(m.key,old);}
    if(!old)errors.push('mesh not held: '+m.key);return old;};
  const renderRoom=(enc,r,w,h,output,transient)=>{
    const t=target(r.key,w,h);let frame=new Float32Array(r.frame);let uniform=buffer(frame,GPUBufferUsage.UNIFORM);transient.push(uniform);
    const groups=[],buckets=new Map();
    for(const batch of r.batches){const key=batch.sampleHdr+'|'+batch.casts+'|'+batch.mesh.key+'|'+(batch.picture?.key||'')+'|'+batch.target+'|'+batch.feedback;
      let bucket=buckets.get(key);if(!bucket){bucket={batch,parts:[],size:0};buckets.set(key,bucket);}bucket.parts.push(batch.instances);bucket.size+=batch.instances.length;
    }
    for(const bucket of buckets.values()){const batch=bucket.batch,m=mesh(batch.mesh);if(!m)continue;const data=new Float32Array(bucket.size);let offset=0;for(const part of bucket.parts){data.set(part,offset);offset+=part.length;}const instances=buffer(data,GPUBufferUsage.VERTEX);transient.push(instances);
      let image=white,viewFormat=undefined,mip=false;
      if(batch.picture){const held=picture(batch.picture);if(held){image=held.texture;mip=held.packed;}}
      if(batch.target){const guest=targets.get(batch.target);if(guest){image=batch.sampleHdr?guest.hdr:batch.feedback?guest.previous:guest.out;if(!batch.sampleHdr)viewFormat='rgba8unorm-srgb';}}
      groups.push({m,instances,count:data.length/32,image,viewFormat,mip,casts:batch.casts,pixel:batch.picture?.pixel||false});
    }
    const lookBuffers={};for(const [pass,values]of Object.entries(r.uniforms)){lookBuffers[pass]=buffer(values,GPUBufferUsage.UNIFORM);transient.push(lookBuffers[pass]);}
    const customScene=custom.get(r.sceneShader),customShadow=custom.get(r.shadowShader);
    let pass;
    for(let layer=0;layer<frame[442];layer++){
      const data=new Float32Array(frame);data[443]=layer;const u=buffer(data,GPUBufferUsage.UNIFORM);transient.push(u);
      const shadowGroup=device.createBindGroup({layout:shadowLayout,entries:[{binding:0,resource:{buffer:u}},{binding:6,resource:{buffer:lookBuffers.shadow}}]});
      pass=enc.beginRenderPass({colorAttachments:[],depthStencilAttachment:{view:t.shadow.createView({dimension:'2d',baseArrayLayer:layer,arrayLayerCount:1}),depthLoadOp:'clear',depthClearValue:1,depthStoreOp:'store'}});
      pass.setPipeline(customShadow||shadowPipeline);pass.setBindGroup(0,shadowGroup);
      for(const g of groups){if(!g.casts)continue;pass.setVertexBuffer(0,g.m.buffer);pass.setVertexBuffer(1,g.instances);pass.setIndexBuffer(g.m.index,g.m.format);pass.drawIndexed(g.m.count,g.count);}pass.end();
    }
    pass=enc.beginRenderPass({colorAttachments:[{view:t.hdr.createView(),loadOp:'clear',clearValue:r.clear,storeOp:'store'},{view:t.normal.createView(),loadOp:'clear',clearValue:[0.5,1,0.5,1],storeOp:'store'}],depthStencilAttachment:{view:t.depth.createView(),depthLoadOp:'clear',depthClearValue:1,depthStoreOp:'store'}});
    pass.setPipeline(customScene||scenePipeline);
    for(const g of groups){const bg=device.createBindGroup({layout:sceneLayout,entries:[{binding:0,resource:{buffer:uniform}},{binding:6,resource:{buffer:lookBuffers.scene}},{binding:1,resource:g.pixel?pixelSampler:g.mip?mipSampler:sampler},{binding:2,resource:g.image.createView(g.viewFormat?{format:g.viewFormat}:{})},{binding:3,resource:t.shadow.createView({dimension:'2d-array'})},{binding:4,resource:compareSampler}]});pass.setBindGroup(0,bg);pass.setVertexBuffer(0,g.m.buffer);pass.setVertexBuffer(1,g.instances);pass.setIndexBuffer(g.m.index,g.m.format);pass.drawIndexed(g.m.count,g.count);}pass.end();
    const post=(entry,source,bloom,destination,direction=[0,0],customKey='',blend=1,load=false)=>{
      const data=new Float32Array(frame);data[52]=direction[0];data[53]=direction[1];const u=buffer(data,GPUBufferUsage.UNIFORM);transient.push(u);
      const bg=device.createBindGroup({layout:postLayout,entries:[{binding:0,resource:{buffer:u}},{binding:6,resource:{buffer:lookBuffers[entry==='bright'?'bright':entry==='blur'?'blur':'composite']}},{binding:1,resource:sampler},{binding:2,resource:source.createView()},{binding:3,resource:bloom.createView()},{binding:4,resource:t.normal.createView()},{binding:5,resource:t.depth.createView()}]});
      const p=enc.beginRenderPass({colorAttachments:[{view:destination,loadOp:load?'load':'clear',clearValue:[0,0,0,1],storeOp:'store'}]});p.setBlendConstant([blend,blend,blend,blend]);p.setPipeline(custom.get(customKey)||postPipelines[entry]);p.setBindGroup(0,bg);p.draw(3);p.end();
    };
    if(r.raw)return;
    let lit=t.hdr;
    if(frame[39]>0){post('ao',t.hdr,t.hdr,t.ao.createView());post('ao_blur',t.ao,t.hdr,t.aoBlur.createView());post('ao_apply',t.hdr,t.aoBlur,t.lit.createView());lit=t.lit;}
    post('bright',lit,lit,t.bright.createView(),[0,0],r.brightShader);
    for(let i=0;i<r.blurPasses;i++){post('blur',t.bright,lit,t.blur.createView(),[1,0],r.blurShader);post('blur',t.blur,lit,t.bright.createView(),[0,1],r.blurShader);}
    let bloomResult=t.bright;
    if(frame[441]>0&&t.chain.length){
      let source=t.bright;
      for(let i=0;i<t.chain.length;i++){post('bloom_down',source,lit,t.chain[i].createView(),[i===0?1:0,0]);source=t.chain[i];}
      for(let i=t.chain.length-1;i>0;i--)post('bloom_up',t.chain[i],lit,t.chain[i-1].createView(),[1,0],'',1,true);
      post('bloom_mix',t.bright,t.chain[0],t.blur.createView(),[Math.min(frame[441],1),1/t.chain.length]);bloomResult=t.blur;
    }
    let drawn=0;for(const mix of r.composites){drawn+=mix.weight;post(output?'composite':'feed',lit,bloomResult,output||t.out.createView(),[0,0],output?mix.shader:mix.feed,mix.weight/drawn,drawn>mix.weight);}
  };
  init();
  return {
    generation:()=>generation,
    packs:()=>!!device&&packs,
    initialized:()=>!!device&&!!builtinScene&&!pending,
    ready:()=>!!device&&!!scenePipeline&&!pending,
    diagnostics:()=>errors.slice(),
    async prepare(shaders,fallback){
      if(!device)return false;custom.clear();errors.length=0;scenePipeline=builtinScene;
      const mine=generation;pending++;
      try{for(const s of shaders){if(custom.has(s.key))continue;const module=device.createShaderModule({code:s.code});const report=await module.getCompilationInfo();const bad=report.messages.filter(m=>m.type==='error');if(bad.length){errors.push(s.name+': '+bad.map(m=>m.message).join('; '));if(!fallback)scenePipeline=null;continue;}
        device.pushErrorScope('validation');let pipeline;
        try{pipeline=s.pass==='scene'?createScene(module):s.pass==='shadow'?createScene(module,true):createPost(module,'fs_main',s.format);}catch(e){errors.push(s.name+': '+e);}
        const error=await device.popErrorScope();if(error){errors.push(s.name+': '+error.message);if(!fallback)scenePipeline=null;}else if(pipeline&&mine===generation)custom.set(s.key,pipeline);
      }}finally{pending--;}return true;
    },
    render(rooms,w,h){if(!device||!scenePipeline||pending)return false;canvas.width=w;canvas.height=h;const enc=device.createCommandEncoder(),transient=[];
      for(let i=0;i<rooms.length;i++)target(rooms[i].key,i?rooms[i].width:w,i?rooms[i].height:h);
      for(const room of rooms)for(const batch of room.batches){if(batch.mesh.vertices)mesh(batch.mesh);if(batch.picture?.bytes)picture(batch.picture);}
      for(const t of targets.values()){const previous=t.previous;t.previous=t.out;t.out=previous;}
      for(let i=1;i<rooms.length;i++)renderRoom(enc,rooms[i],rooms[i].width,rooms[i].height,null,transient);
      renderRoom(enc,rooms[0],w,h,context.getCurrentTexture().createView(),transient);
      device.queue.submit([enc.finish()]);
      const usedMeshes=new Set(),usedTextures=new Set(),usedTargets=new Set();
      for(const room of rooms){usedTargets.add(room.key);for(const batch of room.batches){usedMeshes.add(batch.mesh.key);if(batch.picture)usedTextures.add(batch.picture.key);if(batch.target)usedTargets.add(batch.target);}}
      const retired=[];
      for(const [key,value]of meshes)if(!usedMeshes.has(key)){meshes.delete(key);retired.push(()=>{value.buffer.destroy();value.index.destroy();});}
      for(const [key,value]of textures)if(!usedTextures.has(key)){textures.delete(key);retired.push(()=>value.texture.destroy());}
      for(const [key,value]of targets)if(!usedTargets.has(key)){targets.delete(key);retired.push(()=>destroyTarget(value));}
      device.queue.onSubmittedWorkDone().then(()=>{for(const b of transient)b.destroy();for(const release of retired)release();});return true;},
    recreate(){device?.destroy();dispose();errors.length=0;init();},
    destroy(){closed=true;generation++;device?.destroy();dispose();}
  };
}};
