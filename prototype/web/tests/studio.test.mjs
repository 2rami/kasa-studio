import { test } from 'node:test';
import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import express from 'express';
import multer from 'multer';
import { createServer } from 'node:http';
import { WebSocketServer, WebSocket } from 'ws';
import { zipSync, strToU8 } from 'fflate';
import { createStudio } from '../server/index.mjs';
import { importModelZip, safeEntry } from '../server/models.mjs';

async function fixture(t) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'kasa-studio-test-'));
  const studio = await createStudio({ dataDir: directory, frontend: false });
  const origin = await studio.listen(0);
  t.after(async () => { await studio.close(); await fs.rm(directory, { recursive: true }); });
  const request = async (url, data, headers = {}) => {
    const response = await fetch(`${origin}/api/${url}`, { ...(data === undefined ? {} : { method: 'POST', headers: { 'Content-Type': 'application/json', ...headers }, body: JSON.stringify(data) }) });
    return { status: response.status, data: await response.json() };
  };
  return { directory, studio, origin, request };
}

test('설정 부분 저장, 범위 검사와 재시작 복구', async t => {
  const { directory, request } = await fixture(t);
  const initial = await request('state');
  assert.equal(initial.data.config.mode, 'studio');
  assert.equal(initial.data.obs.connected, false);
  const saved = await request('config', { avatar: { scale: 1.3 }, agent: { name: '테스트 파트너', apiKey: 'never-store-me' } });
  assert.equal(saved.data.config.avatar.scale, 1.3);
  assert.equal(saved.data.config.avatar.gaze, true);
  assert.equal(saved.data.config.agent.name, '테스트 파트너');
  assert.equal((await request('config', { avatar: { scale: 99 } })).status, 400);
  const persisted = await fs.readFile(path.join(directory, 'settings.json'), 'utf8');
  assert.equal(JSON.parse(persisted).avatar.scale, 1.3);
  assert.equal(persisted.includes('never-store-me'), false);
  assert.equal((await request('obs/action', { action: 'recordStart' })).status, 409);
});

test('외부 사이트 접근 거부와 에이전트 토큰 검사', async t => {
  const { origin, request } = await fixture(t);
  const denied = await fetch(`${origin}/api/config`, { method: 'POST', headers: { Origin: 'https://example.com', 'Content-Type': 'application/json' }, body: '{"mode":"agent"}' });
  assert.equal(denied.status, 403);
  assert.equal((await request('agent/signal', { text: '인증 없음' })).status, 401);
  const { data: { token } } = await request('agent/token');
  const sent = await request('agent/signal', { text: '표정 신호', emotion: 'smile', motion: 'Nod' }, { 'X-Studio-Token': token });
  assert.equal(sent.status, 200);
  assert.equal(sent.data.emotion, 'smile');
  assert.equal((await request('state')).data.agent.lastSignal.text, '표정 신호');
  assert.equal(JSON.stringify((await request('state')).data).includes(token), false);
});

test('아바타 WebSocket의 입·시선 동기화와 범위 제한', async t => {
  const { origin } = await fixture(t);
  const a = new WebSocket(origin.replace('http', 'ws') + '/ws');
  const b = new WebSocket(origin.replace('http', 'ws') + '/ws');
  t.after(() => { a.terminate(); b.terminate(); });
  await Promise.all([new Promise(resolve => a.once('open', resolve)), new Promise(resolve => b.once('open', resolve))]);
  const received = new Promise(resolve => b.on('message', bytes => { const data = JSON.parse(bytes); if (data.type === 'pose') resolve(data); }));
  a.send(JSON.stringify({ type: 'pose', mouth: 9, lookX: -5, lookY: 0.25 }));
  const pose = await received;
  assert.equal(pose.mouth, 1); assert.equal(pose.lookX, -1); assert.equal(pose.lookY, 0.25);
});

test('모델 ZIP의 참조 파일, 경로 탈출과 중복 명세 검사', async t => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'kasa-studio-test-model-'));
  t.after(() => fs.rm(directory, { recursive: true }));
  assert.equal(safeEntry('../secret'), false); assert.equal(safeEntry('a/../../secret'), false); assert.equal(safeEntry('/etc/test'), false);
  const manifest = { Version: 3, FileReferences: { Moc: 'avatar.moc3', Textures: ['textures/a.png'] } };
  const good = zipSync({ 'avatar/avatar.model3.json': strToU8(JSON.stringify(manifest)), 'avatar/avatar.moc3': new Uint8Array([1, 2]), 'avatar/textures/a.png': new Uint8Array([3, 4]) });
  const result = await importModelZip(good, directory, '테스트 모델');
  assert.equal(result.name, '테스트 모델'); assert.match(result.url, /avatar\/avatar.model3.json$/);
  const missing = zipSync({ 'avatar.model3.json': strToU8(JSON.stringify(manifest)) });
  await assert.rejects(importModelZip(missing, directory), /참조 파일/);
  const escape = zipSync({ '../escape.model3.json': strToU8('{}') });
  await assert.rejects(importModelZip(escape, directory), /파일 경로/);
  const duplicate = zipSync({ 'a.model3.json': strToU8('{}'), 'b.model3.json': strToU8('{}') });
  await assert.rejects(importModelZip(duplicate, directory), /하나만/);
});

