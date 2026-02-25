# Phase 4 + Phase 5 통합 가이드 (BP 생성 + 테스트)

---

## BP 생성 가이드

### 생성 순서 (의존성 기준)
```
① BP_HellunaLobbyGameMode      (의존 없음)
② BP_HellunaLobbyController    (의존 없음, 위젯은 나중에 지정)
③ WBP_HellunaLobbyPanel        (의존 없음, Grid 설정이 핵심)
④ WBP_HellunaLobbyStashWidget  (③에 의존)
⑤ ②로 돌아가서 → 로비 창고 위젯 클래스 = ④ 지정
⑥ ①로 돌아가서 → Player Controller Class = ② 지정
⑦ 로비 맵 World Settings → GameMode Override = ① 지정
```

---

### ① BP_HellunaLobbyGameMode

#### 이게 뭐야?
로비 맵 전용 게임모드. 플레이어가 로비에 들어오면(PostLogin) SQLite에서 창고 아이템을 불러오고, 나가면(Logout) 현재 창고 상태를 SQLite에 저장한다. 크래시 복구도 여기서 처리한다.

#### 만드는 법
1. Content Browser에서 우클릭 → Blueprint Class
2. All Classes 검색 → `HellunaLobbyGameMode` 선택 → Create
3. 이름: `BP_HellunaLobbyGameMode`
4. 더블클릭으로 열기 → Class Defaults (클래스 기본값)

#### 설정할 항목
| 항목 | 위치 | 값 | 설명 |
|---|---|---|---|
| Player Controller Class | Classes 섹션 | `BP_HellunaLobbyController` (②에서 생성 후 지정) | 로비에 접속하는 플레이어에게 어떤 Controller를 부여할지 |
| Default Pawn Class | Classes 섹션 | `None` | 로비에서는 3D 캐릭터 필요 없음 (이미 C++에서 nullptr 설정됨, 확인만) |
| bDebugSkipLogin | 커스텀 섹션 | `true` 체크 | PIE 테스트용. true면 PlayerId를 "debug_lobby_player"로 고정. 실제 서버에서는 false |

#### 주의
- Player Controller Class는 ②를 먼저 만든 뒤에 설정 (⑥단계)
- bDebugSkipLogin을 안 켜면 PIE에서 PlayerId가 비어서 Stash 로드가 스킵됨

---

### ② BP_HellunaLobbyController

#### 이게 뭐야?
로비에서 플레이어 한 명을 대표하는 컨트롤러. 내부에 창고(StashComp)와 출격장비(LoadoutComp) 2개의 인벤토리 컴포넌트를 갖고 있다. 아이템 전송 RPC, 출격 RPC, 로비 UI 생성을 담당한다.

#### 만드는 법
1. Content Browser에서 우클릭 → Blueprint Class
2. All Classes 검색 → `HellunaLobbyController` 선택 → Create
3. 이름: `BP_HellunaLobbyController`
4. 더블클릭으로 열기 → Class Defaults

#### 설정할 항목
| 항목 | 위치 | 값 | 설명 |
|---|---|---|---|
| 로비 창고 위젯 클래스 | 로비\|UI 카테고리 | `WBP_HellunaLobbyStashWidget` (④에서 생성 후 지정) | BeginPlay에서 이 위젯을 CreateWidget으로 생성하여 화면에 표시 |
| 게임 맵 URL | 로비\|출격 카테고리 | 비워두기 (테스트 단계) | 출격 버튼 누르면 이 URL로 ClientTravel. 나중에 `/Game/Maps/L_Defense?listen` 같은 값 입력 |

#### 주의
- 로비 창고 위젯 클래스는 ④를 먼저 만든 뒤에 설정 (⑤단계)
- 게임 맵 URL이 비어있으면 출격 버튼 눌러도 맵 이동 안 함 (테스트 단계에서는 정상)
- StashComp, LoadoutComp는 C++ 생성자에서 자동 생성됨 → BP에서 건드릴 필요 없음

---

### ③ WBP_HellunaLobbyPanel (가장 중요하고 복잡)

#### 이게 뭐야?
인벤토리 패널 하나. 장비/소모품/재료 3개 탭이 있고, 각 탭에 테트리스 Grid가 하나씩 있다. WidgetSwitcher로 탭을 전환한다. 이 위젯을 2개 만들어서 하나는 창고(Stash), 하나는 출격장비(Loadout)로 사용한다.

