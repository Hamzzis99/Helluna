// File: Plugins/Inventory/Source/Inventory/Public/InventoryManagement/Components/Inv_LootContainerComponent.h
// ════════════════════════════════════════════════════════════════════════════════
// UInv_LootContainerComponent — 루트 컨테이너 컴포넌트
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    어떤 액터든 이 컴포넌트를 붙이면 루팅 가능한 컨테이너가 됨
//    상자, 사체, 크래프팅 보관함 등 통합 처리
//
// 📌 사용 예시:
//    - 레벨에 배치된 상자: bActivated=true, PresetItems/LootTable 설정
//    - 플레이어 사체: bActivated=false → OnHeroDeath에서 Activate()
//    - 빈 상자 (크래프팅): PresetItems 없음, 양방향 전송
//
// 📌 아이템 관리:
//    FInv_InventoryFastArray를 직접 소유 → FastArray 리플리케이션으로 동기화
//    OwnerComponent = this (PostReplicatedAdd에서 이중 캐스트 필요)
//
// 📌 잠금:
//    CurrentUser != nullptr이면 다른 플레이어 접근 불가 (1인 전용)
//    EndPlay 시 자동 해제 (PlayerController에서 처리)
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryManagement/FastArray/Inv_FastArray.h"
#include "Interaction/Inv_Highlightable.h"
#include "Inv_LootContainerComponent.generated.h"

class UInv_ItemComponent;
struct FInv_SavedItemData;

