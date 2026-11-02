# Kasa Studio Native

C++20 방송 작업실. 공식 Cubism SDK for Native로 Live2D를 그리며 GLFW·Dear ImGui로 화면을 구성한다. macOS 창·파일 선택·HTTP/WebSocket·마이크·음성 읽기는 작은 Objective-C++ 계층에서 시스템 API에 연결한다. 실행에 Node, Electron, 브라우저, 웹뷰가 필요하지 않다.

## 실행과 빌드

현재 빌드는 Apple Silicon macOS용이다. 완성된 `Kasa Studio Native.app`을 실행하면 된다. 개발 빌드에는 Xcode 명령행 도구와 CMake, 공식 Cubism Native SDK가 필요하다.

```sh
./native/scripts/setup.sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --parallel 3
```

앱: `native/build/Kasa Studio Native.app`. 이 작업 환경에서는 기존 `kasapet-native/vendor`의 공식 SDK를 읽기만 하여 사용한다. 다른 위치에 설치했다면 CMake에 `-DCUBISM_SDK_ROOT=/SDK/경로`를 지정한다. SDK Core는 재배포 저장소에 복사하지 않는다.

기본값은 30fps·2048px 텍스처와 Retina 렌더링이다. 작업실의 화질 설정에서 절전 1024px·화면 맞춤 2048px·고화질 4096px 및 60fps를 선택할 수 있다. 미리보기는 화면 배율에 맞춰, 펫은 640×1080으로 렌더링한다. 펫 창은 사용자가 켰을 때 만들고, 작업실과 모델·텍스처를 공유한다. 기존 사용자 펫의 프로세스와 설정은 변경하지 않는다.

RVC 전용 화면에서는 사용하지 않는 아바타를 불러오거나 매 프레임 갱신하지 않는다. 펫을 켜거나 아바타 화면으로 이동하면 공식 SDK 모델을 준비한다. 펫을 켠 상태에서 작업실을 닫으면 펫은 계속 동작하며, 메뉴 막대의 `K`에서 작업실을 다시 열 수 있다.

## 모델과 방송

모델 화면에서 `.model3.json` 파일 또는 모델 ZIP을 선택한다. JSON 직접 선택은 원래 폴더를 참조하며, ZIP은 앱의 모델 보관함으로 가져온다. 모델의 본체·텍스처·물리·표정·모션 참조를 확인하고 경로 탈출·심볼릭 링크를 거부한다. ZIP 해제 상한은 256MB다. 기본 모델은 자체 생성한 후드캣이다.

모델에 정의된 표정과 동작은 작업실의 미리보기 아래에 표시한다. 표정 버튼과 에이전트 신호는 `neutral`, `smile`, `surprise`, `sad`, `angry`, `shy`, `wink`라는 표정 이름을 사용한다. 모델에 정의된 다른 이름의 표정도 해당 목록에서 직접 선택한다. 눈깜빡임·숨쉬기·모션·물리·입 모양은 C++ Framework와 Core가 처리한다.

OBS의 도구 → WebSocket 서버 설정에서 서버를 켜고 연결 설정에 주소와 비밀번호를 입력한다. 기본 주소는 `ws://127.0.0.1:4455`다. OBS v5 인증, 실제 장면 목록·미리보기, 장면 전환, 입력 볼륨·음소거, 녹화, 방송 시작·종료를 지원한다. 방송 시작과 종료에는 앱의 확인 창이 있다.

방송용 아바타는 네이티브 `Kasa Studio Pet` 창이다. 펫을 띄운 뒤 OBS 창 캡처에서 해당 창을 선택한다. 구형 `window_capture` 입력을 제공하는 OBS에서는 자동 추가를 시도하며, 창 선택 API가 다른 버전에서는 OBS에서 직접 선택하도록 안내한다. 화면 기록 권한도 OBS에서 허용해야 한다. 웹 브라우저 소스를 사용하지 않는다.

펫은 투명·항상 위 창이며 드래그로 옮길 수 있다. 작업실에서 클릭 통과를 켤 수 있고, 메뉴 막대의 `K`에서 클릭 통과 해제·펫 숨기기·작업실 열기·종료가 가능하다.

## 얼굴 추적

얼굴 추적 화면에서 웹캠·아이폰 연속성 카메라 또는 전용 아이폰 ARKit 연결을 선택한다. 카메라는 추적 시작을 눌렀을 때만 켜진다. 정면 기준 맞추기·좌우 반전·움직임 크기·부드러움과 입 모양 입력을 조절할 수 있다. 고개·눈깜빡임·시선·입·눈썹을 Live2D 표준 파라미터에 연결하며, 해당 파라미터가 없는 모델은 그 움직임을 생략한다. 수동 표정을 선택한 동안 표정과 추적이 충돌하지 않도록 한다.

웹캠은 macOS Vision으로 얼굴과 랜드마크를 처리한다. 연속성 카메라는 macOS에 등록된 카메라 목록에서 선택한다. 검출한 얼굴 윤곽·눈·눈썹·코·입을 점과 점선으로 보여준다. 기본 화면은 얼굴 점·선만 표시하며 카메라 영상은 선택해서 켤 수 있다. 웹캠 추적 화면만 사용할 때는 아바타를 불러오지 않는다. 미리보기는 메모리에서만 처리하며 저장하지 않는다.

