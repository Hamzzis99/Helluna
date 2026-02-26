# Helluna Phase 7 + DB 분리 — 현재 상황 & 다음 작업

> 이 문서는 두 번째 클로드코드 인스턴스에 넘길 컨텍스트입니다.
> 프로젝트 전체 마스터 프롬프트: `memory/lobby-stash-system.md` 참고

---

## 현재 상황 요약

### 완료된 작업
1. **Phase 1~7 C++ 전부 구현 완료** (빌드 미검증)
2. **DB 분리(bFileTransferOnly) 구현 완료** — 게임서버가 DB를 절대 열지 않게 수정

### 핵심 문제와 해결

**문제**: 로비서버(port 7778)와 게임서버(port 7777)가 같은 `Helluna.db`를 동시에 열면 Windows FSQLiteDatabase exclusive lock으로 "disk I/O error" 발생.

**해결**: JSON 파일 전송 패턴으로 DB 접근을 로비서버로 단일화.

| 단계 | 이전 (문제) | 이후 (해결) |
|------|------------|------------|
| 출격 | 로비→ExportLoadoutToFile (이미 존재) | 변경 없음 |
| 게임서버 인벤토리 로드 | ImportLoadoutFromFile → **DB 폴백 (lock!)** | ImportLoadoutFromFile → .sav 폴백 (DB 스킵) |
| 게임 종료 | **DB→MergeGameResultToStash (lock!)** | ExportGameResultToFile (JSON 파일만) |
| 로비 복귀 | 없음 | PostLogin step 0: ImportGameResultFromFile → MergeGameResultToStash |
| DB 재오픈 방지 | 없음 | `bFileTransferOnly=true` → TryReopenDatabase 영구 차단 |

---

## 수정된 파일 목록 (4개 + 1헤더)

### 1. `Public/Lobby/Database/HellunaSQLiteSubsystem.h`
- 추가: `ExportGameResultToFile`, `ImportGameResultFromFile`, `HasPendingGameResultFile` (public)
- 추가: `GetGameResultTransferFilePath` (private)
- 추가: `bool bFileTransferOnly = false;` (private 멤버)

### 2. `Private/Lobby/Database/HellunaSQLiteSubsystem.cpp`
- **Initialize()**: 게임서버 감지 → `bFileTransferOnly=true` + DB 열기 생략
  - `IsRunningDedicatedServer()` + `!CmdLine.Contains("L_Lobby")` 체크
  - `-NoDatabaseOpen` 커맨드라인 플래그 지원
- **TryReopenDatabase()**: `bFileTransferOnly` 가드 추가 (첫 번째 체크, 영구 차단)
- **신규 함수 4개**: ExportGameResultToFile, ImportGameResultFromFile, HasPendingGameResultFile, GetGameResultTransferFilePath
  - 기존 ExportLoadoutToFile 패턴 미러링, JSON 구조만 다름 (survived bool 대신 hero_type int)

### 3. `Private/GameMode/HellunaDefenseGameMode.cpp`
- **ProcessPlayerGameResult**: DB 직접 호출 전부 제거 → `ExportGameResultToFile` 하나로 교체
  - 제거됨: IsDatabaseReady, TryReopenDatabase, MergeGameResultToStash, DeletePlayerLoadout
  - 추가됨: ExportGameResultToFile(PlayerId, ResultItems, bSurvived)

### 4. `Private/Lobby/GameMode/HellunaLobbyGameMode.cpp`
- **PostLogin**: 기존 step 1~3 앞에 **step 0** 추가:
  ```
  0) HasPendingGameResultFile? → ImportGameResultFromFile → MergeGameResultToStash + DeletePlayerLoadout
  1) 크래시 복구 (기존)
  2) Stash 로드 (기존)
  3) Controller 매핑 (기존)
  ```

---

## 미해결 이슈

