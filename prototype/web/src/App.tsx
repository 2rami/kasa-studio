import { useCallback, useEffect, useRef, useState } from 'react';
import { Activity, ArrowUpRight, AudioLines, Check, ChevronDown, ChevronRight, Circle, Clipboard, ExternalLink, FileUp, Focus, HelpCircle, LayoutDashboard, Link2, Maximize, Mic, MicOff, Moon, MoreHorizontal, MousePointer2, PanelRightClose, PanelRightOpen, PawPrint, Plus, Radio, RotateCcw, Settings2, Smile, Sparkles, Square, Sun, Volume2, VolumeX, X } from 'lucide-react';
import { Avatar } from './components/Avatar';
import { AgentChat, AgentSettings } from './components/AgentPanel';
import { VoicePanel } from './components/VoicePanel';
import { Onboarding } from './components/Onboarding';
import { Button, ConnectionForm, Field, Modal, ModeSelector, Range, Section, Status, Toggle, type Run } from './components/Controls';
import { api, useMicrophone, useStageWire, useStudio } from './useStudio';
import type { Config, Emotion, Page, StudioState } from './types';

const navigation = [
  { id: 'studio', label: '방송 작업실', Icon: LayoutDashboard },
  { id: 'models', label: '아바타', Icon: Smile },
  { id: 'voice', label: '목소리', Icon: AudioLines },
  { id: 'agent', label: '에이전트', Icon: Sparkles },
  { id: 'connections', label: '연결 설정', Icon: Link2 },
] as const;
const emotions: { id: Emotion; name: string }[] = [{ id: 'neutral', name: '기본' }, { id: 'smile', name: '웃음' }, { id: 'surprise', name: '놀람' }, { id: 'shy', name: '부끄러움' }, { id: 'sad', name: '슬픔' }, { id: 'angry', name: '화남' }, { id: 'wink', name: '윙크' }];

