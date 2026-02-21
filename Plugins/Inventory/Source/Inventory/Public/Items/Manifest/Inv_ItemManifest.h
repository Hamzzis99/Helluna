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

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 1 최적화] Manifest Fragment 직렬화/역직렬화
	// ════════════════════════════════════════════════════════════════
	// 목적: 저장/로드 시 Fragment 데이터(랜덤 스탯 등)를 바이너리로 보존
	//
	// 사용 흐름:
	//   [저장] CollectInventoryDataForSave() → SerializeFragments() → TArray<uint8>에 저장
	//   [로드] LoadAndSendInventoryToClient() → DeserializeAndApplyFragments() → Fragment 복원
	//
	// 직렬화 대상: Fragments 배열 (TArray<TInstancedStruct<FInv_ItemFragment>>)
	// 직렬화 미대상: ItemCategory, ItemType, DisplayName, PickupActorClass
	//   → 이것들은 CDO(BP 기본값)에서 가져오므로 저장 불필요
	//
	// 기술 근거:
	//   TInstancedStruct는 FArchive 기반 Serialize()를 네이티브 지원
	//   이미 UInv_InventoryItem::ItemManifest가 Replicated로 사용 중
	//   → 동일한 직렬화 경로를 SaveGame에도 적용
	// ════════════════════════════════════════════════════════════════

	/**
	 * 현재 Fragments 배열을 바이너리로 직렬화
	 *
	 * @return 직렬화된 바이트 배열. 실패 시 빈 배열 반환.
	 *
	 * 호출 시점: 저장 데이터 수집 시 (CollectInventoryDataForSave)
	 * 스레드 안전성: GameThread에서만 호출할 것
	 */
	TArray<uint8> SerializeFragments() const;

	/**
	 * 바이너리 데이터에서 Fragments 배열을 역직렬화하여 현재 Manifest에 적용
	 *
	 * @param InData 직렬화된 바이트 배열 (SerializeFragments의 출력)
	 * @return true = 역직렬화 성공, false = 실패 (기존 Fragment 유지)
	 *
	 * 호출 시점: 로드 후 아이템 복원 시 (LoadAndSendInventoryToClient)
	 * 주의: 이 함수 호출 후 Manifest()를 호출하면 값이 다시 초기화됨!
	 *       → 로드 시에는 Manifest() 호출을 건너뛰거나,
	 *         DeserializeAndApplyFragments를 Manifest() 이후에 호출할 것
	 */
	bool DeserializeAndApplyFragments(const TArray<uint8>& InData);

private:
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "프래그먼트 배열", Tooltip = "인벤토리 아이템의 구성요소 배열", ExcludeBaseStruct))
	TArray<TInstancedStruct<FInv_ItemFragment>> Fragments; // 인벤토리 아이템 배열 공간들.

	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "아이템 카테고리", Tooltip = "아이템 분류 (장비/소모품/재료)"))
	EInv_ItemCategory ItemCategory{ EInv_ItemCategory::None }; // 개별 구성요소?

	// 게임플레이 태그 부분
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "아이템 타입", Tooltip = "아이템 종류를 나타내는 GameplayTag", Categories ="GameItems"))
	FGameplayTag ItemType;

	// ⭐ 표시 이름 (UI에서 사용되는 한글 이름)
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "표시 이름", Tooltip = "UI에서 표시되는 아이템 이름"))
	FText DisplayName;

	// 아이템 픽업 액터 클래스
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "픽업 액터 클래스", Tooltip = "월드에 드롭될 때 생성되는 액터 클래스 (장착 BP말고 현재 작성중인 BP로 설정하세요!)"))
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