// ════════════════════════════════════════════════════════════════
// FInv_PresetContainerItem — BP Class Defaults에서 설정하는 고정 아이템
// ════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct INVENTORY_API FInv_PresetContainerItem
{
	GENERATED_BODY()

	/** 아이템 액터 클래스 (ItemComponent를 가진 BP) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Preset",
		meta = (DisplayName = "Item Class (아이템 클래스)"))
	TSubclassOf<AActor> ItemClass;

	/** 수량 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Preset",
		meta = (DisplayName = "Stack Count (수량)", ClampMin = "1"))
	int32 StackCount = 1;
};

// ════════════════════════════════════════════════════════════════
// 컨테이너 아이템 변경 델리게이트
// ════════════════════════════════════════════════════════════════
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FContainerItemChange, UInv_InventoryItem*, Item, int32, EntryIndex);

// ════════════════════════════════════════════════════════════════
// UInv_LootContainerComponent
// ════════════════════════════════════════════════════════════════
UCLASS(ClassGroup=(Inventory), meta=(BlueprintSpawnableComponent,
	DisplayName = "Loot Container Component (루트 컨테이너 컴포넌트)"))
class INVENTORY_API UInv_LootContainerComponent : public UActorComponent, public IInv_Highlightable
{
	GENERATED_BODY()

public:
	UInv_LootContainerComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ════════════════════════════════════════════════════════════════
	// IInv_Highlightable 구현
	// ════════════════════════════════════════════════════════════════

	virtual void Highlight_Implementation() override;
	virtual void UnHighlight_Implementation() override;

	// ════════════════════════════════════════════════════════════════
	// 컨테이너 설정 (BP Class Defaults에서 편집)
	// ════════════════════════════════════════════════════════════════

	/** 고정 아이템 목록 (디자이너가 직접 지정) */
	UPROPERTY(EditAnywhere, Category = "Container|Preset",
		meta = (DisplayName = "Preset Items (고정 아이템)"))
	TArray<FInv_PresetContainerItem> PresetItems;

	/** 랜덤 루트 테이블 — 스폰될 아이템 클래스 후보 */
	UPROPERTY(EditAnywhere, Category = "Container|Random",
		meta = (DisplayName = "Loot Table (루트 테이블)"))
	TArray<TSubclassOf<AActor>> LootTable;

	/** 최소 랜덤 아이템 수 */
	UPROPERTY(EditAnywhere, Category = "Container|Random",
		meta = (DisplayName = "Min Items (최소 아이템 수)", ClampMin = "0"))
	int32 MinItems = 1;

	/** 최대 랜덤 아이템 수 */
	UPROPERTY(EditAnywhere, Category = "Container|Random",
		meta = (DisplayName = "Max Items (최대 아이템 수)", ClampMin = "1"))
	int32 MaxItems = 5;

	/** BeginPlay 시 LootTable에서 랜덤 아이템 생성 여부 */
	UPROPERTY(EditAnywhere, Category = "Container|Random",
		meta = (DisplayName = "Randomize On Spawn (스폰 시 랜덤 생성)"))
	bool bRandomizeLootOnSpawn = false;

	/** 비어있으면 Owner 액터 파괴 여부 */
	UPROPERTY(EditAnywhere, Category = "Container",
		meta = (DisplayName = "Destroy Owner When Empty (비면 파괴)"))
	bool bDestroyOwnerWhenEmpty = false;

	/** 컨테이너 Grid 행 수 */
	UPROPERTY(EditAnywhere, Category = "Container",
		meta = (DisplayName = "Container Rows (행)", ClampMin = "1", ClampMax = "20"))
	int32 ContainerRows = 4;

	/** 컨테이너 Grid 열 수 */
	UPROPERTY(EditAnywhere, Category = "Container",
		meta = (DisplayName = "Container Columns (열)", ClampMin = "1", ClampMax = "20"))
	int32 ContainerColumns = 6;

	/** UI에 표시할 컨테이너 이름 */
	UPROPERTY(EditAnywhere, Category = "Container", Replicated,
		meta = (DisplayName = "Container Display Name (표시 이름)"))
	FText ContainerDisplayName;

	// ════════════════════════════════════════════════════════════════
	// 상태 (Replicated)
	// ════════════════════════════════════════════════════════════════

	/** 컨테이너 아이템 (서버 권위, FastArray 리플리케이션) */
	UPROPERTY(Replicated)
	FInv_InventoryFastArray ContainerInventoryList;

	/** 현재 사용 중인 플레이어 (잠금) — nullptr이면 사용 가능 */
	UPROPERTY(Replicated)
	TObjectPtr<APlayerController> CurrentUser;

	/** 활성화 여부 — false면 상호작용 불가 (사체: 사망 전) */
	UPROPERTY(Replicated)
	bool bActivated = true;

	// ════════════════════════════════════════════════════════════════
	// 델리게이트 — UI 갱신용
	// ════════════════════════════════════════════════════════════════

	/** 컨테이너에 아이템 추가됨 */
	UPROPERTY(BlueprintAssignable, Category = "Container")
	FContainerItemChange OnContainerItemAdded;

	/** 컨테이너에서 아이템 제거됨 */
	UPROPERTY(BlueprintAssignable, Category = "Container")
	FContainerItemChange OnContainerItemRemoved;

	// ════════════════════════════════════════════════════════════════
	// 함수
	// ════════════════════════════════════════════════════════════════

	/** 컨테이너 활성화 (사체: OnHeroDeath에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "Container",
		meta = (DisplayName = "Activate Container (컨테이너 활성화)"))
	void Activate();

	/**
	 * 외부 아이템으로 초기화 (사체: 죽은 플레이어 아이템 복사)
	 * 서버에서만 호출
	 *
	 * @param Items  복사할 아이템 데이터 배열
	 * @param Resolver  아이템 타입 → ItemComponent CDO 매핑 델리게이트
	 */
	void InitializeWithItems(const TArray<FInv_SavedItemData>& Items,
		const FInv_ItemTemplateResolver& Resolver);

	/**
	 * ItemComponent 직접 추가 (서버 전용)
	 * @return 생성된 InventoryItem, 실패 시 nullptr
	 */
	UInv_InventoryItem* AddItem(UInv_ItemComponent* ItemComponent);

	/** 사용 가능 여부 (활성화 + 잠금 없음) */
	UFUNCTION(BlueprintPure, Category = "Container",
		meta = (DisplayName = "Is Available (사용 가능)"))
	bool IsAvailable() const { return bActivated && !IsValid(CurrentUser); }

	/** 활성화 여부 */
	UFUNCTION(BlueprintPure, Category = "Container",
		meta = (DisplayName = "Is Activated (활성화)"))
	bool IsActivated() const { return bActivated; }

	/** 비어있는지 확인 */
	UFUNCTION(BlueprintPure, Category = "Container",
		meta = (DisplayName = "Is Empty (비어있음)"))
	bool IsEmpty() const;

	/** 잠금 설정 */
	void SetCurrentUser(APlayerController* PC);

	/** 잠금 해제 */
	void ClearCurrentUser();

	/** 표시 이름 설정 */
	void SetContainerDisplayName(const FText& InName) { ContainerDisplayName = InName; }

	/** 컨테이너 총 아이템 슬롯 수 반환 */
	int32 GetTotalSlots() const { return ContainerRows * ContainerColumns; }

private:
	// ════════════════════════════════════════════════════════════════
	// 내부 함수
	// ════════════════════════════════════════════════════════════════

	/** BeginPlay에서 호출: PresetItems + LootTable 초기 아이템 생성 (서버 전용) */
	void GenerateInitialItems();

	/** PresetItems 배열 기반 고정 아이템 생성 */
	void GeneratePresetItems();

	/** LootTable 기반 랜덤 아이템 생성 */
	void GenerateRandomLoot();

	/**
	 * 아이템 클래스 → ItemComponent CDO 추출 유틸리티
	 * @param ItemClass  아이템 액터 BP 클래스
	 * @return ItemComponent CDO, 없으면 nullptr
	 */
	UInv_ItemComponent* GetItemComponentFromClass(TSubclassOf<AActor> ItemClass) const;
};
