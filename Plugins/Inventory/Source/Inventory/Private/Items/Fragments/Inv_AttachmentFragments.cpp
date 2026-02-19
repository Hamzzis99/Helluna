// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 시스템 (Attachment System) — Phase 1 구현
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    AttachmentHostFragment (무기 측)과 AttachableFragment (부착물 측)의 함수 구현
//
// 📌 핵심 로직 흐름:
//
//    [부착물 장착 시] (Phase 2에서 Server RPC가 호출)
//    1. CanAttachToSlot() → 호환성 체크 (부착물 Type == 슬롯 Type)
//    2. AttachItem()      → AttachedItems 배열에 부착물 데이터 추가
//    3. OnEquip()         → 부착물의 EquipModifiers 스탯 적용
//
//    [부착물 분리 시] (Phase 2에서 Server RPC가 호출)
//    1. DetachItem()      → AttachedItems 배열에서 제거, 데이터 반환
//    2. OnUnequip()       → 부착물의 EquipModifiers 스탯 해제
//
//    [무기 장착/해제 시] (Phase 2에서 EquipmentComponent 연동)
//    1. OnEquipAllAttachments()   → 무기에 달린 모든 부착물 스탯 일괄 적용
//    2. OnUnequipAllAttachments() → 무기에 달린 모든 부착물 스탯 일괄 해제
//       → 각 부착물의 ItemManifestCopy에서 AttachableFragment를 찾아 OnEquip/OnUnequip 호출
//
//    [부착물 BP 생성 시] (에디터에서 설정)
//    1. Manifest()    → EquipModifiers의 랜덤 스탯 초기화 (LabeledNumber 패턴)
//    2. Assimilate()  → UI 위젯에 부착물 정보 전달 (기존 Composite 패턴)
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Inventory.h"  // INV_DEBUG_ATTACHMENT 매크로
#include "Widgets/Composite/Inv_CompositeBase.h"


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachmentHostFragment — 무기 측 부착물 슬롯 관리
// ════════════════════════════════════════════════════════════════════════════════

// 슬롯 정의 반환 (범위 밖이면 nullptr)
const FInv_AttachmentSlotDef* FInv_AttachmentHostFragment::GetSlotDef(int32 SlotIndex) const
{
	if (SlotDefinitions.IsValidIndex(SlotIndex))
	{
		return &SlotDefinitions[SlotIndex];
	}
	return nullptr;
}

// 해당 슬롯에 부착물이 끼워져 있는지 확인
// AttachedItems 배열을 순회하여 SlotIndex 매칭
bool FInv_AttachmentHostFragment::IsSlotOccupied(int32 SlotIndex) const
{
	for (const FInv_AttachedItemData& Data : AttachedItems)
	{
		if (Data.SlotIndex == SlotIndex)
		{
			return true;
		}
	}
	return false;
}

// 특정 슬롯의 장착된 부착물 데이터 반환 (없으면 nullptr)
const FInv_AttachedItemData* FInv_AttachmentHostFragment::GetAttachedItemData(int32 SlotIndex) const
{
	for (const FInv_AttachedItemData& Data : AttachedItems)
	{
		if (Data.SlotIndex == SlotIndex)
		{
			return &Data;
		}
	}
	return nullptr;
}

// 부착물 장착 — Phase 2의 Server_AttachItemToWeapon에서 호출
// 호출 전에 IsSlotOccupied + CanAttachToSlot 검증 완료된 상태여야 함
void FInv_AttachmentHostFragment::AttachItem(int32 SlotIndex, const FInv_AttachedItemData& Data)
{
	// 이미 해당 슬롯에 장착된 것이 있으면 무시 (외부에서 미리 검증해야 함)
	if (IsSlotOccupied(SlotIndex)) return;

	FInv_AttachedItemData NewData = Data;
	NewData.SlotIndex = SlotIndex;
	AttachedItems.Add(NewData);
}

// 부착물 분리 — Phase 2의 Server_DetachItemFromWeapon에서 호출
// 반환된 데이터의 ItemManifestCopy로 인벤토리 Grid에 아이템 복원
FInv_AttachedItemData FInv_AttachmentHostFragment::DetachItem(int32 SlotIndex)
{
	for (int32 i = 0; i < AttachedItems.Num(); ++i)
	{
		if (AttachedItems[i].SlotIndex == SlotIndex)
		{
			FInv_AttachedItemData Removed = AttachedItems[i];
			AttachedItems.RemoveAt(i);
			return Removed;
		}
	}
	return FInv_AttachedItemData(); // 빈 데이터 반환 (분리할 게 없음)
}

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 4] Manifest — AttachedItems 보존
// ════════════════════════════════════════════════════════════════
// 호출 경로: 아이템 생성/줍기 시 Manifest() 호출 체인 → 이 함수
// 처리 흐름:
//   ⚠️ 의도적으로 AttachedItems를 건드리지 않음!
//   드롭/줍기 사이클에서 부착물 데이터가 보존되어야 하므로
//   SlotDefinitions는 에디터 데이터이므로 변경 불필요
// Phase 연결: Phase 4 드롭/줍기 확장
// ════════════════════════════════════════════════════════════════
void FInv_AttachmentHostFragment::Manifest()
{
	// ⚠️ 의도적으로 AttachedItems를 건드리지 않음!
	// 드롭/줍기 시 부착물 데이터가 보존되어야 함
	// SlotDefinitions는 에디터 데이터이므로 변경 불필요

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment Drop] AttachmentHostFragment::Manifest() — AttachedItems %d개 보존"),
		AttachedItems.Num());
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 [Phase 8] RestoreDesignTimeSlotPositions
// ════════════════════════════════════════════════════════════════
// 세이브/로드 시 역직렬화된 SlotDefinitions에
// CDO(BP 에디터)의 최신 SlotPosition 값을 다시 적용
//
// 이유: DeserializeAndApplyFragments()가 Fragments 전체를 교체하므로
//       BP 수정 후 저장된 옛날 데이터의 SlotPosition이 그대로 남음
//       SlotPosition은 런타임에 변경되지 않는 디자인타임 전용 값
// ════════════════════════════════════════════════════════════════
void FInv_AttachmentHostFragment::RestoreDesignTimeSlotPositions(const TArray<FInv_AttachmentSlotDef>& CDOSlotDefs)
{
	// SlotType 태그 기준으로 매칭하여 SlotPosition 복원
	for (FInv_AttachmentSlotDef& LoadedSlot : SlotDefinitions)
	{
		for (const FInv_AttachmentSlotDef& CDOSlot : CDOSlotDefs)
		{
			if (LoadedSlot.SlotType.MatchesTagExact(CDOSlot.SlotType))
			{
				LoadedSlot.SlotPosition = CDOSlot.SlotPosition;
				break;
			}
		}
	}
}

