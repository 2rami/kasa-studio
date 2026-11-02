import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { inspectModel } from '../server/models.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const core = path.join(root, 'public/vendor/live2dcubismcore.min.js');
await fs.mkdir(path.dirname(core), { recursive: true });
if (!(await fs.stat(core).catch(() => null))) {
  const response = await fetch('https://cubism.live2d.com/sdk-web/cubismcore/live2dcubismcore.min.js');
  if (!response.ok) throw new Error('Live2D Core를 내려받을 수 없어요.');
  await fs.writeFile(core, Buffer.from(await response.arrayBuffer()));
}
const source = process.argv[2] || path.resolve(root, '../../l2dforge/out/hoodiecat_fb16e3_nowedge');
const destination = path.join(root, 'data/models/hoodiecat');
if (await fs.stat(source).catch(() => null)) {
  await fs.mkdir(destination, { recursive: true });
  const allowed = /\.(model3\.json|moc3|physics3\.json|exp3\.json|motion3\.json|png)$/;
  await fs.cp(source, destination, { recursive: true, filter: async file => (await fs.stat(file)).isDirectory() || allowed.test(file) });
  const model = await inspectModel(destination, 'hoodiecat.model3.json', 'hoodiecat', '후드캣');
  await fs.writeFile(path.join(destination, '.studio.json'), JSON.stringify(model, null, 2));
  process.stdout.write('후드캣 Live2D 모델 준비 완료\n');
} else process.stdout.write('기본 모델 없음: 작업실에서 Live2D ZIP을 가져오세요.\n');
