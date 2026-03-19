#include "InventoryManagement/FastArray/Inv_FastArray.h"

#include "Inventory.h"  // INV_DEBUG_INVENTORY 매크로 정의
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Components/Inv_LootContainerComponent.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Types/Inv_GridTypes.h"

TArray<UInv_InventoryItem*> FInv_InventoryFastArray::GetAllItems() const
{
	TArray<UInv_InventoryItem*> Results;
	Results.Reserve(Entries.Num());
	for (const auto& Entry : Entries)
	{
		if (!IsValid(Entry.Item)) continue;
		Results.Add(Entry.Item);
	}
	return Results;
}

void FInv_InventoryFastArray::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	// ⭐ [Phase 9] OwnerComponent가 InventoryComponent 또는 LootContainerComponent인지 분기
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	UInv_LootContainerComponent* ContainerComp = nullptr;
	if (!IsValid(IC))
	{
		ContainerComp = Cast<UInv_LootContainerComponent>(OwnerComponent);
		if (!IsValid(ContainerComp)) return;
	}

#if INV_DEBUG_INVENTORY
	// 🔍 [진단] PreReplicatedRemove 호출 컨텍스트
	UE_LOG(LogTemp, Error, TEXT("🔍 [PreReplicatedRemove 진단] RemovedIndices=%d, FinalSize=%d, Entries=%d"),
		RemovedIndices.Num(), FinalSize, Entries.Num());
	for (int32 DiagIdx : RemovedIndices)
	{
		if (Entries.IsValidIndex(DiagIdx) && IsValid(Entries[DiagIdx].Item))
		{
			UE_LOG(LogTemp, Error, TEXT("🔍 [PreReplicatedRemove 진단] Index=%d, ItemType=%s"),
				DiagIdx, *Entries[DiagIdx].Item->GetItemManifest().GetItemType().ToString());
		}
	}
#endif

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PreReplicatedRemove 호출됨! (FastArray) ==="));
	UE_LOG(LogTemp, Warning, TEXT("제거된 항목 개수: %d / 최종 크기: %d"), RemovedIndices.Num(), FinalSize);
#endif

	for (int32 Index : RemovedIndices)
	{
		if (!Entries.IsValidIndex(Index))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("❌ 잘못된 Index: %d"), Index);
#endif
			continue;
		}

		UInv_InventoryItem* RemovedItem = Entries[Index].Item;
		if (IsValid(RemovedItem))
		{
			// ⭐ GameplayTag 복사 (안전!)
			FGameplayTag ItemType = RemovedItem->GetItemManifest().GetItemType();

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("🗑️ 제거될 아이템: %s (Index: %d)"),
				*ItemType.ToString(), Index);
#endif

			// ⭐ OnItemRemoved 델리게이트 브로드캐스트 (모든 아이템)
			if (IsValid(IC))
			{
				#if INV_DEBUG_INVENTORY
				// [Fix29진단] Remove Broadcast 직전
				UE_LOG(LogTemp, Warning, TEXT("[PreRepRemove진단] Broadcast: IC=%s (ptr=%p) | Item=%s (ptr=%p) | EntryIdx=%d"),
					*IC->GetName(), IC,
					*ItemType.ToString(), RemovedItem, Index);
#endif
				IC->OnItemRemoved.Broadcast(RemovedItem, Index);
			}
			else if (IsValid(ContainerComp))
			{
				// [Phase 9] 컨테이너 아이템 제거 델리게이트
				ContainerComp->OnContainerItemRemoved.Broadcast(RemovedItem, Index);
			}

			// ⭐⭐⭐ Stackable 아이템만 OnMaterialStacksChanged 호출!
			// Non-stackable(장비)은 UpdateMaterialStacksByTag 실행 안 함 (GameplayTag 기반 삭제 방지)
			if (IsValid(IC) && RemovedItem->IsStackable())
			{
				IC->OnMaterialStacksChanged.Broadcast(ItemType);
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("✅ OnItemRemoved & OnMaterialStacksChanged 브로드캐스트 완료 (Stackable)"));
#endif
			}
#if INV_DEBUG_INVENTORY
			else if (IsValid(IC))
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ OnItemRemoved 브로드캐스트 완료 (Non-stackable, OnMaterialStacksChanged 스킵)"));
			}