export function App() {
  const { state, online } = useStudio();
  const { pose, signal, sendPose } = useStageWire();
  const microphone = useMicrophone(sendPose);
  const [page, setPage] = useState<Page>('studio');
  const [inspector, setInspector] = useState<'avatar' | 'voice' | 'agent'>('avatar');
  const [inspectorVisible, setInspectorVisible] = useState(() => !matchMedia('(max-width: 700px)').matches);
  const [guide, setGuide] = useState(false), [guideInitial, setGuideInitial] = useState(false);
  const [toast, setToast] = useState<{ text: string; error: boolean } | null>(null);
  const [pending, setPending] = useState(0), [ready, setReady] = useState(false);
  const [preview, setPreview] = useState<'avatar' | 'obs'>('avatar');
  const [obsImage, setObsImage] = useState(''), [previewError, setPreviewError] = useState('');
  const [confirmBroadcast, setConfirmBroadcast] = useState(false);
  const [importing, setImporting] = useState(false), [clock, setClock] = useState(new Date());
  const fileInput = useRef<HTMLInputElement>(null);
  const stageElement = useRef<HTMLDivElement>(null);
  const overlay = location.pathname === '/overlay' || location.pathname === '/pet';
  const pet = location.pathname === '/pet';
  const run: Run = useCallback(async (task, success) => {
    setPending(count => count + 1);
    try { const result = await task(); if (success) setToast({ text: success, error: false }); return result; }
    catch (error) { setToast({ text: error instanceof Error ? error.message : '작업을 처리하지 못했어요.', error: true }); return undefined; }
    finally { setPending(count => count - 1); }
  }, []);
  useEffect(() => {
    const media = matchMedia('(max-width: 700px)');
    const change = () => setInspectorVisible(!media.matches);
    media.addEventListener('change', change);
    return () => media.removeEventListener('change', change);
  }, []);
  useEffect(() => {
    if (state && !guideInitial) { setGuideInitial(true); if (!state.config.onboardingCompleted && !overlay) setGuide(true); }
  }, [state, guideInitial, overlay]);
  useEffect(() => { if (state) { document.documentElement.dataset.theme = state.config.theme; document.body.classList.toggle('overlay-body', overlay); } }, [state?.config.theme, overlay]);
  useEffect(() => { if (state?.config.mode === 'rvc') setInspector('voice'); else if (state?.config.mode === 'agent') setInspector('agent'); else setInspector('avatar'); }, [state?.config.mode]);
  useEffect(() => { if (!toast) return; const timer = setTimeout(() => setToast(null), toast.error ? 9000 : 3500); return () => clearTimeout(timer); }, [toast]);
  useEffect(() => { const timer = setInterval(() => setClock(new Date()), 1000); return () => clearInterval(timer); }, []);
  useEffect(() => {
    if (!signal || Date.now() > signal.at + signal.duration * 1000 || overlay || !state?.config.agent.speak || !signal.text) return;
    const utterance = new SpeechSynthesisUtterance(signal.text); utterance.lang = 'ko-KR'; utterance.rate = 1.03;
    let timer: ReturnType<typeof setInterval>;
    utterance.onstart = () => { timer = setInterval(() => sendPose({ mouth: 0.15 + (Math.sin(performance.now() / 85) + 1) * 0.25 }), 75); };
    const end = () => { clearInterval(timer); sendPose({ mouth: 0 }); };
    utterance.onend = end; utterance.onerror = end;
    speechSynthesis.cancel(); speechSynthesis.speak(utterance);
    return () => { speechSynthesis.cancel(); end(); };
  }, [signal?.id]);
  useEffect(() => {
    if (preview !== 'obs' || !state?.obs.connected || page !== 'studio') return;
    let stopped = false;
    const load = async () => {
      try { const data = await api<{ image: string }>('obs/preview'); if (!stopped) { setObsImage(data.image); setPreviewError(''); } }
      catch (error) { if (!stopped) setPreviewError(error instanceof Error ? error.message : '미리보기를 가져오지 못했어요.'); }
    };
    load(); const timer = setInterval(load, 2000); return () => { stopped = true; clearInterval(timer); };
  }, [preview, state?.obs.connected, state?.obs.currentScene, page]);
  useEffect(() => {
    const hotkey = (event: KeyboardEvent) => {
      if ((event.target as HTMLElement).matches('input, textarea, select') || event.metaKey || event.ctrlKey || event.altKey) return;
      if (event.key.toLowerCase() === 'm') { event.preventDefault(); if (microphone.enabled) microphone.stop(); else run(microphone.start); }
      const index = Number(event.key) - 1;
      if (index >= 0 && index < emotions.length && !guide) run(() => api('stage/signal', { emotion: emotions[index].id, duration: 8 }));
    };
    if (!overlay) window.addEventListener('keydown', hotkey); return () => window.removeEventListener('keydown', hotkey);
  }, [microphone.enabled, microphone.start, guide, overlay, run]);

  if (!state) return <div className="boot-screen"><span className="brand-mark">k.</span><p>작업실에 연결하고 있어요</p>{!online && <span className="spinner" />}</div>;
  const config = state.config;
  const selectedModel = state.models.find(model => model.id === config.modelId);
  const updateAvatar = (patch: Partial<Config['avatar']>) => run(() => api('config', { avatar: patch }));
  const obsAction = (action: string, fields: Record<string, unknown> = {}) => run(() => api('obs/action', { action, ...fields }));
  const importZip = async (files: FileList | null) => {
    const file = files?.[0]; if (!file) return;
    setImporting(true);
    await run(() => api('models/import', undefined, { method: 'POST', headers: { 'Content-Type': 'application/zip' }, body: file }), 'Live2D 모델을 가져왔어요.');
    setImporting(false); if (fileInput.current) fileInput.current.value = '';
  };
  const copy = (text: string, success: string) => run(() => navigator.clipboard.writeText(text), success);
  const activeSignal = signal && clock.getTime() < signal.at + signal.duration * 1000 ? signal : null;
  const avatar = <Avatar model={selectedModel} config={config.avatar} pose={pose} signal={signal} onReady={setReady} />;
  const openPet = () => {
    if (window.studioDesktop) run(() => window.studioDesktop!.pet('open'), '바탕화면에 펫을 띄웠어요.');
    else { window.open(`${state.origin}/pet`, 'kasa-pet', 'width=340,height=560'); setToast({ text: '투명·항상 위 펫은 데스크톱 앱에서 사용할 수 있어요.', error: false }); }
  };
  if (overlay) return <div className={`overlay-root ${pet ? 'pet' : ''}`}>
    {avatar}
    {activeSignal?.text && <div className="pet-bubble" role="status">{activeSignal.text}</div>}
    {pet && <div className="pet-controls"><button title="작업실 열기" onClick={() => window.studioDesktop ? window.studioDesktop.show() : window.open(state.origin, '_blank')}><Settings2 size={17} /></button>{window.studioDesktop && <button title="클릭 통과 (메뉴 막대에서 해제)" onClick={() => window.studioDesktop!.pet('clickThrough')}><MousePointer2 size={17} /></button>}<button title="펫 닫기" onClick={() => window.studioDesktop ? window.studioDesktop.pet('close') : window.close()}><X size={17} /></button></div>}
  </div>;
  return <div className={`app-shell ${window.studioDesktop ? 'desktop' : ''}`}>
    <input ref={fileInput} type="file" accept=".zip" hidden aria-label="Live2D ZIP 가져오기" onChange={e => importZip(e.target.files)} />
    <aside className="sidebar">
      <a className="brand" href="/" onClick={e => { e.preventDefault(); setPage('studio'); }}><span className="brand-mark">k.</span><span>KASA<small>STUDIO</small></span></a>
      <div className="workspace-name"><span className="workspace-initial">K</span><span>내 방송 작업실<small>로컬 워크스페이스</small></span><ChevronDown size={14} /></div>
      <nav className="main-nav" aria-label="작업실 메뉴">{navigation.map(({ id, label, Icon }) => <button key={id} className={page === id ? 'active' : ''} aria-current={page === id ? 'page' : undefined} onClick={() => setPage(id)}><Icon size={18} strokeWidth={1.7} /><span>{label}</span>{page === id && <i />}</button>)}</nav>
      <div className="sidebar-scenes"><div className="sidebar-label">OBS 장면<Status active={state.obs.connected}>{state.obs.connected ? '연결' : '오프라인'}</Status></div>
        {state.obs.scenes.length ? <div className="scene-list">{state.obs.scenes.map((scene, i) => <button key={scene.name} className={state.obs.currentScene === scene.name ? 'active' : ''} onClick={() => { setPage('studio'); obsAction('scene', { name: scene.name }); }}><span className="scene-number">{String(i + 1).padStart(2, '0')}</span><span>{scene.name}</span>{state.obs.currentScene === scene.name && <span className="scene-live" />}</button>)}</div> : <button className="scene-empty" onClick={() => setPage('connections')}><Plus size={16} /><span>OBS 장면 불러오기</span></button>}
      </div>
      <div className="sidebar-bottom"><button className="pet-shortcut" onClick={openPet}><PawPrint size={18} /><span>바탕화면 펫 띄우기</span><ArrowUpRight size={15} /></button><button className="guide-shortcut" onClick={() => setGuide(true)}><HelpCircle size={17} />작업실 설정 가이드<ChevronRight size={14} /></button><div className="sidebar-footer"><Status active={online}>{online ? '이 기기에서 실행 중' : '작업실 연결 끊김'}</Status><span>v0.1</span></div></div>
    </aside>
    <main className="workspace">
      <header className="workspace-header"><div className="heading"><span className="mobile-brand brand-mark">k.</span><h1>{navigation.find(item => item.id === page)?.label}</h1><span className="local-tag">로컬</span></div><div className="header-actions"><button className="icon-button theme-toggle" aria-label={config.theme === 'personal' ? '남색 테마로 전환' : '개인사이트 테마로 전환'} onClick={() => run(() => api('config', { theme: config.theme === 'personal' ? 'night' : 'personal' }))}>{config.theme === 'personal' ? <Moon size={18} /> : <Sun size={18} />}</button><Button disabled={!state.obs.connected || pending > 0} variant={state.obs.streaming ? 'danger' : 'primary'} onClick={() => setConfirmBroadcast(true)}><Radio size={16} /><span>{state.obs.streaming ? '방송 종료' : '방송 시작'}</span></Button></div></header>
      <div className="mode-bar"><ModeSelector config={config} run={run} /><div className="mode-bar-right"><Status active={state.obs.connected}>OBS {state.obs.connected ? '연결됨' : '연결 전'}</Status><span className="divider" /><Status active={ready}>{ready ? '모델 준비됨' : selectedModel ? '모델 로딩' : '모델 없음'}</Status><button className="icon-button" aria-label={inspectorVisible ? '설정 패널 접기' : '설정 패널 펼치기'} onClick={() => setInspectorVisible(!inspectorVisible)}>{inspectorVisible ? <PanelRightClose size={17} /> : <PanelRightOpen size={17} />}</button></div></div>
      <div className={`workarea ${inspectorVisible ? '' : 'inspector-hidden'} page-${page}`}>
        <div className="main-content">
          {page === 'studio' && <>
            <div className="preview-title"><div className="preview-tabs"><button className={preview === 'avatar' ? 'active' : ''} onClick={() => setPreview('avatar')}>아바타 미리보기</button><button className={preview === 'obs' ? 'active' : ''} disabled={!state.obs.connected} onClick={() => setPreview('obs')}>OBS 장면</button></div><span>{preview === 'avatar' ? selectedModel?.name || '모델 없음' : state.obs.currentScene}<MoreHorizontal size={16} /></span></div>
            <div className={`stage-wrap background-${config.avatar.background}`} ref={stageElement} onPointerMove={e => { if (!config.avatar.gaze) return; const r = e.currentTarget.getBoundingClientRect(); sendPose({ lookX: (e.clientX - r.left) / r.width * 2 - 1, lookY: 1 - (e.clientY - r.top) / r.height * 2 }); }} onPointerLeave={() => sendPose({ lookX: 0, lookY: 0 })}>
              {preview === 'avatar' ? <><div className="stage-wordmark" aria-hidden="true">ON<br /><span>AIR.</span></div><div className="stage-dotfield" aria-hidden="true" />{avatar}<div className="stage-corner top-left" /><div className="stage-corner top-right" /><div className="stage-corner bottom-left" /><div className="stage-corner bottom-right" />{activeSignal?.text && <div className="stage-bubble" role="status">{activeSignal.text}</div>}<div className="stage-caption"><span><i className={ready ? 'on' : ''} />{selectedModel?.name || '아바타'}</span><span>LIVE2D</span></div></> : <div className="obs-preview">{obsImage && <img src={obsImage} alt={`OBS ${state.obs.currentScene} 장면`} />}<span className="obs-preview-label">OBS 장면 미리보기 · 2초마다 갱신</span>{previewError && <p role="alert">{previewError}</p>}</div>}
            </div>
            <div className="preview-tools"><div className="tool-group"><button className={`tool-button ${microphone.enabled ? 'active' : ''}`} title="마이크 입 모양 (M)" onClick={() => microphone.enabled ? microphone.stop() : run(microphone.start)}>{microphone.enabled ? <Mic size={17} /> : <MicOff size={17} />}<span>{microphone.enabled ? '마이크 켜짐' : '마이크 꺼짐'}</span></button><div className="mic-meter" aria-label={`마이크 입력 ${Math.round(microphone.level * 100)}%`}>{Array.from({ length: 14 }, (_, i) => <i key={i} className={microphone.enabled && microphone.level * 14 > i ? 'lit' : ''} />)}</div></div><div className="tool-group"><button className="icon-button" title="아바타 위치 초기화" aria-label="아바타 위치 초기화" onClick={() => updateAvatar({ scale: 1, x: 0, y: 0 })}><Focus size={17} /></button><button className="icon-button" title="전체 화면" aria-label="미리보기 전체 화면" onClick={() => run(async () => { if (document.fullscreenElement) await document.exitFullscreen(); else await stageElement.current?.requestFullscreen(); })}><Maximize size={16} /></button><button className="icon-button" title="투명 오버레이 열기" aria-label="투명 오버레이 열기" onClick={() => window.open(state.overlayUrl, '_blank')}><ExternalLink size={16} /></button></div></div>
            <div className="expression-strip"><span>표정</span><div>{emotions.filter(emotion => emotion.id === 'neutral' || selectedModel?.expressions.includes(emotion.id)).map((emotion, i) => <button title={`${emotion.name} (${i + 1})`} aria-pressed={(activeSignal?.emotion || 'neutral') === emotion.id} key={emotion.id} className={(activeSignal?.emotion || 'neutral') === emotion.id ? 'active' : ''} onClick={() => run(() => api('stage/signal', { emotion: emotion.id, duration: 8 }))}>{emotion.name}</button>)}</div><button className="icon-button" aria-label="고개 끄덕이기" title="고개 끄덕이기" disabled={!ready} onClick={() => run(() => api('stage/signal', { motion: 'Nod', duration: 4 }))}><Activity size={17} /></button></div>
            <div className="lower-panels"><section className="mixer-panel"><div className="section-title"><h3>오디오 믹서</h3><span>OBS</span></div>{state.obs.inputs.length ? state.obs.inputs.map(input => <div className="mixer-channel" key={input.name}><div><button className="icon-button" aria-label={`${input.name} ${input.muted ? '음소거 해제' : '음소거'}`} onClick={() => obsAction('mute', { name: input.name, value: !input.muted })}>{input.muted ? <VolumeX size={16} /> : <Volume2 size={16} />}</button><span>{input.name}</span><output>{Number(input.volume).toFixed(0)} dB</output></div><Range label={`${input.name} 볼륨`} value={Math.max(-60, input.volume)} min={-60} max={0} unit=" dB" onCommit={value => obsAction('volume', { name: input.name, value })} /></div>) : <div className="empty-mixer"><AudioLines size={24} strokeWidth={1.4} /><span>OBS를 연결하면 오디오 입력이 보여요.</span><button className="text-button" onClick={() => setPage('connections')}>OBS 연결<ArrowUpRight size={13} /></button></div>}</section><section className="activity-panel"><div className="section-title"><h3>작업 기록</h3><span className="subtle-dot" /></div>{state.events.length ? <ol>{state.events.slice(0, 4).map(event => <li key={event.id}><time>{new Date(event.time).toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit', hour12: false })}</time><span>{event.text}</span></li>)}</ol> : <div className="activity-empty"><span className="activity-line" /><p>연결과 작업 기록이 여기에 남아요.</p><span>작업실 준비 완료</span></div>}</section></div>
          </>}
          {page === 'models' && <div className="page-content models-page"><div className="page-heading"><div><span className="section-kicker">아바타 보관함</span><h2>Live2D 모델</h2></div><Button onClick={() => fileInput.current?.click()} disabled={importing}><Plus size={16} />{importing ? '가져오는 중' : '모델 가져오기'}</Button></div><div className="model-detail"><div className="model-preview background-studio">{avatar}</div><div className="model-list"><div className="section-title"><h3>Live2D 모델</h3></div>{state.models.map(model => <button key={model.id} className={`model-row ${model.id === config.modelId ? 'selected' : ''}`} onClick={() => run(() => api('config', { modelId: model.id }))}><span className="model-monogram">{model.name.slice(0, 1)}</span><span><strong>{model.name}</strong><small>{model.kind} · {model.expressions.length ? '표정 지원' : '기본 표정'}</small></span>{model.id === config.modelId ? <Check size={17} /> : <ChevronRight size={17} />}</button>)}<button className="model-drop" onClick={() => fileInput.current?.click()} disabled={importing}><FileUp size={26} strokeWidth={1.4} /><strong>Live2D ZIP 가져오기</strong><span>model3.json · moc3 · 텍스처</span></button></div></div></div>}
          {page === 'voice' && <div className="page-content"><div className="page-heading"><div><span className="section-kicker">목소리</span><h2>RVC 음성 변환</h2></div><Status active={state.rvc.connected}>{state.rvc.connected ? 'Voice Changer 연결됨' : '서버 연결 전'}</Status></div><VoicePanel state={state} run={run} /></div>}
          {page === 'agent' && <div className="page-content agent-page"><div className="page-heading"><div><span className="section-kicker">방송 파트너</span><h2>에이전트 파트너</h2></div><Button onClick={openPet}><PawPrint size={16} />펫 띄우기</Button></div><div className="agent-stage"><div className="agent-avatar">{avatar}{activeSignal?.text && <div className="stage-bubble">{activeSignal.text}</div>}</div><AgentChat state={state} run={run} onSettings={() => { setInspector('agent'); setInspectorVisible(true); }} expanded /></div></div>}
          {page === 'connections' && <div className="page-content connections-page"><div className="page-heading"><div><span className="section-kicker">기기와 연결</span><h2>연결 설정</h2></div><Button onClick={() => setGuide(true)}><HelpCircle size={16} />설정 가이드</Button></div><div className="connection-sections"><Section title="OBS Studio" action={<Status active={state.obs.connected}>{state.obs.connected ? '연결됨' : '연결 전'}</Status>}><ConnectionForm kind="obs" state={state} run={run} /><p className="help-copy">OBS → 도구 → WebSocket 서버 설정에서 서버를 활성화해 주세요. 장면과 오디오 입력을 작업실에서 제어해요.</p><div className="inline-actions"><a href="https://obsproject.com/download" target="_blank" rel="noreferrer">OBS 받기<ArrowUpRight size={14} /></a>{state.obs.connected && <button className="text-button" onClick={() => run(() => api('obs/disconnect', {}))}>연결 해제</button>}</div></Section><Section title="Voice Changer" action={<Status active={state.rvc.connected}>{state.rvc.connected ? '연결됨' : '연결 전'}</Status>}><ConnectionForm kind="rvc" state={state} run={run} /><p className="help-copy">w-okada의 MMVCServerSIO 주소를 연결해요. 모델과 가상 출력 장치는 목소리 탭에서 설정해요.</p><a href="https://github.com/w-okada/voice-changer/releases" target="_blank" rel="noreferrer">Voice Changer 받기<ArrowUpRight size={14} /></a></Section><Section title="방송용 투명 아바타"><p className="help-copy">OBS 브라우저 소스에 아래 주소를 넣어 주세요. 작업실의 모델·표정·입 모양이 함께 반영돼요.</p><div className="copy-field"><code>{state.overlayUrl}</code><button className="icon-button" aria-label="오버레이 주소 복사" onClick={() => copy(state.overlayUrl, '투명 아바타 주소를 복사했어요.')}><Clipboard size={17} /></button></div><Button disabled={!state.obs.connected} onClick={() => obsAction('addAvatar')}><Plus size={15} />현재 OBS 장면에 아바타 추가</Button></Section><Section title="데스크톱 펫"><p className="help-copy">데스크톱 앱에서는 투명한 아바타를 항상 위에 띄우고 끌어서 옮길 수 있어요. 클릭 통과는 메뉴 막대에서 해제할 수 있어요.</p><Button onClick={openPet}><PawPrint size={16} />펫 띄우기</Button></Section></div></div>}
        </div>
        {inspectorVisible && <aside className="inspector"><div className="inspector-tabs" aria-label="설정 패널"><button className={inspector === 'avatar' ? 'active' : ''} onClick={() => setInspector('avatar')}>모델</button><button className={inspector === 'voice' ? 'active' : ''} onClick={() => setInspector('voice')}>음성</button><button className={inspector === 'agent' ? 'active' : ''} onClick={() => setInspector('agent')}>파트너</button><button className="icon-button" aria-label="설정 패널 닫기" onClick={() => setInspectorVisible(false)}><X size={16} /></button></div><div className="inspector-scroll">
          {inspector === 'avatar' && <><Section title="아바타" action={<button className="text-button" onClick={() => setPage('models')}>관리<ChevronRight size={13} /></button>}><div className="selected-model"><span className="model-monogram">{selectedModel?.name.slice(0, 1) || 'K'}</span><span><strong>{selectedModel?.name || '모델 선택'}</strong><small>Live2D Cubism</small></span><Status active={ready}>{ready ? '준비' : '대기'}</Status></div><Field label="모델 선택"><select value={config.modelId} onChange={e => run(() => api('config', { modelId: e.target.value }))}>{!state.models.length && <option value="">모델 가져오기</option>}{state.models.map(model => <option key={model.id} value={model.id}>{model.name}</option>)}</select></Field><Button className="full-width" disabled={importing} onClick={() => fileInput.current?.click()}><Plus size={15} />{importing ? '가져오는 중' : '모델 가져오기'}</Button></Section><Section title="입 모양"><Toggle label="마이크 입 모양" checked={microphone.enabled} onChange={() => microphone.enabled ? microphone.stop() : run(microphone.start)} /><Field label="입 모양 입력" hint="기본 마이크로 한 번 켜면 장치 목록을 가져와요."><select value={microphone.device} onChange={e => { microphone.stop(); microphone.setDevice(e.target.value); }}><option value="">기본 마이크</option>{microphone.devices.map(device => <option key={device.deviceId} value={device.deviceId}>{device.label || "오디오 입력"}</option>)}</select></Field></Section><Section title="화면 배치" action={<button className="icon-button" title="배치 초기화" aria-label="배치 초기화" onClick={() => updateAvatar({ scale: 1, x: 0, y: 0 })}><RotateCcw size={14} /></button>}><Range label="크기" value={Math.round(config.avatar.scale * 100)} min={30} max={200} unit="%" onCommit={value => updateAvatar({ scale: value / 100 })} /><Range label="가로 위치" value={Math.round(config.avatar.x * 100)} min={-70} max={70} onCommit={value => updateAvatar({ x: value / 100 })} /><Range label="세로 위치" value={Math.round(config.avatar.y * 100)} min={-50} max={50} onCommit={value => updateAvatar({ y: value / 100 })} /><Toggle label="마우스 시선 따라가기" checked={config.avatar.gaze} onChange={() => updateAvatar({ gaze: !config.avatar.gaze })} /></Section><Section title="미리보기 배경"><div className="background-options">{[{ id: 'studio', name: '작업실' }, { id: 'transparent', name: '투명' }, { id: 'green', name: '크로마키' }].map(bg => <button key={bg.id} className={config.avatar.background === bg.id ? 'active' : ''} aria-pressed={config.avatar.background === bg.id} onClick={() => updateAvatar({ background: bg.id as Config['avatar']['background'] })}><i className={`swatch swatch-${bg.id}`} />{bg.name}</button>)}</div></Section><Section title="내보내기"><Button className="full-width" disabled={!state.obs.connected} onClick={() => obsAction('addAvatar')}><Plus size={15} />OBS에 아바타 추가</Button><button className="export-link" onClick={() => copy(state.overlayUrl, '오버레이 주소를 복사했어요.')}><Clipboard size={14} />브라우저 소스 주소 복사<ArrowUpRight size={13} /></button><Button className="full-width" onClick={openPet}><PawPrint size={15} />바탕화면 펫 띄우기</Button></Section><Section title="동작 테스트"><div className="motion-buttons">{[{ id: 'Nod', name: '끄덕이기' }, { id: 'Shake', name: '도리도리' }, { id: 'Blink', name: '깜빡이기' }].map(motion => <button disabled={!selectedModel?.motions.includes(motion.id)} key={motion.id} onClick={() => run(() => api('stage/signal', { motion: motion.id, duration: 4 }))}>{motion.name}</button>)}</div></Section></>}
          {inspector === 'voice' && <VoicePanel compact state={state} run={run} />}
          {inspector === 'agent' && <><AgentSettings state={state} run={run} />{page !== 'agent' && <AgentChat state={state} run={run} onSettings={() => setPage('agent')} />}</>}
        </div></aside>}
      </div>
      <footer className="statusbar"><div><span className={`status-light ${online ? 'on' : ''}`} />{state.obs.streaming ? '방송 중' : '방송 대기'}{state.obs.recording && <span className="recording-label"><Circle size={9} fill="currentColor" />녹화 중</span>}<span className="statusbar-mode">{config.mode === 'studio' ? 'OBS 작업실' : config.mode === 'agent' ? '에이전트 펫' : 'RVC 음성 변환'}</span></div><div><button disabled={!state.obs.connected || pending > 0} className={`record-button ${state.obs.recording ? 'recording' : ''}`} onClick={() => obsAction(state.obs.recording ? 'recordStop' : 'recordStart')}>{state.obs.recording ? <Square size={10} fill="currentColor" /> : <Circle size={10} />}<span>{state.obs.recording ? '녹화 중지' : '녹화'}</span></button><time>{clock.toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit', hour12: false })}</time></div></footer>
    </main>
    {toast && <div className={`toast ${toast.error ? 'error' : ''}`} role={toast.error ? 'alert' : 'status'}>{toast.error ? <Circle size={15} /> : <Check size={16} />}<span>{toast.text}</span><button aria-label="알림 닫기" onClick={() => setToast(null)}><X size={15} /></button></div>}
    {guide && <Onboarding state={state} run={run} onClose={() => setGuide(false)} onImport={() => fileInput.current?.click()} />}
    {confirmBroadcast && <Modal title={state.obs.streaming ? '방송을 종료할까요?' : '방송을 시작할까요?'} onClose={() => setConfirmBroadcast(false)}><p className="help-copy">{state.obs.streaming ? 'OBS의 현재 생방송을 종료해요.' : 'OBS에 설정된 방송 채널로 현재 장면이 송출돼요.'}</p><div className="confirm-scene"><span>현재 장면</span><strong>{state.obs.currentScene}</strong></div><div className="modal-actions"><Button onClick={() => setConfirmBroadcast(false)}>취소</Button><Button variant={state.obs.streaming ? 'danger' : 'primary'} onClick={async () => { const result = await obsAction(state.obs.streaming ? 'streamStop' : 'streamStart'); if (result) setConfirmBroadcast(false); }}>{state.obs.streaming ? '방송 종료' : '방송 시작'}</Button></div></Modal>}
  </div>;
}
