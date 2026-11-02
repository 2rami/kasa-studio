import { useCallback, useEffect, useRef, useState } from 'react';
import type { Pose, Signal, StudioState } from './types';

export async function api<T = StudioState>(endpoint: string, body?: unknown, init?: RequestInit): Promise<T> {
  const response = await fetch(`/api/${endpoint}`, init || (body === undefined ? {} : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }));
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '요청을 처리할 수 없어요.');
  return data;
}

export function useStudio() {
  const [state, setState] = useState<StudioState | null>(null);
  const [online, setOnline] = useState(false);
  useEffect(() => {
    const source = new EventSource('/api/events');
    source.onmessage = event => { setState(JSON.parse(event.data)); setOnline(true); };
    source.onerror = () => setOnline(false);
    return () => source.close();
  }, []);
  return { state, online };
}

export function useStageWire() {
  const pose = useRef<Pose>({ mouth: 0, lookX: 0, lookY: 0, emotion: 'neutral', visible: true });
  const socket = useRef<WebSocket | null>(null);
  const [signal, setSignal] = useState<Signal | null>(null);
  useEffect(() => {
    let disposed = false, retry: ReturnType<typeof setTimeout>;
    const connect = () => {
      if (disposed) return;
      const client = new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`);
      socket.current = client;
      client.onmessage = event => {
        const data = JSON.parse(event.data);
        if (data.type === 'init') { Object.assign(pose.current, data.stage); setSignal(data.signal); }
        if (data.type === 'pose') Object.assign(pose.current, data);
        if (data.type === 'signal') setSignal(data.signal);
      };
      client.onclose = () => { if (!disposed) retry = setTimeout(connect, 2000); };
      client.onerror = () => client.close();
    };
    connect();
    return () => { disposed = true; clearTimeout(retry); socket.current?.close(); };
  }, []);
  const sendPose = useCallback((next: Partial<Pose>) => {
    Object.assign(pose.current, next);
    if (socket.current?.readyState === WebSocket.OPEN) socket.current.send(JSON.stringify({ type: 'pose', ...next }));
  }, []);
  return { pose, signal, sendPose };
}

export function useMicrophone(sendPose: (pose: Partial<Pose>) => void) {
  const [enabled, setEnabled] = useState(false);
  const [level, setLevel] = useState(0);
  const [devices, setDevices] = useState<MediaDeviceInfo[]>([]);
  const [device, setDevice] = useState('');
  const stream = useRef<MediaStream | null>(null);
  const context = useRef<AudioContext | null>(null);
  const timer = useRef<ReturnType<typeof setInterval> | null>(null);
  const stop = useCallback(() => {
    stream.current?.getTracks().forEach(track => track.stop()); stream.current = null;
    if (timer.current) clearInterval(timer.current);
    context.current?.close(); context.current = null;
    setEnabled(false); setLevel(0); sendPose({ mouth: 0 });
  }, [sendPose]);
  const start = useCallback(async () => {
    stop();
    try {
      const input = await navigator.mediaDevices.getUserMedia({ audio: device ? { deviceId: { exact: device }, echoCancellation: true, noiseSuppression: true } : true });
      stream.current = input;
      const ctx = new AudioContext(); context.current = ctx;
      const source = ctx.createMediaStreamSource(input), analyser = ctx.createAnalyser();
      analyser.fftSize = 512; source.connect(analyser);
      const samples = new Float32Array(analyser.fftSize);
      timer.current = setInterval(() => {
        analyser.getFloatTimeDomainData(samples);
        const rms = Math.sqrt(samples.reduce((sum, v) => sum + v * v, 0) / samples.length);
        const mouth = Math.min(1, Math.max(0, rms - 0.008) * 12);
        setLevel(mouth); sendPose({ mouth });
      }, 65);
      input.getAudioTracks()[0].onended = stop;
      setDevices((await navigator.mediaDevices.enumerateDevices()).filter(item => item.kind === 'audioinput'));
      setEnabled(true);
    } catch (error) { stop(); throw error; }
  }, [device, sendPose, stop]);
  useEffect(() => stop, [stop]);
  return { enabled, level, devices, device, setDevice, start, stop };
}
