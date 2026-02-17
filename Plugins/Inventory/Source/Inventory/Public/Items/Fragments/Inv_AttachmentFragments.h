// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 시스템 (Attachment System) — Phase 1 완료
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    인벤토리 플러그인의 부착물 시스템 핵심 데이터 구조 정의
//    무기에 스코프/소음기/그립 등을 장착하는 타르코프 방식 시스템
//
// 📌 포함된 구조체 (4개):
//    ① FInv_AttachmentSlotDef     — 슬롯 1개의 정의 (순수 데이터, Fragment 아님)
//    ② FInv_AttachedItemData      — 장착된 부착물의 런타임 데이터
//    ③ FInv_AttachmentHostFragment — 무기가 가지는 Fragment ("나는 부착물 슬롯이 있어")
//    ④ FInv_AttachableFragment    — 부착물이 가지는 Fragment ("나는 이 슬롯에 들어가")
//
// 📌 사용 예시:
//    BP_Inv_Rifle (총) → Fragments에 FInv_AttachmentHostFragment 추가
//      → SlotDefinitions: [Scope, Muzzle, Grip] 3개 슬롯 정의
//    BP_Inv_Scope (스코프) → Fragments에 FInv_AttachableFragment 추가
//      → AttachmentType: "AttachmentSlot.Scope"
//      → EquipModifiers: [DamageModifier +5]
//
// 📌 Phase 진행 상황:
//    ✅ Phase 1: Fragment 정의 (이 파일)
//    ⬜ Phase 2: 부착/분리 서버 로직 (Inv_InventoryComponent에 Server RPC 추가)
//    ⬜ Phase 3: UI (Inv_AttachmentPanel, Inv_AttachmentSlotWidget 신규)
//    ⬜ Phase 4: 드롭/줍기 확장 (ItemManifest에 부착물 데이터 보존)
//    ⬜ Phase 5: 시각적 표현 (Inv_EquipActor에 소켓 메시 Attach)
//    ⬜ Phase 6: 저장/로드 확장 (FInv_SavedItemData에 부착물 배열 추가)
//
// 📌 Phase 2에서 이 파일과 연결되는 부분:
//    - Inv_InventoryComponent에서 Server_AttachItemToWeapon() RPC 추가
//      → 무기 아이템의 AttachmentHostFragment를 GetFragmentOfTypeMutable로 가져옴
//      → AttachItem() 호출하여 부착물 장착
//    - Inv_InventoryComponent에서 Server_DetachItemFromWeapon() RPC 추가
//      → DetachItem() 호출하여 부착물 분리, Grid에 아이템 복귀
//    - Inv_EquipmentComponent의 OnItemEquipped/OnItemUnequipped에서
//      → OnEquipAllAttachments() / OnUnequipAllAttachments() 호출하여 스탯 합산
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Items/Manifest/Inv_ItemManifest.h"

#include "Inv_AttachmentFragments.generated.h"

class UStaticMesh;
class APlayerController;

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachmentSlotDef - 부착물 슬롯 정의
// ════════════════════════════════════════════════════════════════════════════════
// 부착물 슬롯 1개의 정의 (Fragment가 아닌 순수 데이터)
// SlotType 태그로 부착물의 AttachmentType과 매칭
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachmentSlotDef
{
	GENERATED_BODY()

	// 슬롯 타입 태그 (예: "AttachmentSlot.Scope", "AttachmentSlot.Muzzle", "AttachmentSlot.Grip")
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (Categories = "AttachmentSlot", DisplayName = "SlotType (슬롯 타입)", Tooltip = "부착물의 AttachmentType과 매칭되는 슬롯 타입 태그"))
	FGameplayTag SlotType;

	// UI에 표시할 슬롯 이름 ("스코프 슬롯", "총구 슬롯")
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "SlotDisplayName (슬롯 표시 이름)", Tooltip = "UI에 표시될 슬롯 이름"))
	FText SlotDisplayName;

	// EquipActor의 소켓 이름 (Phase 5 시각적 부착용)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "AttachSocket (부착 소켓)", Tooltip = "EquipActor 메시의 소켓 이름 (예: socket_scope)"))
	FName AttachSocket{NAME_None};

	// 이 슬롯에 장착 가능한 부착물 수 (보통 1)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "MaxCount (최대 장착 수)", Tooltip = "이 슬롯에 동시에 장착 가능한 부착물 수", ClampMin = 1))
	int32 MaxCount{1};
};


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachedItemData - 장착된 부착물 런타임 데이터
// ════════════════════════════════════════════════════════════════════════════════
// 무기에 장착된 부착물 1개의 런타임 데이터
// 드롭/줍기, 저장/로드 시 부착물 아이템을 완전 복원하기 위해
// ItemManifest 전체 사본을 보관함
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachedItemData
{
	GENERATED_BODY()

	// 어느 슬롯에 장착되어 있는지
	UPROPERTY()
	int32 SlotIndex{INDEX_NONE};

	// 부착물 아이템 종류 (ResolveItemClass로 복원 시 사용)
	UPROPERTY()
	FGameplayTag AttachmentItemType;

	// 부착물 아이템의 전체 Manifest 사본 (스탯, 아이콘 등 모든 Fragment 포함)
	UPROPERTY()
	FInv_ItemManifest ItemManifestCopy;
};


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachmentHostFragment - 부착물 호스트 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// "이 아이템은 부착물 슬롯을 가진 호스트(무기)입니다"
// 무기 BP의 ItemManifest Fragments 배열에 추가하여 사용
// SlotDefinitions: 에디터에서 슬롯 구성 정의
// AttachedItems: 런타임에 서버 RPC로 관리되는 장착 상태
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachmentHostFragment : public FInv_ItemFragment
{
	GENERATED_BODY()

	// ── 슬롯 정의 접근 ──

	int32 GetSlotCount() const { return SlotDefinitions.Num(); }
	const FInv_AttachmentSlotDef* GetSlotDef(int32 SlotIndex) const;
	const TArray<FInv_AttachmentSlotDef>& GetSlotDefinitions() const { return SlotDefinitions; }

	// ── 장착 상태 조회 ──

	bool IsSlotOccupied(int32 SlotIndex) const;
	const FInv_AttachedItemData* GetAttachedItemData(int32 SlotIndex) const;
	const TArray<FInv_AttachedItemData>& GetAttachedItems() const { return AttachedItems; }

	// ── 장착/분리 조작 ──

	void AttachItem(int32 SlotIndex, const FInv_AttachedItemData& Data);
	FInv_AttachedItemData DetachItem(int32 SlotIndex);

	// ── 부착물 스탯 일괄 적용/해제 ──

	void OnEquipAllAttachments(APlayerController* PC);
	void OnUnequipAllAttachments(APlayerController* PC);

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 4] Manifest 시 AttachedItems 보존
	// ════════════════════════════════════════════════════════════════
	// 기본 Manifest()는 Fragment를 초기화하지만
	// AttachedItems는 런타임 장착 데이터이므로 보존해야 함
	// 드롭/줍기 시 부착물 데이터가 유지됨
	// ════════════════════════════════════════════════════════════════
	virtual void Manifest() override;

