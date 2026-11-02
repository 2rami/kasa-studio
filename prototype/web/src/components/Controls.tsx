import { useEffect, useRef, useState, type ReactNode, type InputHTMLAttributes } from 'react';
import { Check, ChevronRight, Link2, Loader2, X } from 'lucide-react';
import type { Config, StudioState } from '../types';
import { api } from '../useStudio';

export type Run = <T>(task: () => Promise<T>, success?: string) => Promise<T | undefined>;
export function Button({ children, onClick, disabled, variant = '', className = '', title, type = 'button' }: { children: ReactNode; onClick?: () => void; disabled?: boolean; variant?: string; className?: string; title?: string; type?: 'button' | 'submit' }) {
  return <button type={type} title={title} className={`button ${variant} ${className}`} onClick={onClick} disabled={disabled}>{children}</button>;
}
export function Field({ label, hint, children }: { label: string; hint?: string; children: ReactNode }) {
  return <label className="field"><span className="field-label">{label}</span>{children}{hint && <span className="field-hint">{hint}</span>}</label>;
}
export function Input(props: InputHTMLAttributes<HTMLInputElement>) { return <input {...props} className={`input ${props.className || ''}`} />; }
export function Range({ label, value, min, max, step = 1, unit = '', onCommit, disabled }: { label: string; value: number; min: number; max: number; step?: number; unit?: string; onCommit: (value: number) => void; disabled?: boolean }) {
  const [draft, setDraft] = useState<number | null>(null);
  const current = draft ?? value;
  const commit = () => { if (draft !== null) { onCommit(draft); setDraft(null); } };
  return <label className="range-field"><span>{label}<output>{Number(current.toFixed(2))}{unit}</output></span>
    <input type="range" aria-label={label} min={min} max={max} step={step} value={current} disabled={disabled} onChange={e => setDraft(Number(e.target.value))} onPointerUp={commit} onKeyUp={commit} onBlur={commit} />
  </label>;
}
export function Toggle({ label, checked, onChange, disabled }: { label: string; checked: boolean; onChange: () => void; disabled?: boolean }) {
  return <button className="toggle-row" role="switch" aria-checked={checked} onClick={onChange} disabled={disabled}><span>{label}</span><span className={`toggle ${checked ? 'on' : ''}`}><i /></span></button>;
}
export function Status({ active, children }: { active: boolean; children: ReactNode }) { return <span className={`status ${active ? 'connected' : ''}`}><i />{children}</span>; }
export function Section({ title, action, children }: { title: string; action?: ReactNode; children: ReactNode }) {
  return <section className="control-section"><div className="section-title"><h3>{title}</h3>{action}</div>{children}</section>;
}
export function Modal({ title, children, onClose, wide = false }: { title: string; children: ReactNode; onClose: () => void; wide?: boolean }) {
  const dialog = useRef<HTMLElement>(null);
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null;
    dialog.current?.querySelector<HTMLElement>('button')?.focus();
    return () => previous?.focus();
  }, []);
  return <div className="modal-backdrop" onPointerDown={e => { if (e.target === e.currentTarget) onClose(); }} onKeyDown={e => { if (e.key === 'Escape') onClose(); }}>
    <section ref={dialog} className={`modal ${wide ? 'wide' : ''}`} role="dialog" aria-modal="true" aria-label={title} onKeyDown={e => {
      if (e.key !== 'Tab') return;
      const targets = [...(dialog.current?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), select:not(:disabled), textarea:not(:disabled), a[href]') || [])];
      const first = targets[0], last = targets.at(-1);
      if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last?.focus(); }
      else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first?.focus(); }
    }}>
      <div className="modal-head"><h2>{title}</h2><button className="icon-button" aria-label="닫기" onClick={onClose}><X size={19} /></button></div>{children}
    </section>
  </div>;
}
export function ConnectionForm({ kind, state, run }: { kind: 'obs' | 'rvc'; state: StudioState; run: Run }) {
  const settings = state.config[kind];
  const [url, setUrl] = useState(settings.url), [password, setPassword] = useState(''), [busy, setBusy] = useState(false);
  const connected = state[kind].connected;
  const connect = async () => {
    setBusy(true);
    await run(async () => { await api('config', { [kind]: { url } }); await api(`${kind}/connect`, kind === 'obs' ? { password } : {}); setPassword(''); });
    setBusy(false);
  };
  return <form className="connection-form" onSubmit={e => { e.preventDefault(); connect(); }}>
    <Field label={kind === 'obs' ? 'OBS WebSocket 주소' : 'Voice Changer 서버 주소'}><Input value={url} type="url" required onChange={e => setUrl(e.target.value)} placeholder={kind === 'obs' ? 'ws://127.0.0.1:4455' : 'http://127.0.0.1:18888'} /></Field>
    {kind === 'obs' && <Field label="WebSocket 비밀번호" hint="비밀번호는 연결에만 사용하고 저장하지 않아요."><Input type="password" autoComplete="off" value={password} onChange={e => setPassword(e.target.value)} placeholder="OBS에서 설정한 비밀번호" /></Field>}
    <div className="form-actions"><Status active={connected}>{connected ? '연결됨' : '연결 안 됨'}</Status><Button type="submit" disabled={busy} variant="primary">{busy ? <Loader2 size={16} className="spin" /> : connected ? <Check size={16} /> : <Link2 size={16} />}{busy ? '연결 중' : connected ? '다시 연결' : '연결하기'}</Button></div>
    {state[kind].error && <p className="inline-error">{state[kind].error}</p>}
  </form>;
}
export const modes = [
  { id: 'studio', label: '방송', detail: 'OBS 장면과 아바타' },
  { id: 'agent', label: '에이전트 펫', detail: '대화·표정·바탕화면 펫' },
  { id: 'rvc', label: 'RVC', detail: '실시간 목소리 변환' },
] as const;
export function ModeSelector({ config, run, expanded = false }: { config: Config; run: Run; expanded?: boolean }) {
  return <div className={expanded ? 'mode-list' : 'segmented'} aria-label="작업 모드">
    {modes.map(mode => <button key={mode.id} className={config.mode === mode.id ? 'active' : ''} aria-pressed={config.mode === mode.id} onClick={() => run(() => api('config', { mode: mode.id }))}><span>{mode.label}{expanded && <small>{mode.detail}</small>}</span>{expanded && <ChevronRight size={18} />}</button>)}
  </div>;
}