#endif
		}
#if INV_DEBUG_INVENTORY
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Index %d의 Item이 nullptr"), Index);
		}
#endif
	}

	RebuildItemTypeIndex(); // ⚠️ 클라이언트 인덱스 캐시 동기화

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PreReplicatedRemove 완료! ==="));
#endif
}

void FInv_InventoryFastArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	// ⭐ [Phase 9] OwnerComponent 이중 캐스트
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	UInv_LootContainerComponent* ContainerComp = nullptr;
	if (!IsValid(IC))
	{
		ContainerComp = Cast<UInv_LootContainerComponent>(OwnerComponent);
		if (!IsValid(ContainerComp)) return;
	}

#if INV_DEBUG_INVENTORY
	// [진단] PostReplicatedAdd 시점 Owner 정보
	AActor* DiagOwner = IsValid(IC) ? IC->GetOwner() : (IsValid(ContainerComp) ? ContainerComp->GetOwner() : nullptr);
	UE_LOG(LogTemp, Error, TEXT("[PostRepAdd진단] IC=%p, ContainerComp=%p, Entries=%d, AddedIndices=%d, Owner=%s"),
		IC, ContainerComp, Entries.Num(), AddedIndices.Num(),
		DiagOwner ? *DiagOwner->GetName() : TEXT("nullptr"));
#endif

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedAdd 호출됨! (FastArray) ==="));
	UE_LOG(LogTemp, Warning, TEXT("추가된 항목 개수: %d / 전체 Entry 수: %d"), AddedIndices.Num(), Entries.Num());
#endif

	// 인벤토리 컴포넌트에 있는 아이템을 서버에서 클라이언트로 받는 거?
	for (int32 Index : AddedIndices)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PostReplicatedAdd] 처리 중: Index=%d"), Index);
#endif

		if (!Entries.IsValidIndex(Index))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("[PostReplicatedAdd] ❌ Index %d는 유효하지 않음! Entries.Num()=%d"), Index, Entries.Num());
#endif
			continue;
		}

		if (!IsValid(Entries[Index].Item))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("[PostReplicatedAdd] ❌ Index %d의 Item이 nullptr입니다!"), Index);
#endif
			continue;
		}

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PostReplicatedAdd] Index: %d, ItemType: %s"),
			Index, *Entries[Index].Item->GetItemManifest().GetItemType().ToString());
#endif
#if INV_DEBUG_ATTACHMENT
		// ★ [부착진단-클라] PostReplicatedAdd 수신 데이터 확인 ★
		{
			const FInv_AttachmentHostFragment* DiagHost =
				Entries[Index].Item->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
			if (DiagHost && DiagHost->GetAttachedItems().Num() > 0)
			{
				UE_LOG(LogTemp, Error, TEXT("[부착진단-클라] PostReplicatedAdd 수신: Index=%d, %s, AttachedItems=%d"),
					Index,
					*Entries[Index].Item->GetItemManifest().GetItemType().ToString(),
					DiagHost->GetAttachedItems().Num());
			}
			else if (Entries[Index].Item->HasAttachmentSlots())
			{
				UE_LOG(LogTemp, Error, TEXT("[부착진단-클라] PostReplicatedAdd 수신: Index=%d, %s, HasSlots=TRUE, AttachedItems=%d"),
					Index,
					*Entries[Index].Item->GetItemManifest().GetItemType().ToString(),
					DiagHost ? DiagHost->GetAttachedItems().Num() : -1);
			}
		}
