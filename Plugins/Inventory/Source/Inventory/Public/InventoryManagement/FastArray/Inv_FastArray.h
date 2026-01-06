#pragma once
// Fast Array <- 이거는 빠른 직렬화 배열로 네트워크 동기화에 사용한다고 하네요?

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "Inv_FastArray.generated.h"

class UInv_InventoryComponent;
class UInv_InventoryItem;
class UInv_ItemComponent;
struct FGameplayTag;

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
	//아니 당초에 구조체인데 왜 굳이 private 섹션을 만들어서 friend를 선언하지? 진짜 모르겠네
	friend struct FInv_InventoryFastArray;
	friend UInv_InventoryComponent;

	UPROPERTY()
	TObjectPtr<UInv_InventoryItem> Item = nullptr; // 아이템은 초기화 해야지 당연히
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

	//새항목 추가 관련 구조체들
	UInv_InventoryItem* AddEntry(UInv_ItemComponent* ItemComponent); // 인벤토리 항목 추가
	UInv_InventoryItem* AddEntry(UInv_InventoryItem* Item);
	void RemoveEntry(UInv_InventoryItem* Item); // 인벤토리 항목 제거
	UInv_InventoryItem* FindFirstItemByType(const FGameplayTag& ItemType);

private:
	//아니 구조체인데 왜 friend를 선언하냐고!!! 야!!
	friend UInv_InventoryComponent;

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