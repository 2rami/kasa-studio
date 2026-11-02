import { useEffect, useRef, useState, type RefObject } from 'react';
import * as PIXI from 'pixi.js';
import type { AvatarModel, Config, Pose, Signal } from '../types';

window.PIXI = PIXI;

interface Props { model?: AvatarModel; config: Config['avatar']; pose: RefObject<Pose>; signal: Signal | null; onReady?: (ready: boolean) => void }
export function Avatar({ model, config, pose, signal, onReady }: Props) {
  const host = useRef<HTMLDivElement>(null);
  const runtime = useRef<any>(null);
  const current = useRef({ config, signal }); current.current = { config, signal };
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(true);
  useEffect(() => {
    if (!host.current || !model) { setLoading(false); onReady?.(false); return; }
    let disposed = false;
    setLoading(true); setError(''); onReady?.(false);
    const app = new PIXI.Application({ width: host.current.clientWidth, height: host.current.clientHeight, backgroundAlpha: 0, antialias: true, resolution: Math.min(devicePixelRatio || 1, 2), autoDensity: true });
    host.current.appendChild(app.view);
    let observer: ResizeObserver | undefined;
    let loaded: any;
    (async () => {
      const { Live2DModel } = await import('pixi-live2d-display/cubism4');
      const instance = await Live2DModel.from(model.url, { autoInteract: false, autoUpdate: true });
      loaded = instance;
      if (disposed) { instance.destroy(); return; }
      runtime.current = instance; app.stage.addChild(instance); instance.anchor.set(0, 0);
      const raw = instance.internalModel.coreModel as any;
      const drawables = raw.getModel().drawables;
      const bounds = { left: Infinity, top: Infinity, right: -Infinity, bottom: -Infinity };
      for (let i = 0; i < drawables.count; i++) {
        if (drawables.opacities[i] < 0.01) continue;
        const vertices = instance.internalModel.getDrawableVertices(i);
        for (let j = 0; j < vertices.length; j += 2) {
          const point = instance.internalModel.localTransform.apply(new PIXI.Point(vertices[j], vertices[j + 1]));
          bounds.left = Math.min(bounds.left, point.x); bounds.right = Math.max(bounds.right, point.x);
          bounds.top = Math.min(bounds.top, point.y); bounds.bottom = Math.max(bounds.bottom, point.y);
        }
      }
      const original = Number.isFinite(bounds.left)
        ? { width: bounds.right - bounds.left, height: bounds.bottom - bounds.top }
        : { width: instance.width, height: instance.height };
      // 생성 모델의 캔버스 원점은 그림 가운데와 다를 수 있다.
      instance.pivot.set(Number.isFinite(bounds.left) ? (bounds.left + bounds.right) / 2 : original.width / 2, Number.isFinite(bounds.top) ? (bounds.top + bounds.bottom) / 2 : original.height / 2);
      const fit = () => {
        if (!host.current || disposed) return;
        const w = host.current.clientWidth, h = host.current.clientHeight;
        app.renderer.resize(w, h);
        const { scale, x, y } = current.current.config;
        instance.scale.set(Math.min(w * 0.88 / original.width, h * 0.95 / original.height) * scale);
        instance.position.set(w * (0.5 + x), h * (0.53 + y));
      };
      observer = new ResizeObserver(fit); observer.observe(host.current!); fit();
      let lastTransform = '';
      const smooth = { mouth: 0, x: 0, y: 0 };
      instance.internalModel.on('beforeModelUpdate', () => {
        const { config: settings } = current.current;
        const transform = `${settings.scale}:${settings.x}:${settings.y}`;
        if (lastTransform !== transform) { fit(); lastTransform = transform; }
        const core = instance.internalModel.coreModel as { setParameterValueById: (id: string, value: number) => void };
        smooth.mouth += (pose.current.mouth - smooth.mouth) * 0.32;
        smooth.x += (pose.current.lookX - smooth.x) * 0.06; smooth.y += (pose.current.lookY - smooth.y) * 0.06;
        core.setParameterValueById('ParamMouthOpenY', smooth.mouth);
        if (settings.gaze) {
          core.setParameterValueById('ParamAngleX', smooth.x * 15); core.setParameterValueById('ParamAngleY', smooth.y * 10);
          core.setParameterValueById('ParamEyeBallX', smooth.x * 0.65); core.setParameterValueById('ParamEyeBallY', smooth.y * 0.65);
        }
      });
      setLoading(false); onReady?.(true);
    })().catch(error => { if (!disposed) { setError(error.message || '모델을 불러올 수 없어요.'); setLoading(false); onReady?.(false); } });
    return () => { disposed = true; observer?.disconnect(); runtime.current = null; if (loaded) loaded.destroy(); app.destroy(true, { children: false, texture: false, baseTexture: false }); };
  }, [model?.url]);
  useEffect(() => {
    const instance = runtime.current;
    if (!instance || !signal || Date.now() > signal.at + signal.duration * 1000) return;
    const name = signal.emotion;
    if (name !== 'neutral' && model?.expressions.includes(name)) instance.expression(name).catch(() => {});
    else instance.internalModel.motionManager.expressionManager?.resetExpression();
    if (signal.motion && model?.motions.includes(signal.motion)) instance.motion(signal.motion).catch(() => {});
    const reset = setTimeout(() => instance.internalModel?.motionManager.expressionManager?.resetExpression(), Math.max(0, signal.at + signal.duration * 1000 - Date.now()));
    return () => clearTimeout(reset);
  }, [signal, loading]);
  return <div className="avatar-host" ref={host} aria-label={model ? `${model.name} Live2D 미리보기` : '아바타 미리보기'}>
    {loading && <div className="avatar-loading"><span className="spinner" />모델을 불러오고 있어요</div>}
    {error && <div className="avatar-error" role="alert">모델을 불러오지 못했어요<span>{error}</span></div>}
    {!model && !loading && <div className="avatar-error">Live2D 모델을 가져와 주세요</div>}
  </div>;
}