#endif

		// ⭐ Entry Index도 함께 전달하여 클라이언트에서 저장 가능!
		// ⭐ [부착물 시스템] bIsAttachedToWeapon 아이템은 그리드에 추가하지 않음
		if (Entries[Index].bIsAttachedToWeapon)
		{
#if INV_DEBUG_ATTACHMENT
			UE_LOG(LogTemp, Log, TEXT("[PostReplicatedAdd] Entry[%d] bIsAttachedToWeapon=true → 그리드 추가 스킵"), Index);
#endif
			continue;
		}

		// ⭐ [Fix 13] 장착 아이템은 그리드에 추가하지 않음 (공간 선점 방지)
		if (Entries[Index].bIsEquipped)
		{
			UE_LOG(LogTemp, Log, TEXT("[PostReplicatedAdd] Entry[%d] bIsEquipped=true → 그리드 추가 스킵 (%s)"),
				Index, *Entries[Index].Item->GetItemManifest().GetItemType().ToString());
			continue;
		}

		// ⭐ [Phase 9] InventoryComponent 또는 LootContainerComponent 분기
		if (IsValid(IC))
		{
	#if INV_DEBUG_INVENTORY
			// [Fix29진단] Broadcast 직전 — 어떤 IC에서 발생하는지 확인
			UE_LOG(LogTemp, Warning, TEXT("[PostRepAdd진단] Broadcast: IC=%s (ptr=%p) | ItemCat=%d | Item=%s | EntryIdx=%d"),
				*IC->GetName(), IC,
				(int32)Entries[Index].Item->GetItemManifest().GetItemCategory(),
				*Entries[Index].Item->GetItemManifest().GetItemType().ToString(),
				Index);
#endif
			IC->OnItemAdded.Broadcast(Entries[Index].Item, Index);
		}
		else if (IsValid(ContainerComp))
		{
			ContainerComp->OnContainerItemAdded.Broadcast(Entries[Index].Item, Index);
		}
	}

	RebuildItemTypeIndex(); // ⚠️ 클라이언트 인덱스 캐시 동기화

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedAdd 완료! ==="));
#endif
}

void FInv_InventoryFastArray::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	// ⭐ [Phase 9] OwnerComponent 이중 캐스트
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	UInv_LootContainerComponent* ContainerComp = nullptr;
	if (!IsValid(IC))
	{
		ContainerComp = Cast<UInv_LootContainerComponent>(OwnerComponent);
		if (!IsValid(ContainerComp)) return;
	}

#if INV_DEBUG_INVENTORY
	// 🔍 [진단] PostReplicatedChange 호출 컨텍스트
	UE_LOG(LogTemp, Error, TEXT("🔍 [PostReplicatedChange 진단] ChangedIndices=%d, FinalSize=%d, Entries=%d"),
		ChangedIndices.Num(), FinalSize, Entries.Num());
	for (int32 DiagIdx : ChangedIndices)
	{
		if (Entries.IsValidIndex(DiagIdx) && IsValid(Entries[DiagIdx].Item))
		{
			UE_LOG(LogTemp, Error, TEXT("🔍 [PostReplicatedChange 진단] Index=%d, ItemType=%s, Category=%d"),
				DiagIdx, *Entries[DiagIdx].Item->GetItemManifest().GetItemType().ToString(),
				(int32)Entries[DiagIdx].Item->GetItemManifest().GetItemCategory());
		}
	}
#endif

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedChange 호출됨 (FastArray) ==="));
	UE_LOG(LogTemp, Warning, TEXT("📋 변경된 항목 개수: %d / 전체 Entry 수: %d"), ChangedIndices.Num(), Entries.Num());
#endif

	for (int32 Index : ChangedIndices)
	{
		if (!Entries.IsValidIndex(Index))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("❌ 잘못된 Index: %d (전체: %d)"), Index, Entries.Num());
#endif
			continue;
		}

		UInv_InventoryItem* ChangedItem = Entries[Index].Item;
		if (!IsValid(ChangedItem))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Entry[%d]: Item이 nullptr (제거됨)"), Index);
#endif
			continue;
		}

#if INV_DEBUG_ATTACHMENT
		// ★ [부착진단-클라] 리플리케이션 수신 데이터 확인 ★
		{
			const FInv_AttachmentHostFragment* DiagHost =
				ChangedItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
			if (DiagHost && DiagHost->GetAttachedItems().Num() > 0)
			{
				UE_LOG(LogTemp, Error, TEXT("[부착진단-클라] PostReplicatedChange 수신: Index=%d, %s, AttachedItems=%d"),
					Index,
					*ChangedItem->GetItemManifest().GetItemType().ToString(),
					DiagHost->GetAttachedItems().Num());
				for (int32 d = 0; d < DiagHost->GetAttachedItems().Num(); d++)
				{
					const FInv_AttachedItemData& DiagData = DiagHost->GetAttachedItems()[d];
					UE_LOG(LogTemp, Error, TEXT("[부착진단-클라]   [%d] Type=%s (Slot=%d), ManifestCopy.ItemType=%s"),
						d, *DiagData.AttachmentItemType.ToString(), DiagData.SlotIndex,
						*DiagData.ItemManifestCopy.GetItemType().ToString());
				}
			}
			else if (ChangedItem->HasAttachmentSlots())
			{
				UE_LOG(LogTemp, Error, TEXT("[부착진단-클라] PostReplicatedChange 수신: Index=%d, %s, HasSlots=TRUE 이지만 AttachedItems=%d!"),
					Index,
					*ChangedItem->GetItemManifest().GetItemType().ToString(),
					DiagHost ? DiagHost->GetAttachedItems().Num() : -1);
			}
		}