async function mockHttp(t, app) {
  const server = createServer(app);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => new Promise(resolve => { server.close(resolve); server.closeAllConnections(); }));
  return `http://127.0.0.1:${server.address().port}`;
}

test('w-okada 연결·실제 폼 필드·기기 선택·변환 시작 및 중지', async t => {
  const app = express(), form = multer();
  const info = { status: 'OK', modelSlotIndex: 0, modelSlots: [{ slotIndex: 0, name: '음성', modelFile: 'voice.pth', voiceChangerType: 'RVC' }], enableServerAudio: 0, serverAudioStated: 0, serverInputDeviceId: -1, serverOutputDeviceId: -1, serverAudioInputDevices: [{ index: 3, name: 'Test Mic' }], serverAudioOutputDevices: [{ index: 7, name: 'Test Cable' }] };
  const changes = [];
  app.get('/info', (_req, res) => res.json(info));
  app.post('/update_settings', form.none(), (req, res) => { changes.push(req.body); info[req.body.key] = Number(req.body.val); res.json(info); });
  const url = await mockHttp(t, app);
  const { request } = await fixture(t);
  await request('config', { rvc: { url } });
  assert.equal((await request('rvc/connect', {})).data.rvc.connected, true);
  assert.equal((await request('rvc/setting', { key: 'serverAudioStated', value: 1 })).status, 409);
  await request('rvc/setting', { key: 'serverInputDeviceId', value: 3 });
  await request('rvc/setting', { key: 'serverOutputDeviceId', value: 7 });
  await request('rvc/setting', { key: 'enableServerAudio', value: 1 });
  const started = await request('rvc/setting', { key: 'serverAudioStated', value: 1 });
  assert.equal(started.data.rvc.info.serverAudioStated, 1);
  assert.equal(changes.at(-1).key, 'serverAudioStated'); assert.equal(changes.at(-1).val, '1');
  assert.equal((await request('config', { rvc: { url: 'http://127.0.0.1:19999' } })).status, 409);
  await request('rvc/setting', { key: 'serverAudioStated', value: 0 });
  assert.equal((await request('rvc/setting', { key: 'tran', value: 100 })).status, 502);
  assert.equal((await request('state')).data.rvc.info.serverAudioStated, 0);
});

test('RVC 모델의 분할 업로드·합치기·슬롯 등록과 기존 슬롯 보호', async t => {
  const app = express(), form = multer();
  const info = { status: 'OK', enableServerAudio: 0, modelSlots: [] };
  const chunks = [], concats = [];
  let loaded;
  app.get('/info', (_req, res) => res.json(info));
  app.post('/upload_file', form.single('file'), (req, res) => { chunks.push({ name: req.body.filename, size: req.file.size }); res.json({ status: 'OK' }); });
  app.post('/concat_uploaded_file', form.none(), (req, res) => { concats.push(req.body); res.json({ status: 'OK' }); });
  app.post('/load_model', form.none(), (req, res) => {
    loaded = JSON.parse(req.body.params);
    assert.equal(req.body.isHalf, 'false');
    info.modelSlots = [{ slotIndex: loaded.slot, name: 'voice', modelFile: 'voice.pth', voiceChangerType: 'RVC' }];
    res.json({ status: 'OK' });
  });
  const url = await mockHttp(t, app);
  const { origin, request } = await fixture(t);
  await request('config', { rvc: { url } }); await request('rvc/connect', {});
  const body = new FormData(); body.set('slot', '0');
  body.append('files', new Blob([new Uint8Array(1024 * 1024 + 20)]), 'voice.pth');
  body.append('files', new Blob([new Uint8Array(10)]), 'voice.index');
  const response = await fetch(`${origin}/api/rvc/models`, { method: 'POST', body });
  assert.equal(response.status, 200);
  assert.deepEqual(chunks.map(chunk => chunk.name), ['voice.pth_0', 'voice.pth_1', 'voice.index_0']);
  assert.equal(chunks[1].size, 20); assert.equal(concats[0].filenameChunkNum, '2');
  assert.equal(loaded.slot, 0); assert.equal(loaded.voiceChangerType, 'RVC');
  assert.deepEqual(loaded.files.map(file => file.kind), ['rvcModel', 'rvcIndex']);
  const repeated = new FormData(); repeated.set('slot', '0'); repeated.append('files', new Blob([new Uint8Array(1)]), 'voice.pth');
  assert.equal((await fetch(`${origin}/api/rvc/models`, { method: 'POST', body: repeated })).status, 409);
});

