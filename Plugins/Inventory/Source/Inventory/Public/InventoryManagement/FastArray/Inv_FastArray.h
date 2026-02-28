#pragma once
// Fast Array <- 이거는 빠른 직렬화 배열로 네트워크 동기화에 사용한다고 하네요?

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"

#include "Inv_FastArray.generated.h"

class UInv_InventoryComponent;
class UInv_LootContainerComponent;
class UInv_InventoryItem;
class UInv_ItemComponent;

/* A Single entry in an Inventory*/

USTRUCT(BlueprintType)
//구조체는 F라는 것으로 하는구나?
struct FInv_InventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FInv_InventoryEntry() {}

private:
	//재고 구성요소는 friend로 선언된 클래스에서만 접근 가능 
	//이거 좀 공부 해야겠는데? friend 다 까먹었어
	//아니 당초에 구조체인데 왜 굴이 private 섹션을 만들어서 friend를 선언하지? 진짜 모르겠네
	friend struct FInv_InventoryFastArray;
	friend UInv_InventoryComponent;
	friend class UInv_InventoryGrid; // ⭐ TargetGridIndex 접근용

	UPROPERTY()
	TObjectPtr<UInv_InventoryItem> Item = nullptr; // 아이템은 초기화 해야지 당연히
	
	// ⭐ Grid 위치 정보 (테트리스 방식 인벤토리용)
	// INDEX_NONE = 아직 배치 안 됨 or PickUp 대기 중
	UPROPERTY()
	int32 GridIndex = INDEX_NONE;
	
	// ⭐ 어느 Grid에 속하는지 (Equippables=0, Consumables=1, Craftables=2)
	UPROPERTY()
	uint8 GridCategory = 0;

	// ⭐ Split 아이템의 목표 Grid 위치 (클라이언트에서 마우스 위치에 배치하기 위함)
	// INDEX_NONE = 첫 번째 빈 슬롯에 자동 배치, 유효한 값 = 해당 위치에 배치
	UPROPERTY()
	int32 TargetGridIndex = INDEX_NONE;

	// ⭐ [부착물 시스템] 무기에 부착된 상태 플래그
	// true면 Entry는 FastArray에 남아있지만 그리드에서는 숨김 (인덱스 밀림 방지)
	UPROPERTY()
	bool bIsAttachedToWeapon = false;

	// ⭐ [장착 시스템] 장착된 아이템 플래그
	// true면 PostReplicatedAdd에서 그리드 배치를 스킵 (공간 선점 방지)
	UPROPERTY()
	bool bIsEquipped = false;

	// ⭐ [Fix14] 장착 무기 슬롯 인덱스 (-1=미장착, 0=주무기, 1=보조무기)
	// bIsEquipped=true일 때 어느 슬롯에 장착됐는지 기록
	UPROPERTY()
	int32 WeaponSlotIndex = -1;

	// R키 아이템 회전 상태 (90도 회전 여부)
	UPROPERTY()
	bool bRotated = false;
};

/* List of inventory Items 
인벤토리 아이템 등록하는 거?*/
USTRUCT(BlueprintType)
struct FInv_InventoryFastArray : public FFastArraySerializer
{
	GENERATED_BODY()

	FInv_InventoryFastArray() : OwnerComponent(nullptr) {}
	FInv_InventoryFastArray(UActorComponent* InOwnerComponent) : OwnerComponent(InOwnerComponent) {}

	//유틸리티 함수 구현
	TArray<UInv_InventoryItem*> GetAllItems() const; // 아이템 정보 얻어오기

	// FFastArraySerializer contract
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize); // 제거 전 처리
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize); // 변경 후 처리
	// End of FFastArraySerializer contract
	
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		// 와 개어려워.
		// 인벤토리 빠른 배열의 실제 인스턴스를 전달해야 하기 때문에 역참조를 사용한 것.
		return FastArrayDeltaSerialize<FInv_InventoryEntry, FInv_InventoryFastArray>(Entries, DeltaParams, *this);
	}

	// 새항목 추가 관련 구조체들
	UInv_InventoryItem* AddEntry(UInv_ItemComponent* ItemComponent); // 인벤토리 항목 추가
	UInv_InventoryItem* AddEntry(UInv_InventoryItem* Item);
	void RemoveEntry(UInv_InventoryItem* Item); // 인벤토리 항목 제거
	void ClearAllEntries(); // 모든 항목 제거 (로드 시 중복 방지)
	UInv_InventoryItem* FindFirstItemByType(const FGameplayTag& ItemType);

	// ⭐ [최적화 #4] 아이템 타입별 개수 조회 (O(1) 해시 조회)
	int32 GetTotalCountByType(const FGameplayTag& ItemType) const;

private:
	// TODO [Phase C] GridModel 도입 시 이 멤버를 GridModel로 이관 검토
	// ⭐ [최적화 #4] 아이템 타입별 인덱스 캐시 (O(n) 선형 탐색 → O(1) 해시 조회)
	// Key = ItemType GameplayTag, Value = Entries 배열 인덱스
	// AddEntry/RemoveEntry 시 재구축
	TMultiMap<FGameplayTag, int32> ItemTypeIndex;

	// ⭐ [최적화 #4] 아이템 타입 인덱스 재구축
	void RebuildItemTypeIndex();
	friend UInv_InventoryComponent;
	friend UInv_LootContainerComponent; // ⭐ [Phase 9] 컨테이너 Entries 접근용
	friend class UInv_InventoryGrid; // ⭐ Entries 접근용

	// Replicated list of items 
	UPROPERTY()
	TArray<FInv_InventoryEntry> Entries;
	UPROPERTY(NotReplicated) // 이거는 복제 안되는 속성이라는 뜻인가?
	TObjectPtr<UActorComponent> OwnerComponent;
};

template<>
struct TStructOpsTypeTraits<FInv_InventoryFastArray> : public TStructOpsTypeTraitsBase2<FInv_InventoryFastArray>
{
	enum { WithNetDeltaSerializer = true }; // 이 구조체가 네트워크 델타 직렬화를 지원함을 나타냄
};