#endif

		int32 NewStackCount = ChangedItem->GetTotalStackCount();
		EInv_ItemCategory Category = ChangedItem->GetItemManifest().GetItemCategory();

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("📦 FastArray 변경 감지 [%d]: Item포인터=%p, ItemType=%s, Category=%d, NewStackCount=%d, bIsAttached=%s"),
			Index, ChangedItem, *ChangedItem->GetItemManifest().GetItemType().ToString(),
			(int32)Category, NewStackCount,
			Entries[Index].bIsAttachedToWeapon ? TEXT("TRUE") : TEXT("FALSE"));
#endif

		// ⭐ [부착물 시스템] bIsAttachedToWeapon 플래그 처리
		// true → 그리드에서 숨김 (OnItemRemoved), false → 그리드에 표시 (OnItemAdded)
		if (Entries[Index].bIsAttachedToWeapon)
		{
			// 부착됨 → 그리드에서 제거
			if (IsValid(IC))
			{
				IC->OnItemRemoved.Broadcast(ChangedItem, Index);
			}
			else if (IsValid(ContainerComp))
			{
				ContainerComp->OnContainerItemRemoved.Broadcast(ChangedItem, Index);
			}
#if INV_DEBUG_ATTACHMENT
			UE_LOG(LogTemp, Log, TEXT("[PostReplicatedChange] Entry[%d] bIsAttachedToWeapon=true → OnItemRemoved (그리드에서 숨김)"), Index);
#endif
			continue;
		}

		// ⭐ [Phase 9] 컨테이너는 아이템 전체 이동만 수행 (스택 변경 RPC 없음)
		// B10: OnContainerItemAdded는 "추가" 이벤트이므로 "변경"에서 호출하면 중복 UI 발생
		// 향후 부분 전송/스택 분할 추가 시 OnContainerItemChanged 델리게이트 필요
		if (IsValid(ContainerComp))
		{
			continue;
		}

		// ⭐⭐⭐ Craftables(재료)만 AddStacks() 호출! (차감 로직)
		if (Category == EInv_ItemCategory::Craftable)
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("  → Craftable 재료: OnStackChange 호출 (차감/분배 로직)"));
#endif

			FInv_SlotAvailabilityResult Result;
			Result.Item = ChangedItem;
			Result.bStackable = true;
			Result.TotalRoomToFill = NewStackCount;
			Result.EntryIndex = Index;

			IC->OnStackChange.Broadcast(Result);  // AddStacks() 호출

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("✅ OnStackChange 브로드캐스트 완료! (Entry[%d], NewCount: %d)"),
				Index, NewStackCount);
#endif
		}
		else
		{
#if INV_DEBUG_INVENTORY
			// ⭐⭐⭐ Equippables, Consumables는 직접 UI 업데이트!
			UE_LOG(LogTemp, Warning, TEXT("  → Non-Craftable (Category=%d): OnItemStackChanged 호출 (직접 UI 업데이트)"),
				(int32)Category);
#endif

			// ⭐ OnStackChange 대신 OnItemStackChanged 브로드캐스트 (스택 증가 전용!)
			// EntryIndex와 NewStackCount를 포함한 Result 생성
			FInv_SlotAvailabilityResult Result;
			Result.Item = ChangedItem;
			Result.bStackable = true;
			Result.TotalRoomToFill = NewStackCount;
			Result.EntryIndex = Index;

			// ⭐ 새로운 델리게이트 대신 기존 OnItemAdded 재사용 (UI가 아이템 찾아서 업데이트)
			IC->OnItemAdded.Broadcast(ChangedItem, Index);

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("✅ OnItemAdded 브로드캐스트 완료! (Entry[%d], NewCount: %d)"),
				Index, NewStackCount);
