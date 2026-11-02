import express from 'express';
import multer from 'multer';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { promises as fs } from 'node:fs';
import { createServer } from 'node:http';
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { WebSocketServer, WebSocket } from 'ws';
import OBSWebSocket from 'obs-websocket-js/json';
import { configSchema, mergeConfig, serviceUrl, signalSchema } from './config.mjs';
import { listModels, importModelZip } from './models.mjs';
import { rvcRequest, updateRvc, uploadRvc } from './rvc.mjs';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const upload = multer({ storage: multer.memoryStorage(), limits: { fileSize: 160 * 1024 * 1024, files: 2 } });

export async function createStudio(options = {}) {
  const dataRoot = options.dataDir || process.env.STUDIO_DATA_DIR || path.join(projectRoot, 'data');
  const modelsRoot = path.join(dataRoot, 'models');
  await fs.mkdir(modelsRoot, { recursive: true });
  const configFile = path.join(dataRoot, 'settings.json');
  let config = configSchema.parse({});
  try { config = configSchema.parse(JSON.parse(await fs.readFile(configFile, 'utf8'))); }
  catch (error) { if (error.code !== 'ENOENT') throw new Error(`설정 파일을 읽을 수 없어요: ${error.message}`); }
  const tokenFile = path.join(dataRoot, 'agent-token');
  let token;
  try { token = (await fs.readFile(tokenFile, 'utf8')).trim(); }
  catch { token = randomBytes(24).toString('hex'); await fs.writeFile(tokenFile, token, { mode: 0o600 }); }
  let models = await listModels(modelsRoot);
  if (!models.some(model => model.id === config.modelId)) config.modelId = models[0]?.id || '';
  const app = express();
  const server = createServer(app);
  const ws = new WebSocketServer({ noServer: true, maxPayload: 64 * 1024 });
  const obs = new OBSWebSocket();
  const subscribers = new Set();
  let writeQueue = Promise.resolve();
  let agentKey = process.env.STUDIO_AGENT_KEY || '';
  let closed = false;
  let origin = '';
  let obsBusy = false;
  const state = {
    obs: { connected: false, scenes: [], inputs: [], currentScene: '', streaming: false, recording: false, error: '', stats: null },
    rvc: { connected: false, info: null, error: '' },
    agent: { busy: false, configured: false, models: [], messages: [], lastSignal: null },
    stage: { mouth: 0, lookX: 0, lookY: 0, emotion: 'neutral', visible: true },
    events: [],
  };
  function snapshot() {
    return { config, models, ...state, agent: { ...state.agent, configured: Boolean(config.agent.model) }, origin, desktop: Boolean(options.desktop), overlayUrl: `${origin}/overlay` };
  }
  function emit(message) {
    const data = JSON.stringify(message);
    for (const client of ws.clients) if (client.readyState === WebSocket.OPEN) client.send(data);
  }
  function publish() {
    if (closed) return;
    const data = `data: ${JSON.stringify(snapshot())}\n\n`;
    for (const subscriber of subscribers) subscriber.write(data);
  }
  function event(text, level = 'info') {
    state.events = [{ id: randomBytes(6).toString('hex'), time: Date.now(), text, level }, ...state.events].slice(0, 30);
    publish();
  }
  async function save(patch) {
    config = mergeConfig(config, patch);
    const serialized = JSON.stringify(config, null, 2);
    writeQueue = writeQueue.then(async () => {
      await fs.writeFile(`${configFile}.tmp`, serialized, { mode: 0o600 });
      await fs.rename(`${configFile}.tmp`, configFile);
    });
    await writeQueue;
    emit({ type: 'config', config, model: models.find(model => model.id === config.modelId) || null });
    publish();
  }
  const validOrigin = request => !request.headers.origin || request.headers.origin === origin;
  app.disable('x-powered-by');
  app.use((req, res, next) => {
    const host = req.headers.host || '';
    if (!/^(127\.0\.0\.1|localhost)(:\d+)?$/.test(host) || !validOrigin(req) || (!['GET', 'HEAD'].includes(req.method) && req.headers['sec-fetch-site'] === 'cross-site')) return res.status(403).json({ error: '이 기기의 작업실에서만 접근할 수 있어요.' });
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'no-referrer');
    res.setHeader('Cache-Control', 'no-store');
    next();
  });
  app.use(express.json({ limit: '64kb' }));
  app.get('/api/health', (_req, res) => res.json({ ok: true, name: 'Kasa Studio' }));
  app.get('/api/state', (_req, res) => res.json(snapshot()));
  app.get('/api/events', (req, res) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Connection', 'keep-alive'); res.flushHeaders();
    subscribers.add(res); res.write(`data: ${JSON.stringify(snapshot())}\n\n`);
    req.on('close', () => subscribers.delete(res));
  });
  const heartbeat = setInterval(() => { for (const res of subscribers) res.write(': heartbeat\n\n'); }, 15000);
  heartbeat.unref();

  app.post('/api/config', async (req, res) => {
    const patch = req.body;
    if (patch.modelId && !models.some(model => model.id === patch.modelId)) return res.status(400).json({ error: '모델을 찾을 수 없어요.' });
    if (patch.obs?.url && patch.obs.url !== config.obs.url) { serviceUrl(patch.obs.url, 'ws'); await obs.disconnect(); }
    if (patch.rvc?.url && patch.rvc.url !== config.rvc.url) {
      serviceUrl(patch.rvc.url);
      if (state.rvc.info?.serverAudioStated) return res.status(409).json({ error: '음성 변환을 먼저 중지해 주세요.' });
      state.rvc = { connected: false, info: null, error: '' };
    }
    if (patch.agent?.endpoint) serviceUrl(patch.agent.endpoint);
    await save(patch); res.json(snapshot());
  });
  app.post('/api/models/import', express.raw({ type: ['application/zip', 'application/octet-stream'], limit: '100mb' }), async (req, res) => {
    if (!Buffer.isBuffer(req.body)) return res.status(400).json({ error: 'Live2D ZIP 파일을 선택해 주세요.' });
    const model = await importModelZip(req.body, modelsRoot, typeof req.query.name === 'string' ? req.query.name.slice(0, 80) : undefined);
    models = await listModels(modelsRoot); await save({ modelId: model.id });
    event(`${model.name} 모델을 가져왔어요.`); res.json(model);
  });
  app.use('/models', express.static(modelsRoot, { dotfiles: 'deny', index: false, fallthrough: false }));

  async function refreshObs() {
    if (!state.obs.connected) return;
    const [scenes, inputs, stream, record] = await Promise.all([
      obs.call('GetSceneList'), obs.call('GetInputList'), obs.call('GetStreamStatus'), obs.call('GetRecordStatus'),
    ]);
    state.obs.scenes = scenes.scenes.map(scene => ({ name: scene.sceneName, index: scene.sceneIndex }));
    state.obs.currentScene = scenes.currentProgramSceneName;
    state.obs.streaming = stream.outputActive; state.obs.recording = record.outputActive;
    state.obs.inputs = await Promise.all(inputs.inputs.map(async input => {
      try {
        const [volume, muted] = await Promise.all([obs.call('GetInputVolume', { inputName: input.inputName }), obs.call('GetInputMute', { inputName: input.inputName })]);
        return { name: input.inputName, volume: volume.inputVolumeDb, muted: muted.inputMuted };
      } catch { return null; }
    }));
    state.obs.inputs = state.obs.inputs.filter(Boolean); publish();
  }
  obs.on('ConnectionError', () => {});
  obs.on('ConnectionClosed', () => {
    state.obs = { connected: false, scenes: [], inputs: [], currentScene: '', streaming: false, recording: false, error: '', stats: null }; publish();
  });
  obs.on('CurrentProgramSceneChanged', ({ sceneName }) => { state.obs.currentScene = sceneName; publish(); });
  obs.on('StreamStateChanged', ({ outputActive }) => { state.obs.streaming = outputActive; publish(); });
  obs.on('RecordStateChanged', ({ outputActive }) => { state.obs.recording = outputActive; publish(); });
  for (const name of ['InputMuteStateChanged', 'InputVolumeChanged', 'InputCreated', 'InputRemoved', 'SceneCreated', 'SceneRemoved']) obs.on(name, () => refreshObs().catch(() => {}));
  app.post('/api/obs/connect', async (req, res) => {
    if (obsBusy) return res.status(409).json({ error: 'OBS에 연결하고 있어요.' });
    obsBusy = true;
    try {
      await obs.disconnect();
      await obs.connect(serviceUrl(config.obs.url, 'ws'), String(req.body.password || ''), { rpcVersion: 1 });
      state.obs.connected = true; state.obs.error = '';
      await refreshObs(); event('OBS에 연결했어요.'); res.json(snapshot());
    } catch (error) {
      await obs.disconnect(); state.obs.error = error.code === 4009 ? 'OBS 비밀번호를 확인해 주세요.' : `OBS 연결 실패: ${error.message}`;
      publish(); res.status(502).json({ error: state.obs.error });
    } finally { obsBusy = false; }
  });
  app.post('/api/obs/disconnect', async (_req, res) => { await obs.disconnect(); res.json(snapshot()); });
  app.post('/api/obs/action', async (req, res) => {
    if (!state.obs.connected) return res.status(409).json({ error: 'OBS를 먼저 연결해 주세요.' });
    const { action, name, value } = req.body;
    if (action === 'scene') { await obs.call('SetCurrentProgramScene', { sceneName: String(name) }); event(`장면 전환: ${name}`); }
    else if (action === 'mute') await obs.call('SetInputMute', { inputName: String(name), inputMuted: Boolean(value) });
    else if (action === 'volume') {
      if (!Number.isFinite(value) || value < -100 || value > 0) throw new Error('볼륨 범위를 확인해 주세요.');
      await obs.call('SetInputVolume', { inputName: String(name), inputVolumeDb: value });
    }
    else if (action === 'streamStart') { await obs.call('StartStream'); event('방송을 시작했어요.'); }
    else if (action === 'streamStop') { await obs.call('StopStream'); event('방송을 종료했어요.'); }
    else if (action === 'recordStart') { await obs.call('StartRecord'); event('녹화를 시작했어요.'); }
    else if (action === 'recordStop') { await obs.call('StopRecord'); event('녹화를 종료했어요.'); }
    else if (action === 'addAvatar') {
      await obs.call('CreateInput', { sceneName: state.obs.currentScene, inputName: 'Kasa Studio 아바타', inputKind: 'browser_source', inputSettings: { url: `${origin}/overlay`, width: 1080, height: 1080, fps: 30, css: 'body { margin: 0; background: transparent; overflow: hidden; }', reroute_audio: false }, sceneItemEnabled: true });
      event('현재 OBS 장면에 아바타를 추가했어요.');
    }
    else return res.status(400).json({ error: '알 수 없는 OBS 동작이에요.' });
    await refreshObs(); res.json(snapshot());
  });
  app.get('/api/obs/preview', async (_req, res) => {
    if (!state.obs.connected || !state.obs.currentScene) return res.status(409).json({ error: 'OBS 장면이 연결되지 않았어요.' });
    const shot = await obs.call('GetSourceScreenshot', { sourceName: state.obs.currentScene, imageFormat: 'jpeg', imageWidth: 960, imageCompressionQuality: 75 });
    res.json({ image: shot.imageData });
  });

  app.post('/api/rvc/connect', async (_req, res) => {
    try {
      const info = await rvcRequest(config.rvc.url, '/info');
      if (!Array.isArray(info.modelSlots) || !('enableServerAudio' in info)) throw new Error('w-okada 서버 오디오 API를 지원하는 주소가 필요해요.');
      state.rvc = { connected: true, info, error: '' }; event('Voice Changer에 연결했어요.'); res.json(snapshot());
    } catch (error) { state.rvc = { connected: false, info: null, error: `RVC 연결 실패: ${error.message}` }; publish(); res.status(502).json({ error: state.rvc.error }); }
  });
  app.post('/api/rvc/setting', async (req, res) => {
    if (!state.rvc.connected) return res.status(409).json({ error: 'Voice Changer를 먼저 연결해 주세요.' });
    const { key, value } = req.body;
    if (key === 'serverAudioStated' && value === 1 && (Number(state.rvc.info?.modelSlotIndex) < 0 || Number(state.rvc.info?.serverInputDeviceId) < 0 || Number(state.rvc.info?.serverOutputDeviceId) < 0)) return res.status(409).json({ error: '음성 모델, 입력 마이크, 출력 장치를 모두 선택해 주세요.' });
    state.rvc.info = await updateRvc(config.rvc.url, key, value);
    if (key === 'serverAudioStated') event(value ? 'RVC 음성 변환을 시작했어요.' : 'RVC 음성 변환을 중지했어요.');
    publish(); res.json(snapshot());
  });
  app.post('/api/rvc/models', upload.array('files', 2), async (req, res) => {
    if (!state.rvc.connected) return res.status(409).json({ error: 'Voice Changer를 먼저 연결해 주세요.' });
    const slot = Number(req.body.slot);
    if (state.rvc.info.modelSlots.some(item => Number(item.slotIndex) === slot && item.modelFile)) return res.status(409).json({ error: '이미 모델이 있는 슬롯이에요. 빈 슬롯을 선택해 주세요.' });
    state.rvc.info = await uploadRvc(config.rvc.url, req.files || [], slot);
    event('RVC 음성 모델을 가져왔어요.'); res.json(snapshot());
  });
  app.get('/api/rvc/performance', async (_req, res) => {
    if (!state.rvc.connected) return res.status(409).json({ error: 'Voice Changer가 연결되지 않았어요.' });
    const [info, performance] = await Promise.all([rvcRequest(config.rvc.url, '/info'), rvcRequest(config.rvc.url, '/performance')]);
    state.rvc.info = info; publish(); res.json(performance);
  });

  function signal(input) {
    const data = { ...signalSchema.parse(input), id: randomBytes(6).toString('hex'), at: Date.now() };
    state.agent.lastSignal = data; state.stage.emotion = data.emotion;
    emit({ type: 'signal', signal: data }); publish(); return data;
  }
  app.get('/api/agent/token', (_req, res) => res.json({ token }));
  app.post('/api/agent/key', (req, res) => { agentKey = String(req.body.key || '').slice(0, 10000); res.json({ configured: Boolean(agentKey) }); });
  app.post('/api/agent/probe', async (_req, res) => {
    const endpoint = serviceUrl(config.agent.endpoint);
    const response = await fetch(`${endpoint}/models`, { headers: agentKey ? { Authorization: `Bearer ${agentKey}` } : {}, signal: AbortSignal.timeout(10000) });
    if (!response.ok) throw new Error(`에이전트 API 응답 오류 (${response.status})`);
    const data = await response.json();
    state.agent.models = (data.data || []).map(model => String(model.id));
    if (!config.agent.model && state.agent.models[0]) await save({ agent: { model: state.agent.models[0] } });
    event('에이전트 모델 목록을 확인했어요.'); res.json(snapshot());
  });
  app.post('/api/agent/signal', (req, res) => {
    const supplied = Buffer.from(String(req.headers['x-studio-token'] || ''));
    const expected = Buffer.from(token);
    if (supplied.length !== expected.length || !timingSafeEqual(supplied, expected)) return res.status(401).json({ error: '에이전트 연결 토큰이 필요해요.' });
    res.json(signal(req.body));
  });
  app.post('/api/stage/signal', (req, res) => res.json(signal(req.body)));
  app.post('/api/agent/chat', async (req, res) => {
    const text = String(req.body.text || '').trim();
    if (!text || text.length > 4000) return res.status(400).json({ error: '메시지는 1~4000자로 입력해 주세요.' });
    if (!config.agent.model) return res.status(409).json({ error: '에이전트 모델을 먼저 설정해 주세요.' });
    if (state.agent.busy) return res.status(409).json({ error: '파트너가 대답하고 있어요.' });
    state.agent.busy = true; publish();
    const history = [...state.agent.messages.slice(-18), { role: 'user', content: text }];
    try {
      const response = await fetch(`${serviceUrl(config.agent.endpoint)}/chat/completions`, {
        method: 'POST', signal: AbortSignal.timeout(60000),
        headers: { 'Content-Type': 'application/json', ...(agentKey ? { Authorization: `Bearer ${agentKey}` } : {}) },
        body: JSON.stringify({ model: config.agent.model, stream: false, messages: [
          { role: 'system', content: `${config.agent.prompt}\nJSON 객체로 답하세요: {"text":"할 말", "emotion":"neutral|smile|surprise|sad|angry|shy|wink", "motion":"|Nod|Shake|Blink"}. text는 500자 이하로 하세요.` }, ...history,
        ] }),
      });
      if (!response.ok) throw new Error(`에이전트 응답 오류 (${response.status})`);
      const data = await response.json(); const content = data.choices?.[0]?.message?.content;
      if (typeof content !== 'string' || !content.trim()) throw new Error('에이전트가 빈 대답을 보냈어요.');
      let reply = { text: content.slice(0, 2000), emotion: 'neutral', motion: '', duration: 8 };
      try { reply = signalSchema.parse({ ...JSON.parse(content.replace(/^```(?:json)?\s*|\s*```$/g, '')), duration: 10 }); } catch { /* 호환 API의 일반 텍스트 응답도 말풍선으로 전달한다. */ }
      state.agent.messages = [...history, { role: 'assistant', content: reply.text }].slice(-20);
      signal(reply); res.json(reply);
    } finally { state.agent.busy = false; publish(); }
  });
  app.post('/api/agent/clear', (_req, res) => { state.agent.messages = []; publish(); res.json({ ok: true }); });

  server.on('upgrade', (req, socket, head) => {
    if (new URL(req.url, 'http://localhost').pathname !== '/ws') return;
    if (!validOrigin(req)) return socket.destroy();
    ws.handleUpgrade(req, socket, head, client => ws.emit('connection', client, req));
  });
  ws.on('connection', client => {
    client.send(JSON.stringify({ type: 'init', config, model: models.find(model => model.id === config.modelId) || null, stage: state.stage, signal: state.agent.lastSignal }));
    client.on('message', bytes => {
      try {
        const data = JSON.parse(bytes.toString());
        if (data.type !== 'pose') return;
        const pose = {};
        for (const key of ['mouth', 'lookX', 'lookY']) if (typeof data[key] === 'number' && Number.isFinite(data[key])) pose[key] = Math.max(key === 'mouth' ? 0 : -1, Math.min(1, data[key]));
        Object.assign(state.stage, pose); emit({ type: 'pose', ...pose });
      } catch { /* 잘못된 메시지 하나로 아바타 연결을 끊지 않는다. */ }
    });
    client.on('error', () => {});
  });

  if (options.production) {
    app.use(express.static(path.join(projectRoot, 'dist'), { index: false }));
    app.get(['/', '/overlay', '/pet'], (_req, res) => res.sendFile(path.join(projectRoot, 'dist/index.html')));
  } else if (options.frontend !== false) {
    const { createServer: createVite } = await import('vite');
    const vite = await createVite({ root: projectRoot, configFile: path.join(projectRoot, 'vite.config.ts'), server: { middlewareMode: true, hmr: { server } }, appType: 'spa' });
    app.use(vite.middlewares); server.on('close', () => vite.close());
  }
  app.use((error, _req, res, _next) => {
    const status = error.name === 'ZodError' || error instanceof SyntaxError ? 400 : error.status || 502;
    res.status(status).json({ error: error.name === 'ZodError' ? '입력한 설정의 형식이나 범위를 확인해 주세요.' : error.message || '처리 중 오류가 발생했어요.' });
  });
  return {
    app, server, snapshot,
    async listen(port = 4780) { await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, '127.0.0.1', resolve); }); origin = `http://127.0.0.1:${server.address().port}`; return origin; },
    async close() {
      closed = true; clearInterval(heartbeat);
      for (const res of subscribers) res.end();
      for (const client of ws.clients) client.terminate(); ws.close();
      await obs.disconnect(); await writeQueue;
      await new Promise(resolve => server.close(resolve)); server.closeAllConnections();
    },
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const studio = await createStudio({ production: process.argv.includes('--production') });
  const origin = await studio.listen(Number(process.env.STUDIO_PORT || 4780));
  process.stdout.write(`Kasa Studio ${origin}\n`);
  for (const name of ['SIGINT', 'SIGTERM']) process.once(name, async () => { await studio.close(); process.exit(0); });
}
