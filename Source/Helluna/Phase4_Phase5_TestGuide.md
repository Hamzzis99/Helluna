# Phase 4 + Phase 5 통합 가이드

---

## Phase 4: 로비 듀얼 Grid 시스템 (Stash ↔ Loadout)

### Phase 4에서 구현된 C++ 코드

| 파일 | 역할 |
|---|---|
| `Inv_InventoryGrid.h/.cpp` | bSkipAutoInit + SetInventoryComponent() 추가 (플러그인 수정) |
| `Inv_InventoryComponent.h/.cpp` | TransferItemTo() 추가 (크로스 컴포넌트 전송) |
| `HellunaLobbyPanel.h/.cpp` | 단일 패널 (3탭 Grid + WidgetSwitcher) |
| `HellunaLobbyStashWidget.h/.cpp` | 듀얼 위젯 (StashPanel + LoadoutPanel + 출격버튼) |
| `HellunaLobbyController.h/.cpp` | StashComp + LoadoutComp + Server_TransferItem RPC |

### Phase 4 BP 생성

#### BP-1: WBP_HellunaLobbyPanel
- 생성: 우클릭 → Widget Blueprint → 부모: `HellunaLobbyPanel`
- 위젯 트리 (이름 정확히 일치 = BindWidget):

```
[Root]
└─ VerticalBox
   ├─ Text_PanelTitle           ← TextBlock (선택적, 없어도 OK)
   ├─ HorizontalBox
   │  ├─ Button_Equippables     ← Button (텍스트: "장비")
   │  ├─ Button_Consumables     ← Button (텍스트: "소모품")
   │  └─ Button_Craftables      ← Button (텍스트: "재료")
   └─ Switcher                  ← WidgetSwitcher
      ├─ Grid_Equippables       ← Inv_InventoryGrid
      ├─ Grid_Consumables       ← Inv_InventoryGrid
      └─ Grid_Craftables        ← Inv_InventoryGrid
```

- 각 Grid (3개 모두) Details 패널 필수 설정:

| 항목 | 값 | 이유 |
|---|---|---|
| 자동 초기화 스킵 (bSkipAutoInit) | true 체크! | false면 잘못된 InvComp에 자동 바인딩 |
| Item Category | 각각 Equippables / Consumables / Craftables | Grid 카테고리 |
| Rows | 8 (또는 원하는 값) | Grid 크기 |
| Columns | 6 (또는 원하는 값) | Grid 크기 |
| Tile Size | 기존 인게임 Grid와 동일 (예: 64) | 슬롯 크기 |
| Grid Slot Class | 기존 인게임과 동일 | 슬롯 위젯 |
| Slotted Item Class | 기존 인게임과 동일 | 배치된 아이템 위젯 |
| Hover Item Class | 기존 인게임과 동일 | 드래그 시 따라다니는 위젯 |

#### BP-2: WBP_HellunaLobbyStashWidget
- 생성: 우클릭 → Widget Blueprint → 부모: `HellunaLobbyStashWidget`
- 위젯 트리:

```
[Root]
└─ VerticalBox
   ├─ HorizontalBox
   │  ├─ StashPanel             ← WBP_HellunaLobbyPanel (BP-1의 인스턴스)
   │  └─ LoadoutPanel           ← WBP_HellunaLobbyPanel (BP-1의 인스턴스, 별개)
   └─ Button_Deploy             ← Button (텍스트: "출격")
```

- 주의: StashPanel과 LoadoutPanel은 같은 WBP_HellunaLobbyPanel 클래스를 2번 드래그, 이름만 각각 StashPanel / LoadoutPanel으로 변경

### Phase 4 테스트

#### 테스트 4-1: Grid 바인딩 확인
- PIE 실행 후 Output Log에서 LogHellunaLobby 필터
- 확인할 로그:
```
[LobbyPanel] NativeOnInitialized 시작
[LobbyPanel]   Grid_Equippables=바인딩됨
[LobbyPanel]   Grid_Consumables=바인딩됨
[LobbyPanel]   Grid_Craftables=바인딩됨
[LobbyPanel]   Switcher=바인딩됨
```
- ⚠ nullptr 로그가 보이면 → WBP_HellunaLobbyPanel의 BindWidget 이름 확인

#### 테스트 4-2: 듀얼 패널 바인딩 확인
- 확인할 로그:
```
[StashWidget] NativeOnInitialized 시작
[StashWidget]   StashPanel=바인딩됨
[StashWidget]   LoadoutPanel=바인딩됨
[StashWidget]   Button_Deploy=바인딩됨
[StashWidget] InitializePanels 완료
[StashWidget]   StashPanel ← StashComp 바인딩 완료
[StashWidget]   LoadoutPanel ← LoadoutComp 바인딩 완료
```

