# Phase 4: 로비 듀얼 Grid UI — 구현 가이드

## 완료된 작업 요약

### C++ 코드 변경 (빌드 필요)

| 파일 | 변경 내용 | 상태 |
|---|---|---|
| `Plugins/.../Inv_SpatialInventory.h` | `SetInventoryComponent()`, `BoundInventoryComponent`, `GetBoundInventoryComponent()` 추가 | 완료 |
| `Plugins/.../Inv_SpatialInventory.cpp` | SetInventoryComponent 구현 + 4곳 GetInventoryComponent/GetInventoryWidget → 캐시 우선으로 변경 | 완료 |
| `Source/.../HellunaLobbyStashWidget.h` | `LoadoutPanel` (HellunaLobbyPanel) → `LoadoutSpatialInventory` (Inv_SpatialInventory) 교체 | 완료 |
| `Source/.../HellunaLobbyStashWidget.cpp` | InitializePanels에서 SpatialInventory::SetInventoryComponent(LoadoutComp) 호출 | 완료 |
| `Source/.../HellunaLobbyGameMode.cpp` | PlayerId 디버그 모드 고정 ("DebugPlayer") | 완료 |
| `Source/.../HellunaLobbyGameMode.h` | GetLobbyPlayerId() 디버그 모드 고정 | 완료 |
| `Source/.../HellunaLobbyLog.h` (신규) | 공유 DECLARE_LOG_CATEGORY_EXTERN (unity build 에러 해결) | 완료 |
| `Plugins/.../Inv_InventoryGrid.cpp` | bSkipAutoInit 가드를 ConstructGrid 앞으로 이동 + SetInventoryComponent 시 deferred ConstructGrid | 완료 |

### 이전 세션에서 해결된 버그
- **C2011 struct redefinition**: unity build 시 로그 카테고리 중복 → 공유 헤더 HellunaLobbyLog.h 생성
- **C2039 IsServer not found**: `GetWorld()->IsServer()` → `GetWorld()->GetNetMode()` 로 변경
- **EXCEPTION_ACCESS_VIOLATION (ConstructGrid)**: CanvasPanel nullptr → bSkipAutoInit 가드 위치 수정
- **PlayerId mismatch**: DebugSave("DebugPlayer") vs PostLogin(random) → 디버그 모드 시 고정 ID

---

## BP 수정 가이드 (에디터에서 수행)

### 1. WBP_HellunaLobbyStashWidget 수정

**중요**: BindWidget 이름이 C++ 코드와 정확히 일치해야 합니다!

#### 기존 구조 (삭제 대상):
```
WBP_HellunaLobbyStashWidget
├── StashPanel (HellunaLobbyPanel)     ← 유지
├── LoadoutPanel (HellunaLobbyPanel)   ← 삭제!
└── Button_Deploy (Button)             ← 유지
```

#### 새 구조:
```
WBP_HellunaLobbyStashWidget
├── StashPanel (HellunaLobbyPanel)              ← 유지 (변경 없음)
├── LoadoutSpatialInventory (Inv_SpatialInventory) ← 신규!
└── Button_Deploy (Button)                      ← 유지 (변경 없음)
```

#### 수정 순서:
1. **기존 `LoadoutPanel` 위젯 삭제** (HellunaLobbyPanel 타입)
2. **새 위젯 추가**: 이름 = `LoadoutSpatialInventory`
   - 타입: **WBP_Inv_SpatialInventory** (인게임에서 쓰던 기존 BP)
   - 위치: StashPanel 오른쪽 (기존 LoadoutPanel 자리)
3. **내부 Grid 설정** (WBP_Inv_SpatialInventory 내부):
   - `Grid_Equippables` → Details → `bSkipAutoInit` = **true** 체크
   - `Grid_Consumables` → Details → `bSkipAutoInit` = **true** 체크
   - `Grid_Craftables` → Details → `bSkipAutoInit` = **true** 체크
4. 저장 후 컴파일

### 2. Stash Grid 크기 조절 (선택 사항)

Stash는 큰 창고이므로 Grid를 크게 하고 싶으면:

1. `Inv_InventoryGrid`를 상속하는 **BP_Inv_LobbyInventoryGrid** 생성
2. Rows, Columns, TileSize를 크게 설정 (예: Rows=12, Columns=8)
3. WBP_HellunaLobbyPanel의 Grid 3개를 이 BP로 교체

### 3. 확인 사항

- StashPanel 내부 Grid 3개도 `bSkipAutoInit = true` 설정되어 있는지 확인
- 모든 BindWidget 이름이 정확한지 확인:
  - `StashPanel` (HellunaLobbyPanel)
  - `LoadoutSpatialInventory` (Inv_SpatialInventory)
  - `Button_Deploy` (Button)

---

## 테스트 방법

### 1차: 기본 UI 표시 테스트
1. 에디터에서 빌드 (Ctrl+Shift+B 또는 빌드 버튼)
2. `Helluna.SQLite.DebugSave` 콘솔 명령 → Stash에 테스트 데이터 저장
3. 로비 맵 PIE (As a Client 모드) 실행
4. 확인사항:
   - StashPanel에 Stash 아이템 표시 여부
   - LoadoutSpatialInventory에 빈 Grid + 장착 슬롯 표시 여부
   - 로그에서 `[SpatialInventory] SetInventoryComponent 완료` 확인
   - 로그에서 `[StashWidget] LoadoutSpatialInventory ← LoadoutComp 바인딩 완료` 확인

