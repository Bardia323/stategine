"""Two independent DSL Pong worlds, real boundary bytes, no server."""
import json
import pathlib
import socket
import subprocess
import sys
import uuid
import hashlib
import struct

exe, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
keys = pathlib.Path(sys.argv[3])
output.mkdir(parents=True, exist_ok=True)

def journal(path, raw=None):
    raw = path.read_bytes() if raw is None else raw
    records = []
    for start in (0,16777344):
        try:
            n = struct.unpack_from('>Q',raw,start)[0]
            assert raw[start+8:start+8+n] == b'sg.net.durable-record.v1'
            offset = start+8+n
            sequence = struct.unpack_from('>Q',raw,offset)[0]
            digest = raw[offset+8:offset+40]
            n = struct.unpack_from('>Q',raw,offset+40)[0]
            payload = raw[offset+48:offset+48+n]
            domain = b'sg.net.durable-checksum.v1'
            checksum = struct.pack('>Q',len(domain))+domain+struct.pack('>QQ',sequence,n)+payload
            assert len(payload) == n and hashlib.blake2b(checksum,digest_size=32).digest() == digest
            records.append((sequence,payload))
        except (AssertionError,struct.error):
            pass
    assert records, 'at least one durably checksummed record'
    return sorted(records)


def ports():
    for base in range(49300, 65000, 7):
        sockets = []
        try:
            for i in range(2):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sockets.append(sock)
                sock.bind(('127.0.0.1', base + i))
            return base
        except OSError:
            pass
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError('no free Pong ports')


def run(modes, delay=0):
    credentials = output / ('credentials-' + uuid.uuid4().hex)
    subprocess.run([str(keys),str(credentials),'2','2','0','pong'],check=True,capture_output=True,text=True)
    base = ports()
    children, paths = [], []
    label = '-'.join(modes) + f'-delay{delay}'
    try:
        for player, mode in enumerate(modes):
            path = output / f'{label}-p{player}.json'
            paths.append(path)
            children.append(subprocess.Popen([
                str(exe), '--player', str(player), '--backend', mode,
                '--port', str(base), '--headless', '--steps', '600',
                '--delay-ms', str(delay if player == 1 else 0), '--out', str(path)
                , '--credentials', str(credentials)
            ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        logs = []
        for player, child in enumerate(children):
            log, _ = child.communicate(timeout=80)
            (output / f'{label}-p{player}.log').write_text(log)
            logs.append(log)
        if all(child.returncode == 77 for child in children):
            print('SKIP CUDA Pong: unavailable')
            return None
        assert all(child.returncode == 0 for child in children), logs
        states = [json.loads(path.read_text()) for path in paths]
        for state in states:
            assert state['epoch'] == 600
            assert state['hits'] > 0, 'a rally exercises paddle collisions'
            assert state['left_score'] + state['right_score'] > 0, 'a miss exercises scoring and serving'
            assert not state['remote_observations'], 'only the neighboring overlap arrives'
            assert state['sent'] > 600 and state['received'] > 600
            assert 0 < state['maximum_prediction_lead'] <= 12
        for player, state in enumerate(states):
            records = journal(credentials / f'p{player}.journal')
            assert len(records) == 2, 'preceding record survives alongside the latest'
            payload = records[-1][1]
            n = struct.unpack_from('>Q',payload)[0]
            assert payload[8:8+n] == b'sg.net.agreement-journal.v1'
            offset = 8+n
            assert payload[offset+32:offset+64].hex() == state['receipt']
            assert struct.unpack_from('>Q',payload,offset+64)[0] == 600
            # A torn overwrite of the older slot must not make its old payload
            # look newer than the last flushed vote/checkpoint.
            path = credentials / f'p{player}.journal'
            torn = bytearray(path.read_bytes())
            older_slot = (records[0][0]-1)%2
            at = older_slot*16777344
            name_length = struct.unpack_from('>Q',torn,at)[0]
            struct.pack_into('>Q',torn,at+8+name_length,records[-1][0]+1)
            recovered = journal(path,torn)
            assert recovered == [records[-1]], 'torn header cannot roll vote safety back'
        replay = subprocess.run([str(exe),'--player','0','--credentials',str(credentials),'--backend','cpu','--port',str(base),'--headless','--steps','1'],capture_output=True,text=True,timeout=10)
        assert replay.returncode != 0 and 'used identity requires' in replay.stderr, 'restart cannot forget votes or silently reset world truth'
        assert states[0]['receipt'] == states[1]['receipt'], 'same finalized hash chain'
        world = {key: value for key, value in states[0].items() if key not in ('sent', 'received', 'remote_observations','maximum_prediction_lead','replayed','rejected')}
        for key, value in world.items():
            assert value == states[1][key] if isinstance(value,str) else abs(value - states[1][key]) < 1e-12, (label, key, value, states[1][key])
        world.pop('receipt')  # Different test sessions pin different public keys.
        print(f'ok {label}: 600 world steps; equal ball, paddles, rally and scores; strict laws pass')
        return world
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.wait()


reference = run(('cpu', 'cpu'))
assert run(('cpu', 'cpu'), 3) == reference, 'delay does not change the world'
assert run(('cpu', 'cpu'), 20) == reference, 'prediction and delayed finalization do not change the world'
gpu = run(('cuda', 'cuda'))
if gpu is not None:
    assert gpu == reference, 'backend does not change the world'
    assert run(('cpu', 'cuda')) == reference, 'heterogeneous peers agree'