#### 테스트 4-3: 아이템 전송 (Stash → Loadout)
- Stash에 아이템이 있는 상태에서 (테스트 5-3에서 데이터 삽입 후)
- BP 이벤트 그래프 또는 콘솔에서:
  - Get Lobby Controller → Server Transfer Item (EntryIndex=0, Direction=창고→출격장비)
- 확인할 로그:
```
[LobbyPC] Server_TransferItem 시작
[InvComp] TransferItemTo: [아이템태그] x[수량] | Source=StashComp → Target=LoadoutComp
[InvComp] TransferItemTo 완료
[LobbyPC] Server_TransferItem 성공
```

#### 테스트 4-4: 아이템 역전송 (Loadout → Stash)
- Loadout에 아이템이 있는 상태에서
- Server Transfer Item (EntryIndex=0, Direction=출격장비→창고)
- 로그 동일 패턴, Direction만 반대

---

## Phase 5: 로비 환경 구축

### Phase 5에서 구현된 C++ 코드

| 파일 | 역할 |
|---|---|
| `HellunaLobbyGameMode.h/.cpp` | PostLogin(크래시복구+Stash로드), Logout(저장), ResolveItemTemplate |
| `HellunaLobbyController.h/.cpp` | Server_Deploy RPC, Client_ExecuteDeploy, ShowLobbyWidget |

### Phase 5 BP 생성

#### BP-3: BP_HellunaLobbyGameMode
- 생성: 우클릭 → Blueprint Class → 부모: `HellunaLobbyGameMode`
- 설정:

| 항목 | 값 |
|---|---|
| Player Controller Class | BP_HellunaLobbyController (BP-4) |
| Default Pawn Class | None (C++에서 이미 nullptr, 확인만) |
| bDebugSkipLogin | true (PIE 테스트용, 나중에 false) |

#### BP-4: BP_HellunaLobbyController
- 생성: 우클릭 → Blueprint Class → 부모: `HellunaLobbyController`
- 설정:

| 항목 | 값 |
|---|---|
| 로비 창고 위젯 클래스 | WBP_HellunaLobbyStashWidget (BP-2) |
| 게임 맵 URL | 비워두기 (테스트 단계), 나중에 /Game/Maps/게임맵?listen |

#### BP-5: 로비 맵 설정
- 기존 맵 사용 또는 새 빈 맵 생성
- World Settings:

| 항목 | 값 |
|---|---|
| GameMode Override | BP_HellunaLobbyGameMode (BP-3) |

### BP 생성 순서 (의존성)

```
① BP_HellunaLobbyGameMode     (의존 없음)
② BP_HellunaLobbyController   (의존 없음, 위젯은 나중에 지정)
③ WBP_HellunaLobbyPanel       (Grid 3개 + bSkipAutoInit 체크!)
④ WBP_HellunaLobbyStashWidget (③을 사용)
⑤ ②로 돌아가서 → 로비 창고 위젯 클래스 = ④ 지정
⑥ ①로 돌아가서 → Player Controller Class = ② 지정
⑦ 로비 맵 World Settings → GameMode Override = ① 지정
```

### Phase 5 테스트

#### 테스트 5-1: GameMode + Controller 연결 확인
- 로비 맵을 PIE 실행
- Output Log → LogHellunaLobby 필터
- 확인할 로그:
```
[LobbyGM] PostLogin 시작 | PC=BP_HellunaLobbyController_0
[LobbyGM] LobbyPC 캐스팅 성공 | StashComp=O | LoadoutComp=O
[LobbyGM] SQLite 서브시스템 캐시: 성공
```
- 실패 시:

| 로그 | 원인 | 해결 |
|---|---|---|
| Controller가 HellunaLobbyController가 아닙니다 | GameMode의 PC 클래스 오류 | BP_LobbyGameMode → Player Controller Class 확인 |
| SQLite 서브시스템 없음 | DB 미초기화 | Helluna.Build.cs에 SQLite 모듈 확인 |
| PostLogin 시작 로그 자체가 안 뜸 | GameMode가 적용 안 됨 | 맵 World Settings → GameMode Override 확인 |

#### 테스트 5-2: Stash 로드 확인
- 확인할 로그:
```
[LobbyGM] [1/3] 크래시 복구 체크...
[LobbyGM] [2/3] Stash 로드...
[LobbyGM] SQLite Stash 로드 완료 | 아이템 0개
[LobbyGM] Stash가 비어있음 → 빈 인벤토리로 시작
[LobbyGM] [3/3] Controller-PlayerId 매핑 등록...
[LobbyGM] PostLogin 완료
```
- 아이템 0개는 정상 (아직 테스트 데이터 안 넣었으므로)

