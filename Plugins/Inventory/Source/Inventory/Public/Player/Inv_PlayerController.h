#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Inv_PlayerController.generated.h"

class UInv_InventoryComponent;
class UInv_EquipmentComponent;
class AInv_EquipActor;
class UInv_InventoryItem;
class UInputMappingContext;
class UInputAction;
class UInv_HUDWidget;
class UInv_ContainerWidget;
class UInv_LootContainerComponent;

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 6] 부착물 저장 데이터
// ════════════════════════════════════════════════════════════════
// 무기에 장착된 부착물 1개의 저장 정보
// FInv_SavedItemData 안에 TArray로 포함됨
// ════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct INVENTORY_API FInv_SavedAttachmentData
{
	GENERATED_BODY()

	// 부착물 아이템 타입
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "인벤토리|저장",
		meta = (DisplayName = "부착물 아이템 타입"))
	FGameplayTag AttachmentItemType;

	// 장착된 슬롯 인덱스
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "인벤토리|저장",
		meta = (DisplayName = "장착 슬롯 인덱스"))
	int32 SlotIndex = INDEX_NONE;

	// 부착물의 AttachmentType 태그 (AttachableFragment의 AttachmentType)
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "인벤토리|저장",
		meta = (DisplayName = "부착물 타입 태그"))
	FGameplayTag AttachmentType;

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 1 최적화] 부착물 Fragment 직렬화 데이터
	// ════════════════════════════════════════════════════════════════
	// 부착물의 전체 Fragment 데이터를 바이너리로 보존
	// 로드 시 랜덤 스탯 재결정 방지
	// 빈 배열이면 SaveVersion 2 이하 데이터 (하위 호환)
	// ════════════════════════════════════════════════════════════════
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "인벤토리|저장",
		meta = (DisplayName = "부착물 직렬화 데이터"))
	TArray<uint8> SerializedManifest;
};

// ============================================
// 📦 인벤토리 저장용 순수 데이터 구조체
// ============================================
// ⚠️ 주의: 이 구조체는 델리게이트보다 먼저 선언되어야 합니다!
// ============================================
/**
 * 단일 아이템의 저장 데이터 (플러그인 전용, Helluna 의존성 없음)
 * 
 * ============================================
 * 📌 용도:
 * ============================================
 * - 클라이언트 UI 상태를 수집하여 서버로 전송할 때 사용
 * - Split된 스택도 개별 항목으로 저장됨
 *   예: 포션 20개를 9개+11개로 Split → 2개의 FInv_SavedItemData 생성
 * 
 * ============================================
 * 📌 데이터 흐름:
 * ============================================
 * [클라이언트]
 *   UInv_InventoryGrid::CollectGridState()
 *     → SlottedItems 순회
 *     → GridSlot에서 StackCount 읽기 (Split 반영!)
 *     → TArray<FInv_SavedItemData> 반환
 *       ↓
 *   AInv_PlayerController::CollectInventoryGridState()
 *     → 3개 Grid 수집 결과 합침
 *       ↓
 *   Server RPC로 서버에 전송 (Phase 4에서 구현)
 *       ↓
 * [서버]
 *   FInv_SavedItemData → FHellunaInventoryItemData 변환
 *     → SaveGame에 저장
 * 
 * ============================================
 * 📌 주의사항:
 * ============================================
 * - 이 구조체는 플러그인에 있으므로 Helluna 타입을 사용하면 안 됨!
 * - Helluna에서 FHellunaInventoryItemData로 변환하여 저장
 */
USTRUCT(BlueprintType)
struct INVENTORY_API FInv_SavedItemData
{
	GENERATED_BODY()

	FInv_SavedItemData()
		: ItemType(FGameplayTag::EmptyTag)
		, StackCount(0)
		, GridPosition(FIntPoint(-1, -1))
		, GridCategory(0)
		, bEquipped(false)
		, WeaponSlotIndex(-1)
	{
	}