#endif
		}
	}

	RebuildItemTypeIndex(); // ⚠️ 클라이언트 인덱스 캐시 동기화

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedChange 완료 (총 %d개 Entry 처리됨) ==="), ChangedIndices.Num());
#endif
}

// FastArray에 항목을 추가해주는 기능들.
UInv_InventoryItem* FInv_InventoryFastArray::AddEntry(UInv_ItemComponent* ItemComponent)
{
	//TODO : Implement once ItemComponent is more complete
	// [Fix29-H] check() → safe return (Shipping 빌드에서 프로세스 종료 방지)
	if (!OwnerComponent) { UE_LOG(LogTemp, Error, TEXT("[FastArray] AddEntry(ItemComp): OwnerComponent is null!")); return nullptr; }

#if INV_DEBUG_INVENTORY
	// [진단] AddEntry 호출 시 콜스택 추적 (아이템 중복 원인 파악)
	UE_LOG(LogTemp, Error, TEXT("[AddEntry진단] 호출됨! 현재 Entries=%d, ItemType=%s"),
		Entries.Num(),
		*ItemComponent->GetItemManifest().GetItemType().ToString());
	FDebug::DumpStackTraceToLog(ELogVerbosity::Error);
#endif

	AActor* OwningActor = OwnerComponent->GetOwner(); // 소유자 확보
	if (!OwningActor || !OwningActor->HasAuthority()) return nullptr; // C4: 안전한 early return (check 크래시 방지)
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent); // 소유자 컴포넌트를 인벤토리 컴포넌트로 캐스팅
	UInv_LootContainerComponent* ContainerComp = nullptr;
	if (!IsValid(IC))
	{
		// [Phase 9] LootContainerComponent에서 호출된 경우
		ContainerComp = Cast<UInv_LootContainerComponent>(OwnerComponent);
		if (!IsValid(ContainerComp)) return nullptr;
	}

#if INV_DEBUG_INVENTORY
	// ★ [Phase8진단] ItemComponent의 원본 Manifest에서 SlotPosition 확인 ★
	{
		FInv_ItemManifest SrcManifest = ItemComponent->GetItemManifest();
		const FInv_AttachmentHostFragment* SrcHost = SrcManifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
		if (SrcHost)
		{
			const auto& SrcSlots = SrcHost->GetSlotDefinitions();
			UE_LOG(LogTemp, Error, TEXT("[Phase8진단-AddEntry] ItemComponent 원본 SlotDefs=%d"), SrcSlots.Num());
			for (int32 d = 0; d < SrcSlots.Num(); ++d)
			{
				UE_LOG(LogTemp, Error, TEXT("[Phase8진단-AddEntry]   [%d] %s → Position=%d"),
					d, *SrcSlots[d].SlotType.ToString(), (int32)SrcSlots[d].SlotPosition);
			}
		}
		const FInv_EquipmentFragment* SrcEquip = SrcManifest.GetFragmentOfType<FInv_EquipmentFragment>();
		if (SrcEquip)
		{
			UE_LOG(LogTemp, Error, TEXT("[Phase8진단-AddEntry] PreviewMesh=%s"),
				SrcEquip->HasPreviewMesh() ? TEXT("있음") : TEXT("없음(null)"));
		}
	}
#endif

	FInv_InventoryEntry& NewEntry = Entries.AddDefaulted_GetRef(); // 새 항목 추가
	NewEntry.Item = ItemComponent->GetItemManifestMutable().Manifest(OwningActor); // 항목 매니페스트에서 항목 가져오기 (새로 생성된 아이템의 소유자 지정)

	// ⭐ [Fix11] 비스택 아이템은 Manifest() 후 TotalStackCount가 0으로 남음
	// "아이템이 존재한다 = 최소 1개"이므로 비스택 아이템은 TotalStackCount=1로 초기화
	if (NewEntry.Item && !NewEntry.Item->IsStackable())
	{
		NewEntry.Item->SetTotalStackCount(1);
	}