#### 테스트 5-3: 테스트 데이터 삽입 + Stash 표시
- PIE 콘솔(~ 키)에서:
```
Helluna.SQLite.DebugSave
```
- PIE 재시작 (로비맵 다시 로드)
- 확인할 로그:
```
[LobbyGM] SQLite Stash 로드 완료 | 아이템 N개
[LobbyGM]   [0] ItemType=Item.xxx | Stack=1 | GridPos=(0,0)
[LobbyGM] RestoreFromSaveData 호출 → StashComp에 N개 아이템 복원 시작
[LobbyGM] StashComp 복원 완료
```
- 화면의 Stash Grid에 아이템이 표시되는지 확인

#### 테스트 5-4: UI 생성 확인
- 확인할 로그:
```
[LobbyPC] BeginPlay | IsLocalController=true
[LobbyPC] 로컬 컨트롤러 → 0.5초 후 ShowLobbyWidget 예약
[LobbyPC] CreateWidget 시작
[LobbyPC] 위젯 AddToViewport 완료
[LobbyPC] InitializePanels 호출
[LobbyPC] ShowLobbyWidget 완료
```
- 화면에 Stash(좌) + Loadout(우) + 출격 버튼이 보이면 성공
- 실패 시:

| 로그 | 원인 | 해결 |
|---|---|---|
| LobbyStashWidgetClass가 설정되지 않음 | Controller에 위젯 미지정 | BP_LobbyController → 로비 창고 위젯 클래스 설정 |
| CreateWidget 실패 | WBP 내부 BindWidget 오류 | WBP에서 StashPanel/LoadoutPanel/Button_Deploy 이름 확인 |

#### 테스트 5-5: 출격 (Deploy) — 나중에
- BP_HellunaLobbyController에서 게임 맵 URL 설정
- Loadout에 아이템이 있는 상태에서 출격 버튼 클릭
- 확인할 로그:
```
[LobbyPC] Server_Deploy 시작
[LobbyPC] Deploy [3a]: 잔여 Stash 저장 성공
[LobbyPC] Deploy [3b]: SavePlayerLoadout 성공
[LobbyPC] Deploy [4]: Client_ExecuteDeploy → /Game/Maps/...
```
- 게임 맵이 없으면 에러나지만 SQLite 저장은 확인 가능
- 이 테스트는 Phase 6 전에 하면 됨, 지금 안 해도 OK

#### 테스트 5-6: Logout 저장 확인
- PIE 종료 시 로그 확인:
```
[LobbyGM] Logout 시작
[LobbyGM] Logout: 정상 경로 | PlayerId=...
[LobbyGM] Stash SQLite 저장 성공
[LobbyGM] Logout 완료
```

---

## 테스트 실행 순서 (권장)

```
1단계: BP 생성 (①~⑦)
2단계: 테스트 5-1 (GameMode+Controller 연결)
3단계: 테스트 5-2 (Stash 로드 — 빈 상태)
4단계: 테스트 5-4 (UI 생성 — 위젯 표시)
5단계: 테스트 4-1 (Grid 바인딩)
6단계: 테스트 4-2 (듀얼 패널 바인딩)
7단계: 테스트 5-3 (DebugSave → 아이템 표시)
8단계: 테스트 4-3 (아이템 전송 Stash→Loadout)
9단계: 테스트 4-4 (아이템 역전송 Loadout→Stash)
10단계: 테스트 5-6 (Logout 저장)
```

1~6단계에서 로그가 정상이면 기본 파이프라인 성공.
7~10단계는 실제 아이템 데이터 흐름 검증.

---

## 문제 발생 시 체크리스트

| 증상 | 확인할 곳 |
|---|---|
| 위젯이 안 뜸 | BP_LobbyController → 로비 창고 위젯 클래스 설정 여부 |
| 위젯 뜨는데 Grid가 비어있음 | 각 Grid의 bSkipAutoInit이 true인지 |
| 크래시 (위젯 생성 시) | WBP의 BindWidget 이름 불일치 (대소문자 정확히!) |
| PostLogin 로그 안 뜸 | 맵 World Settings → GameMode Override 확인 |
| SQLite 에러 | Helluna.Build.cs에 SQLite3 모듈 의존성 확인 |
| 아이템 전송 안 됨 | HasAuthority 로그 확인 (서버에서만 실행되어야 함) |
| 탭 전환 안 됨 | Switcher의 자식으로 Grid 3개가 올바른 순서로 있는지 |