// 무기 장착 시 모든 부착물의 스탯 일괄 적용
// Phase 2에서 EquipmentComponent::OnItemEquipped() 확장 시 호출
// 흐름: 무기 OnEquip → OnEquipAllAttachments → 각 부착물 Modifier OnEquip
void FInv_AttachmentHostFragment::OnEquipAllAttachments(APlayerController* PC)
{
	for (FInv_AttachedItemData& Data : AttachedItems)
	{
		// 각 부착물의 ManifestCopy에서 AttachableFragment를 찾아 OnEquip
		FInv_AttachableFragment* Attachable = Data.ItemManifestCopy.GetFragmentOfTypeMutable<FInv_AttachableFragment>();
		if (Attachable)
		{
			Attachable->OnEquip(PC);
		}
	}
}

// 무기 해제 시 모든 부착물의 스탯 일괄 해제
// Phase 2에서 EquipmentComponent::OnItemUnequipped() 확장 시 호출
// 흐름: OnUnequipAllAttachments → 각 부착물 Modifier OnUnequip → 무기 OnUnequip
void FInv_AttachmentHostFragment::OnUnequipAllAttachments(APlayerController* PC)
{
	for (FInv_AttachedItemData& Data : AttachedItems)
	{
		FInv_AttachableFragment* Attachable = Data.ItemManifestCopy.GetFragmentOfTypeMutable<FInv_AttachableFragment>();
		if (Attachable)
		{
			Attachable->OnUnequip(PC);
		}
	}
}


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_AttachableFragment — 부착물 아이템 측 로직
// ════════════════════════════════════════════════════════════════════════════════

// 호환성 체크 — 이 부착물이 해당 슬롯에 들어갈 수 있는지
// 부착물의 AttachmentType과 슬롯의 SlotType이 정확히 일치해야 함
// Phase 2의 Server_AttachItemToWeapon에서 AttachItem 호출 전에 검증용
bool FInv_AttachableFragment::CanAttachToSlot(const FInv_AttachmentSlotDef& SlotDef) const
{
	const bool bResult = AttachmentType.MatchesTagExact(SlotDef.SlotType);
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment] CanAttachToSlot: 부착물=%s, 슬롯=%s → %s"),
		*AttachmentType.ToString(), *SlotDef.SlotType.ToString(),
		bResult ? TEXT("호환") : TEXT("불일치"));
#endif
	return bResult;
}

// 부착물 장착 시 스탯 적용 — 기존 FInv_EquipmentFragment::OnEquip과 동일한 패턴
// EquipModifiers 배열의 각 Modifier에 대해 OnEquip 호출
// 예: DamageModifier +5 → 캐릭터 공격력 +5 적용
void FInv_AttachableFragment::OnEquip(APlayerController* PC)
{
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModRef = Modifier.GetMutable();
		ModRef.OnEquip(PC);
	}
}

// 부착물 분리 시 스탯 해제 — OnEquip의 역순
void FInv_AttachableFragment::OnUnequip(APlayerController* PC)
{
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModRef = Modifier.GetMutable();
		ModRef.OnUnequip(PC);
	}
}

// UI 동화 — 기존 Composite 패턴 따름
// 부착물의 EquipModifiers 정보를 UI 위젯에 전달
// Phase 3의 AttachmentSlotWidget에서 부착물 스탯 표시에 사용
void FInv_AttachableFragment::Assimilate(UInv_CompositeBase* Composite) const
{
	FInv_InventoryItemFragment::Assimilate(Composite);
	for (const auto& Modifier : EquipModifiers)
	{
		const auto& ModRef = Modifier.Get();
		ModRef.Assimilate(Composite);
	}
}

// Manifest 초기화 — 부착물 아이템이 처음 생성될 때 호출
// EquipModifiers의 랜덤 스탯 초기화 (LabeledNumber의 Min~Max 범위에서 결정)
// 한 번 결정된 값은 bRandomizeOnManifest = false로 보존됨
void FInv_AttachableFragment::Manifest()
{
	FInv_InventoryItemFragment::Manifest();
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModRef = Modifier.GetMutable();
		ModRef.Manifest();
	}
}