	// Grid 아이템용 생성자
	FInv_SavedItemData(const FGameplayTag& InItemType, int32 InStackCount, const FIntPoint& InGridPosition, uint8 InGridCategory)
		: ItemType(InItemType)
		, StackCount(InStackCount)
		, GridPosition(InGridPosition)
		, GridCategory(InGridCategory)
		, bEquipped(false)
		, WeaponSlotIndex(-1)
	{
	}

	// ============================================
	// 🆕 Phase 6: 장착 아이템용 생성자
	// ============================================
	FInv_SavedItemData(const FGameplayTag& InItemType, int32 InStackCount, int32 InWeaponSlotIndex)
		: ItemType(InItemType)
		, StackCount(InStackCount)
		, GridPosition(FIntPoint(-1, -1))  // 장착된 아이템은 Grid에 없음
		, GridCategory(0)  // 장비 카테고리
		, bEquipped(true)
		, WeaponSlotIndex(InWeaponSlotIndex)
	{
	}

	/**
	 * 아이템 종류 (GameplayTag)
	 * 예: "GameItems.Consumables.Potions.Health"
	 * 
	 * 로드 시 이 태그로 DataTable에서 Actor 클래스를 조회함
	 */
	UPROPERTY(BlueprintReadWrite, Category = "인벤토리|저장",
		meta = (DisplayName = "아이템 타입", Tooltip = "아이템 종류를 나타내는 GameplayTag입니다."))
	FGameplayTag ItemType;

	/**
	 * 스택 수량 (Split된 개별 스택 수량)
	 * 
	 * ⭐ 중요: 서버의 TotalStackCount가 아니라 UI의 GridSlot->GetStackCount() 값!
	 * Split 시: 서버 Entry(20개) → UI 슬롯1(9개) + UI 슬롯2(11개)
	 *           → FInv_SavedItemData 2개 생성 (9, 11)
	 */
	UPROPERTY(BlueprintReadWrite, Category = "인벤토리|저장",
		meta = (DisplayName = "스택 수량", Tooltip = "스택 수량입니다. Split된 경우 개별 스택의 수량이 저장됩니다."))
	int32 StackCount;

	/**
	 * Grid 내 위치 (X=Column, Y=Row)
	 * 
	 * GridIndex → GridPosition 변환:
	 *   X = GridIndex % Columns
	 *   Y = GridIndex / Columns
	 * 
	 * 예: Columns=8, GridIndex=19 → X=3, Y=2 → (3, 2)
	 */
	UPROPERTY(BlueprintReadWrite, Category = "인벤토리|저장",
		meta = (DisplayName = "그리드 위치", Tooltip = "인벤토리 그리드 내 위치입니다. X=열, Y=행입니다."))
	FIntPoint GridPosition;

	/** 
	 * Grid 카테고리 (어느 탭에 있는지)
	 * 
	 * 0 = Grid_Equippables (장비)   - EInv_ItemCategory::Equippable
	 * 1 = Grid_Consumables (소모품) - EInv_ItemCategory::Consumable
	 * 2 = Grid_Craftables (재료)    - EInv_ItemCategory::Craftable
	 */
	UPROPERTY(BlueprintReadWrite, Category = "인벤토리|저장",
		meta = (DisplayName = "그리드 카테고리", Tooltip = "아이템이 속한 그리드 탭입니다. 0=장비, 1=소모품, 2=재료입니다."))
	uint8 GridCategory;

	// ============================================
	// 🆕 Phase 6: 장착 상태 필드
	// ============================================

	/**
	 * 장착 여부
	 * 
	 * true = 장착 슬롯에 있음 (Grid에 없음)
	 * false = Grid에 있음 (기본값)
	 */
	UPROPERTY(BlueprintReadWrite, Category = "인벤토리|저장",
		meta = (DisplayName = "장착 여부", Tooltip = "장착 여부입니다. true이면 장착 슬롯에, false이면 그리드에 있습니다."))
	bool bEquipped;

