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

## 문제 해결

환경 변수 `MOVIE_PLAYER_MPV_LOG`에 파일 경로를 지정하면 mpv 로그를 남깁니다. 하드웨어 디코딩 사용 여부나 선택된 자막 트랙을 확인할 때 유용합니다.
