// Gihyeon's Inventory Project

//인벤토리 베이스 자식 클래스

#pragma once

#include "CoreMinimal.h"
//#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Components/ActorComponent.h"
#include "InventoryManagement/FastArray/Inv_FastArray.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData 사용
#include "Inv_InventoryComponent.generated.h"

class UInv_ItemComponent;
class UInv_InventoryItem;
class UInv_InventoryBase;
class UInv_InventoryGrid;
struct FInv_ItemManifest;

//델리게이트
// ⭐ TwoParams로 변경: Item + EntryIndex (서버-클라이언트 포인터 불일치 해결용)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInventoryItemChange, UInv_InventoryItem*, Item, int32, EntryIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoRoomInInventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStackChange, const FInv_SlotAvailabilityResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FItemEquipStatusChanged, UInv_InventoryItem*, Item, int32, WeaponSlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInventoryMenuToggled, bool, bOpen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMaterialStacksChanged, const FGameplayTag&, MaterialTag); // Building 시스템용

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable ) // Blueprintable : 블루프린트에서 상속
class INVENTORY_API UInv_InventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UInv_InventoryComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "인벤토리", meta = (DisplayName = "아이템 추가 시도"))
	void TryAddItem(UInv_ItemComponent* ItemComponent);

	//서버 부분 RPC로 만들 것
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_AddNewItem(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder);

	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_AddStacksToItem(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder);

	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_DropItem(UInv_InventoryItem* Item, int32 StackCount); // 아이템을 서버에다 어떻게 버릴지
	
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_ConsumeItem(UInv_InventoryItem* Item);

	UFUNCTION(Server, Reliable) // 재료 소비 (Building 시스템용)
	void Server_ConsumeMaterials(const FGameplayTag& MaterialTag, int32 Amount);

	UFUNCTION(Server, Reliable) // 재료 소비 - 여러 스택에서 차감 (Building 시스템용)
	void Server_ConsumeMaterialsMultiStack(const FGameplayTag& MaterialTag, int32 Amount);

	UFUNCTION(Server, Reliable) // Split 시 서버의 TotalStackCount 업데이트
	void Server_UpdateItemStackCount(UInv_InventoryItem* Item, int32 NewStackCount);

	UFUNCTION(Server, Reliable) // ⭐ Phase 8: Split 시 서버에서 새 Entry 생성 (포인터 분리)
	void Server_SplitItemEntry(UInv_InventoryItem* OriginalItem, int32 OriginalNewStackCount, int32 SplitStackCount, int32 TargetGridIndex = INDEX_NONE);

	// ⭐ [Phase 4 방법2] 클라이언트 Grid 위치를 서버 Entry에 동기화
	// 클라이언트에서 아이템을 Grid에 배치/이동할 때 호출
	UFUNCTION(Server, Reliable)
	void Server_UpdateItemGridPosition(UInv_InventoryItem* Item, int32 GridIndex, uint8 GridCategory);

	UFUNCTION(Server, Reliable) // 크래프팅: 서버에서 아이템 생성 및 인벤토리 추가
	void Server_CraftItem(TSubclassOf<AActor> ItemActorClass);

	// ⭐ 크래프팅 통합 RPC: 공간 체크 → 재료 차감 → 아이템 생성
	UFUNCTION(Server, Reliable)
	void Server_CraftItemWithMaterials(TSubclassOf<AActor> ItemActorClass,
		const FGameplayTag& MaterialTag1, int32 Amount1,
		const FGameplayTag& MaterialTag2, int32 Amount2,
		const FGameplayTag& MaterialTag3, int32 Amount3,
		int32 CraftedAmount = 1);  // ⭐ 제작 개수 (기본값 1)

	UFUNCTION(NetMulticast, Reliable) // 모든 클라이언트의 UI 업데이트 (Building 재료 차감)
	void Multicast_ConsumeMaterialsUI(const FGameplayTag& MaterialTag, int32 Amount);

	// 같은 타입의 모든 스택 개수 합산 (Building UI용)
	UFUNCTION(BlueprintCallable, Category = "인벤토리", meta = (DisplayName = "총 재료 수량 가져오기"))
	int32 GetTotalMaterialCount(const FGameplayTag& MaterialTag) const;
	
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex = -1);

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 2] 부착/분리 Server RPC
	// ════════════════════════════════════════════════════════════════

	// 부착물 장착: 인벤토리 Grid에서 부착물을 무기 슬롯에 장착
	UFUNCTION(Server, Reliable)
	void Server_AttachItemToWeapon(int32 WeaponEntryIndex, int32 AttachmentEntryIndex, int32 SlotIndex);

	// 부착물 분리: 무기 슬롯에서 부착물을 분리하여 인벤토리 Grid로 복귀
	UFUNCTION(Server, Reliable)
	void Server_DetachItemFromWeapon(int32 WeaponEntryIndex, int32 SlotIndex);

	// 호환성 체크 (UI에서 드래그 중 슬롯 하이라이트용, 읽기 전용)
	UFUNCTION(BlueprintCallable, Category = "인벤토리|부착물", meta = (DisplayName = "무기에 부착 가능 여부"))
	bool CanAttachToWeapon(int32 WeaponEntryIndex, int32 AttachmentEntryIndex, int32 SlotIndex) const;
	
	UFUNCTION(NetMulticast, Reliable) // 멀티캐스트 함수 (서버에서 모든 클라이언트로 호출)
	void Multicast_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex = -1);
	
	
	//서버 RPC 전송하는 부분 함수들
	
	void ToggleInventoryMenu(); //인벤토리 메뉴 토글 함수
	void AddRepSubObj(UObject* SubObj); //복제 하위 객체 추가 함수
	void SpawnDroppedItem(UInv_InventoryItem* Item, int32 StackCount); // 떨어진 아이템 생성 함수
	UInv_InventoryBase* GetInventoryMenu() const {return InventoryMenu;};
	bool IsMenuOpen() const { return bInventoryMenuOpen; }

	// InventoryList 접근용 (재료 체크 등에 사용)
	const FInv_InventoryFastArray& GetInventoryList() const { return InventoryList; }
	FInv_InventoryFastArray& GetInventoryList() { return InventoryList; } // non-const 오버로드

	// ============================================
	// 🆕 [Phase 6] ItemType으로 아이템 찾기
	// ============================================
	UInv_InventoryItem* FindItemByType(const FGameplayTag& ItemType);
	
	// 🆕 [Phase 6] 제외 목록을 사용한 아이템 검색 (같은 타입 다중 장착 지원)
	UInv_InventoryItem* FindItemByTypeExcluding(const FGameplayTag& ItemType, const TSet<UInv_InventoryItem*>& ExcludeItems);

	// ⭐ [Phase 5 Fix] 마지막으로 추가된 Entry의 Grid 위치 설정 (로드 시 사용)
	void SetLastEntryGridPosition(int32 GridIndex, uint8 GridCategory);

	/**
	 * [Phase 4 CDO 최적화] Manifest로부터 직접 인벤토리 아이템 추가
	 *
	 * SpawnActor 없이 CDO/SCS에서 추출한 Manifest를 사용하여 아이템 생성.
	 * Server_CraftItem_Implementation (line 618-648)과 동일한 검증된 패턴.
	 *
	 * @param ManifestCopy  아이템 Manifest 복사본 (Fragment 역직렬화 완료 상태)
	 *                      ⚠️ Manifest() 호출 시 ClearFragments()로 파괴됨
	 * @param StackCount    스택 수량
	 * @return 생성된 UInv_InventoryItem, 실패 시 nullptr
	 */
	UInv_InventoryItem* AddItemFromManifest(FInv_ItemManifest& ManifestCopy, int32 StackCount);

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 3] Entry Index 검색 헬퍼
	// ════════════════════════════════════════════════════════════════
	// 아이템 포인터로 현재 InventoryList의 Entry Index를 찾는다.
	// Entry가 추가/제거되면 인덱스가 변하므로, 캐시된 값 대신 이 함수를 사용할 것.
	int32 FindEntryIndexForItem(const UInv_InventoryItem* Item) const;

	// ⭐ [Phase 4 개선] 서버에서 직접 인벤토리 데이터 수집 (Logout 시 저장용)
	// RPC 없이 서버의 FastArray에서 직접 읽어서 반환
	TArray<FInv_SavedItemData> CollectInventoryDataForSave() const;
	
	// 서버 브로드캐스트 함수들.
	FInventoryItemChange OnItemAdded;
	FInventoryItemChange OnItemRemoved;
	FNoRoomInInventory NoRoomInInventory;
	FStackChange OnStackChange;
	FItemEquipStatusChanged OnItemEquipped;
	FItemEquipStatusChanged OnItemUnequipped;
	FInventoryMenuToggled OnInventoryMenuToggled;
	FMaterialStacksChanged OnMaterialStacksChanged; // Building 시스템용
	
	
protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// ⭐ Blueprint에서 인벤토리 Grid 크기 참조 (모든 카테고리 공통 사용)
	// WBP_SpatialInventory의 Grid_Equippables를 선택하면 Rows/Columns 자동 참조!
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|그리드 설정",
		meta = (DisplayName = "인벤토리 그리드 (크기 자동 참조)", Tooltip = "인벤토리 Grid 위젯 참조입니다. Rows/Columns 크기를 자동으로 가져옵니다."))
	TObjectPtr<UInv_InventoryGrid> InventoryGridReference = nullptr;

private:

	TWeakObjectPtr<APlayerController> OwningController;

	void ConstructInventory();
	
	// ⭐ Blueprint Widget의 Grid 크기를 Component 설정으로 동기화
	void SyncGridSizesFromWidget();

	// ⭐ 서버 전용: InventoryList 기반 공간 체크 (UI 없이 작동!)
	bool HasRoomInInventoryList(const FInv_ItemManifest& Manifest) const;

	// ⭐ [SERVER-ONLY] 서버의 InventoryList를 기준으로 실제 재료 보유 여부를 확인합니다.
	bool HasRequiredMaterialsOnServer(const FGameplayTag& MaterialTag, int32 RequiredAmount) const;

	/**
	 * 리슨서버 호스트 또는 스탠드얼론인지 확인
	 *
	 * 📌 용도:
	 *    FastArray 리플리케이션이 자기 자신에게 안 되는 환경에서
	 *    직접 UI 갱신이 필요한지 판단
	 *
	 * @return true = 리슨서버 호스트 또는 스탠드얼론 (직접 UI 갱신 필요)
	 */
	bool IsListenServerOrStandalone() const;

	// ⭐ Grid 크기 (BeginPlay 시 Widget에서 자동 설정됨 - 모든 카테고리 공통 사용)
	int32 GridRows = 6;
	int32 GridColumns = 8;

	UPROPERTY(Replicated)
	FInv_InventoryFastArray InventoryList; // 인벤토리 

	UPROPERTY() // 이거는 소유권을 확보하는 것. 소유권을 잃지 않게 해주는 것.
	TObjectPtr<UInv_InventoryBase> InventoryMenu; //인벤토리 메뉴 위젯 인스턴스

	UPROPERTY(EditAnywhere, Category = "인벤토리",
		meta = (DisplayName = "인벤토리 메뉴 클래스", Tooltip = "인벤토리 메뉴로 사용할 위젯 블루프린트 클래스를 지정합니다."))
	TSubclassOf<UInv_InventoryBase> InventoryMenuClass;

	bool bInventoryMenuOpen; //인벤토리 메뉴 열림 여부
	void OpenInventoryMenu();
	void CloseInventoryMenu();
	void CloseOtherMenus(); // BuildMenu와 CraftingMenu 닫기
	
	//아이템 드롭 시 빙글빙글 돌아요
	UPROPERTY(EditAnywhere, Category = "인벤토리",
		meta = (DisplayName = "드롭 스폰 최소 각도", Tooltip = "아이템 드롭 시 스폰되는 최소 각도입니다."))
	float DropSpawnAngleMin = -85.f;

	UPROPERTY(EditAnywhere, Category = "인벤토리",
		meta = (DisplayName = "드롭 스폰 최대 각도", Tooltip = "아이템 드롭 시 스폰되는 최대 각도입니다."))
	float DropSpawnAngleMax = 85.f;

	UPROPERTY(EditAnywhere, Category = "인벤토리",
		meta = (DisplayName = "드롭 스폰 최소 거리", Tooltip = "아이템 드롭 시 스폰되는 최소 거리입니다."))
	float DropSpawnDistanceMin = 10.f;

	UPROPERTY(EditAnywhere, Category = "인벤토리",
		meta = (DisplayName = "드롭 스폰 최대 거리", Tooltip = "아이템 드롭 시 스폰되는 최대 거리입니다."))
	float DropSpawnDistanceMax = 50.f;

	UPROPERTY(EditAnywhere, Category = "인벤토리",
		meta = (DisplayName = "상대 스폰 높이", Tooltip = "아이템 드롭 시 캐릭터 기준 상대적인 스폰 높이 오프셋입니다."))
	float RelativeSpawnElevation = 70.f; // 스폰위치를 아래로 밀고싶다? 뭔 소리야?
};

