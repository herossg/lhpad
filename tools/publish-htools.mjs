// Workspace deployment helper: uses the existing pinned SSH connection helper.

// It never reads or publishes the server configuration as an artifact.

import { readFile, writeFile, mkdir, copyFile, rename } from 'node:fs/promises';

import { resolve } from 'node:path';

import { createHash } from 'node:crypto';

import { spawnSync } from 'node:child_process';

import { configuration, connect, remote, quote, sftpFor, sftpPut, sftpWrite } from '../../HStack/ops/remote.mjs';



const project = resolve(import.meta.dirname, '..');

const portal = resolve(project, '../../AndroidServer/htools-portal');

const version = '1.1.2';

const name = `LHPad-${version}-windows-x64.zip`;

const zipPath = resolve(project, 'artifacts', name);

const sha = value => createHash('sha256').update(value).digest('hex');

const zip = await readFile(zipPath);

const digest = sha(zip);

const stamp = new Date().toISOString().replace(/[-:.]/g, '').replace('T', '-').replace('Z', '');

const stage = resolve(project, 'build', `htools-publish-${stamp}`);

const site = 'https://htools.duckdns.org/';

const names = ['index.html', 'downloads/releases.json', 'downloads/SHA256SUMS.txt'];

const signatures = JSON.parse(await readFile(resolve(project, 'artifacts', 'native-1.1.2', 'docs/signature-verification.json'), 'utf8').then(text => text.replace(/^\uFEFF/, '')));

if (signatures.length !== 5 || signatures.some(s => s.status !== 'Valid' || s.thumbprint !== '537D10B46D7742E859498D5642863AEC947973CB'))

  throw new Error('The release must have five verified signatures before publication.');

const cfg = await configuration();

const client = await connect(cfg);

const backup = `/data/data/com.termux/files/home/backups/htools-lhpad-signed-${stamp}`;

const upload = `/data/data/com.termux/files/home/lhpad-signed-upload-${stamp}`;

function once(text, from, to) {

  if (text.split(from).length !== 2) throw new Error('Expected one LHPad replacement: ' + from);

  return text.replace(from, to);

}

async function publicBytes(path) {

  const response = await fetch(new URL(path + '?release=' + stamp, site), { signal: AbortSignal.timeout(45000) });

  if (!response.ok) throw new Error(`HTTPS download failed: ${path} (${response.status})`);

  return Buffer.from(await response.arrayBuffer());

}

