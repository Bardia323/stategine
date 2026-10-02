globalThis.sgDOM={create(selector){
  const canvas=document.querySelector(selector),q=[],handlers=[];
  if(!canvas)throw Error('input canvas absent');
  let overflow=false;
  const push=(device,key,x=0,y=0,value=0)=>{
    if(q.length===512){overflow=true;return;}q.push({device,key,x,y,value});
  };
  const on=(target,name,fn)=>{target.addEventListener(name,fn);handlers.push([target,name,fn]);};
  on(canvas,'keydown',e=>{if(!e.repeat)push('keyboard',e.code,0,0,1);e.preventDefault();});
  on(canvas,'keyup',e=>{push('keyboard',e.code+'.up',0,0,0);e.preventDefault();});
  on(canvas,'pointerdown',e=>{canvas.focus();push('pointer','button'+e.button,e.offsetX,e.offsetY,1);});
  on(canvas,'pointerup',e=>push('pointer','button'+e.button+'.up',e.offsetX,e.offsetY,0));
  on(canvas,'pointermove',e=>push('pointer','move',e.movementX,e.movementY));
  on(canvas,'touchstart',e=>{for(const t of e.changedTouches)push('touch','down',t.clientX,t.clientY,1);});
  on(canvas,'touchend',e=>{for(const t of e.changedTouches)push('touch','up',t.clientX,t.clientY,0);});
  on(canvas,'touchmove',e=>{for(const t of e.changedTouches)push('touch','move',t.clientX,t.clientY,1);});
  on(canvas,'blur',()=>push('keyboard','blur'));
  return {poll(){for(const pad of navigator.getGamepads?.()||[]){if(!pad)continue;for(let i=0;i<pad.axes.length;i++)push('gamepad','axis'+i,0,0,pad.axes[i]);for(let i=0;i<pad.buttons.length;i++)push('gamepad','button'+i,0,0,pad.buttons[i].value);}},
    receive(){if(overflow){overflow=false;q.length=0;throw Error('DOM input queue overflow; application must resample physical input');}return q.length?q.shift():null;},
    pointerLock(){canvas.requestPointerLock();},destroy(){for(const [t,n,f]of handlers)t.removeEventListener(n,f);q.length=0;}};
}};
