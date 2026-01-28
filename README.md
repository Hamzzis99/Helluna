
대규모 프로젝트들

<details> <summary>🖥️ (로그인창) 전체 구현 로드맵 전부 완료!</summary>


Note: 이 섹션은 서버 접속부터 로그인 검증, 게임 진입까지의 전 과정을 포함합니다.

<details> <summary>📂 Phase 1: 기본 구조 세팅 (완료)</summary>

[x] Step 1. LoginLevel 맵 생성

배경 없는 빈 레벨 구성

[x] Step 2. 폴더 구조 생성

Source/Helluna/Public(Private)/Login/

Source/Helluna/Public(Private)/Player/

</details>

<details> <summary>⚙️ Phase 2: 핵심 클래스 생성 (완료)</summary>

[x] Step 3. HellunaPlayerState 생성

PlayerUniqueId (FString, Replicated)

bIsLoggedIn (bool, Replicated)

[x] Step 4. HellunaAccountSaveGame 생성

FHellunaAccountData 구조체 정의

TMap<FString, FHellunaAccountData> Accounts

HasAccount(), ValidatePassword(), CreateAccount() 구현

[x] Step 5. HellunaLoginController 생성

로그인 UI 표시 로직

Server_RequestLogin() / Client_LoginResult() RPC 틀 작성

[x] Step 6. HellunaLoginGameMode 생성

TSet<FString> LoggedInPlayerIds

계정 로드/저장 및 Seamless Travel 설정

</details>

<details> <summary>🎨 Phase 3: UI 위젯 생성 (완료)</summary>

[x] Step 7. HellunaLoginWidget (C++)

IP, ID, PW 입력용 EditableText 바인딩

접속/로그인 버튼 델리게이트 연결

[x] Step 8. WBP_LoginWidget (Blueprint)

C++ 클래스 상속 및 UI 스타일링

[x] Step 9. 로딩 화면 위젯

</details>

<details open> <summary>🔗 Phase 4: 로직 연결 (완료)</summary>

[x] Step 10. IP 입력 → 서버 접속 로직

Open IP:Port 콘솔 명령 실행 및 접속 상태 감지

[x] Step 11. 로그인 버튼 → 검증 로직

Server_RequestLogin() 호출 → GameMode 데이터 검증

[x] Step 12. 로그인 성공 → 맵 이동

ServerTravel("GihyeonMap") 실행 및 데이터 유지 확인

</details>

<details> <summary>🎮 Phase 5: 게임 맵 연동(완료)</summary>

[x] Step 13. GihyeonMap 설정

PlayerStart 배치 및 HellunaDefenseGameMode 할당

[x] Step 14. 캐릭터 스폰 확인

[x] Step 15. Logout 처리

LoggedInPlayerIds 제거 및 데이터 저장 (인벤토리 연동 대비)

</details>

<details> <summary>⚠️ Phase 6: 예외 처리</summary>

[x] Step 16. 에러 메시지 UI 처리 (비밀번호 불일치, 중복 접속 등)

[x] Step 17. 연결 끊김 처리 (40초 타임아웃 및 로그인 창 복귀)

</details>

<details> <summary>🧪 Phase 7: 테스트</summary>

[x] Step 18. 단일 클라이언트 (접속 → 로그인 → 진입)

[x] Step 19. 멀티 클라이언트 (3인 동시 접속 및 중복 ID 차단)

[x] Step 20. 데이터 영속성 (서버 재시작 후 계정 유지 확인)

</details>

</details>

📦 인벤토리 저장 시스템 - 전체 구현 로드맵
로그인 성공 후 인벤토리 로드부터, 로그아웃/맵 이동 시 저장까지의 전 과정을 포함합니다.

📂 Phase 0: 사전 분석 (완료)
[x] Step 0-1. 로그인 시스템 전체 구조 분석 (PlayerUniqueId를 저장 키로 사용)

[x] Step 0-2. InventoryComponent 구조 분석 (InventoryList FastArray, 서버-클라이언트 동기화)

[x] Step 0-3. FastArray 리플리케이션 메커니즘 분석 (FInv_InventoryEntry)

[x] Step 0-4. Grid 위치 동기화 문제점 파악 (서버는 실제 Grid 위치를 모름 → 인벤토리 닫을 때 동기화 필요)

[x] Step 0-5. 기존 아이템 목록 확인 (Axe, Potion(Red/Blue), FireFern, LuminDaisy)

[x] Step 0-6. GameplayTag 구조 확인 (GameItems.Equipment... 등)

📂 Phase 1: DataTable 매핑 시스템
목표: ItemType(GameplayTag) ↔ Actor Blueprint Class 매핑 구현

[ ] Step 1-1. 구조체 정의 (FItemTypeToActorMapping)

파일: HellunaItemTypeMapping.h

[ ] Step 1-2. 조회 함수 구현 (GetActorClassFromItemType)

파일: HellunaItemTypeMapping.cpp

[ ] Step 1-3. DataTable 에셋 생성

위치: Content/Data/Inventory/DT_ItemTypeMapping

[ ] Step 1-4. 기존 5개 아이템 행 추가

Axe, PotionBlue, PotionRed, FireFern, LuminDaisy

