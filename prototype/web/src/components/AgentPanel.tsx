import { useRef, useState, useEffect } from 'react';
import { ArrowUp, Clipboard, Ellipsis, Loader2, Volume2, X } from 'lucide-react';
import type { StudioState } from '../types';
import { api } from '../useStudio';
import { Button, Field, Input, Section, Toggle, type Run } from './Controls';

export function AgentSettings({ state, run }: { state: StudioState; run: Run }) {
  const [draft, setDraft] = useState(state.config.agent), [key, setKey] = useState(''), [busy, setBusy] = useState(false);
  const update = (patch: Partial<typeof draft>) => setDraft({ ...draft, ...patch });
  const save = () => run(async () => { await api('config', { agent: draft }); if (key) { await api('agent/key', { key }); setKey(''); } }, '에이전트 설정을 저장했어요.');
  const probe = async () => { setBusy(true); await run(async () => { await api('config', { agent: draft }); if (key) { await api('agent/key', { key }); setKey(''); } const next = await api('agent/probe', {}); setDraft(next.config.agent); }); setBusy(false); };
  return <>
    <Section title="파트너 설정">
      <Field label="파트너 이름"><Input value={draft.name} maxLength={40} onChange={e => update({ name: e.target.value })} /></Field>
      <Field label="호환 API 주소" hint="Ollama·LM Studio·vLLM의 /v1 주소를 연결해요."><Input value={draft.endpoint} onChange={e => update({ endpoint: e.target.value })} placeholder="http://127.0.0.1:11434/v1" /></Field>
      <Field label="API 키" hint="키는 이번 실행 동안만 보관해요. 환경변수 STUDIO_AGENT_KEY도 사용할 수 있어요."><Input value={key} type="password" autoComplete="off" onChange={e => setKey(e.target.value)} placeholder="로컬 서버는 비워 두기" /></Field>
      <div className="field-with-action"><Field label="모델 이름"><Input list="agent-models" value={draft.model} onChange={e => update({ model: e.target.value })} placeholder="서버에 설치한 모델" /><datalist id="agent-models">{state.agent.models.map(model => <option key={model} value={model} />)}</datalist></Field><Button onClick={probe} disabled={busy}>{busy ? <Loader2 size={14} className="spin" /> : <Ellipsis size={18} />}조회</Button></div>
      <Field label="파트너 지침"><textarea className="input" rows={4} value={draft.prompt} onChange={e => update({ prompt: e.target.value })} /></Field>
      <Toggle label="대답을 기기 목소리로 읽기" checked={draft.speak} onChange={() => update({ speak: !draft.speak })} />
      <Button variant="primary" className="full-width" onClick={save}>파트너 설정 저장</Button>
    </Section>
    <Section title="외부 에이전트 연결">
      <p className="help-copy">외부 에이전트가 로컬 API로 대사·표정·동작을 보낼 수 있어요.</p>
      <Button className="full-width" onClick={() => run(async () => {
        const { token } = await api<{ token: string }>('agent/token');
        const payload = JSON.stringify({ text: '방송 준비됐어요.', emotion: 'smile', motion: 'Nod', duration: 8 });
        await navigator.clipboard.writeText(`curl -X POST '${state.origin}/api/agent/signal' -H 'Content-Type: application/json' -H 'X-Studio-Token: ${token}' -d '${payload}'`);
      }, '에이전트 연결 명령을 복사했어요.')}><Clipboard size={15} />연결 명령 복사</Button>
      <p className="field-hint">연결 명령에는 이 작업실의 전용 토큰이 포함돼요.</p>
    </Section>
  </>;
}

export function AgentChat({ state, run, onSettings, expanded = false }: { state: StudioState; run: Run; onSettings: () => void; expanded?: boolean }) {
  const [text, setText] = useState('');
  const end = useRef<HTMLDivElement>(null);
  useEffect(() => { end.current?.scrollIntoView({ block: 'nearest' }); }, [state.agent.messages.length]);
  const send = async () => {
    if (!text.trim()) return;
    const sent = text; setText('');
    const result = await run(() => api('agent/chat', { text: sent }));
    if (!result) setText(sent);
  };
  return <div className={`agent-chat ${expanded ? 'expanded' : ''}`}>
    <div className="chat-head"><span className="partner-dot" /><strong>{state.config.agent.name}</strong><button className="icon-button" aria-label="파트너 설정" onClick={onSettings}><Ellipsis size={19} /></button></div>
    <div className="chat-messages" aria-live="polite">
      {!state.agent.messages.length && <div className="chat-empty"><span className="partner-symbol">k.</span><p>{state.agent.configured ? '파트너에게 말을 걸어 보세요.' : '방송을 함께할 파트너'}</p><button className="text-button" onClick={onSettings}>{state.agent.configured ? state.config.agent.model : '에이전트 연결하기'}</button></div>}
      {state.agent.messages.map((message, i) => <div className={`chat-message ${message.role}`} key={i}><small>{message.role === 'user' ? '나' : state.config.agent.name}</small><p>{message.content}</p></div>)}
      {state.agent.busy && <div className="chat-thinking"><Loader2 size={15} className="spin" />대답을 기다리고 있어요</div>}<div ref={end} />
    </div>
    <form className="chat-input" onSubmit={e => { e.preventDefault(); send(); }}><input aria-label="파트너에게 보낼 메시지" value={text} maxLength={4000} onChange={e => setText(e.target.value)} placeholder={state.agent.configured ? '파트너에게 메시지' : '에이전트를 먼저 연결해 주세요'} disabled={!state.agent.configured || state.agent.busy} /><button aria-label="메시지 보내기" disabled={!text.trim() || state.agent.busy || !state.agent.configured}><ArrowUp size={19} /></button></form>
    {state.config.agent.speak && <div className="chat-voice"><Volume2 size={12} />기기 음성 읽기 켜짐<button className="icon-button" aria-label="음성 읽기 끄기" onClick={() => run(() => api('config', { agent: { speak: false } }))}><X size={12} /></button></div>}
  </div>;
}