```
┌──────────────────────────────────┐
│ [장비] [소모품] [재료]  ← 탭 버튼 │
│ ┌──────────────────────────────┐ │
│ │                              │ │
│ │    테트리스 Grid (탭별)       │ │
│ │                              │ │
│ └──────────────────────────────┘ │
└──────────────────────────────────┘
```

#### 만드는 법
1. Content Browser에서 우클릭 → User Interface → Widget Blueprint
2. 부모 클래스 선택 창이 뜨면 → All Classes에서 `HellunaLobbyPanel` 검색 → 선택
3. 이름: `WBP_HellunaLobbyPanel`
4. 더블클릭으로 열기 → Designer 탭

#### 위젯 트리 만들기 (순서대로)

**아래 위젯 이름은 BindWidget이므로 대소문자 포함 정확히 일치해야 한다! 틀리면 크래시남.**

```
[Root] (Canvas Panel — 기본 생성됨)
└─ VerticalBox
   │
   ├─ Text_PanelTitle            (선택적)
   │   → 타입: TextBlock
   │   → 팔레트에서 TextBlock 드래그 → 이름을 Text_PanelTitle으로 변경
   │   → 없어도 됨 (BindWidgetOptional이라 크래시 안 남)
   │
   ├─ HorizontalBox             (탭 버튼 영역)
   │  │
   │  ├─ Button_Equippables
   │  │   → 타입: Button
   │  │   → 팔레트에서 Button 드래그 → 이름을 Button_Equippables로 변경
   │  │   → Button 안에 TextBlock 넣고 텍스트 "장비" 입력
   │  │
   │  ├─ Button_Consumables
   │  │   → 타입: Button
   │  │   → 이름을 Button_Consumables로 변경
   │  │   → 안에 TextBlock "소모품"
   │  │
   │  └─ Button_Craftables
   │      → 타입: Button
   │      → 이름을 Button_Craftables로 변경
   │      → 안에 TextBlock "재료"
   │
   └─ Switcher                   (Grid 전환용)
       → 타입: WidgetSwitcher
       → 팔레트에서 Widget Switcher 드래그 → 이름을 Switcher로 변경
       │
       ├─ Grid_Equippables
       │   → 타입: Inv_InventoryGrid (팔레트에서 검색)
       │   → 이름을 Grid_Equippables로 변경
       │
       ├─ Grid_Consumables
       │   → 타입: Inv_InventoryGrid
       │   → 이름을 Grid_Consumables로 변경
       │
       └─ Grid_Craftables
           → 타입: Inv_InventoryGrid
           → 이름을 Grid_Craftables로 변경
```

#### 각 Grid Details 패널 설정 (3개 모두!)

Grid_Equippables를 클릭하고 우측 Details 패널에서:

| 항목 | 값 | 설명 |
|---|---|---|
| **자동 초기화 스킵 (bSkipAutoInit)** | **true 체크** | **이걸 안 하면 전부 망한다!** false면 Grid가 자동으로 첫 번째 InvComp만 잡아서 Stash/Loadout 구분 불가 |
| Item Category | `Equippables` | 이 Grid가 장비만 표시 |
| Rows | 8 (원하는 값) | Grid 세로 칸 수 |
| Columns | 6 (원하는 값) | Grid 가로 칸 수 |
| Tile Size | 64 (기존 인게임과 동일) | 한 칸 픽셀 크기 |
| Grid Slot Class | 기존 인게임 Grid에서 쓰는 것과 동일 | 각 칸을 표시하는 위젯 클래스 |
| Slotted Item Class | 기존 인게임 Grid에서 쓰는 것과 동일 | Grid에 배치된 아이템을 표시하는 위젯 |
| Hover Item Class | 기존 인게임 Grid에서 쓰는 것과 동일 | 마우스로 아이템 잡았을 때 표시되는 위젯 |

Grid_Consumables도 동일하게 설정 (Item Category만 `Consumables`로)
Grid_Craftables도 동일하게 설정 (Item Category만 `Craftables`로)

#### 기존 인게임 Grid 설정값 참고하는 법
기존 인게임에서 사용 중인 WBP (예: WBP_SpatialInventory)를 열어서
Grid의 Details에서 Grid Slot Class, Slotted Item Class, Hover Item Class, Tile Size 값을 확인하고 동일하게 입력.

