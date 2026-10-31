// The modeller's meshes kept between two programs (tests/test_cache.cpp's second
// half), for a Wasm build: a Wasm program cannot start another, so Node starts
// each - the same sg_cache.js, as `child` - over one folder on the real disk
// (it is linked with NODERAWFS). The same checks as the native test makes.
// node cache_programs.js <sg_cache.js>
const {spawnSync} = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');
const program = path.resolve(process.argv[2]);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'sg_cache_programs_'));
let failures = 0;
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) ++failures; };
const run = which => {
    const r = spawnSync(process.execPath, [program, 'child', dir, which], {encoding: 'utf8'});
    const line = (r.stdout || '').split('\n')[0];
    return (r.status === 0 ? '' : 'bad ') + line;
};
let said = run('this');
check(!said.includes('bad') && said.includes('stored=1'), "one program makes a recipe's mesh and keeps it: " + said);
said = run('this');
check(!said.includes('bad') && said.includes('hits=1') && said.includes('stored=0'), 'another reads it back, the same as building it: ' + said);
said = run('other');
check(!said.includes('bad') && said.includes('hits=0') && said.includes('stored=1'), 'a changed recipe misses, and is made: ' + said);
for (const name of fs.readdirSync(path.join(dir, 'modeler'))) {
    const fd = fs.openSync(path.join(dir, 'modeler', name), 'r+');
    fs.writeSync(fd, Buffer.from([0x7f]), 0, 1, 60);
    fs.closeSync(fd);
}
said = run('this');
check(!said.includes('bad') && said.includes('rejected=1') && said.includes('stored=1'),
      'a damaged mesh is refused, made again as it should be, and kept again: ' + said);
said = run('this');
check(!said.includes('bad') && said.includes('hits=1'), 'and is read back after: ' + said);
fs.rmSync(dir, {recursive: true, force: true});
console.log(failures ? failures + ' FAILED' : 'all ok');
process.exit(failures ? 1 : 0);
