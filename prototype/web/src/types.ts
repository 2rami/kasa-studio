export type Mode = 'studio' | 'agent' | 'rvc';
export type Emotion = 'neutral' | 'smile' | 'surprise' | 'sad' | 'angry' | 'shy' | 'wink';
export type Page = 'studio' | 'models' | 'voice' | 'agent' | 'connections';
export interface AvatarModel { id: string; name: string; url: string; expressions: string[]; motions: string[]; kind: string }
export interface Config {
  theme: 'personal' | 'night'; mode: Mode; onboardingCompleted: boolean; modelId: string;
  avatar: { scale: number; x: number; y: number; gaze: boolean; background: 'studio' | 'transparent' | 'green' };
  obs: { url: string }; rvc: { url: string };
  agent: { endpoint: string; model: string; name: string; prompt: string; speak: boolean };
}
export interface Signal { id: string; at: number; text: string; emotion: Emotion; motion: string; duration: number }
export interface Pose { mouth: number; lookX: number; lookY: number; emotion: string; visible: boolean }
export interface RvcDevice { index: number; name: string; hostAPI?: string }
export interface RvcSlot { slotIndex: number; name: string; modelFile?: string; voiceChangerType: string; iconFile?: string }
export interface RvcInfo {
  modelSlotIndex: number; modelSlots: RvcSlot[];
  serverAudioInputDevices?: RvcDevice[]; serverAudioOutputDevices?: RvcDevice[];
  enableServerAudio: number; serverAudioStated: number;
  serverInputDeviceId: number; serverOutputDeviceId: number; serverMonitorDeviceId: number;
  serverReadChunkSize: number; serverAudioSampleRate: number;
  tran?: number; indexRatio?: number; protect?: number; f0Detector?: string;
}
export interface StudioState {
  config: Config; models: AvatarModel[]; origin: string; desktop: boolean; overlayUrl: string;
  obs: { connected: boolean; scenes: { name: string; index: number }[]; inputs: { name: string; volume: number; muted: boolean }[]; currentScene: string; streaming: boolean; recording: boolean; error: string };
  rvc: { connected: boolean; info: RvcInfo | null; error: string };
  agent: { busy: boolean; configured: boolean; models: string[]; messages: { role: string; content: string }[]; lastSignal: Signal | null };
  stage: Pose; events: { id: string; time: number; text: string; level: string }[];
}
declare global {
  interface Window { studioDesktop?: { pet: (action: 'open' | 'close' | 'clickThrough') => Promise<{ open: boolean }>; show: () => Promise<void> }; PIXI: unknown }
}
