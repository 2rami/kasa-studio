import path from 'node:path';
import { promises as fs } from 'node:fs';
import { randomUUID } from 'node:crypto';
import { unzipSync } from 'fflate';

const allowedExtension = /\.(json|moc3|png|jpe?g|webp|wav|mp3|ogg)$/i;
export function safeEntry(name) {
  return typeof name === 'string' && name.length < 400 && !name.includes('\\') && !name.includes('\0') && !path.posix.isAbsolute(name) && !name.split('/').some(part => part === '..' || part.startsWith('.')) && !name.includes(':');
}

export async function inspectModel(directory, entry, id, displayName) {
  if (!safeEntry(entry) || !entry.endsWith('.model3.json')) throw new Error('model3.json 파일이 필요해요.');
  const modelFile = path.join(directory, entry);
  const manifest = JSON.parse(await fs.readFile(modelFile, 'utf8'));
  const refs = manifest.FileReferences;
  if (!refs?.Moc || !Array.isArray(refs.Textures) || !refs.Textures.length) throw new Error('모델 본체와 텍스처 참조가 필요해요.');
  const base = path.dirname(modelFile);
  const files = [refs.Moc, ...refs.Textures, refs.Physics, refs.Pose, ...(refs.Expressions || []).map(item => item.File), ...Object.values(refs.Motions || {}).flat().map(item => item.File)].filter(Boolean);
  for (const file of files) {
    if (!safeEntry(file)) throw new Error('모델에 허용되지 않는 파일 경로가 있어요.');
    const resolved = path.resolve(base, file);
    if (!resolved.startsWith(path.resolve(directory) + path.sep)) throw new Error('모델 폴더 밖의 파일은 불러올 수 없어요.');
    if (!(await fs.stat(resolved).catch(() => null))?.isFile()) throw new Error(`참조 파일이 없어요: ${file}`);
  }
  return { id, name: displayName || path.basename(entry, '.model3.json'), entry, url: `/models/${id}/${entry.split('/').map(encodeURIComponent).join('/')}`, expressions: (refs.Expressions || []).map(item => item.Name), motions: Object.keys(refs.Motions || {}), kind: 'Live2D' };
}

export async function importModelZip(bytes, modelsRoot, name) {
  let total = 0;
  const unpacked = unzipSync(bytes, { filter(file) {
    if (file.name.endsWith('/') || file.name.startsWith('__MACOSX/')) return false;
    if (!safeEntry(file.name)) throw new Error('ZIP 안의 파일 경로를 확인해 주세요.');
    if (!allowedExtension.test(file.name)) return false;
    total += file.originalSize;
    if (total > 256 * 1024 * 1024 || file.originalSize > 128 * 1024 * 1024) throw new Error('압축을 푼 모델은 256MB 이하여야 해요.');
    return true;
  } });
  const entries = Object.keys(unpacked).filter(file => file.endsWith('.model3.json'));
  if (entries.length !== 1) throw new Error('ZIP에는 model3.json 파일을 하나만 넣어 주세요.');
  const id = randomUUID();
  const directory = path.join(modelsRoot, id);
  await fs.mkdir(directory, { recursive: true });
  try {
    for (const [file, buffer] of Object.entries(unpacked)) {
      const target = path.join(directory, file);
      await fs.mkdir(path.dirname(target), { recursive: true });
      await fs.writeFile(target, buffer);
    }
    const model = await inspectModel(directory, entries[0], id, name);
    await fs.writeFile(path.join(directory, '.studio.json'), JSON.stringify(model));
    return model;
  } catch (error) {
    await fs.rm(directory, { recursive: true, force: true });
    throw error;
  }
}

export async function listModels(root) {
  const entries = await fs.readdir(root, { withFileTypes: true });
  const models = await Promise.all(entries.filter(entry => entry.isDirectory()).map(async entry => {
    try {
      const meta = JSON.parse(await fs.readFile(path.join(root, entry.name, '.studio.json'), 'utf8'));
      return await inspectModel(path.join(root, entry.name), meta.entry, entry.name, meta.name);
    } catch { return null; }
  }));
  return models.filter(Boolean);
}
