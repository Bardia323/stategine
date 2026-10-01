"""Separate equal peers, real UDP bytes and the same DSL world on each."""
import pathlib
import socket
import subprocess
import sys

exe, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)


def ports(count):
    for base in range(49100, 65000, 13):
        sockets = []
        try:
            for i in range(count):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sockets.append(sock)
                sock.bind(('127.0.0.1', base + i))
            return base
        except OSError:
            pass
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError('no free peer ports')


def run(count, rounds, backend):
    base = ports(count)
    children, paths = [], []
    try:
        for rank in range(count):
            path = output / f'{backend}-{count}-{rank}.txt'
            paths.append(path)
            children.append(subprocess.Popen([str(exe), str(rank), str(count), str(rounds), str(path), backend, str(base)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        for rank, child in enumerate(children):
            log, _ = child.communicate(timeout=60)
            (output / f'{backend}-{count}-{rank}.log').write_text(log)
            if child.returncode == 77 and count == 1:
                print(log.strip())
                return None
            if child.returncode:
                raise RuntimeError(log)
            print(log.strip())
        values = {}
        for path in paths:
            for row in path.read_text().splitlines():
                stalk, coordinate, value = row.split()
                key = (stalk, int(coordinate))
                assert key not in values, 'one execution owner per coordinate'
                values[key] = float(value)
        assert len(values) == 6
        return values
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.wait()


reference = run(1, 1, 'cpu')
for backend in ('cpu', 'cuda'):
    if backend == 'cuda' and run(1, 1, backend) is None:
        continue
    values = run(4, 400, backend)
    assert max(abs(values[key] - reference[key]) for key in values) < 1e-8
    print(f'ok four independent {backend} processes equal the single-machine result')