	/**
	 * 무기 슬롯 인덱스 (장착된 경우에만 유효)
	 *
	 * -1 = 미장착 (Grid에 있음)
	 *  0 = 주무기 슬롯
	 *  1 = 보조무기 슬롯
	 */
	UPROPERTY(BlueprintReadWrite, Category = "인벤토리|저장",
		meta = (DisplayName = "무기 슬롯 인덱스", Tooltip = "무기 슬롯 인덱스입니다. -1은 미장착, 0은 주무기, 1은 보조무기입니다."))
	int32 WeaponSlotIndex;

	// R키 아이템 회전 상태 (90도 회전 여부)
	UPROPERTY(BlueprintReadWrite, Category = "인벤토리|저장",
		meta = (DisplayName = "회전 여부 (Rotated)", Tooltip = "90도 회전 상태입니다."))
	bool bRotated = false;

	// ============================================
	// 📌 [Phase 6 Attachment] 부착물 저장 데이터
	// ============================================
	// 무기 아이템인 경우, 장착된 부착물 목록
	// 비무기 아이템이면 빈 배열
	// ============================================
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "인벤토리|저장",
		meta = (DisplayName = "부착물 목록"))
	TArray<FInv_SavedAttachmentData> Attachments;

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 1 최적화] 아이템 Fragment 직렬화 데이터
	// ════════════════════════════════════════════════════════════════
	// 아이템의 전체 Fragment 데이터(랜덤 스탯, 장비 정보 등)를 바이너리로 보존
	//
	// 포함되는 데이터 예시:
	//   - FInv_LabeledNumberFragment의 Value (랜덤 결정된 스탯값)
	//   - FInv_EquipmentFragment의 EquipModifiers (장비 효과)
	//   - FInv_AttachmentHostFragment의 AttachedItems (부착물 목록)
	//   - FInv_StackableFragment의 StackCount
	//   - 기타 모든 Fragment의 UPROPERTY 값
	//
	// 빈 배열이면 SaveVersion 2 이하 데이터 → CDO 기본값 사용 (하위 호환)
	//
	// 직렬화/역직렬화: FInv_ItemManifest::SerializeFragments() / DeserializeAndApplyFragments()
	// ════════════════════════════════════════════════════════════════
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "인벤토리|저장",
		meta = (DisplayName = "아이템 직렬화 데이터"))
	TArray<uint8> SerializedManifest;

	/** 유효한 데이터인지 확인 */
	bool IsValid() const
	{
		return ItemType.IsValid() && StackCount > 0;
	}

	/** 카테고리 이름 반환 (디버그용) */
	FString GetCategoryName() const
	{
		switch (GridCategory)
		{
			case 0: return TEXT("장비");
			case 1: return TEXT("소모품");
			case 2: return TEXT("재료");
			default: return TEXT("???");
		}
	}

	/** 디버그 문자열 */
	FString ToString() const
	{
		FString Result;
		if (bEquipped)
		{
			Result = FString::Printf(TEXT("[%s x%d @ 장착슬롯(%d)]"),
				*ItemType.ToString(),
				StackCount,
				WeaponSlotIndex);
		}
		else
		{
			Result = FString::Printf(TEXT("[%s x%d @ Grid%d(%s) Pos(%d,%d)]"),
				*ItemType.ToString(),
				StackCount,
				GridCategory,
				*GetCategoryName(),
				GridPosition.X, GridPosition.Y);
		}

		// 부착물 정보 추가
		if (Attachments.Num() > 0)
		{
			Result += FString::Printf(TEXT(" +부착물%d개"), Attachments.Num());
		}

		// 직렬화 데이터 크기 표시
		if (SerializedManifest.Num() > 0)
		{
			Result += FString::Printf(TEXT(" [Manifest=%dB]"), SerializedManifest.Num());
		}

		return Result;
	}
};

