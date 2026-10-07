'use strict';

// Silent, isolated property-initialization probe. It never imports a user
// wallpaper, emits audio, or changes the desktop wallpaper configuration.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {spawn} = require('node:child_process');
const crypto = require('node:crypto');
const os = require('node:os');

async function main() {
  const executable = process.argv[2];
  if (!executable || !fs.existsSync(executable)) throw Error('WE executable required');
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'PHSRadioInitialVolumePlayback-'));
  const nonce = crypto.randomUUID().replaceAll('-', '');
  const events = [];
  const results = [];
  const names = [];
  const server = http.createServer((request, response) => {
    response.setHeader('Access-Control-Allow-Origin', '*');
    response.setHeader('Cache-Control', 'no-store');
    const query = new URL(request.url, 'http://127.0.0.1').searchParams;
    try { events.push({instance: query.get('instance'), properties: JSON.parse(query.get('properties'))}); }
    catch (_) {}
    response.end('ok');
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const command = args => new Promise((resolve, reject) => {
    const child = spawn(executable, args, {windowsHide:true, windowsVerbatimArguments:true,
      cwd:path.dirname(executable), stdio:'ignore'});
    const timeout = setTimeout(() => {
      child.kill(); reject(Error('Dedicated WE command timed out'));
    }, 8000);
    child.once('error', error => {clearTimeout(timeout); reject(error);});
    child.once('close', code => {clearTimeout(timeout); code === 0 ? resolve() : reject(Error('WE exit '+code));});
  });
  const waitFor = async condition => {
    const end = Date.now() + 5000;
    while (!condition() && Date.now() < end) await new Promise(resolve => setTimeout(resolve, 40));
    return condition();
  };
  const desktopConfig = () => JSON.stringify(Object.values(JSON.parse(fs.readFileSync(
    path.join(path.dirname(executable), 'config.json'), 'utf8'))).map(user => user?.general?.wallpaperconfig));
  const desktopBefore = desktopConfig();
  const flat = {volume:0, probeMarker:37};
  const wrapped = {volume:{value:0}, probeMarker:{value:37}};
  const cases = [];
  for (const field of ['defaultproperties', 'presetproperties', 'initialproperties', 'initproperties', 'initalwproperties']) {
    cases.push({label:field+' flat', mutate:project => {project[field] = flat;}});
    cases.push({label:field+' wrapped', mutate:project => {project[field] = wrapped;}});
  }
  for (const field of ['defaultproperties', 'presetproperties', 'initialproperties'])
    cases.push({label:'general.'+field, mutate:project => {project.general[field] = flat;}});
  for (const field of ['initialproperties', 'initalwproperties', 'presetproperties'])
    cases.push({label:'CLI -'+field, args:['-'+field, 'RAW~('+JSON.stringify(flat)+')~END']});
  const selected = process.argv[3] ? cases.filter(item => item.label.includes(process.argv[3])) : cases;
  try {
    for (let n = 0; n < selected.length; ++n) {
      const fixture = selected[n];
      const name = `PHSRadioWallpaper_InitialVolumePlayback_${process.pid}_${n}_${nonce}`;
      names.push(name);
      const folder = path.join(directory, String(n)); fs.mkdirSync(folder);
      fs.writeFileSync(path.join(folder, 'index.html'), `<!doctype html><meta charset="utf-8">
        <style>html{background:#000;color:#aaa;font:10px sans-serif}</style><p>Silent init fixture</p>
        <script>window.wallpaperPropertyListener={applyUserProperties(properties){
          fetch('http://127.0.0.1:${port}/?instance=${n}&properties='+encodeURIComponent(JSON.stringify(properties))).catch(()=>{});
        }};</script>`);
      const project = {title:'PHS Radio silent initial-volume fixture', type:'web', file:'index.html',
        general:{properties:{
          volume:{type:'slider',text:'Volume fixture',value:61,min:0,max:100},
          probeMarker:{type:'slider',text:'Marker',value:99,min:0,max:100}
        }}};
      fixture.mutate?.(project);
      fs.writeFileSync(path.join(folder,'project.json'),JSON.stringify(project));
      try {
        await command(['-control','openWallpaper','-file',`"${path.join(folder,'project.json')}"`,
          '-playInWindow',name,'-width','96','-height','64','-x','0','-y','0','-activate','0','-borderless','1',
          ...(fixture.args || [])]);
        const acknowledged = await waitFor(() => events.some(e => e.instance === String(n)));
        const first = events.find(e => e.instance === String(n));
        const result = {case:fixture.label, acknowledged,
          volume:first?.properties.volume?.value, marker:first?.properties.probeMarker?.value};
        results.push(result); console.log(JSON.stringify(result));
      } finally {
        await command(['-control','closeWallpaper','-location',name]).catch(()=>{});
      }
      if (desktopConfig() !== desktopBefore) throw Error('Desktop wallpaper configuration changed');
    }
    console.log(JSON.stringify({result:'complete',desktopUnchanged:desktopConfig() === desktopBefore,
      initialVolumeZero:results.filter(item => item.volume === 0),directory}));
  } finally {
    for (const name of names) await command(['-control','closeWallpaper','-location',name]).catch(()=>{});
    server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
  }
}

main().catch(error => {console.error(error.stack);process.exitCode=1;});
