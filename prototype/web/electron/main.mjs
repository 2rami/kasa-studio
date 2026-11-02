import { app, BrowserWindow, ipcMain, Menu, Tray, nativeImage, screen, shell } from 'electron';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createStudio } from '../server/index.mjs';
import { promises as fs } from 'node:fs';
import { inspectModel } from '../server/models.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
if (!app.requestSingleInstanceLock()) app.quit();
else {
  let studio, mainWindow, petWindow, tray, quitting = false;
  const headless = process.env.STUDIO_HEADLESS === '1';
  app.on('second-instance', () => { mainWindow?.show(); mainWindow?.focus(); });
  app.whenReady().then(async () => {
  const dataDir = process.env.STUDIO_DATA_DIR || (app.isPackaged ? app.getPath('userData') : path.resolve(here, '../data'));
  if (app.isPackaged && !(await fs.stat(path.join(dataDir, 'models/hoodiecat')).catch(() => null))) {
    const destination = path.join(dataDir, 'models/hoodiecat');
    await fs.cp(path.join(process.resourcesPath, 'models/hoodiecat'), destination, { recursive: true });
    const model = await inspectModel(destination, 'hoodiecat.model3.json', 'hoodiecat', '후드캣');
    await fs.writeFile(path.join(destination, '.studio.json'), JSON.stringify(model));
  }
  studio = await createStudio({ production: true, desktop: true, dataDir });
  let origin;
  try { origin = await studio.listen(Number(process.env.STUDIO_PORT || 4780)); }
  catch (error) {
    if (error.code !== 'EADDRINUSE') throw error;
    origin = await studio.listen(0);
  }
  const preload = path.join(here, 'preload.cjs');
  const secure = win => {
    win.webContents.setWindowOpenHandler(({ url }) => {
      const parsed = new URL(url);
      if (parsed.origin === origin && ['/overlay', '/pet'].includes(parsed.pathname)) {
        if (parsed.pathname === '/pet') openPet();
        else shell.openExternal(url);
      } else if (parsed.protocol === 'https:' && ['obsproject.com', 'github.com', 'existential.audio', 'vb-audio.com'].includes(parsed.hostname)) shell.openExternal(url);
      return { action: 'deny' };
    });
    win.webContents.on('will-navigate', (event, url) => { if (new URL(url).origin !== origin) event.preventDefault(); });
  };
  mainWindow = new BrowserWindow({ show: !headless, width: 1480, height: 940, minWidth: 800, minHeight: 640, title: 'Kasa Studio', backgroundColor: '#FBFAF6', titleBarStyle: 'hiddenInset', trafficLightPosition: { x: 18, y: 18 }, webPreferences: { preload, contextIsolation: true, nodeIntegration: false, sandbox: true, offscreen: headless } });
  secure(mainWindow); await mainWindow.loadURL(origin);
  function openPet() {
    if (petWindow && !petWindow.isDestroyed()) { petWindow.show(); return; }
    const area = screen.getPrimaryDisplay().workArea;
    petWindow = new BrowserWindow({ show: !headless, width: 320, height: 540, x: area.x + area.width - 340, y: area.y + area.height - 550, frame: false, transparent: true, alwaysOnTop: true, resizable: true, hasShadow: false, skipTaskbar: true, webPreferences: { preload, contextIsolation: true, nodeIntegration: false, sandbox: true, offscreen: headless } });
    secure(petWindow); petWindow.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true }); petWindow.loadURL(`${origin}/pet`);
    petWindow.on('closed', () => { petWindow = null; });
  }
  const trusted = event => { if (!event.senderFrame || new URL(event.senderFrame.url).origin !== origin) throw new Error('허용되지 않는 창이에요.'); };
  ipcMain.handle('studio:pet', (event, action) => {
    trusted(event);
    if (action === 'open') openPet();
    if (action === 'close') petWindow?.close();
    if (action === 'clickThrough') petWindow?.setIgnoreMouseEvents(true, { forward: true });
    return { open: Boolean(petWindow && !petWindow.isDestroyed()) };
  });
  ipcMain.handle('studio:show', event => { trusted(event); mainWindow.show(); mainWindow.focus(); });
  const image = nativeImage.createEmpty();
  if (!headless) { tray = new Tray(image); tray.setTitle('K'); tray.setToolTip('Kasa Studio');
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: '방송 작업실 열기', click: () => { mainWindow.show(); mainWindow.focus(); } },
    { label: '펫 띄우기', click: openPet },
    { label: '펫 클릭 통과 해제', click: () => petWindow?.setIgnoreMouseEvents(false) },
    { label: '펫 닫기', click: () => petWindow?.close() },
    { type: 'separator' }, { label: '종료', click: () => app.quit() },
  ]));
  }
  mainWindow.on('close', event => { if (!quitting) { event.preventDefault(); mainWindow.hide(); } });
  app.on('before-quit', () => { quitting = true; });
  app.on('will-quit', () => { studio.close(); });
  app.on('activate', () => { mainWindow.show(); mainWindow.focus(); });
  }).catch(error => {
    process.stderr.write(`Kasa Studio 실행 오류: ${error.message}\n`);
    app.exit(1);
  });
}
