import { _electron } from 'playwright-core';
import electronPath from 'electron';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const output = path.join(root, 'artifacts', `desktop-${Date.now()}`);
await fs.mkdir(output, { recursive: true });
const packaged = process.env.STUDIO_TEST_EXECUTABLE;
const application = await _electron.launch({ executablePath: packaged || electronPath, args: packaged ? [] : ['.'], cwd: root, env: { ...process.env, STUDIO_HEADLESS: '1', STUDIO_PORT: '4791', ...(packaged ? { STUDIO_DATA_DIR: path.join(output, 'data') } : {}) }, timeout: 30000 });
try {
  const main = await application.firstWindow();
  const errors = [];
  main.on('pageerror', error => errors.push(error.message));
  await main.waitForFunction(() => document.body.innerText.includes('모델 준비됨'), undefined, { timeout: 45000 });
  const dismiss = main.getByRole('button', { name: '나중에 설정', exact: true });
  if (await dismiss.isVisible()) await dismiss.click();
  assert.equal(await main.evaluate(() => typeof window.studioDesktop?.pet), 'function');
  await main.screenshot({ path: path.join(output, 'studio.png') });
  const petPromise = application.waitForEvent('window');
  await main.getByRole('button', { name: '바탕화면 펫 띄우기', exact: true }).first().click();
  const pet = await petPromise;
  pet.on('pageerror', error => errors.push(error.message));
  await pet.waitForFunction(() => !!document.querySelector('canvas') && !document.querySelector('.avatar-loading'), undefined, { timeout: 45000 });
  await pet.screenshot({ path: path.join(output, 'pet.png'), omitBackground: true });
  const windows = await application.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().map(window => ({ url: window.webContents.getURL(), visible: window.isVisible(), top: window.isAlwaysOnTop(), background: window.getBackgroundColor() })));
  assert.equal(windows.length, 2); assert.equal(windows.every(window => !window.visible), true);
  assert.equal(windows.find(window => window.url.endsWith('/pet'))?.top, true);
  const alpha = await application.evaluate(async ({ BrowserWindow }) => {
    const window = BrowserWindow.getAllWindows().find(item => item.webContents.getURL().endsWith('/pet'));
    const image = await window.webContents.capturePage();
    const bitmap = image.toBitmap();
    let transparent = 0, visible = 0;
    for (let i = 3; i < bitmap.length; i += 4) { if (bitmap[i] === 0) transparent++; else visible++; }
    return { transparent, visible };
  });
  assert.ok(alpha.transparent > 100); assert.ok(alpha.visible > 100);
  await main.evaluate(() => window.studioDesktop.pet('clickThrough'));
  await main.evaluate(() => window.studioDesktop.pet('close'));
  assert.equal((await application.windows()).length, 1);
  assert.deepEqual(errors, []);
  await fs.writeFile(path.join(output, 'report.json'), JSON.stringify({ passed: true, windows, alpha, errors }, null, 2));
  process.stdout.write(`데스크톱·투명 펫·항상 위·클릭 통과·닫기 확인 완료\n${output}\n`);
} finally { await application.close(); }
