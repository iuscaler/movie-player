# 동영상 플레이어

Windows 용 동영상 플레이어

C++ / Win32 + [libmpv](https://mpv.io/manual/master/#embedding-into-other-programs-libmpv) 기반입니다. 지금은 영상 재생만 되는 테스트 버전입니다.

## 빌드

Visual Studio 2026의 "C++를 사용한 데스크톱 개발" 워크로드가 필요합니다. libmpv는 처음 구성할 때 `third_party/`에 자동으로 받습니다.

- **Visual Studio:** 이 폴더를 열고(폴더 열기) 구성 `Visual Studio 2026 (x64)`를 선택한 뒤 빌드합니다.
- **명령줄(Developer PowerShell):**

  ```powershell
  cmake --preset vs2026-x64
  cmake --build --preset release
  ```

## 실행

```powershell
build\Release\MoviePlayer.exe "동영상 경로"
```

경로를 주지 않으면 파일 선택 창이 열립니다.

## 구조

| 파일 | 역할 |
|---|---|
| `src/main.cpp` | 시작할 파일 결정, 메시지 루프 |
| `src/MainWindow.*` | Win32 창과 입력 처리. mpv가 그릴 영상 자식 창을 가짐 |
| `src/ControlBar.*` | 영상 위에 합성되는 재생 바 (Direct2D + DirectComposition) |
| `src/ContextMenu.*` | 우클릭 메뉴 |
| `src/FileAssociation.*` | 탐색기 연결 프로그램 등록과 해제 |
| `src/FileDialogs.*` | 파일 선택 창, 파일 종류 판별 |
| `src/Settings.*` | 설정 저장과 불러오기 |
| `src/SmiSubtitles.*` | 여러 언어가 든 SMI 파일을 언어별로 분리 |
| `src/MpvPlayer.*` | libmpv 호출을 모두 담당 |

## 조작

| 입력 | 동작 |
|---|---|
| `Space` | 재생/일시정지 (영상이 끝난 뒤에는 처음부터 재생) |
| `←` / `→` | 5초 뒤로/앞으로 |
| `↑` / `↓`, 마우스 휠 | 볼륨 조절 |
| `Enter`, 더블클릭 | 전체화면 전환 |
| `Esc` | 전체화면 해제 |
| 파일 끌어다 놓기 | 해당 파일 재생 |
| 재생 바 버튼, 탐색 트랙 | 재생/일시정지, 클릭 또는 드래그로 탐색 |
| 우클릭 | 파일 열기, 자막/오디오 트랙 선택, 자막 인코딩 메뉴 |
| `S` / `A` | 자막/오디오 트랙 전환 |
| `,` / `.` / `/` | 자막 싱크 0.1초 빠르게/느리게/초기화 |
| 자막 파일 끌어다 놓기 | 현재 영상에 자막 추가 |

영상과 이름이 같거나 이름을 포함하는 자막 파일은 자동으로 불러옵니다. 유니코드가 아닌 자막은 CP949로 읽으며, 다른 인코딩은 메뉴의 "자막 인코딩 > 자동 감지"로 읽을 수 있습니다. 한 SMI 파일에 여러 언어가 있으면 언어별 트랙으로 나누고 한국어를 먼저 선택합니다.

## 탐색기에서 열기

우클릭 메뉴의 "탐색기 연결 프로그램에 등록"을 누르면 영상 파일의 "연결 프로그램" 목록에 추가됩니다. 현재 사용자 레지스트리에만 기록하며 기존 기본 앱은 바꾸지 않습니다. 더블클릭으로 바로 열려면 Windows 기본 앱 설정에서 직접 지정하세요. 실행 파일을 옮겼다면 다시 등록해야 합니다.

이미 실행 중이면 새 창을 띄우지 않고 기존 창에서 엽니다.

## 설정

볼륨, 창 크기와 위치, 자막 인코딩은 `%APPDATA%\MoviePlayer\settings.ini`에 저장되어 다음 실행에도 유지됩니다.

## 문제 해결

환경 변수 `MOVIE_PLAYER_MPV_LOG`에 파일 경로를 지정하면 mpv 로그를 남깁니다. 하드웨어 디코딩 사용 여부나 선택된 자막 트랙을 확인할 때 유용합니다.