#if INV_DEBUG_INVENTORY
	// ★ [Phase8진단] 생성된 아이템의 SlotPosition 확인 ★
	{
		const FInv_AttachmentHostFragment* NewHost = NewEntry.Item->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
		if (NewHost)
		{
			const auto& NewSlots = NewHost->GetSlotDefinitions();
			UE_LOG(LogTemp, Error, TEXT("[Phase8진단-AddEntry] 생성된 아이템 SlotDefs=%d"), NewSlots.Num());
			for (int32 d = 0; d < NewSlots.Num(); ++d)
			{
				UE_LOG(LogTemp, Error, TEXT("[Phase8진단-AddEntry]   [%d] %s → Position=%d"),
					d, *NewSlots[d].SlotType.ToString(), (int32)NewSlots[d].SlotPosition);
			}
		}
	}
#endif

	// [Phase 9] InventoryComponent 또는 LootContainerComponent에 리플리케이션 서브오브젝트 등록
	if (IsValid(IC))
	{
		IC->AddRepSubObj(NewEntry.Item);
	}
	else if (IsValid(ContainerComp))
	{
		if (ContainerComp->IsUsingRegisteredSubObjectList() && ContainerComp->IsReadyForReplication() && IsValid(NewEntry.Item))
		{
			ContainerComp->AddReplicatedSubObject(NewEntry.Item);
		}
	}
	MarkItemDirty(NewEntry); // 복제되어야 함을 알려주는 것.
	RebuildItemTypeIndex(); // ⭐ [최적화 #4] 인덱스 캐시 재구축

	return NewEntry.Item; // 새로 추가된 항목 반환
}

UInv_InventoryItem* FInv_InventoryFastArray::AddEntry(UInv_InventoryItem* Item)
{
	// [Fix29-H] check() → safe return (Shipping 빌드에서 프로세스 종료 방지)
	if (!OwnerComponent) { UE_LOG(LogTemp, Error, TEXT("[FastArray] AddEntry(Item*): OwnerComponent is null!")); return nullptr; }

#if INV_DEBUG_INVENTORY
	// [진단] AddEntry(Item*) 호출 콜스택
	UE_LOG(LogTemp, Error, TEXT("[AddEntry진단-Item] 호출됨! 현재 Entries=%d, ItemType=%s"),
		Entries.Num(),
		IsValid(Item) ? *Item->GetItemManifest().GetItemType().ToString() : TEXT("nullptr"));
	FDebug::DumpStackTraceToLog(ELogVerbosity::Error);
#endif

	AActor* OwningActor = OwnerComponent->GetOwner();
	if (!OwningActor || !OwningActor->HasAuthority()) return nullptr; // C4: 안전한 early return

	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	UInv_LootContainerComponent* ContainerComp = nullptr;
	if (!IsValid(IC))
	{
		// [Phase 9] LootContainerComponent에서 호출된 경우
		ContainerComp = Cast<UInv_LootContainerComponent>(OwnerComponent);
		if (!IsValid(ContainerComp)) return nullptr;
	}

	FInv_InventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Item = Item;

	// ⭐ [Fix11] 비스택 아이템은 TotalStackCount=0일 수 있음 → 1로 보정
	if (NewEntry.Item && !NewEntry.Item->IsStackable() && NewEntry.Item->GetTotalStackCount() <= 0)
	{
		NewEntry.Item->SetTotalStackCount(1);
	}

	// [Phase 9] 리플리케이션 등록 분기
	if (IsValid(IC))
	{
		IC->AddRepSubObj(NewEntry.Item);
	}
	else if (IsValid(ContainerComp))
	{
		if (ContainerComp->IsUsingRegisteredSubObjectList() && ContainerComp->IsReadyForReplication() && IsValid(NewEntry.Item))
		{
			ContainerComp->AddReplicatedSubObject(NewEntry.Item);
		}
	}
	MarkItemDirty(NewEntry);
	RebuildItemTypeIndex(); // ⭐ [최적화 #4] 인덱스 캐시 재구축

	return Item;
}

