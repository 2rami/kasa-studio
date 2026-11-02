import { useRef, useState } from 'react';
import { ArrowUpRight, AudioLines, FileUp, Play, Square } from 'lucide-react';
import type { StudioState } from '../types';
import { api } from '../useStudio';
import { Button, ConnectionForm, Field, Range, Section, Status, type Run } from './Controls';

export function VoicePanel({ state, run, compact = false }: { state: StudioState; run: Run; compact?: boolean }) {
  const info = state.rvc.info, connected = state.rvc.connected;
  const active = connected && info?.serverAudioStated === 1;
  const [uploading, setUploading] = useState(false);
  const [performance, setPerformance] = useState<number[] | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);
  const setting = (key: string, value: number | string) => run(() => api('rvc/setting', { key, value }));
  const start = () => run(async () => { await api('rvc/setting', { key: 'enableServerAudio', value: 1 }); await api('rvc/setting', { key: 'serverAudioStated', value: 1 }); });
  const upload = async (files: FileList | null) => {
    if (!files?.length) return;
    const occupied = new Set((info?.modelSlots || []).filter(slot => slot.modelFile).map(slot => Number(slot.slotIndex)));
    let slot = 0; while (occupied.has(slot)) slot++;
    setUploading(true);
    await run(async () => { const form = new FormData(); for (const file of files) form.append('files', file); form.append('slot', String(slot)); await api('rvc/models', undefined, { method: 'POST', body: form }); }, '음성 모델을 가져왔어요.');
    setUploading(false); if (fileInput.current) fileInput.current.value = '';
  };
  return <div className={`voice-panel ${compact ? 'compact' : ''}`}>
    {!compact && <div className="audio-route"><span><AudioLines size={22} />마이크</span><i /><span className="route-engine">RVC</span><i /><span>가상 오디오</span></div>}
    {!connected && <><ConnectionForm kind="rvc" state={state} run={run} /><div className="help-note"><p>w-okada Voice Changer의 서버를 먼저 실행해 주세요. 서버 오디오가 지원되는 MMVCServerSIO를 연결해요.</p><a href="https://github.com/w-okada/voice-changer/releases" target="_blank" rel="noreferrer">Voice Changer 받기 <ArrowUpRight size={14} /></a></div></>}
    <Section title="음성 모델" action={<Status active={!!active}>{active ? '변환 중' : '대기'}</Status>}>
      <Field label="RVC 모델"><select aria-label="RVC 모델" disabled={!connected || !!active} value={info?.modelSlotIndex ?? -1} onChange={e => setting('modelSlotIndex', Number(e.target.value))}>
        <option value={-1}>음성 모델 선택</option>{info?.modelSlots.filter(slot => slot.modelFile && slot.voiceChangerType === 'RVC').map(slot => <option key={slot.slotIndex} value={slot.slotIndex}>{slot.name || slot.modelFile}</option>)}
      </select></Field>
      <input ref={fileInput} type="file" accept=".pth,.onnx,.index" multiple hidden onChange={e => upload(e.target.files)} />
      <Button className="full-width" disabled={!connected || uploading || !!active} onClick={() => fileInput.current?.click()}><FileUp size={15} />{uploading ? '음성 모델 전송 중' : '.pth / .onnx 모델 가져오기'}</Button>
      {!compact && <p className="field-hint">.index 파일도 함께 선택할 수 있어요. 빈 슬롯에 추가해요.</p>}
    </Section>
    <Section title="목소리 조절">
      <Range label="음높이" value={info?.tran ?? 0} min={-24} max={24} unit=" st" disabled={!connected} onCommit={value => setting('tran', value)} />
      <Range label="음색 혼합" value={info?.indexRatio ?? 0} min={0} max={1} step={0.05} disabled={!connected} onCommit={value => setting('indexRatio', value)} />
      <Range label="자음 보호" value={info?.protect ?? 0.5} min={0} max={0.5} step={0.05} disabled={!connected} onCommit={value => setting('protect', value)} />
      <Field label="음높이 추출"><select value={info?.f0Detector || 'rmvpe'} disabled={!connected} onChange={e => setting('f0Detector', e.target.value)}><option value="rmvpe">RMVPE</option><option value="crepe">CREPE</option><option value="harvest">Harvest</option><option value="dio">DIO</option></select></Field>
    </Section>
    <Section title="오디오 경로">
      <Field label="입력 마이크"><select aria-label="RVC 입력 마이크" value={info?.serverInputDeviceId ?? -1} disabled={!connected || !!active} onChange={e => setting('serverInputDeviceId', Number(e.target.value))}><option value={-1}>마이크 선택</option>{info?.serverAudioInputDevices?.map(device => <option key={`${device.index}-${device.name}`} value={device.index}>{device.name}</option>)}</select></Field>
      <Field label="변환된 소리 출력" hint="OBS 마이크 입력에는 같은 가상 장치를 선택해 주세요."><select aria-label="RVC 출력 장치" value={info?.serverOutputDeviceId ?? -1} disabled={!connected || !!active} onChange={e => setting('serverOutputDeviceId', Number(e.target.value))}><option value={-1}>BlackHole / 가상 케이블 선택</option>{info?.serverAudioOutputDevices?.map(device => <option key={`${device.index}-${device.name}`} value={device.index}>{device.name}</option>)}</select></Field>
      {!compact && <>
        <Field label="모니터링 출력"><select value={info?.serverMonitorDeviceId ?? -1} disabled={!connected || !!active} onChange={e => setting('serverMonitorDeviceId', Number(e.target.value))}><option value={-1}>모니터링 끄기</option>{info?.serverAudioOutputDevices?.map(device => <option key={`${device.index}-${device.name}`} value={device.index}>{device.name}</option>)}</select></Field>
        <div className="field-pair"><Field label="샘플레이트"><select disabled={!connected || !!active} value={info?.serverAudioSampleRate || 48000} onChange={e => setting('serverAudioSampleRate', Number(e.target.value))}><option value={44100}>44,100 Hz</option><option value={48000}>48,000 Hz</option></select></Field><Field label="처리 블록"><select disabled={!connected || !!active} value={info?.serverReadChunkSize || 256} onChange={e => setting('serverReadChunkSize', Number(e.target.value))}>{[128, 256, 512, 1024].map(n => <option key={n} value={n}>{n}</option>)}</select></Field></div>
      </>}
      <Button variant={active ? 'danger' : 'primary'} className="full-width" disabled={!connected || uploading} onClick={() => active ? setting('serverAudioStated', 0) : start()}>{active ? <Square size={14} /> : <Play size={14} />}{active ? '음성 변환 중지' : '음성 변환 시작'}</Button>
      {!compact && <div className="route-links"><a href="https://existential.audio/blackhole/" target="_blank" rel="noreferrer">Mac 가상 오디오 <ArrowUpRight size={13} /></a><a href="https://vb-audio.com/Cable/" target="_blank" rel="noreferrer">Windows 가상 오디오 <ArrowUpRight size={13} /></a></div>}
    </Section>
    {connected && !compact && <Section title="처리 상태" action={<button className="text-button" onClick={() => run(async () => { const result = await api<number[] | { performance?: number[] }>('rvc/performance'); setPerformance(Array.isArray(result) ? result : result.performance || null); })}>상태 확인</button>}>
      <p className="muted">{performance?.length ? `서버 측정 ${performance.map(value => `${Number(value).toFixed(1)} ms`).join(' / ')}` : '서버에서 처리 시간을 가져올 수 있어요.'}</p>
    </Section>}
  </div>;
}
