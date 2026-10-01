"""Real signed UDP epochs: three executors, four-member committee, one absent."""
import json
import pathlib
import socket
import subprocess
import sys
import uuid

exe, keys, output = map(pathlib.Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)

def run(modes):
    credentials = output / ('credentials-' + uuid.uuid4().hex)
    subprocess.run([str(keys), str(credentials), '4', '3', '1', 'region'], check=True, capture_output=True)
    for base in range(51000, 65000, 4):
        sockets = []
        try:
            for i in range(4):
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sockets.append(s)
                s.bind(('127.0.0.1', base+i))
            break
        except OSError:
            pass
        finally:
            for s in sockets:
                s.close()
    label = '-'.join(modes)
    children = []
    paths = []
    try:
        for rank, mode in enumerate(modes):
            path = output / f'{label}-{rank}.json'
            paths.append(path)
            children.append(subprocess.Popen([str(exe),str(rank),str(base),str(credentials),mode,str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        logs = []
        for rank, child in enumerate(children):
            log, _ = child.communicate(timeout=30)
            (output / f'{label}-{rank}.log').write_text(log)
            logs.append(log)
        if all(child.returncode == 77 for child in children):
            print('SKIP CUDA verified processes: unavailable')
            return None
        assert all(child.returncode == 0 for child in children), logs
        states = [json.loads(p.read_text()) for p in paths]
        assert all(s == states[0] for s in states), states
        assert states[0]['a'] == states[0]['b'] == 6
        assert states[0]['forged_rejected'] == 4
        assert len(states[0]['epochs']) == len(states[0]['receipts']) == 4
        print(f'ok {label}: four canonical epochs, one absent executor, forged/replayed packets rejected, declared ports and strict laws pass')
        return {key: states[0][key] for key in ('a','b','forged_rejected')}
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.wait()

reference = run(('cpu','cpu','cpu'))
gpu = run(('cuda','cuda','cuda'))
if gpu is not None:
    assert gpu == reference
    assert run(('cpu','cuda','cpu')) == reference