아이폰은 같은 Wi-Fi에서 Bonjour로 PC를 찾아 이름을 누르면 바로 연결한다. PC에서 아이폰 입력을 선택하면 연결 대기를 시작하며, 앱을 열 때 자동 대기할지도 설정할 수 있다. 처음 연결할 때 외부망 재연결 정보도 기억한다. 셀룰러·다른 Wi-Fi에서 처음 등록할 때는 PC의 QR 또는 연결 링크를 한 번 전달한다. 이후에는 등록한 PC에 버튼 하나로 연결하며, 직접 연결이 끊기면 [서울 전용 중계소](../relay/README.md)를 사용한다. 영상 대신 회전·표정값만 전송한다. [아이폰 앱 빌드·연결 안내](../iphone/README.md).

## 목소리와 에이전트

RVC는 [w-okada Voice Changer](https://github.com/w-okada/voice-changer)의 MMVCServerSIO 서버 오디오 API에 연결한다. 모델 슬롯과 입력·출력·모니터 장치를 서버에서 가져온다. `.pth`/`.onnx`와 선택한 `.index`를 빈 슬롯에 분할 업로드하고, 음높이·음색 혼합·자음 보호·음높이 추출·샘플레이트·처리 블록을 제어한다. 실제 변환은 별도로 실행한 w-okada 서버가 담당한다.

RVC 출력과 OBS 마이크에 같은 가상 오디오 장치를 선택한다. Mac에서는 [BlackHole](https://existential.audio/blackhole/)을 사용할 수 있다. 얼굴 추적 화면의 입 모양 마이크 버튼은 기본 마이크의 크기로 아바타 입 모양만 움직인다. 사용자가 직접 켰을 때만 마이크 권한을 요청한다. RVC 장치 목록은 연결한 서버 기기의 장치다.

에이전트는 Ollama·LM Studio·vLLM 등의 호환 `/v1` API에 연결한다. 모델 조회와 대화는 C++에서 시스템 HTTP API로 처리한다. JSON 응답의 `text`·`emotion`·`motion`을 대사·표정·동작에 연결하며 일반 텍스트 응답도 표시한다. 기기 음성 읽기는 사용자가 켤 수 있다. 기기 음성의 입 모양은 발화 중 근사 동작이며, 마이크 입 모양은 실제 입력 크기에 따른다.

외부 에이전트는 연결 명령 복사로 전용 토큰이 담긴 `curl` 명령을 받는다. 수신 주소는 기본 `http://127.0.0.1:4781/api/agent/signal`이며 포트가 사용 중이면 빈 포트를 선택한다. `X-Studio-Token` 헤더가 필요하다. 요청 필드는 `text`, `emotion`, `motion`, `duration`이다. 동작은 빈 문자열 또는 `Nod|Shake|Blink`, 지속 시간은 1~60초다. 이 수신 API는 방송을 시작하거나 셸을 실행하지 않는다.

## 보관 위치와 검증

설정과 가져온 ZIP 모델은 `~/Library/Application Support/Kasa Studio Native/`에 저장한다. OBS 비밀번호와 에이전트 API 키는 설정 파일에 저장하지 않는다. API 키는 이번 실행의 메모리 또는 `KASA_STUDIO_AGENT_KEY` 환경변수로 전달한다. 에이전트 수신 토큰은 권한 `0600`으로 보관한다.

```sh
ctest --test-dir native/build --output-on-failure
./native/scripts/verify.sh
```

시험은 C++ 시험 서버와 실제 시스템 HTTP/WebSocket을 사용하여 OBS 인증·장면·녹화, RVC 설정·분할 업로드·슬롯 등록, 에이전트 대화·토큰 검사를 확인한다. 렌더 검증은 숨긴 창과 FBO에서 실행하며 모델 로딩·색상·투명 픽셀·항상 위·클릭 통과·OpenGL 오류를 검사한다. RSS와 macOS 물리 메모리 사용량은 검증 보고서에 별도로 기록한다. GPU와 텍스처, 창 크기에 따라 메모리 사용량은 달라진다.

실제 OBS 송출, 화면 기록 권한, 마이크 장치, RVC 변환음과 지연은 해당 서비스·장치를 연결한 환경에서 확인해야 한다. 카메라와 아이폰의 실제 조명·각도·권한·네트워크 환경에 따른 추적 품질은 기기에서 확인한다. 방송 합성·송출은 OBS가 담당한다.

`KASA_STUDIO_NATIVE_DATA`는 데이터 폴더, `KASA_STUDIO_AGENT_PORT`는 에이전트 수신 포트를 바꾼다. `--headless --frames 90 --snapshot 경로 --pet-shot 경로 --report 경로`로 화면을 띄우지 않고 검증한다. `--page studio|models|tracking|voice|agent|connections`는 검증할 화면을 선택한다.

공식 SDK: [Cubism SDK for Native](https://docs.live2d.com/en/cubism-sdk-manual/cubism-sdk-for-native/). Core·Framework·GLFW·GLEW·ImGui·cpp-httplib·JSON·글꼴의 라이선스와 저작권은 각 제공처를 따른다. 기존 웹판은 디자인 비교용 초기 실험이며 [이전 안내](../docs/web-prototype.md)에 남아 있다.
