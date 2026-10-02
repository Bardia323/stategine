// Scheduling, opaque signaling and durable protocol evidence. No world model.
globalThis.sgSocket={create(url){
  if(!url.startsWith('wss://')&&!url.startsWith('ws://127.0.0.1:')&&!url.startsWith('ws://localhost:'))throw Error('Internet signaling requires wss');
  const q=[];let ws=null,closed=false,nextRetry=0,overflow=false;
  const open=()=>{ws=new WebSocket(url);ws.binaryType='arraybuffer';ws.onmessage=e=>{if(!(e.data instanceof ArrayBuffer)||e.data.byteLength>128*1024)return;if(q.length===256){overflow=true;ws.close();return;}q.push(new Uint8Array(e.data));};ws.onclose=()=>{nextRetry=performance.now()+1000;};};
  open();return {poll(){if(!closed&&ws.readyState===WebSocket.CLOSED&&performance.now()>=nextRetry)open();if(overflow)throw Error('signaling receive backpressure');},
    send(bytes){if(ws.readyState!==WebSocket.OPEN||ws.bufferedAmount>1024*1024)return false;ws.send(bytes);return true;},
    receive(){return q.length?q.shift():null;},destroy(){closed=true;ws.close();q.length=0;}};
}};
globalThis.sgEvidence={exists:key=>localStorage.getItem(key)!==null,store:(key,bytes)=>localStorage.setItem(key,bytes)};