[ ] Step 1-5. 테스트 (태그 → 클래스 매핑 정상 동작 확인)

📂 Phase 2: SaveGame 클래스
목표: 인벤토리 데이터 저장용 구조체 및 SaveGame 클래스 정의

[ ] Step 2-1. 아이템 저장 구조체 정의 (FHellunaItemSaveData)

속성: ItemType, StackCount, GridPosition, GridCategory, bEquipped, WeaponSlotIndex

[ ] Step 2-2. 플레이어별 데이터 구조체 정의 (FHellunaPlayerInventoryData)

속성: Items (Array), ActiveWeaponSlot

[ ] Step 2-3. SaveGame 클래스 구현 (UHellunaInventorySaveGame)

파일: HellunaInventorySaveGame.h/cpp

[ ] Step 2-4. Save/Load 헬퍼 함수 구현

SavePlayerInventory, LoadPlayerInventory, DoesSaveExist

[ ] Step 2-5. 테스트 (빈 데이터 저장 및 로드 확인)

📂 Phase 3: Grid 위치 동기화 RPC
목표: 클라이언트 UI상의 실제 Grid 위치를 서버로 전송하여 저장 준비

[ ] Step 3-1. 동기화용 구조체 정의 (FItemGridSyncData)

파일: Inv_GridTypes.h

[ ] Step 3-2. Server RPC 선언 (Server_SyncGridPositionsForSave)

파일: Inv_InventoryComponent.h

[ ] Step 3-3. CollectAllGridPositions() 구현 (SpatialInventory 순회 및 위치 추출)

[ ] Step 3-4. CloseInventoryMenu() 수정 (인벤토리 닫을 때 RPC 호출)

[ ] Step 3-5. 테스트 (인벤토리 닫을 때 서버 로그로 위치 수신 확인)

📂 Phase 4: 저장 함수 구현
목표: 주요 시점(로그아웃, 맵 이동, 자동저장)에 서버 데이터 저장

[ ] Step 4-1. SavePlayerInventory() 함수 작성 (GameMode)

PlayerUniqueId 기반 파일 생성 로직

[ ] Step 4-2. Logout() 오버라이드 (접속 종료 시 저장)

[ ] Step 4-3. Server_SaveAndMoveLevel() 연동 (맵 이동 시 전원 저장)

[ ] Step 4-4. 자동저장 타이머 구현

주기: 기본 300초 (Blueprint 설정 가능)

[ ] Step 4-5. 테스트 (로그아웃 후 Saved 폴더에 .sav 파일 생성 확인)

📂 Phase 5: 로드 함수 구현
목표: 로그인 시 저장된 데이터를 기반으로 인벤토리 복원

[ ] Step 5-1. LoadPlayerInventory() 함수 작성 (GameMode)

[ ] Step 5-2. SaveGame 로드 및 유효성 검사

[ ] Step 5-3. DataTable 조회 (ItemType → ActorClass)

[ ] Step 5-4. 임시 Actor 스폰 및 Manifest 추출 (SpawnActorDeferred 활용)

[ ] Step 5-5. InventoryItem 생성 및 리스트 추가 (AddEntry)

[ ] Step 5-6. SpawnHeroCharacter() 연동 (캐릭터 스폰 직후 로드 호출)

[ ] Step 5-7. 테스트 (재접속 시 아이템 복구 확인)

📂 Phase 6: 장착 상태 복원
목표: 무기 장착 상태(등/허리 부착) 유지 및 복원

[ ] Step 6-1. 저장 로직 업데이트 (bEquipped, WeaponSlotIndex 저장)

[ ] Step 6-2. 로드 로직 업데이트 (아이템 생성 후 장착 처리 루프 추가)

[ ] Step 6-3. ActiveWeaponSlot 저장/복원 (선택 사항)

[ ] Step 6-4. 테스트 (무기를 등에 멘 상태로 재접속 확인)

🧪 Phase 7: 통합 테스트 (QA)
[ ] Step 7-1. 단일 플레이어 기본 저장/로드 (획득 → 재접속 → 유지)

[ ] Step 7-2. Grid 위치 정합성 (특정 칸 배치 → 재접속 → 같은 칸 확인)

[ ] Step 7-3. 스택 아이템 확인 (수량 유지 확인)

[ ] Step 7-4. 장착 상태 확인 (도끼 장착 상태 유지)

[ ] Step 7-5. 멀티플레이어 독립성 (Player A/B 데이터 섞임 방지)

[ ] Step 7-6. 맵 이동 시 유지 확인

[ ] Step 7-7. 자동저장 동작 확인

[ ] Step 7-8. 비정상 종료 복구 (인벤토리 닫은 직후 강제 종료 → 복구)

⚠️ Phase 8: 예외 처리 및 최적화
[ ] Step 8-1. DataTable 매핑 실패 예외 처리 (로그 경고, 해당 아이템 Skip)

[ ] Step 8-2. SaveGame 파일 손상/버전 불일치 처리

[ ] Step 8-3. 인벤토리 공간 초과 처리 (Grid 변경 시 복원 불가 아이템 처리)

[ ] Step 8-4. 최적화: Dirty Flag 도입 (변경점 없을 시 자동저장 Skip)