private:
	// 에디터에서 정의하는 슬롯 배열 (예: 총은 [Scope, Muzzle, Grip] 3개)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "SlotDefinitions (슬롯 정의 배열)", Tooltip = "이 무기가 가진 부착물 슬롯 목록"))
	TArray<FInv_AttachmentSlotDef> SlotDefinitions;

	// 런타임: 현재 장착된 부착물 목록 (서버 RPC로 변경됨)
	UPROPERTY()
	TArray<FInv_AttachedItemData> AttachedItems;
};


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachableFragment - 부착 가능 아이템 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// "이 아이템은 부착물입니다. 특정 슬롯에 들어갈 수 있습니다"
// 스코프, 소음기, 그립 등 부착물 BP의 ItemManifest에 추가하여 사용
// AttachmentType: 어떤 슬롯에 끼울 수 있는지 (SlotType과 매칭)
// EquipModifiers: 장착 시 적용되는 스탯 효과 (기존 구조 재활용)
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_AttachableFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	// 이 부착물이 해당 슬롯에 호환되는지 검사
	bool CanAttachToSlot(const FInv_AttachmentSlotDef& SlotDef) const;

	// 부착물 장착/해제 시 스탯 적용
	void OnEquip(APlayerController* PC);
	void OnUnequip(APlayerController* PC);

	// 부착물 타입 태그 접근
	FGameplayTag GetAttachmentType() const { return AttachmentType; }

	// 부착물 메시 접근 (Phase 5 시각적 표현용)
	UStaticMesh* GetAttachmentMesh() const { return AttachmentMesh; }
	const FTransform& GetAttachOffset() const { return AttachOffset; }

	// UI 동화 / Manifest 초기화
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;
	virtual void Manifest() override;

private:
	// 이 부착물이 들어갈 수 있는 슬롯 타입
	// 예: "AttachmentSlot.Scope" → Scope 슬롯에만 장착 가능
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (Categories = "AttachmentSlot", DisplayName = "AttachmentType (부착물 타입)", Tooltip = "이 부착물이 장착될 수 있는 슬롯 타입 태그"))
	FGameplayTag AttachmentType;

	// 소켓에 부착될 메시 (Phase 5에서 사용)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "AttachmentMesh (부착물 메시)", Tooltip = "무기 소켓에 부착될 스태틱 메시"))
	TObjectPtr<UStaticMesh> AttachmentMesh = nullptr;

	// 소켓 기준 오프셋 (위치/회전 미세 조정)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "AttachOffset (부착 오프셋)", Tooltip = "소켓 기준 위치/회전 오프셋"))
	FTransform AttachOffset{FTransform::Identity};

	// 장착 시 적용되는 스탯 효과 (기존 EquipModifier 구조 재활용)
	// 예: DamageModifier +5, ArmorModifier +3
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (ExcludeBaseStruct, DisplayName = "EquipModifiers (장착 효과 목록)", Tooltip = "부착물 장착 시 적용될 스탯 효과들"))
	TArray<TInstancedStruct<FInv_EquipModifier>> EquipModifiers;
};