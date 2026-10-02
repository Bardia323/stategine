"""Real browser/Wasm, GPU reconstruction and native WebRTC interoperability.

No mock GPU, fallback transport or skipped interoperability counts as a pass.
Use --ice-build with a tested native libdatachannel build. Browser profile and
credentials are disposable execution data under the Wasm build output.
"""
import argparse
import atexit
import functools
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
def executable(build,name):
    if name in ('sg_cross_platform', 'sg_browser_render', 'sg_web_rtc_peer'):
        build = build/'laws'
    return str(build/(name+('.exe' if os.name=='nt' else '')))
class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*args):pass
def run(args):
    native=Path(args.native).resolve();wasm=Path(args.wasm).resolve();ice=Path(args.ice_build).resolve()
    node=args.node
    reference=json.loads(subprocess.check_output([executable(native,'sg_cross_platform')],text=True))
    compiled=json.loads(subprocess.check_output([node,str(wasm/'sg_cross_platform.js')],text=True))
    assert reference==compiled,(reference,compiled)
    print('PASS native/Wasm facts, laws, canonical doubles, hashes, signatures and receipt',flush=True)
    handler=functools.partial(Quiet,directory=str(wasm))
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    url=f'http://127.0.0.1:{server.server_port}'
    out=wasm/'out'/'browser';out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as directory,sync_playwright() as playwright:
        directory=Path(directory)
        relay=subprocess.Popen([sys.executable,str(ROOT/'examples/ice_rendezvous.py'),'--port','0'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        def stop_relay():
            if relay.poll() is None:relay.terminate();relay.wait(timeout=10)
        atexit.register(stop_relay)
        line=relay.stdout.readline();assert line.startswith('READY '),line
        signaling='ws://127.0.0.1:'+line.split()[1]
        browser=playwright.chromium.launch(executable_path=args.browser or None,headless=True,args=['--enable-unsafe-webgpu','--use-angle=d3d11' if os.name=='nt' else '--use-angle=vulkan'])
        context=browser.new_context(viewport={'width':1100,'height':850});page=context.new_page()
        faults=[];page.on('pageerror',lambda e:faults.append(str(e)))
        page.goto(url+'/render.html');page.wait_for_function('typeof engine!=="undefined" && !!engine && typeof engine.ccall==="function"',timeout=60000)
        assert page.evaluate('engine.ccall("sg_fixture_start","number",[],[])')==1,page.evaluate('engine.ccall("sg_fixture_error","string",[],[])')
        derived=page.evaluate('engine.ccall("sg_fixture_facts","string",[],[])')
        expected=subprocess.check_output([executable(native,'sg_browser_render')],text=True).strip()
        assert derived==expected,(derived,expected)
        for recreate in [False,True]:
            if recreate:page.evaluate('engine.ccall("sg_fixture_recreate",null,[],[])')
            deadline=time.monotonic()+30
            while True:
                rendered=page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')
                assert rendered,page.evaluate('engine.ccall("sg_fixture_error","string",[],[])')
                if rendered==1:break
                assert time.monotonic()<deadline
                page.wait_for_timeout(20)
            page.wait_for_timeout(200)
            assert page.evaluate('engine.ccall("sg_fixture_facts","string",[],[])')==derived
            assert page.evaluate('engine.ccall("sg_fixture_verify","number",[],[])')==1
        page.locator('#canvas').screenshot(path=str(out/'room-webgpu.png'))
        assert not faults,faults
        print('PASS shared native/browser 3D fixture, portals/feeds, Surface2D/CRT, shadows, AO, HDR/Look passes and GPU recreation',flush=True)
        # A custom implementation belongs to the same Look pass. Its uniforms
        # are still supplied from that state, including after device recreation.
        custom=page.evaluate('sgWGSL.post.replace("@fragment fn composite(","@fragment fn fs_main(").replace("f.look[4].y*2.4","sg_uGrain()*2.4")')
        assert page.evaluate('s=>engine.ccall("sg_fixture_shader","number",["string","number"],[s,0])',custom)==1
        deadline=time.monotonic()+30
        while page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==2:
            assert time.monotonic()<deadline
            page.wait_for_timeout(20)
        assert page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==1,page.evaluate('engine.ccall("sg_fixture_error","string",[],[])')
        custom_facts=page.evaluate('engine.ccall("sg_fixture_facts","string",[],[])')
        page.evaluate('engine.ccall("sg_fixture_recreate",null,[],[])')
        deadline=time.monotonic()+30
        while page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==2:
            assert time.monotonic()<deadline
            page.wait_for_timeout(20)
        assert page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==1
        assert page.evaluate('engine.ccall("sg_fixture_facts","string",[],[])')==custom_facts
        assert page.evaluate('engine.ccall("sg_fixture_shader","number",["string","number"],["invalid WGSL",0])')==1
        deadline=time.monotonic()+30
        while page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==2:
            assert time.monotonic()<deadline
            page.wait_for_timeout(20)
        assert page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==0
        assert page.evaluate('engine.ccall("sg_fixture_shader","number",["string","number"],["invalid WGSL",1])')==1
        deadline=time.monotonic()+30
        while page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==2:
            assert time.monotonic()<deadline
            page.wait_for_timeout(20)
        assert page.evaluate('engine.ccall("sg_fixture_draw","number",[],[])')==1
        assert page.evaluate('engine.ccall("sg_fixture_diagnostics","string",[],[])')
        print('PASS custom Look WGSL/uniforms, device-loss preparation, shader refusal and explicit diagnosed fallback',flush=True)
        def load(folder):
            page.goto(url);page.wait_for_function('typeof engine!=="undefined" && !!engine && typeof engine.ccall==="function"',timeout=60000)
            files={name:(folder/name).read_bytes().hex() for name in ['committee.txt','p1.seed']}
            page.evaluate('files=>{engine.FS.mkdirTree("/credentials");for(const [name,hex]of Object.entries(files))engine.FS.writeFile("/credentials/"+name,Uint8Array.from(hex.match(/../g),x=>parseInt(x,16)));}',files)
            assert page.evaluate('url=>engine.ccall("sg_start","number",["number","string","string"],[1,"/credentials",url])',signaling)==1,page.evaluate('engine.ccall("sg_error","string",[],[])')
        def call(name,args=(),result='number'):
            return page.evaluate('p=>engine.ccall(p.name,p.result,p.args.map(x=>typeof x==="string"?"string":"number"),p.args)',{'name':name,'result':result,'args':list(args)})
        def status():return json.loads(call('sg_status',result='string'))
        def check(name,args=()):
            assert call(name,args)==1,(name,call('sg_error',result='string'),faults)
        def poll():check('sg_poll',[time.monotonic()*1000])
        def until(predicate,seconds=30):
            deadline=time.monotonic()+seconds
            while not predicate():
                assert time.monotonic()<deadline,(status(),call('sg_error',result='string'),faults)
                poll();page.wait_for_timeout(10)
        credentials=directory/'transport';subprocess.check_call([executable(native,'sg_net_keys'),str(credentials),'2','2','0','pong'])
        load(credentials)
        frozen=status();
        assert call('sg_input_value')==0
        page.locator('#canvas').dispatch_event('keydown',{'code':'KeyW'})
        page.wait_for_timeout(50)
        assert call('sg_input_value')==0 and status()['facts']==frozen['facts']
        check('sg_step',[0,0]);assert call('sg_input_value')==1
        page.locator('#canvas').dispatch_event('keyup',{'code':'KeyW'})
        assert call('sg_input_value')==1
        check('sg_step',[0,0]);assert call('sg_input_value')==0
        frozen=status()
        print('PASS DOM callbacks queue only; declared input polling changes state',flush=True)
        for value in [b'old',b'middle',b'newest']:
            assert call('sg_probe',['probe.latest','same',value.hex(),1])==0
        native_log=open(out/'interop-native.log','w')
        peer=subprocess.Popen([executable(ice,'sg_web_rtc_peer'),str(credentials),signaling],stdout=native_log,stderr=subprocess.STDOUT)
        try:
            until(lambda:status()['connected'])
            assert status()['facts']==frozen['facts'] and status()['revision']==frozen['revision']
            print('PASS callbacks/connections have no state or graph authority',flush=True)
            replies=[]
            def latest():
                s=call('sg_probe_receive',result='string')
                if s:replies.append(s)
                return any(s=='probe.latest:'+b'newest'.hex() for s in replies)
            until(latest);assert all('6f6c64' not in s and '6d6964646c65' not in s for s in replies)
            payload=bytes((i*31)%256 for i in range(96*1024))
            assert call('sg_probe',['probe','large',payload.hex(),0])==0
            def received():
                s=call('sg_probe_receive',result='string')
                if s:replies.append(s)
                return 'probe:'+payload.hex() in replies
            until(received)
            print('PASS native/browser RTC framing >64 KB and Latest coalescing',flush=True)
            call('sg_reconnect',result=None);until(lambda:status()['connected'])
            assert status()['facts']==frozen['facts']
            assert call('sg_probe',['probe','reconnect',b'reconnected'.hex(),0])==0
            def reconnected():
                s=call('sg_probe_receive',result='string');return s=='probe:'+b'reconnected'.hex()
            until(reconnected)
            print('PASS session generation/reconnection with world unchanged',flush=True)
            def gpu_ready():
                check('sg_draw',[960,540,0]);return status()['ready']
            until(gpu_ready);check('sg_draw',[960,540,0]);page.wait_for_timeout(200)
            page.locator('#canvas').screenshot(path=str(out/'pong-webgpu.png'))
            before=status();call('sg_recreate_gpu',result=None);until(lambda:status()['ready']);check('sg_draw',[960,540,0]);page.wait_for_timeout(100)
            after=status();assert before['facts']==after['facts'] and before['receipt']==after['receipt'] and before['revision']==after['revision']
            print('PASS real WebGPU draw, device/resource reconstruction, one world time',flush=True)
            check('sg_verify')
            # A paused connection makes Reliable queue pressure measurable.
            call('sg_disconnect',result=None)
            pressure=[call('sg_probe',['pressure','slot',payload.hex(),0]) for _ in range(100)]
            assert 0 in pressure and 1 in pressure,pressure
            assert status()['facts']==frozen['facts']
            print('PASS bounded explicit Reliable backpressure and disconnect non-authority',flush=True)
        finally:
            peer.terminate();peer.wait(timeout=10);native_log.close()
        # Fresh identities and a fresh committee isolate durable journal safety.
        credentials=directory/'world';subprocess.check_call([executable(native,'sg_net_keys'),str(credentials),'2','2','0','pong']);load(credentials)
        native_report=directory/'pong-native.json';native_log=open(out/'pong-native.log','w')
        peer=subprocess.Popen([executable(ice,'sg_net_pong'),'--player','0','--credentials',str(credentials),'--signaling',signaling,'--backend','cpu','--headless','--steps','40','--out',str(native_report)],stdout=native_log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+100
            while status()['epoch']<40:
                assert peer.poll() is None,(peer.returncode,(out/'pong-native.log').read_text())
                poll();check('sg_step',[1/60,1])
                if status()['ready']:check('sg_draw',[640,360,0])
                page.wait_for_timeout(3)
                assert time.monotonic()<deadline,(status(),faults)
            for _ in range(80):poll();page.wait_for_timeout(5)
            assert peer.wait(timeout=10)==0,(out/'pong-native.log').read_text()
            native_result=json.loads(native_report.read_text());assert native_result['receipt']==status()['receipt'],(native_result,status())
            check('sg_verify');assert not faults,faults
            print('PASS native/browser signed Pong, same 40 finalized world steps and receipt',flush=True)
        finally:
            if peer.poll() is None:peer.terminate();peer.wait(timeout=10)
            native_log.close();browser.close();relay.terminate();relay.wait(timeout=10);server.shutdown()
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',required=True);parser.add_argument('--wasm',required=True);parser.add_argument('--ice-build',required=True);parser.add_argument('--node',default='node');parser.add_argument('--browser')
    run(parser.parse_args())
