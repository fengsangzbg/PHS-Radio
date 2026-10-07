'use strict';

// Local, silent integration probe. It creates a uniquely named web pop-out,
// updates only that window, then closes it. No desktop wallpaper is changed.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { spawn } = require('node:child_process');
const crypto = require('node:crypto');
const os = require('node:os');

async function main() {
  const executable = process.argv[2];
  if (!executable || !fs.existsSync(executable)) throw Error('Wallpaper Engine executable is required.');
  const names = [0].map(n => `PHSRadioAudioProbe_${process.pid}_${n}_${crypto.randomUUID().replaceAll('-', '')}`);
  // Some WE versions decode their IPC file paths using the system ANSI page.
  // Keep this fixture outside a potentially non-ASCII workspace path.
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'PHSRadioAudioProbe-'));
  const events = [];
  let activeInstance = '0';
  const server = http.createServer((request, response) => {
    response.setHeader('Access-Control-Allow-Origin', '*');
    response.setHeader('Cache-Control', 'no-store');
    const query = new URL(request.url, 'http://127.0.0.1').searchParams;
    try {
      events.push({ instance: query.get('instance'), properties: JSON.parse(query.get('properties')) });
    }
    catch (_) {}
    response.end('ok');
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const command = args => new Promise((resolve, reject) => {
    const child = spawn(executable, args, { windowsHide: true, windowsVerbatimArguments: true,
      cwd: path.dirname(executable), stdio: 'ignore' });
    const timer = setTimeout(() => {
      child.kill();
      reject(Error('The dedicated WE command helper timed out; run the probe outside the IPC sandbox.'));
    }, 8000);
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('close', code => {
      clearTimeout(timer);
      code === 0 ? resolve() : reject(Error(`WE command returned ${code}.`));
    });
  });
  const waitFor = async condition => {
    const deadline = Date.now() + 12000;
    while (!condition() && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 100));
    if (!condition()) throw Error(`Silent fixture did not acknowledge the targeted property update (${events.length} events).`);
  };
  const configPath = path.join(path.dirname(executable), 'config.json');
  const desktopSettings = () => {
    const config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
    return JSON.stringify(Object.values(config).map(user => user?.general?.wallpaperconfig));
  };
  const desktopBefore = desktopSettings();
  console.log('Testing a silent PHSRadioAudioProbe pop-out; the desktop remains untouched.');
  try {
    for (let n = 0; n < names.length; ++n) {
      const folder = path.join(directory, String(n));
      fs.mkdirSync(folder, { recursive: true });
      fs.writeFileSync(path.join(folder, 'index.html'), `<!doctype html><meta charset="utf-8">
        <style>html{background:#000;color:#aaa;font:14px sans-serif}</style><p>Silent property fixture</p>
        <script>window.wallpaperPropertyListener={applyUserProperties(properties){
          fetch('http://127.0.0.1:${port}/?instance=${n}&properties='+encodeURIComponent(JSON.stringify(properties))).catch(()=>{});
        }};</script>`);
      const project = {
        title: 'PHS Radio silent property fixture', type: 'web', file: 'index.html',
        general: { properties: {
          volume: { type: 'slider', text: 'Volume fixture', value: 61, min: 0, max: 100 },
          probeMarker: { type: 'slider', text: 'Marker', value: 99, min: 0, max: 100 },
        } },
      };
      const experiment = process.argv[3];
      if (['presetproperties', 'defaultproperties', 'initalwproperties'].includes(experiment))
        project[experiment] = { volume: 0, probeMarker: 37 };
      fs.writeFileSync(path.join(folder, 'project.json'), JSON.stringify(project));
      if (experiment === 'preapply')
        await command(['-control', 'applyProperties', '-properties', 'RAW~({"volume":0,"probeMarker":37})~END',
          '-location', names[n]]);
      const presetArgs = [];
      if (experiment === 'presetfile') {
        const presetFile = path.join(folder, 'silent-preset.json');
        fs.writeFileSync(presetFile, JSON.stringify({ volume: 0, probeMarker: 37 }));
        presetArgs.push('-preset', `"${presetFile}"`);
      }
      await command(['-control', 'openWallpaper', '-file', `"${path.join(folder, 'project.json')}"`,
        '-playInWindow', names[n], '-width', '96', '-height', '64', '-x', String(n * 100), '-y', '0',
        '-activate', '0', '-borderless', '1', '-properties', 'RAW~({"volume":0,"probeMarker":37})~END', ...presetArgs]);
      await waitFor(() => events.some(e => e.instance === String(n)));
      if (n === 0) {
        const initial = events.find(e => e.instance === '0').properties;
        console.log('Initial properties:', JSON.stringify({ volume: initial.volume?.value, marker: initial.probeMarker?.value }));
        const before = events.length;
        await command(['-control', 'applyProperties', '-properties', 'RAW~({"volume":0,"probeMarker":37})~END',
          '-location', names[0]]);
        await waitFor(() => events.slice(before).some(e => e.instance === '0'
          && e.properties.probeMarker?.value === 37));
        console.log('Scoped properties:', JSON.stringify(events.slice(before).find(e => e.instance === '0'
          && e.properties.probeMarker?.value === 37).properties));
        console.log('The first named pop-out acknowledged the scoped volume command.');
        if (process.argv[3] === 'warm') {
          const replacement = path.join(directory, 'replacement');
          fs.mkdirSync(replacement);
          fs.writeFileSync(path.join(replacement, 'index.html'),
            fs.readFileSync(path.join(folder, 'index.html'), 'utf8').replace('instance=0', 'instance=1'));
          const project = JSON.parse(fs.readFileSync(path.join(folder, 'project.json'), 'utf8'));
          project.general.properties.probeMarker.value = 18;
          fs.writeFileSync(path.join(replacement, 'project.json'), JSON.stringify(project));
          await command(['-control', 'openWallpaper', '-file', `"${path.join(replacement, 'project.json')}"`,
            '-playInWindow', names[0], '-width', '96', '-height', '64', '-activate', '0', '-borderless', '1']);
          await waitFor(() => events.some(e => e.instance === '1'));
          const inherited = events.find(e => e.instance === '1').properties;
          activeInstance = '1';
          console.log('Same-window replacement initial properties:', JSON.stringify({
            volume: inherited.volume?.value, marker: inherited.probeMarker?.value,
          }));
        }
      }
    }
    await waitFor(() => events.some(e => e.instance === activeInstance));
    console.log('The silent property listener is connected.');
    const priorEvents = events.length;
    await command(['-control', 'applyProperties', '-properties', 'RAW~({"volume":0,"probeMarker":37})~END',
      '-location', names[0]]);
    await waitFor(() => events.slice(priorEvents).some(e => e.instance === activeInstance
      && e.properties.probeMarker?.value === 37));
    await new Promise(resolve => setTimeout(resolve, 500));
    const changes = events.slice(priorEvents);
    const updated = changes.find(e => e.instance === activeInstance && e.properties.probeMarker?.value === 37);
    if (updated.properties.volume?.value !== 0) throw Error('Target pop-out did not receive volume zero.');
    if (desktopSettings() !== desktopBefore) throw Error('Desktop wallpaper configuration changed.');
    console.log('PASS: raw volume=0 is delivered to the named pop-out; desktop configuration is unchanged.');
    console.log('NOTE: this fixture contains no audio; it verifies property delivery and scope, not audible playback.');
  } finally {
    for (const name of names) await command(['-control', 'closeWallpaper', '-location', name]).catch(() => {});
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
