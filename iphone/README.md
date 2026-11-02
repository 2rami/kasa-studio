# Kasa Face

Kasa Studio용 iPhone 얼굴 추적 앱. SwiftUI·ARKit·Network 프레임워크를 사용하며 iOS 17 이상과 ARKit 얼굴 추적을 지원하는 기기가 필요하다. 얼굴 영상은 아이폰에서 처리하고, 맥에는 고개 회전과 표정값만 전송한다.

## 연결

같은 Wi-Fi에서는 PC의 얼굴 추적 화면에서 아이폰을 선택한다. Kasa Face의 주변 PC 목록에 나타난 이름을 누르면 연결과 추적을 시작한다. 주소·포트·코드를 입력할 필요가 없다. PC가 아직 준비되지 않았다면 아이폰의 연결하고 추적 시작을 누른 상태에서 PC의 아이폰 자동 연결을 켜도 된다.

셀룰러·다른 Wi-Fi에서는 PC가 연결 대기 중일 때 표시되는 QR을 처음 한 번 등록한다. 연결 링크를 전달해 붙여넣어도 된다. 같은 Wi-Fi에서 연결했던 PC는 외부망 연결 정보도 자동으로 기억한다. 이후에는 연결하고 추적 시작을 누르면 같은 Wi-Fi 직접 연결 또는 서울 중계소 연결을 자동으로 고른다. 연결 상태에 실제 사용 중인 경로가 표시된다.

카메라·로컬 네트워크 권한을 허용한 뒤 정면을 바라보고 PC의 정면 기준 맞추기를 누른다. QR을 열거나 앱을 실행하는 것만으로 카메라가 켜지지 않는다. 중지를 누르거나 앱이 백그라운드로 이동하면 카메라와 송신을 멈춘다. 얼굴 영상은 보내지 않으며 등록 정보는 Keychain에 보관한다.

직접 연결은 기본 UDP 포트 4782, 외부망은 `wss://face.debimarlene.com/socket`을 사용한다. 기기 간 통신을 차단하는 Wi-Fi에서는 QR 등록 후 서울 경유로 연결한다.

## 빌드

Xcode와 XcodeGen이 필요하다. `project.yml`의 `DEVELOPMENT_TEAM`을 사용할 개인 개발 팀으로 지정한다.

```sh
cd iphone
xcodegen generate
xcodebuild -project KasaFace.xcodeproj -scheme KasaFace \
  -configuration Debug -destination 'generic/platform=iOS' \
  -derivedDataPath build -allowProvisioningUpdates build
```

아이폰에 설치할 때는 잠금을 해제하고 Mac을 신뢰해야 한다. 필요한 경우 iOS 설정의 개발자 모드와 개발자 앱 신뢰를 활성화한다. 개인 개발 서명은 장기 배포용 서명이 아니며 프로비저닝 프로파일의 만료 전에 다시 서명해야 한다.

```sh
swift test
```

호스트 시험은 회전 변환·등록 정보·JSON 프로토콜을 확인한다. `KASA_FACE_TEST_RELAY_URL=wss://face.debimarlene.com/socket swift test`는 실제 송신 코드의 서울 왕복·직접 연결·연결 끊김 뒤 서울 복귀를 검증한다. 실제 카메라·ARKit 추적과 네트워크 권한은 아이폰에서 확인한다. 영상은 네트워크로 전송하지 않으며 등록 정보는 Keychain에 보관한다.