---

### ④ WBP_HellunaLobbyStashWidget

#### 이게 뭐야?
로비 메인 화면. 좌측에 창고(Stash) 패널, 우측에 출격장비(Loadout) 패널, 하단에 출격 버튼이 있다. 타르코프의 창고 화면을 생각하면 된다.

```
┌─── 창고 패널 (좌) ──────┐  ┌─── 출격장비 패널 (우) ──┐
│ [장비][소모품][재료]     │  │ [장비][소모품][재료]     │
│ ┌─────────────────────┐ │  │ ┌─────────────────────┐ │
│ │     Grid (탭별)     │ │  │ │     Grid (탭별)     │ │
│ └─────────────────────┘ │  │ └─────────────────────┘ │
└─────────────────────────┘  └─────────────────────────┘
                    [ 출격 버튼 ]
```

#### 만드는 법
1. Content Browser에서 우클릭 → User Interface → Widget Blueprint
2. 부모 클래스: `HellunaLobbyStashWidget` 검색 → 선택
3. 이름: `WBP_HellunaLobbyStashWidget`
4. 더블클릭으로 열기 → Designer 탭

#### 위젯 트리 만들기

**이름 정확히 일치 필수 (BindWidget)**

```
[Root] (Canvas Panel)
└─ VerticalBox
   │
   ├─ HorizontalBox             (좌우 패널 영역)
   │  │
   │  ├─ StashPanel
   │  │   → 타입: WBP_HellunaLobbyPanel (③에서 만든 위젯)
   │  │   → 팔레트에서 "WBP_HellunaLobbyPanel" 검색 → 드래그
   │  │   → 이름을 StashPanel으로 변경
   │  │   → 이게 좌측 창고 패널이 됨
   │  │
   │  └─ LoadoutPanel
   │      → 타입: WBP_HellunaLobbyPanel (동일한 클래스를 또 드래그)
   │      → 이름을 LoadoutPanel으로 변경
   │      → 이게 우측 출격장비 패널이 됨
   │
   └─ Button_Deploy              (출격 버튼)
       → 타입: Button
       → 이름을 Button_Deploy로 변경
       → Button 안에 TextBlock 넣고 텍스트 "출격" 입력
```

#### 핵심 주의
- StashPanel과 LoadoutPanel은 **같은 WBP_HellunaLobbyPanel** 클래스의 **서로 다른 인스턴스**
- 팔레트에서 WBP_HellunaLobbyPanel을 **2번** 드래그해서 각각 이름만 변경
- C++에서 InitializePanels() 호출 시 StashPanel에는 StashComp를, LoadoutPanel에는 LoadoutComp를 자동으로 바인딩함

---

### ⑤⑥⑦ 되돌아가서 연결

| 단계 | 어디서 | 뭘 설정 | 값 |
|---|---|---|---|
| ⑤ | BP_HellunaLobbyController | 로비 창고 위젯 클래스 | WBP_HellunaLobbyStashWidget |
| ⑥ | BP_HellunaLobbyGameMode | Player Controller Class | BP_HellunaLobbyController |
| ⑦ | 로비 맵 World Settings | GameMode Override | BP_HellunaLobbyGameMode |

---

## Phase 5 테스트

### 테스트 5-1: GameMode + Controller 연결 확인
- 목적: GameMode가 올바른 Controller를 스폰하고, SQLite 연결이 되는지
- 방법: 로비 맵 PIE 실행 → Output Log에서 `LogHellunaLobby` 필터
- 정상 로그:
```
[LobbyGM] PostLogin 시작 | PC=BP_HellunaLobbyController_0
[LobbyGM] LobbyPC 캐스팅 성공 | StashComp=O | LoadoutComp=O
[LobbyGM] SQLite 서브시스템 캐시: 성공
```
- 실패 시:

| 로그 | 원인 | 해결 |
|---|---|---|
| PostLogin 로그 자체가 안 뜸 | GameMode 미적용 | 맵 World Settings → GameMode Override 확인 (⑦) |
| Controller가 HellunaLobbyController가 아닙니다 | PC 클래스 오류 | BP_LobbyGameMode → Player Controller Class 확인 (⑥) |
| SQLite 서브시스템 없음 | DB 미초기화 | Helluna.Build.cs SQLite 모듈 확인 |