void FInv_InventoryFastArray::RemoveEntry(UInv_InventoryItem* Item)
{
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt) // 반복자가 가리키는 항목?
	{
		FInv_InventoryEntry& Entry = *EntryIt;
		if (Entry.Item == Item)
		{
#if INV_DEBUG_INVENTORY
			// [Swap버그추적] RemoveEntry 콜스택
			UE_LOG(LogTemp, Error, TEXT("========== [RemoveEntry] 삭제 대상: %s =========="),
				IsValid(Item) ? *Item->GetItemManifest().GetItemType().ToString() : TEXT("nullptr"));
			FDebug::DumpStackTraceToLog(ELogVerbosity::Error);
			UE_LOG(LogTemp, Error, TEXT("========== [RemoveEntry] 콜스택 끝 =========="));
#endif

			// ⚠️ 복제 서브오브젝트 등록 해제 (GC 누수 + 네트워크 대역폭 누수 방지)
			if (UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent))
			{
				IC->RemoveRepSubObj(Item);
			}
			// [Phase 9] LootContainerComponent에서도 서브오브젝트 해제
			else if (UInv_LootContainerComponent* CC = Cast<UInv_LootContainerComponent>(OwnerComponent))
			{
				if (CC->IsUsingRegisteredSubObjectList() && IsValid(Item))
				{
					CC->RemoveReplicatedSubObject(Item);
				}
			}

			EntryIt.RemoveCurrent(); // 현재 항목 제거
			MarkArrayDirty();
			RebuildItemTypeIndex(); // ⭐ [최적화 #4] 인덱스 캐시 재구축
			return; // 아이템 찾았으므로 즉시 반환
		}
	}
}

void FInv_InventoryFastArray::ClearAllEntries()
{
	// [Fix29-I] 리플리케이션 서브오브젝트 해제 후 엔트리 제거 (네트워크 + GC 누수 방지)
	if (OwnerComponent)
	{
		if (UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent))
		{
			for (const FInv_InventoryEntry& Entry : Entries)
			{
				if (IsValid(Entry.Item))
				{
					IC->RemoveRepSubObj(Entry.Item);
				}
			}
		}
		else if (UInv_LootContainerComponent* CC = Cast<UInv_LootContainerComponent>(OwnerComponent))
		{
			for (const FInv_InventoryEntry& Entry : Entries)
			{
				if (IsValid(Entry.Item) && CC->IsUsingRegisteredSubObjectList())
				{
					CC->RemoveReplicatedSubObject(Entry.Item);
				}
			}
		}
	}

	// B3: nullptr 엔트리도 포함하여 모든 엔트리 제거
	Entries.Empty();
	MarkArrayDirty();
	RebuildItemTypeIndex();
}

UInv_InventoryItem* FInv_InventoryFastArray::FindFirstItemByType(const FGameplayTag& ItemType)
{
	// ⭐ [최적화 #4] 캐시가 구축되어 있으면 O(1) 조회
	if (ItemTypeIndex.Num() > 0)
	{
		TArray<int32> Indices;
		ItemTypeIndex.MultiFind(ItemType, Indices);
		for (int32 Idx : Indices)
		{
			if (Entries.IsValidIndex(Idx) && IsValid(Entries[Idx].Item))
			{
				return Entries[Idx].Item;
			}
		}
		return nullptr;
	}

	// 캐시 미구축 시 기존 O(n) 폴백
	auto* FoundItem = Entries.FindByPredicate([ItemType = ItemType](const FInv_InventoryEntry& Entry)
	{
		return IsValid(Entry.Item) && Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(ItemType);
	});
	return FoundItem ? FoundItem->Item : nullptr;
}

// ⭐ [최적화 #4] 아이템 타입별 인덱스 캐시 재구축
void FInv_InventoryFastArray::RebuildItemTypeIndex()
{
	ItemTypeIndex.Reset();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (IsValid(Entries[i].Item) && !Entries[i].bIsAttachedToWeapon) // ⚠️ 부착물은 제외 — 재료 소비 시 부착물이 잡히는 버그 방지
		{
			ItemTypeIndex.Add(Entries[i].Item->GetItemManifest().GetItemType(), i);
		}
	}
}

// ⭐ [최적화 #4] 아이템 타입별 개수 조회 (O(1) 해시 조회)
int32 FInv_InventoryFastArray::GetTotalCountByType(const FGameplayTag& ItemType) const
{
	return ItemTypeIndex.Num(ItemType);
}