USTRUCT(BlueprintType)
struct INVENTORY_API FInv_GridPositionSyncData
{
	GENERATED_BODY()

	FInv_GridPositionSyncData() = default;

	FInv_GridPositionSyncData(UInv_InventoryItem* InItem, int32 InGridIndex, uint8 InGridCategory, bool bInRotated)
		: Item(InItem)
		, GridIndex(InGridIndex)
		, GridCategory(InGridCategory)
		, bRotated(bInRotated)
	{
	}

	UPROPERTY()
	TObjectPtr<UInv_InventoryItem> Item = nullptr;

	UPROPERTY()
	int32 GridIndex = INDEX_NONE;

	UPROPERTY()
	uint8 GridCategory = 0;

	UPROPERTY()
	bool bRotated = false;
};

// ============================================
// 📌 델리게이트 선언 (Phase 4)
// ============================================
// ⚠️ 주의: FInv_SavedItemData 구조체 정의 이후에 선언해야 합니다!
// ============================================
/**
 * 서버에서 클라이언트로부터 인벤토리 상태를 수신했을 때 브로드캐스트
 * GameMode에서 바인딩하여 저장 처리
 * 
 * @param PlayerController - 데이터를 보낸 플레이어
 * @param SavedItems - 수신된 인벤토리 데이터
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnInventoryStateReceived,
	AInv_PlayerController*, PlayerController,
	const TArray<FInv_SavedItemData>&, SavedItems
);

/**
 * ⭐ [Phase 4 개선] PlayerController EndPlay 시 브로드캐스트
 * Helluna GameMode에서 바인딩하여 인벤토리 저장 및 로그아웃 처리
 * 
 * @param PlayerController - 종료되는 PlayerController
 * @param SavedItems - 수집된 인벤토리 데이터 (서버에서만 유효)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnInvControllerEndPlay,
	AInv_PlayerController*, PlayerController,
	const TArray<FInv_SavedItemData>&, SavedItems
);

UCLASS()
class INVENTORY_API AInv_PlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	AInv_PlayerController();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "인벤토리",
		meta = (DisplayName = "인벤토리 토글"))
	void ToggleInventory();

	// ═══════════════════════════════════════════
	// Phase 9: 컨테이너 상호작용
	// ═══════════════════════════════════════════

	/** 컨테이너 UI가 열려있는지 확인 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리|컨테이너",
		meta = (DisplayName = "Is Viewing Container (컨테이너 보는 중)"))
	bool IsViewingContainer() const { return bIsViewingContainer; }

	/** [클라이언트 → 서버] 컨테이너 열기 요청 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_OpenContainer(UInv_LootContainerComponent* Container);
	bool Server_OpenContainer_Validate(UInv_LootContainerComponent* Container);

	/** [클라이언트 → 서버] 컨테이너 닫기 요청 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_CloseContainer();
	bool Server_CloseContainer_Validate();

	/** [서버 → 클라이언트] 컨테이너 UI 표시 */
	UFUNCTION(Client, Reliable)
	void Client_ShowContainerUI(UInv_LootContainerComponent* Container);

	/** [서버 → 클라이언트] 컨테이너 UI 숨기기 */
	UFUNCTION(Client, Reliable)
	void Client_HideContainerUI();

	// ============================================
	// 🆕 [Phase 7.5] 현재 활성 무기의 EquipActor 반환
	// ============================================
	// [2026-02-18] 작업자: 김기현
	// ────────────────────────────────────────────
	// 목적:
	//   팀원의 GA/무기 코드(Helluna 모듈)에서
	//   인벤토리 플러그인 내부 구조를 모르더라도
	//   EquipActor에 접근할 수 있는 단일 진입점 제공
	//
	// 접근 경로:
	//   [팀원 코드]
	//     AInv_PlayerController* PC = Cast<AInv_PlayerController>(Hero->GetController());
	//     AInv_EquipActor* EA = PC ? PC->GetCurrentEquipActor() : nullptr;
	//
	//   [내부 동작]
	//     this->EquipmentComponent->GetActiveWeaponActor()
	//       → ActiveWeaponSlot 기반 분기
	//       → AInv_EquipActor* 반환
	//
	// 반환값:
	//   - 현재 손에 든 무기의 AInv_EquipActor 포인터
	//   - 맨손이거나 EquipmentComponent가 없으면 nullptr
	//
	// 사용 예시 (팀원 코드):
	//   // 소음기 장착 시 소음 사운드 사용
	//   USoundBase* Sound = EA ? EA->GetFireSound() : DefaultSound;
	//
	//   // 스코프 장착 시 줌 FOV 오버라이드
	//   float FOV = EA ? EA->GetZoomFOV() : DefaultFOV;
	//
	// 전제조건:
	//   BP_HellunaCharacterController가 AInv_PlayerController의 자식이므로
	//   Cast<AInv_PlayerController>는 항상 성공함
	// ============================================
	UFUNCTION(BlueprintCallable, Category = "인벤토리|무기",
		meta = (DisplayName = "현재 활성 장비액터 가져오기"))
	AInv_EquipActor* GetCurrentEquipActor() const;

	// ============================================
	// 📌 인벤토리 저장/로드용 함수 (Phase 3)
	// ============================================
	
	/**
	 * 현재 클라이언트 UI의 인벤토리 Grid 상태를 수집
	 * 
	 * ============================================
	 * 📌 호출 시점:
	 * ============================================
	 * - 서버에서 Client_RequestInventoryState() RPC 수신 시
	 * - 자동저장(300초) / 로그아웃 / 맵이동 전에 호출됨
	 * 
	 * ============================================
	 * 📌 수집 과정:
	 * ============================================
	 * 1. InventoryComponent → InventoryMenu(SpatialInventory) 접근
	 * 2. 3개 Grid 순회 (Equippables, Consumables, Craftables)
	 * 3. 각 Grid의 SlottedItems 맵 순회
	 * 4. GridSlot에서 StackCount 읽기 (⭐ Split 반영!)
	 * 5. GridIndex → GridPosition 변환
	 * 
	 * ============================================
	 * 📌 Split 처리:
	 * ============================================
	 * 서버: Entry 1개 (TotalStackCount=20)
	 * UI:   슬롯1(9개) + 슬롯2(11개)
	 * 결과: FInv_SavedItemData 2개 생성!
	 * 
	 * @return 모든 Grid의 아이템 데이터 배열 (Split 스택 포함)
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리|저장",
		meta = (DisplayName = "인벤토리 그리드 상태 수집"))
	TArray<FInv_SavedItemData> CollectInventoryGridState();

	/**
	 * 저장된 상태로 인벤토리 Grid 복원
	 * 
	 * ============================================
	 * 📌 호출 시점:
	 * ============================================
	 * - 서버에서 아이템 생성 완료 후 (FastArray 리플리케이션 후)
	 * - Client_RestoreGridPositions() RPC 수신 시
	 * 
	 * ============================================
	 * 📌 복원 과정 (Phase 5에서 구현):
	 * ============================================
	 * 1. 이미 UI에 아이템이 자동 배치된 상태
	 * 2. 각 SavedItem의 GridCategory로 해당 Grid 선택
	 * 3. 아이템을 저장된 GridPosition으로 이동
	 * 4. Split 상태 복원 (같은 ItemType 여러 위치)
	 * 
	 * @param SavedItems - 복원할 아이템 데이터 배열
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리|저장",
		meta = (DisplayName = "인벤토리 상태 복원"))
	void RestoreInventoryFromState(const TArray<FInv_SavedItemData>& SavedItems);

	// ============================================
	// 📌 인벤토리 저장 RPC (Phase 4)
	// ============================================
	
	/**
	 * [서버 → 클라이언트] 인벤토리 상태 요청
	 * 
	 * 서버에서 자동저장 타이머 또는 로그아웃 시 호출
	 * 클라이언트는 이 RPC를 받으면 CollectInventoryGridState()로 수집 후
	 * Server_ReceiveInventoryState()로 서버에 전송
	 */
	UFUNCTION(Client, Reliable)
	void Client_RequestInventoryState();

	/**
	 * [클라이언트 → 서버] 수집된 인벤토리 상태 전송
	 * 
	 * Client_RequestInventoryState() 수신 후 호출됨
	 * 서버에서 OnInventoryStateReceived 델리게이트 브로드캐스트
	 * 
	 * @param SavedItems - 클라이언트에서 수집한 인벤토리 데이터
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReceiveInventoryState(const TArray<FInv_SavedItemData>& SavedItems);

	/** [Fix15] 청크 분할 전송 — 65KB RPC 제한 초과 방지
	 *  클라이언트가 N개씩 나눠 보내고, bIsLastChunk=true일 때 델리게이트 브로드캐스트 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReceiveInventoryStateChunk(const TArray<FInv_SavedItemData>& ChunkItems, bool bIsLastChunk);

	/**
	 * 서버에서 인벤토리 상태 수신 시 브로드캐스트되는 델리게이트
	 * GameMode에서 바인딩하여 저장 처리
	 */
	UPROPERTY(BlueprintAssignable, Category = "인벤토리|저장",
		meta = (DisplayName = "인벤토리 상태 수신 이벤트"))
	FOnInventoryStateReceived OnInventoryStateReceived;

	/**
	 * ⭐ [Phase 4 개선] Controller EndPlay 시 브로드캐스트
	 * GameMode에서 바인딩하여 인벤토리 저장 및 로그아웃 처리
	 * 
	 * 장점: Controller가 EndPlay될 때 InventoryComponent가 아직 유효함!
	 */
	UPROPERTY(BlueprintAssignable, Category = "인벤토리|저장",
		meta = (DisplayName = "컨트롤러 종료 이벤트"))
	FOnInvControllerEndPlay OnControllerEndPlay;

	// ============================================
	// 📌 인벤토리 로드 RPC (Phase 5)
	// ============================================

	/**
	 * [서버 → 클라이언트] 저장된 인벤토리 데이터 전송
	 *
	 * 서버에서 로그인 성공 후 저장된 인벤토리 데이터를 로드하여
	 * 이 RPC로 클라이언트에 전송함
	 *
	 * 클라이언트는 수신된 데이터로 Grid 위치 복원
	 *
	 * @param SavedItems - 복원할 인벤토리 데이터
	 */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveInventoryData(const TArray<FInv_SavedItemData>& SavedItems);

	/** [네트워크 최적화] 인벤토리 데이터 청크 분할 전송
	 *  인벤토리가 꽉 찼을 때 64KB 번치 크기 제한 초과 방지
	 *  서버가 N개씩 나눠 보내고, bIsLastChunk=true일 때 복원 시작 */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveInventoryDataChunk(const TArray<FInv_SavedItemData>& ChunkItems, bool bIsLastChunk);

	/** [네트워크 최적화] FastArray 리플리케이션 완료 대기 후 복원 실행 */
	void PollAndRestoreInventory();

	/**
	 * 인벤토리 로드 완료 대기 후 Grid 복원
	 * FastArray 리플리케이션 완료 대기를 위한 딜레이 처리
	 */
	void DelayedRestoreGridPositions(const TArray<FInv_SavedItemData>& SavedItems);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;  // ⭐ [Phase 4 개선] 인벤토리 저장 및 로그아웃
	virtual void SetupInputComponent() override;

