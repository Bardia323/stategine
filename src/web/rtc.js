// External handles and bounded opaque queues only. No Wasm callback, graph,
// participant, committee or game operation is available to these callbacks.
globalThis.sgRTC = {
  create(c) {
    const peers = new Map(), incoming = [], notices = [], signals = [];
    let receiveBytes = 0, closing = false;
    const notify = (peer,kind,channel='') => {
      if (!notices.some(n=>n.peer===peer&&n.kind===kind&&n.channel===channel))
        notices.push({peer,kind,channel});
    };
    const current = (p,l,s) => !closing && peers.get(p)?.[+l]===s && !s.paused;
    const emit = (p,l,s,kind,value='',type='') => {
      if(!current(p,l,s))return;
      if(signals.length>=c.channels*16){s.failed=true;notify(p,0);return;}
      signals.push({peer:p,lane:l,session:s.id.toString(),kind,value,type});
    };
    const attach = (p,l,s,dc) => {
      if(!current(p,l,s)||dc.label!=='_sg'||s.dc){dc.close();s.failed=true;return;}
      s.dc=dc;dc.binaryType='arraybuffer';
      dc.onopen=()=>{if(current(p,l,s))notify(p,1);};
      dc.onclose=()=>{if(current(p,l,s)){s.failed=true;notify(p,0);}};
      dc.onerror=()=>{if(current(p,l,s)){s.failed=true;notify(p,0);}};
      dc.onmessage=e=>{
        if(!current(p,l,s))return;
        if(!(e.data instanceof ArrayBuffer)){s.failed=true;notify(p,0);return;}
        const bytes=new Uint8Array(e.data);
        if(!bytes.length||bytes.length>c.maximum+129||!bytes[0]||bytes[0]>128||bytes.length<1+bytes[0]){s.failed=true;notify(p,0);return;}
        if(incoming.length>=c.receiveMessages||receiveBytes+bytes.length>c.receiveBytes){
          if(!l){notify(p,3);notify(p,2);s.failed=true;dc.close();}
          return; // Latest may be lost; Reliable fails visibly and needs signed frontier replay.
        }
        receiveBytes+=bytes.length;incoming.push({peer:p,bytes});
      };
    };
    const install = (p,l,id,paused=false) => {
      const old=peers.get(p)?.[+l];
      if(old){old.paused=true;old.dc?.close();old.pc?.close();if(old.sent&&!l)notify(p,2);notify(p,0);}
      const s={id,paused,failed:false,dc:null,pc:null,candidates:[],description:false,sent:false,busy:false};
      peers.get(p)[+l]=s;if(paused)return s;
      const servers=c.servers.map(uri=>{
        // Same external URI convention as libdatachannel. Credentials never
        // become protocol identity or graph data.
        if(uri.startsWith('turn:')||uri.startsWith('turns:')){
          const match=uri.match(/^(turns?):\/\/([^:]+):([^@]+)@(.+)$/);
          if(match)return {urls:match[1]+':'+match[4],username:decodeURIComponent(match[2]),credential:decodeURIComponent(match[3])};
        }
        return {urls:uri.replace('://',':')};
      });
      s.pc=new RTCPeerConnection({iceServers:servers,iceTransportPolicy:c.relayOnly?'relay':'all'});
      s.pc.onicecandidate=e=>{if(e.candidate)emit(p,l,s,1,e.candidate.candidate,e.candidate.sdpMid||'0');};
      s.pc.onconnectionstatechange=()=>{
        if(!current(p,l,s))return;
        if(s.pc.connectionState==='connected')notify(p,1);
        if(['disconnected','failed','closed'].includes(s.pc.connectionState))notify(p,0);
        if(['failed','closed'].includes(s.pc.connectionState))s.failed=true;
      };
      s.pc.ondatachannel=e=>attach(p,l,s,e.channel);
      if(c.peer<p){
        attach(p,l,s,s.pc.createDataChannel('_sg',l?{ordered:false,maxRetransmits:0}:{ordered:true}));
        s.pc.createOffer().then(d=>s.pc.setLocalDescription(d)).then(()=>emit(p,l,s,0,s.pc.localDescription.sdp,'offer')).catch(()=>{s.failed=true;notify(p,0);});
      }
      return s;
    };
    const checkPeer = p => {
      if(!p||p===c.peer||p.length>4096)throw Error('invalid ICE peer');
    };
    return {
      known:p=>peers.has(p),
      connect(p){checkPeer(p);if(peers.has(p))return;if(peers.size>=c.channels)throw Error('too many ICE peers');peers.set(p,[null,null]);install(p,false,0n);install(p,true,0n);},
      disconnect(p){for(const l of [false,true]){const s=peers.get(p)?.[+l];if(!s)throw Error('unknown peer');if(s.id===18446744073709551615n)throw Error('session overflow');install(p,l,s.id+1n,true);}},
      reconnect(p){for(const l of [false,true]){const s=peers.get(p)?.[+l];if(!s)throw Error('unknown peer');if(s.id===18446744073709551615n)throw Error('session overflow');const n=install(p,l,s.id+1n);emit(p,l,n,2);}},
      signal(p,l,generation,kind,value,type){
        if(!peers.has(p))throw Error('unknown signaling identity');
        let s=peers.get(p)[+l];const id=BigInt(generation);
        if(id<s.id||s.paused)return;if(id>s.id)s=install(p,l,id);
        if(kind===1){if(s.candidates.length>=256){s.failed=true;return;}const candidate={candidate:value,sdpMid:type};
          if(s.description)s.pc.addIceCandidate(candidate).catch(()=>{s.failed=true;});else s.candidates.push(candidate);
        }else if(kind===0){
          // Serialize description/candidate application on this session only.
          s.pc.setRemoteDescription({sdp:value,type}).then(async()=>{
            if(!current(p,l,s))return;s.description=true;
            for(const candidate of s.candidates)await s.pc.addIceCandidate(candidate);s.candidates=[];
            if(type==='offer'){await s.pc.setLocalDescription(await s.pc.createAnswer());emit(p,l,s,0,s.pc.localDescription.sdp,'answer');}
          }).catch(()=>{s.failed=true;notify(p,0);});
        }
      },
      send(p,l,bytes,cap){
        const s=peers.get(p)?.[+l];if(!s||s.paused||s.failed||s.dc?.readyState!=='open')return false;
        let buffered=0;for(const pair of peers.values())buffered+=pair[+l]?.dc?.bufferedAmount||0;
        if(bytes.length>cap||buffered>cap-bytes.length)return false;
        const maximum=s.pc.sctp?.maxMessageSize||65536;
        if(maximum&&bytes.length>maximum)throw Error('message exceeds negotiated ICE framing limit');
        try{s.dc.send(bytes);s.sent=true;return true;}catch(e){s.failed=true;notify(p,2);return false;}
      },
      poll(){for(const [p,pair] of peers)for(const l of [false,true]){const s=pair[+l];if(s.failed&&!s.paused){if(s.id===18446744073709551615n)throw Error('session overflow');const n=install(p,l,s.id+1n);emit(p,l,n,2);}}},
      receive(){if(!incoming.length)return null;const p=incoming.shift();receiveBytes-=p.bytes.length;return p;},
      signals(){return signals.splice(0);},events(){return notices.splice(0);},
      telemetry(p){const pair=peers.get(p)||[];return {connected:pair.length===2&&pair.every(s=>s.pc?.connectionState==='connected'),buffered:pair.reduce((a,s)=>a+(s.dc?.bufferedAmount||0),0)};},
      destroy(){closing=true;for(const pair of peers.values())for(const s of pair){s.paused=true;s.dc?.close();s.pc?.close();}peers.clear();incoming.length=signals.length=notices.length=0;receiveBytes=0;}
    };
  }
};
