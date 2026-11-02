# Kasa Studio

이 문서는 `prototype/web/`에 보관한 초기 웹판의 기록이다. 현재 앱은 [C++ 네이티브](../native/README.md)다.

React + Electron 방송 작업실. OBS Studio를 방송 엔진으로 사용하고, Live2D 아바타·에이전트·w-okada RVC를 한 화면에서 제어한다. 개인 허브 `geono-hub`의 Paperlogy·Anton과 하늘색·연두색 테마, 남색 테마를 사용한다.

## 실행

Node 22 이상이 필요하다.

```sh
npm install
npm run setup
npm run dev
```

브라우저 작업실: <http://127.0.0.1:4780>. 데스크톱 앱은 `npm run desktop`으로 실행한다. 기본 포트가 사용 중이면 앱은 빈 포트를 선택하며 화면의 오버레이 주소에 반영한다. 맥 앱 묶음은 `npm run package:mac`으로 `release/mac-arm64/Kasa Studio.app`에 생성한다. 앱을 닫으면 메뉴 막대에 남으며, 메뉴 막대의 종료로 서버까지 종료한다.

`npm run setup`은 이 작업 폴더 옆 `l2dforge/out/hoodiecat_fb16e3_nowedge`의 자체 생성 모델을 준비한다. 다른 기기에서는 작업실의 모델 가져오기로 ZIP을 추가한다. 모델은 `model3.json` 하나와 해당 파일이 참조하는 `.moc3`, 텍스처, 물리·표정·모션 파일을 포함해야 한다. 파일 경로 검사와 압축 해제 용량 제한을 적용한다.

## 연결

- **OBS**: OBS의 도구 → WebSocket 서버 설정에서 활성화하고 주소와 비밀번호를 작업실에 입력한다. 장면 전환·실제 장면 미리보기·입력별 볼륨·음소거·녹화·방송 시작 및 종료를 지원한다. 방송 버튼은 앱 안에서 별도 확인을 거친다. OBS를 설치하거나 송출 채널을 자동 설정하지 않는다.
- **RVC**: [w-okada Voice Changer](https://github.com/w-okada/voice-changer)의 MMVCServerSIO 서버 오디오 API를 연결한다. `/info`에서 모델과 장치를 가져오며 모델 `.pth`/`.onnx`와 선택한 `.index`를 빈 슬롯에 올릴 수 있다. 음높이·음색 혼합·자음 보호·음높이 추출·샘플레이트·처리 블록·입력·출력·모니터를 제어한다. 실제 변환은 서버가 실행한다. 새 w-okada 클라이언트나 별도 RVC 구현이 이 API를 제공하지 않으면 연결하지 않는다.
- **오디오 출력**: Mac은 [BlackHole](https://existential.audio/blackhole/), Windows는 [VB-CABLE](https://vb-audio.com/Cable/) 같은 가상 장치를 RVC 출력과 OBS 입력으로 선택한다. 드라이버와 대형 음성 모델은 자동 설치하지 않는다. 원격 서버 연결 시 장치 목록은 그 서버 기기의 장치다.
- **에이전트**: Ollama·LM Studio·vLLM 등의 `/v1` API 주소와 모델 이름을 설정한다. 모델 조회는 `GET /models`, 대화는 `POST /chat/completions`를 사용한다. 대답의 JSON `text`·`emotion`·`motion`을 말풍선·표정·동작으로 전달하며 일반 텍스트 응답도 허용한다. 기기 음성 읽기는 선택 사항이다. 이 음성의 입 모양은 발화 이벤트에 맞춘 근사 동작이다.
- **외부 에이전트**: 파트너 설정의 연결 명령 복사로 전용 토큰이 포함된 `curl` 명령을 받는다. `POST /api/agent/signal`의 `X-Studio-Token` 헤더가 필요하다. 요청 필드는 `text`, `emotion`, `motion`, `duration`이다. `emotion`은 `neutral|smile|surprise|sad|angry|shy|wink`, `motion`은 빈 문자열 또는 `Nod|Shake|Blink`, `duration`은 1~60초다. 이 API는 OBS 방송 시작 권한을 갖지 않는다.
- **OBS 아바타**: <http://127.0.0.1:4780/overlay>를 브라우저 소스로 사용하거나, 연결 설정의 현재 OBS 장면에 아바타 추가를 누른다. 모델·배치·표정·마이크 입 모양이 작업실과 동기화된다. OBS가 다른 컴퓨터에서 실행되면 그 컴퓨터에서도 작업실을 실행해야 한다.
- **펫**: Electron 앱에서 투명·항상 위 창으로 실행한다. 드래그로 이동하고, 클릭 통과를 켤 수 있다. 클릭 통과 해제와 펫 닫기는 메뉴 막대에서 가능하다. 웹 브라우저의 펫 창에는 OS의 항상 위·투명 기능이 적용되지 않는다.

마이크는 버튼을 눌렀을 때만 접근한다. `M`은 마이크 입 모양 전환, `1`~`7`은 표정이다. 웹 작업실의 마이크는 입 모양만 구동하며 RVC 변환용 입력은 Voice Changer에서 별도로 선택한다. 카메라 얼굴 추적과 화면 캡처 합성 엔진은 이 판에 포함되지 않는다.

## 데이터와 검증

설정과 가져온 모델은 `data/`에, 패키징한 앱에서는 앱의 사용자 데이터 폴더에 보관한다. `STUDIO_DATA_DIR`로 바꿀 수 있다. OBS 비밀번호와 에이전트 API 키는 설정 파일에 저장하지 않는다. API 키는 `STUDIO_AGENT_KEY` 환경변수로도 전달할 수 있다. 에이전트 토큰은 데이터 폴더의 `agent-token`에 권한 `0600`으로 저장한다. 작업실은 `127.0.0.1`에만 바인딩하고 외부 출처의 변경 요청을 거부한다.

```sh
npm run check
node scripts/verify-desktop.mjs
```

통합 시험은 독립된 임시 서버로 OBS v5 핸드셰이크·장면·녹화·브라우저 소스 요청, w-okada multipart 설정, 에이전트 대화와 인증, 아바타 동기화, ZIP 경로 검사를 확인한다. 실제 OBS 송출·GPU 음성 변환·실제 마이크 소리는 연결한 환경에서 별도로 확인해야 한다. 데스크톱 검증은 숨겨진 창에서 실제 Electron 앱의 아바타와 투명 펫을 구동하며 화면을 전면에 띄우지 않는다.

Live2D Core는 [공식 Web SDK](https://www.live2d.com/en/sdk/download/web/)의 런타임을 로컬 준비해 사용한다. 원본 런타임과 글꼴·라이브러리의 라이선스는 각각의 제공처를 따른다. 모델 원본과 기존 리깅 실험 파일은 수정하지 않는다.
