# Kasa Studio Native

C++20와 공식 Live2D Cubism SDK for Native를 사용하는 방송 작업실이다. GLFW·Dear ImGui로 UI를 그리며, 실행에 Node·Electron·브라우저·웹뷰가 필요하지 않다.

OBS 제어, Live2D 모델 가져오기와 표정·동작, w-okada RVC 설정·모델 업로드, 에이전트 연결과 투명 펫, 웹캠·아이폰 ARKit 얼굴 추적, 첫 실행 온보딩을 제공한다. 개인사이트의 Paperlogy·Anton·하늘색·연두색과 남색 테마를 사용한다.

```sh
./native/scripts/setup.sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --parallel 3
```

실행 파일은 `native/build/Kasa Studio Native.app`이다. [실행·연결·검증 안내](native/README.md)에서 설정과 검증 방법을 확인할 수 있다.

기존 React/Electron 코드는 `prototype/web/`에 디자인 실험으로 보관한다. 현재 앱의 빌드나 실행에는 포함되지 않는다. [이전 웹판 기록](docs/web-prototype.md).