### 1. LoadPlayerStash IsValid() 실패 (빌드 후 확인 필요)
- **증상**: MergeGameResultToStash 성공 → LoadPlayerStash에서 모든 행이 IsValid() 실패 (Stack=0)
- **FInv_SavedItemData::IsValid()** = `ItemType.IsValid() && StackCount > 0` (Inv_PlayerController.h:232)
- **원인 추정**: DB lock으로 인한 읽기 실패였을 가능성 높음 (bFileTransferOnly로 해결됨)
- **만약 여전히 발생 시**: CollectInventoryDataForSave()가 StackCount=0을 반환하는지 확인
  - `Inv_InventoryComponent.cpp:2956` — `Entry.Item->GetTotalStackCount()`
  - ExportGameResultToFile의 JSON에 stack_count가 정상 기록되는지 로그 확인

### 2. Phase 7 BP 설정 미완료
- BP_DefenseGameMode → `LobbyServerURL`, `GameResultWidgetClass`
- BP_HellunaHeroController → `GameResultWidgetClass`
- WBP_HellunaGameResultWidget 위젯 BP 생성

---

## 서버 실행 방법

```bash
# 빌드 디렉토리
D:\UnrealProject\Capston_Project\Helluna\Build\WindowsServer\

# 로비서버 (DB 사용)
HellunaServer.exe L_Lobby -log -port=7778

# 게임서버 (DB 미사용 — bFileTransferOnly 자동 감지)
HellunaServer.exe -log -LobbyURL=127.0.0.1:7778

# PIE 클라이언트 → Net Mode: Play As Client, Server: 127.0.0.1:7778
```

- DB 경로: `{Build}/Helluna/Saved/Database/Helluna.db` (FPaths::ProjectSavedDir() 자동)
- 같은 exe에서 실행하므로 로비/게임 모두 같은 DB 경로 공유
- `-DatabasePath=` 옵션: 특수한 경우에만 오버라이드 (보통 불필요)

---

## 테스트 시나리오 (빌드 후)

### T1: 정상 게임 완료 (핵심)
1. 로비서버 + 게임서버 실행
2. PIE 클라이언트 → 로비 접속 (7778)
3. 출격 → 게임서버 접속 (7777)
4. `EndGame Escaped` 실행
5. **확인**: `Transfer/GameResult_*.json` 생성됨
6. 로비 복귀 → PostLogin step 0 로그 확인
7. **확인**: Stash에 게임 아이템 병합됨

### T2: 게임서버 크래시 복구
1. 출격 → 게임서버 프로세스 강제 종료 (Alt+F4)
2. 로비 재접속
3. **확인**: GameResult 파일 없음 → 크래시 복구 → Loadout이 Stash로 복구

### T3: DB lock 미발생 확인
1. 로비서버 + 게임서버 동시 실행
2. 게임서버 로그: `게임서버 감지 (L_Lobby 없음) → DB 열기 생략`
3. 게임 진행 중 `TryReopenDatabase` 호출 시: `파일 전송 전용 모드 → DB 열기 차단`
4. **확인**: 로비서버가 "disk I/O error" 없이 정상 동작

### T4: 사망 테스트
1. 게임 중 캐릭터 사망
2. `EndGame Escaped` → 사망자는 빈 배열 Export
3. 로비 복귀 → Merge(빈) → Stash에 게임 아이템 없음

---

## 참고 파일 위치 (코드 읽기용)

| 핵심 파일 | 절대 경로 |
|-----------|----------|
| SQLite 헤더 | `Source/Helluna/Public/Lobby/Database/HellunaSQLiteSubsystem.h` |
| SQLite 구현 | `Source/Helluna/Private/Lobby/Database/HellunaSQLiteSubsystem.cpp` |
| 게임 GameMode | `Source/Helluna/Private/GameMode/HellunaDefenseGameMode.cpp` |
| 로비 GameMode | `Source/Helluna/Private/Lobby/GameMode/HellunaLobbyGameMode.cpp` |
| SavedItemData 정의 | `Plugins/Inventory/Source/Inventory/Public/Player/Inv_PlayerController.h:93` |
| InvComp 수집 | `Plugins/Inventory/Source/Inventory/Private/InventoryManagement/Components/Inv_InventoryComponent.cpp:2911` |