// ════════════════════════════════════════════════════════════════════════════════
// 🔮 [미래 기능] 루팅/컨테이너 시스템 (Loot Container)
// ════════════════════════════════════════════════════════════════════════════════
//
// ⚠️ 주의: 이 기능은 아직 구현하지 않음!
//    "루팅 / 컨테이너" 만들어줘  ← 이 명령어가 올 때만 구현할 것
//    그 전까지는 절대 코드를 작성하지 말 것
//
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 개요:
//    월드에 배치된 상자, 적 시체, 금고 등을 상호작용하면
//    "컨테이너 인벤토리 ↔ 내 인벤토리" 두 Grid를 나란히 열어서
//    드래그 앤 드롭으로 아이템을 옮기는 시스템 (타르코프 방식)
//
// ════════════════════════════════════════════════════════════════════════════════
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 1: 컨테이너 액터 (새 파일)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
// 📁 새 파일: Interaction/Inv_LootContainerActor.h/.cpp
//
//    UCLASS()
//    class AInv_LootContainerActor : public AActor, public IInv_Highlightable
//    {
//        // ── 컴포넌트 ──
//        UPROPERTY(VisibleAnywhere)
//        UStaticMeshComponent* ContainerMesh;           // 상자/시체 메시
//
//        UPROPERTY(VisibleAnywhere)
//        USphereComponent* InteractionTrigger;           // 상호작용 범위
//
//        // ── 컨테이너 인벤토리 데이터 (서버 권위) ──
//        UPROPERTY(Replicated)
//        FInv_InventoryFastArray ContainerInventoryList;  // 기존 FastArray 재사용!
//
//        // ── 컨테이너 설정 ──
//        UPROPERTY(EditAnywhere, Category = "Container")
//        int32 ContainerRows = 4;                        // Grid 크기
//        UPROPERTY(EditAnywhere, Category = "Container")
//        int32 ContainerColumns = 6;
//
//        UPROPERTY(EditAnywhere, Category = "Container")
//        TArray<TSubclassOf<AActor>> LootTable;          // 스폰 가능 아이템 목록
//        UPROPERTY(EditAnywhere, Category = "Container")
//        int32 MinItems = 1;
//        UPROPERTY(EditAnywhere, Category = "Container")
//        int32 MaxItems = 5;
//
//        UPROPERTY(EditAnywhere, Category = "Container")
//        bool bRandomizeLootOnSpawn = true;              // BeginPlay 시 랜덤 채우기
//        UPROPERTY(EditAnywhere, Category = "Container")
//        bool bDestroyWhenEmpty = false;                 // 비면 자동 파괴
//
//        // ── 핵심 함수 ──
//        void GenerateRandomLoot();      // BeginPlay에서 호출, CDO 기반으로 아이템 생성
//        bool IsEmpty() const;
//
//        // ── 멀티플레이어: 동시 접근 제어 ──
//        UPROPERTY(Replicated)
//        TWeakObjectPtr<APlayerController> CurrentUser;  // 현재 열고 있는 플레이어
//        bool IsAvailable() const { return !CurrentUser.IsValid(); }
//    };
//
//    📌 GenerateRandomLoot() 구현 핵심:
//       - FindItemComponentTemplate(LootTable[i]) → CDO에서 Manifest 복사
//       - 기존 Phase 4 패턴 그대로 사용 (SpawnActor 없음!)
//       - 랜덤 개수, 랜덤 스택, 랜덤 Grid 위치 배치
//       - 서버 BeginPlay에서 1회 호출
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 2: 컨테이너 열기/닫기 흐름
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
//    [플레이어가 상자에 E키 상호작용]
//        │
//        ▼
//    Inv_PlayerController::PrimaryInteract()
//        │  라인트레이스로 AInv_LootContainerActor 감지
//        │
//        ▼
//    Server_OpenContainer(ContainerActor)   ← 새 Server RPC
//        │
//        ├─ CurrentUser 체크 (다른 플레이어가 사용중이면 거부)
//        ├─ CurrentUser = 요청한 PC
//        │
//        ▼
//    Client_ShowContainerUI(ContainerActor)  ← 새 Client RPC
//        │
//        ▼
//    "듀얼 Grid UI" 생성 (PART 3 참조)
//        │  왼쪽: 컨테이너 Grid  /  오른쪽: 내 인벤토리 Grid
//        │
//    [플레이어가 ESC 또는 인벤토리 닫기]
//        │
//        ▼
//    Server_CloseContainer(ContainerActor)   ← 새 Server RPC
//        │
//        ├─ CurrentUser = nullptr (잠금 해제)
//        ▼
//    컨테이너 UI 제거
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 3: 듀얼 Grid UI (새 위젯)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
// 📁 새 파일: Widgets/Inventory/Container/Inv_ContainerWidget.h/.cpp
//
//    UCLASS()
//    class UInv_ContainerWidget : public UUserWidget
//    {
//        // 왼쪽 패널: 컨테이너의 아이템 Grid
//        UPROPERTY(meta = (BindWidget))
//        UInv_InventoryGrid* ContainerGrid;
//
//        // 오른쪽 패널: 기존 플레이어 인벤토리 Grid (SpatialInventory 재사용)
//        UPROPERTY(meta = (BindWidget))
//        UInv_InventoryGrid* PlayerGrid;
//
//        // 컨테이너 이름 표시
//        UPROPERTY(meta = (BindWidget))
//        UTextBlock* ContainerTitle;
//
//        // 전체 가져오기 버튼 (편의 기능)
//        UPROPERTY(meta = (BindWidget))
//        UButton* TakeAllButton;
//
//        void InitializeFromContainer(AInv_LootContainerActor* Container);
//        void OnTakeAllClicked();
//    };
//
//    📌 핵심 설계:
//       - ContainerGrid는 기존 UInv_InventoryGrid를 그대로 재사용!
//       - DataSource만 다름: 플레이어 InventoryList 대신 Container의 ContainerInventoryList
//       - 기존 드래그 앤 드롭 로직에 "교차 Grid 전송" 분기만 추가
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 4: 아이템 전송 RPC (교차 Grid 이동)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
//    📁 Inv_InventoryComponent.h에 새 RPC 추가:
//
//    // 컨테이너 → 내 인벤토리
//    UFUNCTION(Server, Reliable)
//    void Server_TakeItemFromContainer(
//        AInv_LootContainerActor* Container,
//        int32 ContainerEntryIndex,       // 컨테이너 FastArray 인덱스
//        int32 TargetGridIndex            // 내 Grid에서 놓을 위치
//    );
//
//    // 내 인벤토리 → 컨테이너
//    UFUNCTION(Server, Reliable)
//    void Server_PutItemInContainer(
//        AInv_LootContainerActor* Container,
//        int32 PlayerEntryIndex,          // 내 FastArray 인덱스
//        int32 TargetGridIndex            // 컨테이너 Grid에서 놓을 위치
//    );
//
//    // 전체 가져오기
//    UFUNCTION(Server, Reliable)
//    void Server_TakeAllFromContainer(AInv_LootContainerActor* Container);
//
//    📌 Server_TakeItemFromContainer 구현 흐름:
//       1. Container->CurrentUser == 요청 PC 인지 검증 (보안)
//       2. Container->ContainerInventoryList에서 Entry 가져오기
//       3. HasRoomInInventoryList()로 내 인벤토리 공간 체크
//       4. 내 InventoryList.AddEntry() (기존 로직 재사용)
//       5. Container->ContainerInventoryList.RemoveEntry()
//       6. bDestroyWhenEmpty && IsEmpty() → Container 파괴
//       7. 리슨서버 분기: 양쪽 모두 OnItemAdded/OnItemRemoved 브로드캐스트
//
//    📌 Server_PutItemInContainer 구현 흐름:
//       역방향 동일 — 내 InventoryList에서 빼서 Container에 추가
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 5: 드래그 앤 드롭 확장 (기존 코드 수정)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
//    📁 수정 파일: Inv_InventoryGrid.cpp
//
//    기존 OnDrop 로직에 분기 추가:
//
//    void UInv_InventoryGrid::OnGridSlotDrop(...)
//    {
//        // ── 기존: 같은 Grid 내 이동 ──
//        if (SourceGrid == this)
//        {
//            MoveItemWithinGrid(...);  // 현재 코드 그대로
//        }
//        // ── 신규: 다른 Grid에서 온 드래그 ──
//        else if (SourceGrid->GetOwnerType() == EGridOwnerType::Container)
//        {
//            // 컨테이너 → 플레이어: Server_TakeItemFromContainer 호출
//            InventoryComp->Server_TakeItemFromContainer(
//                SourceGrid->GetOwningContainer(),
//                DraggedEntryIndex,
//                TargetGridIndex
//            );
//        }
//        else if (this->GetOwnerType() == EGridOwnerType::Container)
//        {
//            // 플레이어 → 컨테이너: Server_PutItemInContainer 호출
//            InventoryComp->Server_PutItemInContainer(
//                this->GetOwningContainer(),
//                DraggedEntryIndex,
//                TargetGridIndex
//            );
//        }
//    }
//
//    📌 UInv_InventoryGrid에 추가할 변수:
//       UENUM() enum class EGridOwnerType : uint8 { Player, Container };
//       EGridOwnerType OwnerType = EGridOwnerType::Player;
//       TWeakObjectPtr<AInv_LootContainerActor> OwningContainer;
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 6: 저장/로드 (컨테이너 상태 영속성)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
//    📌 옵션 A — 컨테이너 상태 저장 안 함 (간단):
//       리스폰 시 GenerateRandomLoot()로 매번 새로 생성
//       → 추가 작업 없음
//
//    📌 옵션 B — 컨테이너 상태 저장 (타르코프 방식):
//       Inv_SaveGameMode에 컨테이너 저장 로직 추가:
//       - 월드의 모든 AInv_LootContainerActor 순회
//       - 각 컨테이너의 ContainerInventoryList → 직렬화
//       - 기존 Phase 3 Manifest 직렬화 시스템 재사용
//       - SaveGame 오브젝트에 TMap<FName, TArray<FInv_SavedItemData>> ContainerStates 추가
//         (Key = 컨테이너 액터 이름 또는 고유 ID)
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 7: 새로 만들 파일 목록 총정리
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
//    📁 새 파일 (생성):
//       1. Public/Interaction/Inv_LootContainerActor.h
//       2. Private/Interaction/Inv_LootContainerActor.cpp
//       3. Public/Widgets/Inventory/Container/Inv_ContainerWidget.h
//       4. Private/Widgets/Inventory/Container/Inv_ContainerWidget.cpp
//
//    📁 기존 파일 (수정):
//       5. Inv_InventoryComponent.h  — 3개 새 RPC 선언
//       6. Inv_InventoryComponent.cpp — 3개 새 RPC 구현
//       7. Inv_InventoryGrid.h       — EGridOwnerType, OwningContainer 추가
//       8. Inv_InventoryGrid.cpp     — OnDrop에 교차 Grid 분기 추가
//       9. Inv_PlayerController.cpp  — 라인트레이스에 LootContainerActor 감지 추가
//      10. Inv_SaveGameMode.h/.cpp   — (옵션 B 선택 시) 컨테이너 저장/로드
//
//    📁 Blueprint (에디터에서 생성):
//      11. WBP_Inv_ContainerWidget    — 듀얼 Grid 레이아웃
//      12. BP_Inv_LootContainer_Chest — 상자 컨테이너 프리셋
//      13. BP_Inv_LootContainer_Corpse— 시체 컨테이너 프리셋
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// PART 8: 재사용 가능한 기존 시스템 목록
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//
//    ✅ FInv_InventoryFastArray — 컨테이너 아이템 저장용 (그대로 재사용)
//    ✅ UInv_InventoryGrid      — 컨테이너 Grid UI (DataSource만 교체)
//    ✅ FindItemComponentTemplate() — CDO 기반 아이템 생성 (Phase 4)
//    ✅ FInv_ItemManifest::Manifest() — 아이템 인스턴스화
//    ✅ 드래그 앤 드롭 (Inv_HoverItem + GridSlot) — UI 로직 재사용
//    ✅ IInv_Highlightable — 상호작용 하이라이트 (이미 존재)
//    ✅ IsListenServerOrStandalone() — 리슨서버 분기 (이미 존재)
//
// ════════════════════════════════════════════════════════════════════════════════
