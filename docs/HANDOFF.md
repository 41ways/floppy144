# 맥 작업 인계 (2026-08-31)

## 시작
```bash
git pull origin main
```
읽는 순서: `docs/DESIGN.md`(전체 설계) → 이 문서 → `src/game.c` 상단 tuning 블록.

## 빌드·실행의 현실
- 코드는 **Win32 전용** (WGL·waveOut·GDI). 맥에서는 **빌드도 실행도 안 됨** — 맥에서의 작업은 코드 수정 + 푸시까지.
- 푸시하면 GitHub Actions가 mingw-w64로 크로스 컴파일, 1,474,560B 게이트 검사, `SOUNDING-win64` 아티팩트 업로드 + **releases/latest 자동 갱신** (로그인 불필요): https://github.com/41ways/floppy144/releases/download/latest/SOUNDING.exe
- 윈도우 PC에서 받기: `sh update.sh` 또는 위 링크. 로컬 빌드(윈도우): WinLibs mingw 설치돼 있음, `Makefile` 참조.

## 테스트 하네스 (맥에서 코드만 보고도 검증 설계 가능)
- `SOUNDING.exe "-shot <파일.raw> <프레임> <핑주기> [거미타입]"` — 고정 시드·고정 타임스텝 스크립트 런. 프레임버퍼를 raw로 덤프, `shotlog.txt`에 20프레임마다 `state/pos/travelled`, 셰이더 에러는 `faillog.txt`(모달 대신).
  - 핑주기 `-1`=클릭 없음(타이틀 측정 함정 주의!), `9999`=시작 클릭 1회만, `40`=40프레임마다(단 `frame < shot_at-300`에서 멈추는 가드 있음 — 문 테스트 시 함정).
- `-depth N` 또는 빌드시 `-DSTART_DEPTH=N.0f` — 해당 깊이 스폰(통과 게이트 자동 반영).
- `tools/raw2png.py <디렉>` — raw→png.
- 미로 연결성: 이 대화에서 쓴 BFS 스니펫은 CPU `cave_sdf`의 홀 분기를 JS로 미러링한 것. **필드 수정 시 반드시 재실행** (막힌 시드 = 게임 전체 봉쇄).

## 상태 기계
`ST_TITLE → ST_PLAY ↔ ST_MENU_(Esc) → ST_ROAD(문) → ST_WAKE(램프→소등→눈뜨기)`, 사망시 `ST_FLATLINE`. `ST_SURFACE`는 폐기됨(코드 잔존).

## 방금 추가된 것 (미실기검증 항목 포함)
- **Esc 메뉴**: CONTINUE/SETTING(볼륨 A/D)/EXIT. W/S 이동, Enter/클릭 선택. **컴파일만 검증됨 — 실기 미확인**.
- **홀 미로**: 7m마다 가로벽+출입구 1개(row hash), 기둥 간 차선 봉쇄벽 p=0.36. 출입구는 만물 관통 강제 개방(반폭 1.6m). BFS 검증: 문 도달 OK, 고립 0%.
- 백룸 발소리(1.05m마다), 가이드 펄스시 화면 백색 맥동(`uPulse`), 대사 큐(`g_note`).
- 조명: wrap diffuse + 반구 앰비언트 + 바닥 바운스 (달표면 룩 제거).

## 알려진 함정
1. `-shot` 검증 시 위 핑주기 가드 두 개.
2. 문자열 패치 스크립트로 코드 수정하는 방식 쓰는 중 — 앵커 불일치→부분 적용→중복 정의 사고 이력 있음. 수정 후 `grep -c`로 중복 확인.
3. 셰이더 uniform은 **사용하는 스테이지에 선언** (uFlat/uGrey 사고 2회).
4. 일시정지 중 `now`가 계속 흘러 진행 중 파동이 점프할 수 있음(수용된 결함).
5. 한글은 UTF-8 소스 → `MultiByteToWideChar`+`TextOutW` 경로만 정상.

## 다음 후보 (미착수)
플레이테스트 기반 튜닝 / 매복 12마리 난이도 / 출구 표지판 실루엣 / ST_SURFACE·upload_room 정리 / 제출문·스크린샷 (마감 9/4 23:39).
