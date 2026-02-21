// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Items/Manifest/Inv_ItemManifest.h"
#include "Inv_InventoryItem.generated.h"

/**
 * 
 */
UCLASS()
class INVENTORY_API UInv_InventoryItem : public UObject
{
	GENERATED_BODY()
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override; // 이게 복제해주는 것
	virtual bool IsSupportedForNetworking() const override { return true; } // 네트워킹 지원 여부

	void SetItemManifest(const FInv_ItemManifest& Manifest); // 아이템 매니페스트 설정
	const FInv_ItemManifest& GetItemManifest() const { return ItemManifest.Get<FInv_ItemManifest>(); }
	FInv_ItemManifest& GetItemManifestMutable() { return ItemManifest.GetMutable<FInv_ItemManifest>(); }
	bool IsStackable() const; // 아이템이 쌓을 수 있는지 여부 확인
	bool IsConsumable() const; // 아이템이 소비 가능한지 여부 확인

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 2] 부착물 관련 헬퍼 함수
	// ════════════════════════════════════════════════════════════════
	bool HasAttachmentSlots() const;    // 이 아이템이 부착물 슬롯을 가진 호스트(무기)인지
	int32 GetAttachmentSlotCount() const; // 부착물 슬롯 개수
	bool IsAttachableItem() const;      // 이 아이템이 부착물인지
	int32 GetTotalStackCount() const { return TotalStackCount; } // 총 스택 수 가져오기
	void SetTotalStackCount(int32 Count); // 총 스택 수 설정 (MaxStackSize 검증 포함)

	// ⭐ Grid 위치 정보 (서버에서 클라이언트로 동기화됨!)
	FIntPoint GetGridPosition() const { return GridPosition; }
	void SetGridPosition(int32 Col, int32 Row) { GridPosition = FIntPoint(Col, Row); }
	void SetGridPosition(const FIntPoint& Pos) { GridPosition = Pos; }

private: //이 부분 GPT 돌려보자 항목 복제해서 서버에 전달하는 곳. 그게 바로 Replicated
	UPROPERTY(VisibleAnywhere, meta = (BaseStruct = "/Script/Inventory.Inv_ItemManifest", DisplayName = "아이템 매니페스트", Tooltip = "이 인벤토리 아이템의 매니페스트 데이터. 모든 프래그먼트 정보를 포함합니다."), Replicated) //인벤토리 아이템 블루프린트 만드는 곳? 파생?
	FInstancedStruct ItemManifest; // instance struct? 이게 뭔데?
	
	UPROPERTY(Replicated) // 이 아이템의 총 스택 수 Replicated 뜻이 뭘까?
	int32 TotalStackCount{ 0 };

	// ⭐ Grid 위치 (서버→클라이언트 동기화! 실제 위치 저장!)
	UPROPERTY(Replicated)
	FIntPoint GridPosition{ -1, -1 };  // {-1, -1} = 아직 Grid에 배치 안 됨
};

template <typename FragmentType>
const FragmentType* GetFragment(const UInv_InventoryItem* Item, const FGameplayTag& Tag)
{
	if (!IsValid(Item)) return nullptr;

	const FInv_ItemManifest& Manifest = Item->GetItemManifest();
	return Manifest.GetFragmentOfTypeWithTag<FragmentType>(Tag);
}