private:
	void PrimaryInteract();
	void CreateHUDWidget();
	void TraceForInteractables();
	
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<UInv_EquipmentComponent> EquipmentComponent;

	/** [네트워크 최적화] 클라이언트→서버 청크 분할 수신 시 누적 버퍼 (Client_ReceiveInventoryDataChunk용) */
	TArray<FInv_SavedItemData> PendingSavedItems;

	/** [Fix15] 서버측 청크 누적 버퍼 (Server_ReceiveInventoryStateChunk용) */
	TArray<FInv_SavedItemData> PendingServerChunkItems;

	/** [네트워크 최적화] FastArray 폴링 복원용 */
	TArray<FInv_SavedItemData> PendingRestoreItems;
	FTimerHandle PendingRestoreTimerHandle;
	int32 PendingRestoreRetryCount = 0;

	// [Fix26] 로컬 람다 타이머 핸들 → 멤버로 승격 (EndPlay에서 해제)
	FTimerHandle GridRestoreTimerHandle;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리",
		meta = (DisplayName = "기본 입력 매핑 컨텍스트", Tooltip = "기본 입력 매핑 컨텍스트 배열입니다. 인벤토리 관련 입력을 바인딩합니다."))
	TArray<TObjectPtr<UInputMappingContext>> DefaultIMCs;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리",
		meta = (DisplayName = "상호작용 입력 액션", Tooltip = "상호작용(줍기 등) 입력 액션입니다."))
	TObjectPtr<UInputAction> PrimaryInteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리",
		meta = (DisplayName = "인벤토리 토글 입력 액션", Tooltip = "인벤토리 열기/닫기 입력 액션입니다."))
	TObjectPtr<UInputAction> ToggleInventoryAction;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리|무기",
		meta = (DisplayName = "주무기 전환 입력 액션", Tooltip = "주무기 슬롯 전환 입력 액션입니다."))
	TObjectPtr<UInputAction> PrimaryWeaponAction;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리|무기",
		meta = (DisplayName = "보조무기 전환 입력 액션", Tooltip = "보조무기 슬롯 전환 입력 액션입니다."))
	TObjectPtr<UInputAction> SecondaryWeaponAction;

	void HandlePrimaryWeapon();
	void HandleSecondaryWeapon();

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리",
		meta = (DisplayName = "HUD 위젯 클래스", Tooltip = "화면에 표시할 HUD 위젯 블루프린트 클래스입니다."))
	TSubclassOf<UInv_HUDWidget> HUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UInv_HUDWidget> HUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리",
		meta = (DisplayName = "추적 길이", Tooltip = "상호작용 가능 대상을 탐지하는 라인 트레이스 거리입니다."))
	double TraceLength = 300.0;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리",
		meta = (DisplayName = "아이템 추적 채널", Tooltip = "아이템 탐지용 콜리전 채널입니다."))
	TEnumAsByte<ECollisionChannel> ItemTraceChannel = ECC_Visibility;

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Interact(AActor* TargetActor);

	// 방안 B: 인벤토리 메뉴 상태 변경 시 HUD 자동 관리 (델리게이트 핸들러)
	UFUNCTION()
	void OnInventoryMenuStateChanged(bool bOpen);

	TWeakObjectPtr<AActor> ThisActor;
	TWeakObjectPtr<AActor> LastActor;
	TWeakObjectPtr<AActor> CurrentCraftingStation;

	// ═══════════════════════════════════════════
	// Phase 9: 컨테이너 UI
	// ═══════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리|컨테이너",
		meta = (DisplayName = "Container Widget Class (컨테이너 위젯 클래스)"))
	TSubclassOf<UInv_ContainerWidget> ContainerWidgetClass;

	UPROPERTY()
	TObjectPtr<UInv_ContainerWidget> ContainerWidget;

	bool bIsViewingContainer = false;

	TWeakObjectPtr<UInv_LootContainerComponent> ActiveContainerComp;
};