### 테스트 5-2: Stash 로드 확인 (빈 상태)
- 목적: SQLite에서 Stash를 읽는 경로가 정상인지 (데이터 없어도 OK)
- 방법: 테스트 5-1 로그 이어서 확인
- 정상 로그:
```
[LobbyGM] [1/3] 크래시 복구 체크...
[LobbyGM] [2/3] Stash 로드...
[LobbyGM] SQLite Stash 로드 완료 | 아이템 0개
[LobbyGM] Stash가 비어있음 → 빈 인벤토리로 시작
[LobbyGM] [3/3] Controller-PlayerId 매핑 등록...
[LobbyGM] PostLogin 완료
```
- 아이템 0개 = 정상 (아직 데이터 안 넣었으므로)

### 테스트 5-3: 테스트 데이터 삽입 + Stash 표시
- 목적: SQLite에 아이템을 넣고 로비에서 Grid에 표시되는지
- 방법:
  1. PIE 콘솔(` 키)에서: `Helluna.SQLite.DebugSave`
  2. PIE 종료 → 다시 PIE 실행 (로비맵 재로드)
- 정상 로그:
```
[LobbyGM] SQLite Stash 로드 완료 | 아이템 N개
[LobbyGM]   [0] ItemType=Item.xxx | Stack=1 | GridPos=(0,0)
[LobbyGM] RestoreFromSaveData 호출 → StashComp에 N개 아이템 복원 시작
[LobbyGM] StashComp 복원 완료
```
- 화면 좌측 창고 Grid에 아이템 아이콘이 보이면 성공

### 테스트 5-4: UI 생성 확인
- 목적: 위젯이 정상 생성되어 화면에 표시되는지
- 방법: PIE 실행 후 로그 확인 + 화면 확인
- 정상 로그:
```
[LobbyPC] BeginPlay | IsLocalController=true
[LobbyPC] 로컬 컨트롤러 → 0.5초 후 ShowLobbyWidget 예약
[LobbyPC] CreateWidget 시작
[LobbyPC] 위젯 AddToViewport 완료
[LobbyPC] InitializePanels 호출
[LobbyPC] ShowLobbyWidget 완료
```
- 화면에 창고(좌) + 출격장비(우) + 출격 버튼이 보이면 성공
- 실패 시:

| 로그 | 원인 | 해결 |
|---|---|---|
| LobbyStashWidgetClass가 설정되지 않음 | 위젯 미지정 | BP_LobbyController → 로비 창고 위젯 클래스 설정 (⑤) |
| CreateWidget 실패 | BindWidget 이름 오류 | WBP에서 StashPanel/LoadoutPanel/Button_Deploy 이름 확인 |

### 테스트 5-5: 출격 (Deploy) — 나중에 해도 됨
- 목적: 출격 버튼 → SQLite 저장 → 맵 이동
- 방법:
  1. BP_LobbyController에서 게임 맵 URL 설정 (예: /Game/Maps/L_Defense?listen)
  2. Loadout에 아이템 있는 상태에서 출격 버튼 클릭
- 정상 로그:
```
[LobbyPC] Server_Deploy 시작
[LobbyPC] Deploy [3a]: 잔여 Stash 저장 성공
[LobbyPC] Deploy [3b]: SavePlayerLoadout 성공
[LobbyPC] Deploy [4]: Client_ExecuteDeploy → /Game/Maps/...
```
- 게임 맵 자체가 없으면 이동 에러 뜨지만 SQLite 저장은 확인 가능
- Phase 6 전에 하면 됨

### 테스트 5-6: Logout 저장 확인
- 목적: PIE 종료 시 창고 상태가 SQLite에 저장되는지
- 방법: PIE Stop 버튼 클릭 → Output Log 확인
- 정상 로그:
```
[LobbyGM] Logout 시작
[LobbyGM] Logout: 정상 경로 | PlayerId=debug_lobby_player
[LobbyGM] Stash SQLite 저장 성공
[LobbyGM] Logout 완료
```

---

## Phase 4 테스트

### 테스트 4-1: Grid 바인딩 확인
- 목적: 3개 Grid가 WBP에서 C++에 정상 연결되는지
- 방법: PIE 실행 후 LogHellunaLobby 로그 확인
- 정상 로그:
```
[LobbyPanel] NativeOnInitialized 시작
[LobbyPanel]   Grid_Equippables=바인딩됨
[LobbyPanel]   Grid_Consumables=바인딩됨
[LobbyPanel]   Grid_Craftables=바인딩됨
[LobbyPanel]   Switcher=바인딩됨
```
- ⚠ nullptr이 보이면 → WBP_HellunaLobbyPanel에서 해당 위젯 이름이 정확한지 확인

### 테스트 4-2: 듀얼 패널 바인딩 확인
- 목적: StashWidget에서 양쪽 패널이 각각의 InvComp에 연결되는지
- 방법: 로그 확인
- 정상 로그:
```
[StashWidget] NativeOnInitialized 시작
[StashWidget]   StashPanel=바인딩됨
[StashWidget]   LoadoutPanel=바인딩됨
[StashWidget]   Button_Deploy=바인딩됨
[StashWidget] InitializePanels 완료
[StashWidget]   StashPanel ← StashComp 바인딩 완료
[StashWidget]   LoadoutPanel ← LoadoutComp 바인딩 완료
```

### 테스트 4-3: 아이템 전송 (Stash → Loadout)
- 전제: 테스트 5-3에서 DebugSave로 Stash에 아이템이 있는 상태
- 방법: BP 이벤트 그래프 또는 콘솔에서
  - Get Lobby Controller → Server Transfer Item (EntryIndex=0, Direction=창고→출격장비)
- 정상 로그:
```
[LobbyPC] Server_TransferItem 시작
[LobbyPC]   EntryIndex=0 | Direction=Stash→Loadout
[InvComp] TransferItemTo: Item.xxx x1 | Source=StashInventoryComponent → Target=LoadoutInventoryComponent
[InvComp] TransferItemTo 완료
[LobbyPC] Server_TransferItem 성공
```
- 화면에서 좌측(창고) Grid의 아이템이 사라지고 우측(출격장비) Grid에 나타나면 성공

### 테스트 4-4: 아이템 역전송 (Loadout → Stash)
- 전제: Loadout에 아이템이 있는 상태 (테스트 4-3 이후)
- 방법: Server Transfer Item (EntryIndex=0, Direction=출격장비→창고)
- 로그 동일 패턴, Direction만 반대

---

## 테스트 실행 순서 (권장)

```
1단계: BP 생성 (①~⑦)

--- Phase 5 기본 ---
2단계: 테스트 5-1  GameMode + Controller 연결 확인
3단계: 테스트 5-2  Stash 로드 (빈 상태)
4단계: 테스트 5-4  UI 생성 (위젯 표시)

--- Phase 4 바인딩 ---
5단계: 테스트 4-1  Grid 바인딩 확인
6단계: 테스트 4-2  듀얼 패널 바인딩 확인

--- 데이터 흐름 ---
7단계: 테스트 5-3  DebugSave → 아이템 표시
8단계: 테스트 4-3  아이템 전송 (창고 → 출격장비)
9단계: 테스트 4-4  아이템 역전송 (출격장비 → 창고)
10단계: 테스트 5-6 Logout 저장 확인
```

1~6단계 로그가 전부 정상이면 기본 파이프라인 성공.
7~10단계는 실제 아이템 데이터 흐름 검증.

---

## 문제 발생 시 체크리스트

| 증상 | 확인할 곳 |
|---|---|
| 위젯이 아예 안 뜸 | BP_LobbyController → 로비 창고 위젯 클래스 설정 여부 (⑤) |
| 위젯은 뜨는데 Grid가 비어있음 | 각 Grid의 bSkipAutoInit이 true인지 확인 (③) |
| PIE 시작하자마자 크래시 | WBP의 BindWidget 이름 불일치 — 대소문자 포함 정확히 확인 |
| PostLogin 로그 자체가 안 뜸 | 맵 World Settings → GameMode Override 확인 (⑦) |
| Controller 캐스팅 실패 | BP_LobbyGameMode → Player Controller Class 확인 (⑥) |
| SQLite 에러 | Helluna.Build.cs에 SQLite3 모듈 의존성 확인 |
| 아이템 전송 안 됨 | HasAuthority 로그 확인 — 서버에서만 실행되어야 함 |
| 탭 전환 안 됨 | Switcher의 자식으로 Grid 3개가 순서대로 있는지 (③) |
| 아이템 표시 안 됨 (로드는 성공) | Grid Slot Class / Slotted Item Class / Hover Item Class가 설정되었는지 (③) |