try {

  const root = (await remote(client, 'readlink -f "$HOME/apps/htools/current"\n', 30000)).stdout.trim();

  if (!/^\/data\/data\/com\.termux\/files\/home\/apps\/htools\/releases\/[A-Za-z0-9-]+$/.test(root))

    throw new Error('Unexpected portal root; refusing publication.');

  const original = await Promise.all(names.map(async path => (await remote(client, 'cat ' + quote(root + '/' + path) + '\n', 30000)).stdout));

  const manifest = JSON.parse(original[1]);

  const entries = manifest.files.filter(entry => entry.id === 'litepad');

  if (entries.length !== 1) throw new Error('Expected exactly one LHPad download entry.');

  const previous = entries[0];

  if (!/^(?:LitePad|LHPad)-[0-9.]+-windows-x64\.zip$/.test(previous.file) || !/^[a-f0-9]{64}$/.test(previous.sha256))

    throw new Error('Unexpected existing LHPad metadata.');

  const cards = [...original[0].matchAll(/<article\b[^>]*\bid="litepad"[^>]*>[\s\S]*?<\/article>/g)];

  if (cards.length !== 1) throw new Error('Expected one LHPad portal card.');

  let card = once(cards[0][0], `downloads/${previous.file}`, `downloads/${name}`);

  card = once(card, `v${previous.version} <span data-file-size="litepad"`, `v${version} <span data-file-size="litepad"`);

  const notes = [...card.matchAll(/<div class="download-note">[\s\S]*?<\/div>/g)];

  if (notes.length !== 1) throw new Error('Unexpected LHPad installation note.');

  card = card.replace(notes[0][0], '<div class="download-note">EXE·DLL 서명본입니다. ZIP 전체를 풀고 LHPad.exe를 실행하세요. Register-LHPad.cmd로 Windows 11 첫 우클릭 메뉴에 ‘LHPad로 열기’를 추가합니다. 최초 인증서 등록에는 관리자 승인이 필요할 수 있습니다. 미리보기에는 WebView2가 필요합니다.</div>');

  const updatedIndex = once(original[0], cards[0][0], card);
  const nextEntry = { ...previous, file: name, bytes: zip.length, sha256: digest, version };

  const updatedManifest = JSON.stringify({ ...manifest, files: manifest.files.map(entry => entry.id === 'litepad' ? nextEntry : entry) }, null, 2) + '\n';

  const updatedSums = once(original[2], `${previous.sha256}  ${previous.file}`, `${digest}  ${name}`);

  const updated = [updatedIndex, updatedManifest, updatedSums];

  await mkdir(resolve(stage, 'before/downloads'), { recursive: true });

  await mkdir(resolve(stage, 'after/downloads'), { recursive: true });

  for (let i = 0; i < names.length; ++i) {

    await writeFile(resolve(stage, 'before', names[i]), original[i]);

    await writeFile(resolve(stage, 'after', names[i]), updated[i]);

  }

  if (!process.argv.includes('--publish')) {

    console.log(JSON.stringify({ prepared: true, version, name, bytes: zip.length, sha256: digest, stage }, null, 2));

  } else {

    const sourceChecks = names.map((path, i) => `test "$(sha256sum ${quote(root + '/' + path)} | cut -d' ' -f1)" = ${quote(sha(original[i]))}`).join('\n');

    await remote(client, `set -eu\numask 077\nmkdir -p ${quote(upload + '/downloads')} ${quote(backup + '/downloads')}\n`, 30000);

    const sftp = await sftpFor(client);

    try {

      await sftpPut(sftp, zipPath, `${upload}/downloads/${name}`);

      for (let i = 0; i < names.length; ++i) await sftpWrite(sftp, updated[i], `${upload}/${names[i]}`, 0o644);

    } finally { sftp.end(); }

    try {

      await remote(client, `set -eu

test "$(readlink -f "$HOME/apps/htools/current")" = ${quote(root)}

${sourceChecks}

test ! -e ${quote(root + '/downloads/' + name)}

test "$(sha256sum ${quote(upload + '/downloads/' + name)} | cut -d' ' -f1)" = ${quote(digest)}

${names.map((path, i) => `test "$(sha256sum ${quote(upload + '/' + path)} | cut -d' ' -f1)" = ${quote(sha(updated[i]))}`).join('\n')}

${names.map(path => `cp -p ${quote(root + '/' + path)} ${quote(backup + '/' + path)}`).join('\n')}

touch ${quote(backup + '/ready')}

chmod 644 ${quote(upload + '/downloads/' + name)}

mv ${quote(upload + '/downloads/' + name)} ${quote(root + '/downloads/' + name)}

${[1, 2, 0].map(i => `mv -f ${quote(upload + '/' + names[i])} ${quote(root + '/' + names[i])}`).join('\n')}

`, 30000);

      const actual = await Promise.all([...names, `downloads/${name}`].map(publicBytes));

      for (let i = 0; i < names.length; ++i)

        if (sha(actual[i]) !== sha(updated[i])) throw new Error('Public metadata mismatch: ' + names[i]);

      if (sha(actual[3]) !== digest) throw new Error('Public ZIP SHA-256 mismatch.');

    } catch (error) {

      await remote(client, `set -eu\nif test -f ${quote(backup + '/ready')}; then\n${names.map(path => `cp -p ${quote(backup + '/' + path)} ${quote(root + '/' + path)}`).join('\n')}\nfi\n`, 30000);

      throw error;

    }

    // Update only matching local portal inputs; never overwrite local edits.

    for (const folder of ['public', 'dist']) {

      const path = resolve(portal, folder, 'index.html');

      if (sha(await readFile(path)) !== sha(original[0])) throw new Error('Published successfully, but local portal index differs; retain staged changes: ' + path);

      await writeFile(path, updatedIndex);

    }

    for (let i = 1; i < names.length; ++i) {

      const path = resolve(portal, 'dist', names[i]);

      if (sha(await readFile(path)) !== sha(original[i])) throw new Error('Published successfully, but local portal metadata differs: ' + path);

      await writeFile(path, updated[i]);

    }

    await copyFile(zipPath, resolve(portal, 'dist/downloads', name));

    const buildPath = resolve(portal, 'scripts/build.mjs');

    const build = await readFile(buildPath, 'utf8');

    const lines = build.split('\n').map(line => line.includes("{id:'litepad'") ? line.replaceAll(previous.version, version) : line);
    await writeFile(buildPath, lines.join('\n'));

    const tarPath = resolve(portal, 'htools-portal.tar.gz');

    const nextTar = tarPath + '.lhpad-next';

    const tar = spawnSync('tar', ['-czf', nextTar, '-C', resolve(portal, 'dist'), '.'], { windowsHide: true, timeout: 120000 });

    if (tar.error || tar.status !== 0) throw tar.error || new Error('Published successfully, local portal archive update failed.');

    await rename(nextTar, tarPath);

    const report = { checkedAt: new Date().toISOString(), version, url: new URL(`downloads/${name}`, site).href, bytes: zip.length, sha256: digest, backup, changed: [...names, `downloads/${name}`], httpsSha256Matched: true, signatures, servicesRestarted: false };

    await mkdir(resolve(project, 'docs/evidence'), { recursive: true });

    await writeFile(resolve(project, 'docs/evidence', `htools-signed-${stamp}.json`), JSON.stringify(report, null, 2) + '\n');

    console.log(JSON.stringify(report, null, 2));

  }

} finally { client.end(); }

