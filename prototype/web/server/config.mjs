import { z } from 'zod';

export const configSchema = z.object({
  theme: z.enum(['personal', 'night']).default('personal'),
  mode: z.enum(['studio', 'agent', 'rvc']).default('studio'),
  onboardingCompleted: z.boolean().default(false),
  modelId: z.string().max(100).default(''),
  avatar: z.object({
    scale: z.number().min(0.3).max(2).default(1),
    x: z.number().min(-0.7).max(0.7).default(0),
    y: z.number().min(-0.5).max(0.5).default(0),
    gaze: z.boolean().default(true),
    background: z.enum(['studio', 'transparent', 'green']).default('studio'),
  }).default(() => ({ scale: 1, x: 0, y: 0, gaze: true, background: 'studio' })),
  obs: z.object({ url: z.url().default('ws://127.0.0.1:4455') }).default(() => ({ url: 'ws://127.0.0.1:4455' })),
  rvc: z.object({ url: z.url().default('http://127.0.0.1:18888') }).default(() => ({ url: 'http://127.0.0.1:18888' })),
  agent: z.object({
    endpoint: z.string().max(2000).default('http://127.0.0.1:11434/v1'),
    model: z.string().max(200).default(''),
    name: z.string().min(1).max(40).default('방송 파트너'),
    prompt: z.string().max(12000).default('방송을 함께하는 차분한 파트너입니다. 한국어로 짧고 자연스럽게 대답하세요.'),
    speak: z.boolean().default(false),
  }).default(() => ({ endpoint: 'http://127.0.0.1:11434/v1', model: '', name: '방송 파트너', prompt: '방송을 함께하는 차분한 파트너입니다. 한국어로 짧고 자연스럽게 대답하세요.', speak: false })),
});

export function mergeConfig(current, patch) {
  const merged = { ...current, ...patch };
  for (const key of ['avatar', 'obs', 'rvc', 'agent']) merged[key] = { ...current[key], ...patch[key] };
  return configSchema.parse(merged);
}

export function serviceUrl(raw, protocol = 'http') {
  const url = new URL(raw);
  const allowed = protocol === 'ws' ? ['ws:', 'wss:'] : ['http:', 'https:'];
  if (!allowed.includes(url.protocol) || url.username || url.password) throw new Error('주소 형식을 확인해 주세요.');
  return url.toString().replace(/\/$/, '');
}

export const signalSchema = z.object({
  text: z.string().max(2000).default(''),
  emotion: z.enum(['neutral', 'smile', 'surprise', 'sad', 'angry', 'shy', 'wink']).default('neutral'),
  motion: z.enum(['', 'Nod', 'Shake', 'Blink']).default(''),
  duration: z.number().min(1).max(60).default(8),
});
