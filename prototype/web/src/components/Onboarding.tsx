import { useState } from 'react';
import { ArrowLeft, ArrowRight, Check, Radio, Sparkles } from 'lucide-react';
import type { StudioState } from '../types';
import { api } from '../useStudio';
import { Button, ConnectionForm, Modal, ModeSelector, type Run } from './Controls';

export function Onboarding({ state, run, onClose, onImport }: { state: StudioState; run: Run; onClose: () => void; onImport: () => void }) {
  const [step, setStep] = useState(0);
  const titles = ['작업실 준비', '아바타 선택', 'OBS 연결', '목소리와 파트너'];
  const finish = () => run(async () => { await api('config', { onboardingCompleted: true }); onClose(); }, '작업실 준비를 마쳤어요.');
  return <Modal title="작업실 온보딩" onClose={onClose} wide>
    <div className="onboarding-progress" aria-label={`설정 ${step + 1} / 4`}>{titles.map((title, i) => <span key={title} className={i <= step ? 'current' : ''}><i>{i < step ? <Check size={12} /> : i + 1}</i>{title}</span>)}</div>
    <div className="onboarding-body">
      {step === 0 && <><div className="welcome-mark"><Radio size={38} strokeWidth={1.5} /></div><h3 className="welcome-title">어떤 작업실로 쓸까요?</h3><ModeSelector config={state.config} run={run} expanded /></>}
      {step === 1 && <><h3 className="welcome-title">화면에 함께할 아바타</h3><div className="onboard-models">{state.models.map(model => <button key={model.id} className={`model-row ${model.id === state.config.modelId ? 'selected' : ''}`} onClick={() => run(() => api('config', { modelId: model.id }))}><span className="model-monogram">{model.name.slice(0, 1)}</span><span><strong>{model.name}</strong><small>{model.kind}</small></span>{model.id === state.config.modelId && <Check size={18} />}</button>)}</div><Button onClick={onImport}><Sparkles size={15} />Live2D ZIP 가져오기</Button><p className="help-copy">model3.json·moc3·텍스처가 함께 든 ZIP을 가져와요.</p></>}
      {step === 2 && <><h3 className="welcome-title">방송 화면 연결</h3><p className="help-copy">OBS의 도구 → WebSocket 서버 설정에서 서버를 켠 뒤 연결해 주세요.</p><ConnectionForm kind="obs" state={state} run={run} /><p className="field-hint">펫이나 목소리 변환만 쓸 때는 건너뛰어도 괜찮아요.</p></>}
      {step === 3 && <><h3 className="welcome-title">목소리와 파트너</h3><div className="setup-summary"><div><span>아바타</span><strong>{state.models.find(model => model.id === state.config.modelId)?.name || '나중에 가져오기'}</strong></div><div><span>OBS</span><strong>{state.obs.connected ? '연결됨' : '나중에 연결'}</strong></div><div><span>음성 변환</span><strong>w-okada Voice Changer</strong></div><div><span>에이전트</span><strong>{state.config.agent.model || '설정에서 연결'}</strong></div></div><p className="help-copy">목소리 변환은 음성 탭에서 서버와 모델을 연결해요. 에이전트 탭에서는 파트너 모델이나 외부 에이전트를 연결할 수 있어요.</p><p className="field-hint">마이크는 직접 켰을 때만 사용해요. OBS 연결만으로 방송이 시작되지는 않아요.</p></>}
    </div>
    <div className="onboarding-footer">{step > 0 ? <Button onClick={() => setStep(step - 1)}><ArrowLeft size={15} />이전</Button> : <button className="text-button" onClick={onClose}>나중에 설정</button>}{step < 3 ? <Button variant="primary" onClick={() => setStep(step + 1)}>{step === 2 && !state.obs.connected ? '나중에 연결' : '다음'}<ArrowRight size={16} /></Button> : <Button variant="primary" onClick={finish}><Check size={16} />작업실 열기</Button>}</div>
  </Modal>;
}
