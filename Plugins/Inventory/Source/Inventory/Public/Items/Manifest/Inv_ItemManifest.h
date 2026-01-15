#pragma once
#include "CoreMinimal.h"
#include "Types/Inv_GridTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "GameplayTagContainer.h"

#include "Inv_ItemManifest.generated.h"

/*
	The Item Manifest contains all of the necessary data
	for creating a new Inventory Item
*/
class UInv_InventoryItem;
struct FInv_ItemFragment;
class UInv_CompositeBase;

USTRUCT(BlueprintType)
struct INVENTORY_API FInv_ItemManifest
{
	GENERATED_BODY()

	TArray<TInstancedStruct<FInv_ItemFragment>>& GetFragmentsMutable() { return Fragments; } // 인벤토리 아이템 배열 공간들 얻기
	
	UInv_InventoryItem* Manifest(UObject* NewOuter); //새로운 인벤토리 아이템 만들 때?
	EInv_ItemCategory GetItemCategory() const { return ItemCategory; } // 아이템 카테고리 얻기
	FGameplayTag GetItemType() const { return ItemType; } // 아이템 타입 얻기
	FText GetDisplayName() const { return DisplayName; } // ⭐ 표시 이름 얻기
	void AssimilateInventoryFragments(UInv_CompositeBase* Composite) const; // 인벤토리 구성요소 동화

	template <typename T>  requires std::derived_from<T, FInv_ItemFragment>// T타입만 전달하도록 강제하는 방법은? C++20
	const T* GetFragmentOfTypeWithTag(const FGameplayTag& FragmentTag) const; // 태그로 구성요소 얻기

	template <typename T>  requires std::derived_from<T, FInv_ItemFragment>
	const T* GetFragmentOfType() const; 

	template <typename T>  requires std::derived_from<T, FInv_ItemFragment>
	T* GetFragmentOfTypeMutable();
	
	template<typename T> requires std::derived_from<T, FInv_ItemFragment>
	TArray<const T*> GetAllFragmentsOfType() const;
	
	void SpawnPickupActor(const UObject* WorldContextObject, const FVector& SpawnLocation, const FRotator& SpawnRotation); // 아이템 픽업 액터 생성

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ExcludeBaseStruct)) 
	TArray<TInstancedStruct<FInv_ItemFragment>> Fragments; // 인벤토리 아이템 배열 공간들.

	UPROPERTY(EditAnywhere, Category = "Inventory")
	EInv_ItemCategory ItemCategory{ EInv_ItemCategory::None }; // 개별 구성요소?

	// 게임플레이 태그 부분
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories ="GameItems"))
	FGameplayTag ItemType;

	// ⭐ 표시 이름 (UI에서 사용되는 한글 이름)
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "표시 이름"))
	FText DisplayName;

	// 아이템 픽업 액터 클래스
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<AActor> PickupActorClass;
	
	void ClearFragments();
};

template <typename T>
requires std::derived_from<T, FInv_ItemFragment>// T타입만 전달하도록 강제하는 방법은? C++20
const T* FInv_ItemManifest::GetFragmentOfTypeWithTag(const FGameplayTag& FragmentTag) const
{
	for (const TInstancedStruct<FInv_ItemFragment>& Fragment : Fragments) // 여러개를 찾는 과정
	{
		if (const T* FragmentPtr = Fragment.GetPtr<T>())
		{
			if(!FragmentPtr->GetFragmentTag().MatchesTagExact(FragmentTag)) continue; // 태그가 정확히 일치하는지 확인일치하지 않으면 다음으로 넘어감 
			return FragmentPtr; // 찾았을 땐 하나의 해당 포인터 반환
		}
	}

	return nullptr; // 아무것도 찾지 못했을 땐 nullptr 반환
}

template <typename T> requires std::derived_from<T, FInv_ItemFragment>
const T* FInv_ItemManifest::GetFragmentOfType() const
{
	for (const TInstancedStruct<FInv_ItemFragment>& Fragment : Fragments) // 여러개를 찾는 과정
	{
		if (const T* FragmentPtr = Fragment.GetPtr<T>())
		{
			return FragmentPtr; // 찾았을 땐 하나의 해당 포인터 반환
		}
	}

	return nullptr; // 아무것도 찾지 못했을 땐 nullptr 반환
}

template <typename T> requires std::derived_from<T, FInv_ItemFragment>
T* FInv_ItemManifest::GetFragmentOfTypeMutable()
{
	for (TInstancedStruct<FInv_ItemFragment>& Fragment : Fragments) // 여러개를 찾는 과정
	{
		if (T* FragmentPtr = Fragment.GetMutablePtr<T>()) // 포인터는 상수로 변환
		{
			return FragmentPtr; // 찾았을 땐 하나의 해당 포인터 반환
		}
	}

	return nullptr; // 아무것도 찾지 못했을 땐 nullptr 반환
}

template <typename T> requires std::derived_from<T, FInv_ItemFragment>
TArray<const T*> FInv_ItemManifest::GetAllFragmentsOfType() const // 여러 조각들을 얻는 방법
{
	TArray<const T*> Result; // 결과 배열
	for (const TInstancedStruct<FInv_ItemFragment>& Fragment : Fragments) // 모든 프래그먼트 반복
	{
		if (const T* FragmentPtr = Fragment.GetPtr<T>()) //	 T타입의 포인터 얻기
		{
			Result.Add(FragmentPtr); // 결과 배열에 추가
		}
	}
	return Result; // 결과 반환
}