test('에이전트 호환 API 모델 조회·대화·표정 전달과 실패 시 대화 보존', async t => {
  const app = express(); app.use(express.json());
  app.get('/v1/models', (_req, res) => res.json({ data: [{ id: 'test-partner' }] }));
  let fail = false;
  app.post('/v1/chat/completions', (req, res) => {
    assert.equal(req.body.model, 'test-partner');
    assert.equal(req.headers.authorization, 'Bearer test-key');
    if (fail) return res.status(503).json({ error: 'busy' });
    assert.equal(req.body.messages.at(-1).content, '안녕하세요');
    res.json({ choices: [{ message: { content: JSON.stringify({ text: '반가워요.', emotion: 'smile', motion: 'Nod' }) } }] });
  });
  const url = await mockHttp(t, app);
  const { request, directory } = await fixture(t);
  await request('config', { agent: { endpoint: `${url}/v1` } });
  await request('agent/key', { key: 'test-key' });
  assert.equal((await request('agent/probe', {})).data.config.agent.model, 'test-partner');
  const reply = await request('agent/chat', { text: '안녕하세요' });
  assert.equal(reply.data.text, '반가워요.'); assert.equal(reply.data.emotion, 'smile');
  assert.equal((await request('state')).data.agent.messages.length, 2);
  fail = true;
  assert.equal((await request('agent/chat', { text: '다음 대화' })).status, 502);
  const after = (await request('state')).data;
  assert.equal(after.agent.messages.length, 2); assert.equal(after.agent.busy, false);
  assert.equal((await fs.readFile(path.join(directory, 'settings.json'), 'utf8')).includes('test-key'), false);
});

test('OBS v5 핸드셰이크·장면·오디오·아바타 브라우저 소스와 녹화 제어', async t => {
  const mock = new WebSocketServer({ port: 0, host: '127.0.0.1' });
  await new Promise(resolve => mock.once('listening', resolve));
  t.after(() => new Promise(resolve => { for (const client of mock.clients) client.terminate(); mock.close(resolve); }));
  const calls = []; let currentScene = '대화', recording = false;
  mock.on('connection', client => {
    client.send(JSON.stringify({ op: 0, d: { obsWebSocketVersion: '5.0.0', rpcVersion: 1 } }));
    client.on('message', raw => {
      const data = JSON.parse(raw);
      if (data.op === 1) return client.send(JSON.stringify({ op: 2, d: { negotiatedRpcVersion: 1 } }));
      const { requestType, requestId, requestData } = data.d; calls.push({ requestType, requestData });
      const results = {
        GetSceneList: { currentProgramSceneName: currentScene, scenes: [{ sceneName: '대화', sceneIndex: 0 }, { sceneName: '게임', sceneIndex: 1 }] },
        GetInputList: { inputs: [{ inputName: '마이크' }] },
        GetInputVolume: { inputVolumeDb: -12 }, GetInputMute: { inputMuted: false },
        GetStreamStatus: { outputActive: false }, GetRecordStatus: { outputActive: recording },
      };
      if (requestType === 'SetCurrentProgramScene') currentScene = requestData.sceneName;
      if (requestType === 'StartRecord') recording = true;
      if (requestType === 'StopRecord') recording = false;
      client.send(JSON.stringify({ op: 7, d: { requestType, requestId, requestStatus: { result: true, code: 100 }, responseData: results[requestType] || {} } }));
    });
  });
  const { origin, request } = await fixture(t);
  await request('config', { obs: { url: `ws://127.0.0.1:${mock.address().port}` } });
  const connected = await request('obs/connect', { password: '' });
  assert.equal(connected.data.obs.connected, true); assert.equal(connected.data.obs.inputs[0].volume, -12);
  assert.equal((await request('obs/action', { action: 'scene', name: '게임' })).data.obs.currentScene, '게임');
  await request('obs/action', { action: 'addAvatar' });
  assert.equal(calls.find(call => call.requestType === 'CreateInput').requestData.inputSettings.url, `${origin}/overlay`);
  assert.equal((await request('obs/action', { action: 'recordStart' })).data.obs.recording, true);
  assert.equal((await request('obs/action', { action: 'recordStop' })).data.obs.recording, false);
  assert.equal(calls.some(call => call.requestType === 'StartStream'), false);
});