### 2차: 아이템 전송 테스트
- 아이템 전송은 현재 Server RPC (`Server_TransferItem`) 기반 버튼 방식
- UI 버튼이 아직 없으면 콘솔이나 디버그에서 직접 호출

### 3차: 출격 테스트
- 출격 버튼 클릭 → `[StashWidget] 출격 버튼 클릭!` 로그 확인
- `Helluna.SQLite.DebugLoad` → DB에 Loadout 데이터 저장 확인

---

## 현재 아키텍처 요약

```
┌─── 로비 맵 (L_Lobby) ───────────────────────────────────────┐
│                                                              │
│  AHellunaLobbyGameMode (GameMode)                            │
│    └── PostLogin: 크래시복구 → SQLite Stash 로드              │
│    └── Logout: SQLite 저장                                   │
│                                                              │
│  AHellunaLobbyController (PlayerController)                  │
│    ├── StashInventoryComponent (Stash InvComp)               │
│    ├── LoadoutInventoryComponent (Loadout InvComp)           │
│    └── ShowLobbyWidget() → StashWidget 생성                  │
│                                                              │
│  UHellunaLobbyStashWidget (메인 UI)                          │
│    ├── StashPanel (HellunaLobbyPanel)                        │
│    │   └── Grid 3개 ← StashComp 바인딩                       │
│    ├── LoadoutSpatialInventory (Inv_SpatialInventory) ★신규  │
│    │   ├── 장착 슬롯 (EquippedGridSlots) ← 인게임 동일      │
│    │   └── Grid 3개 ← LoadoutComp 바인딩                     │
│    └── Button_Deploy (출격 버튼)                              │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

---

## 앞으로 할 일 (Phase 4 잔여 + Phase 5~7)

### Phase 4 잔여 작업

| 순서 | 작업 | 설명 |
|---|---|---|
| 4-A | Stash↔Loadout 아이템 전송 UI | 우클릭 → "Loadout으로 보내기" 팝업 or 전송 버튼 |
| 4-B | 드래그앤드롭 크로스 패널 | SharedHoverItem으로 패널 간 드래그앤드롭 |
| 4-C | Stash Grid 크기 커스텀 | BP_Inv_LobbyInventoryGrid 생성 (Rows/Columns 큰 값) |

### Phase 5: 출격 파이프라인
- 출격 버튼 → LoadoutComp 아이템 수집 → SQLite SavePlayerLoadout + Stash 차감 → ClientTravel

### Phase 6: 게임서버 투입
- DefenseGameMode PostLogin에서 LoadPlayerLoadout → AddItemFromManifest → DeletePlayerLoadout

### Phase 7: 게임 종료 + 결과 반영
- 사망: 전부 손실 / 탈출 성공: 전부 유지
- MergeGameResultToStash → ClientTravel(로비)

---

## 다음 세션 마스터 프롬프트

```
# Helluna 프로젝트 — Phase 4 Lobby UI 이어서

## 현재 상태
- Phase 1~3 완료 (SQLite 백엔드 + SaveGameMode 전환)
- Phase 4 진행 중:
  - Step 4-0~4-4 완료: Inv_InventoryGrid bSkipAutoInit, LobbyGameMode, LobbyController,
    LobbyPanel, LobbyStashWidget
  - Inv_SpatialInventory에 SetInventoryComponent() 추가 완료
  - HellunaLobbyStashWidget의 LoadoutPanel → LoadoutSpatialInventory 교체 완료
  - BP 수정 필요: WBP_HellunaLobbyStashWidget에서 LoadoutPanel 삭제,
    LoadoutSpatialInventory (Inv_SpatialInventory) 추가

## 다음 할 일
1. BP 수정 후 빌드 + PIE 테스트 (Stash 표시 + Loadout SpatialInventory 표시 확인)
2. Stash↔Loadout 아이템 전송 UI 구현 (우클릭 팝업 or 버튼)
3. (선택) 드래그앤드롭 크로스 패널 구현

## 가이드 파일
- Source/Helluna/Phase4_LobbyUI_Guide.md — BP 수정 방법, 테스트 절차, 향후 로드맵

## 핵심 규칙
- UE 5.7.2, C++ + BP hybrid, Dedicated Server, As a Client 모드
- 드래그앤드롭 관련 코드에 TODO: [DragDrop] 주석 필수
- 디버그 로그 풍부하게, 주석도 풍부하게
- 로비와 인게임은 완전 분리 (로비=SQLite 저장, 인게임=SQLite 로드)
- 빌드 도구: D:\UnrealServer_Build\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe

## 핵심 파일
- Plugins/Inventory/.../Inv_SpatialInventory.h/.cpp (SetInventoryComponent 추가됨)
- Plugins/Inventory/.../Inv_InventoryGrid.h/.cpp (bSkipAutoInit 추가됨)
- Source/Helluna/.../HellunaLobbyStashWidget.h/.cpp (LoadoutSpatialInventory)
- Source/Helluna/.../HellunaLobbyController.h/.cpp (StashComp + LoadoutComp)
- Source/Helluna/.../HellunaLobbyGameMode.h/.cpp (PostLogin/Logout + SQLite)
- Source/Helluna/.../HellunaSQLiteSubsystem.h/.cpp (SQLite CRUD)
```
