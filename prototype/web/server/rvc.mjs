import { serviceUrl } from './config.mjs';

export const rvcRules = {
  modelSlotIndex: [0, 999], tran: [-24, 24], indexRatio: [0, 1], protect: [0, 0.5],
  serverInputDeviceId: [-1, 10000], serverOutputDeviceId: [-1, 10000], serverMonitorDeviceId: [-1, 10000],
  serverReadChunkSize: [64, 4096], serverAudioSampleRate: [16000, 96000],
  serverInputAudioGain: [0, 3], serverOutputAudioGain: [0, 3], enableServerAudio: [0, 1], serverAudioStated: [0, 1],
};
export function validateRvcSetting(key, value) {
  if (key === 'f0Detector' && ['rmvpe', 'crepe', 'harvest', 'dio'].includes(value)) return;
  const range = rvcRules[key];
  if (!range || typeof value !== 'number' || !Number.isFinite(value) || value < range[0] || value > range[1]) throw new Error('허용되지 않는 음성 설정이에요.');
}

export async function rvcRequest(base, endpoint, form) {
  const response = await fetch(`${serviceUrl(base)}${endpoint}`, {
    ...(form ? { method: 'POST', body: form } : {}),
    signal: AbortSignal.timeout(endpoint === '/load_model' ? 120000 : 12000),
  });
  if (!response.ok) throw new Error(`Voice Changer 응답 오류 (${response.status})`);
  const data = await response.json();
  if (!data || data.status === 'ERROR' || data.error) throw new Error(data?.msg || data?.error || 'Voice Changer 응답을 확인해 주세요.');
  return data;
}

export async function updateRvc(base, key, value) {
  validateRvcSetting(key, value);
  const form = new FormData();
  form.set('key', key); form.set('val', String(value));
  return rvcRequest(base, '/update_settings', form);
}

export async function uploadRvc(base, files, slot) {
  if (!Number.isInteger(slot) || slot < 0 || slot > 999) throw new Error('슬롯 번호를 확인해 주세요.');
  const models = files.filter(file => /\.(pth|onnx)$/i.test(file.originalname));
  if (models.length !== 1 || files.some(file => !/\.(pth|onnx|index)$/i.test(file.originalname)) || files.filter(file => /\.index$/i.test(file.originalname)).length > 1) throw new Error('모델 .pth 또는 .onnx 하나와 선택한 .index 파일을 넣어 주세요.');
  const refs = [];
  for (const file of files) {
    const name = file.originalname.replace(/[^a-zA-Z0-9_.-]/g, '_');
    const chunkSize = 1024 * 1024;
    const count = Math.ceil(file.buffer.length / chunkSize);
    for (let i = 0; i < count; i++) {
      const form = new FormData();
      form.set('file', new Blob([file.buffer.subarray(i * chunkSize, (i + 1) * chunkSize)]), `${name}_${i}`);
      form.set('filename', `${name}_${i}`);
      await rvcRequest(base, '/upload_file', form);
    }
    const concat = new FormData(); concat.set('filename', name); concat.set('filenameChunkNum', String(count));
    await rvcRequest(base, '/concat_uploaded_file', concat);
    refs.push({ name, dir: '', kind: /\.index$/i.test(name) ? 'rvcIndex' : 'rvcModel' });
  }
  const load = new FormData();
  load.set('slot', String(slot)); load.set('isHalf', 'false');
  load.set('params', JSON.stringify({ voiceChangerType: 'RVC', slot, isSampleMode: false, sampleId: '', files: refs, params: {} }));
  await rvcRequest(base, '/load_model', load);
  return rvcRequest(base, '/info');
}
