// Gihyeon's Inventory Project

// ════════════════════════════════════════════════════════════════
// 📌 리슨서버 호환 수정 이력
// ════════════════════════════════════════════════════════════════
// [2026-02-17] 작업자: 김기현
//   - IsListenServerOrStandalone() 헬퍼 함수 추가
//   - Server_ConsumeMaterialsMultiStack: 리슨서버 호스트 UI 갱신 추가
//   - Server_ConsumeItem: 리슨서버 호스트 OnItemRemoved/OnItemAdded 추가
//   - Server_AddStacksToItem: 기존 스택 추가 시 리슨서버 호스트 OnItemAdded 추가
//   - Server_SplitItemEntry: 원본 아이템 스택 변경 시 리슨서버 호스트 OnItemAdded 추가
//   - Server_UpdateItemStackCount: 리슨서버 호스트 OnItemAdded 추가
//   - 기존 NM_ListenServer 분기를 IsListenServerOrStandalone()으로 통합
// ════════════════════════════════════════════════════════════════

#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Components/Inv_LootContainerComponent.h"

#include "Inventory.h"  // INV_DEBUG_INVENTORY 매크로 정의
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Net/UnrealNetwork.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Building/Components/Inv_BuildingComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Kismet/GameplayStatics.h"
#include "Persistence/Inv_SaveGameMode.h"  // [최적화] FindItemComponentTemplate 사용
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData 사용
#include "Blueprint/WidgetBlueprintLibrary.h"  // CloseOtherMenus에서 CraftingMenu 검색용

// ════════════════════════════════════════════════════════════════════════════════
// 📌 IsListenServerOrStandalone
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 용도:
//    FastArray 리플리케이션이 자기 자신(리슨서버 호스트)에게 안 되는 환경에서
//    직접 UI 갱신 Broadcast가 필요한지 판단하는 헬퍼
//
// 📌 호출 위치:
//    Server_AddNewItem, Server_AddStacksToItem, Server_ConsumeMaterialsMultiStack,
//    Server_CraftItemWithMaterials, Server_ConsumeItem 등 서버 RPC 구현부
//
// ════════════════════════════════════════════════════════════════════════════════
namespace
{
bool IsInventoryItemOwnedByThisComponent(const UInv_InventoryComponent* InventoryComponent, const UInv_InventoryItem* Item)
{
	if (!IsValid(InventoryComponent) || !IsValid(Item))
	{
		return false;
	}

	return IsValid(InventoryComponent->GetOwner()) && Item->GetOuter() == InventoryComponent->GetOwner();
}

int32 FindOwnedInventoryEntryIndex(const UInv_InventoryComponent* InventoryComponent, const UInv_InventoryItem* Item)
{
	if (!IsInventoryItemOwnedByThisComponent(InventoryComponent, Item))
	{
		return INDEX_NONE;
	}

	return InventoryComponent->FindEntryIndexForItem(Item);
}

bool HasEquipmentFragment(const UInv_InventoryItem* Item)
{
	return IsValid(Item) && Item->GetItemManifest().GetFragmentOfType<FInv_EquipmentFragment>() != nullptr;
}
}

bool UInv_InventoryComponent::IsListenServerOrStandalone() const
{
	return GetOwner() &&
		(GetOwner()->GetNetMode() == NM_ListenServer ||
		 GetOwner()->GetNetMode() == NM_Standalone);
}

UInv_InventoryComponent::UInv_InventoryComponent() : InventoryList(this)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // 기본적으로 복제 설정
	bReplicateUsingRegisteredSubObjectList = true; // 등록된 하위 객체 목록을 사용하여 복제 설정
	bInventoryMenuOpen = false;	// 인벤토리 메뉴가 열려있는지 여부 초기화
}

void UInv_InventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const // 복제 속성 설정 함수
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, InventoryList); // 인벤토리 목록 복제 설정
}

void UInv_InventoryComponent::TryAddItem(UInv_ItemComponent* ItemComponent)
{
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [PICKUP] TryAddItem 시작 ==="));
#endif

	// 디버깅: ItemComponent 정보 출력
	if (!IsValid(ItemComponent))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[PICKUP] ItemComponent가 nullptr입니다!"));
#endif
		return;
	}

	AActor* OwnerActor = ItemComponent->GetOwner();
	if (IsValid(OwnerActor))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 픽업할 Actor: %s"), *OwnerActor->GetName());
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] Actor Blueprint 클래스: %s"), *OwnerActor->GetClass()->GetName());
#endif
	}

	const FInv_ItemManifest& Manifest = ItemComponent->GetItemManifest();
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 아이템 정보:"));
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - ItemType (GameplayTag): %s"), *Manifest.GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - ItemCategory: %d"), (int32)Manifest.GetItemCategory());
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - PickupMessage: %s"), *ItemComponent->GetPickupMessage());

	// PickupActorClass 정보 추가 (크래프팅에서 사용할 Blueprint 확인용!)
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 📦 이 아이템의 PickupActorClass (크래프팅에 사용해야 하는 Blueprint):"));
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]    Blueprint 클래스: %s"), *OwnerActor->GetClass()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]    전체 경로: %s"), *OwnerActor->GetClass()->GetPathName());
#endif

	FInv_SlotAvailabilityResult Result = InventoryMenu->HasRoomForItem(ItemComponent); // 인벤토리에 아이템을 추가할 수 있는지 확인하는 부분.

	UInv_InventoryItem* FoundItem = InventoryList.FindFirstItemByType(ItemComponent->GetItemManifest().GetItemType()); // 동일한 유형의 아이템이 이미 있는지 확인하는 부분.
	Result.Item = FoundItem; // 찾은 아이템을 결과에 설정.

	if (Result.TotalRoomToFill == 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 인벤토리에 공간이 없습니다!"));
#endif
		NoRoomInInventory.Broadcast(); // 나 인벤토리 꽉찼어! 이걸 알려주는거야! 방송 삐용삐용 모두 알아둬라!
		return;
	}

	// 아이템 스택 가능 정보를 전달하는 것? 서버 RPC로 해보자.
	if (Result.Item.IsValid() && Result.bStackable) // 유효한지 검사하는 작업. 쌓을 수 있다면 다음 부분들을 진행.
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 스택 가능 아이템! 기존 스택에 추가합니다."));
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 추가할 개수: %d"), Result.TotalRoomToFill);
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 남은 개수: %d"), Result.Remainder);
#endif

		// 이미 존재하는 아이템에 스택을 추가하는 부분.
		OnStackChange.Broadcast(Result); // 스택 변경 사항 방송
		Server_AddStacksToItem(ItemComponent, Result.TotalRoomToFill, Result.Remainder); // 아이템을 추가하는 부분.
	}
	// 서버에서 아이템 등록
	else if (Result.TotalRoomToFill > 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 새로운 아이템 추가!"));
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 스택 개수: %d"), Result.bStackable ? Result.TotalRoomToFill : 0);
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 남은 개수: %d"), Result.Remainder);
#endif

		// This item type dosen't exist in the inventory. Create a new one and update all partient slots.
		Server_AddNewItem(ItemComponent, Result.bStackable ? Result.TotalRoomToFill : 0, Result.Remainder); //쌓을 수 있다면 채울 수 있는 공간 이런 문법은 또 처음 보네
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [PICKUP] TryAddItem 완료 ==="));
#endif
}

// ════════════════════════════════════════════════════════════════
// WithValidation: 파라미터 범위/null 검사 (빠른 early reject, 치팅 방지)
// ════════════════════════════════════════════════════════════════

bool UInv_InventoryComponent::Server_AddNewItem_Validate(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder)
{
	// UObject 포인터는 리플리케이션 타이밍으로 일시적 invalid 가능 → Validate에서 체크하면 강제 킥됨
	// null 체크는 _Implementation에서 수행
	return StackCount >= 0 && Remainder >= 0;
}

void UInv_InventoryComponent::Server_AddNewItem_Implementation(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder) // 서버에서 새로운 아이템 추가 구현
{
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER PICKUP] Server_AddNewItem_Implementation 시작 ==="));
#endif

	if (!IsValid(ItemComponent))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER PICKUP] ItemComponent가 nullptr입니다!"));
#endif
		return;
	}

	AActor* OwnerActor = ItemComponent->GetOwner();
#if INV_DEBUG_INVENTORY
	if (IsValid(OwnerActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] Actor: %s (Class: %s)"),
			*OwnerActor->GetName(), *OwnerActor->GetClass()->GetName());
	}

	const FInv_ItemManifest& Manifest = ItemComponent->GetItemManifest();
	UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] 아이템 정보:"));
	UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP]   - GameplayTag: %s"), *Manifest.GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP]   - Category: %d"), (int32)Manifest.GetItemCategory());
	UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP]   - StackCount: %d"), StackCount);
	UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP]   - Remainder: %d"), Remainder);
#endif

	UInv_InventoryItem* NewItem = InventoryList.AddEntry(ItemComponent); // 여기서 아이템을정상적으로 줍게 된다면? 추가를 한다.

	if (!IsValid(NewItem))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER PICKUP] InventoryList.AddEntry 실패!"));
#endif
		return;
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] InventoryList.AddEntry 성공! NewItem 생성됨"));
#endif

	NewItem->SetTotalStackCount(StackCount);

	// ── 리슨서버/스탠드얼론 전용: FastArray 자기 자신 리플리케이션 우회 ──
	// 데디서버에서는 FastArray가 자동으로 클라이언트에 리플리케이션 → PostReplicatedAdd 콜백 → UI 갱신
	// 리슨서버 호스트는 서버=클라이언트이므로 자기 자신에게 리플리케이션이 안 됨
	// → 직접 OnItemAdded.Broadcast()로 UI에 알려야 함
	if (IsListenServerOrStandalone())
	{
		// ⭐ Entry Index 계산 (새로 추가된 항목은 맨 뒤)
		int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] ListenServer/Standalone 모드 - OnItemAdded 델리게이트 브로드캐스트 (EntryIndex=%d)"), NewEntryIndex);
#endif
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
	}
#if INV_DEBUG_INVENTORY
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] Dedicated Server 모드 - FastArray 리플리케이션에 의존"));
	}
#endif

	// 아이템 개수가 인벤토리 개수보다 많아져도 파괴되지 않게 안전장치를 걸기.
	if (Remainder == 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] Remainder == 0, ItemComponent->PickedUp() 호출 (Actor 파괴)"));
#endif
		ItemComponent->PickedUp();
	}
	else if (FInv_StackableFragment* StackableFragment = ItemComponent->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>()) // 복사본이 아니라 실제 참조본을 가져오는 것.
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] Remainder > 0 (%d), StackCount 업데이트"), Remainder);
#endif
		StackableFragment->SetStackCount(Remainder);
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER PICKUP] Server_AddNewItem_Implementation 완료 ==="));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 4] AddItemFromManifest — Manifest에서 직접 아이템 추가 (SpawnActor 불필요)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 패턴 근거:
//    Server_CraftItem_Implementation (line 618-648)에서 검증된 동일 패턴:
//    Manifest(Owner) → AddEntry(Item*) → SetTotalStackCount → OnItemAdded.Broadcast
//
// 📌 호출 시점:
//    LoadAndSendInventoryToClient()에서 CDO 경로로 아이템 추가 시
//
// ════════════════════════════════════════════════════════════════════════════════
UInv_InventoryItem* UInv_InventoryComponent::AddItemFromManifest(FInv_ItemManifest& ManifestCopy, int32 StackCount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return nullptr;
	}

	// Manifest() → UInv_InventoryItem 생성
	// Fragment.Manifest() 호출 시 bRandomizeOnManifest=false면 값 유지
	// 호출 후 ManifestCopy의 Fragments는 ClearFragments()로 비워짐
	UInv_InventoryItem* NewItem = ManifestCopy.Manifest(GetOwner());
	if (!IsValid(NewItem))
	{
		return nullptr;
	}

	// FastArray에 추가 (AddEntry(UInv_InventoryItem*) 오버로드 — RepSubObj + MarkItemDirty 처리)
	InventoryList.AddEntry(NewItem);

	// 스택 수량 설정
	NewItem->SetTotalStackCount(StackCount);

	// 리슨서버/스탠드얼론: FastArray 자기 자신 리플리케이션 우회
	if (IsListenServerOrStandalone())
	{
		int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
	}

	return NewItem;
}

UInv_InventoryItem* UInv_InventoryComponent::AddAttachedItemFromManifest(FInv_ItemManifest& ManifestCopy)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return nullptr;
	}

	// Manifest → UInv_InventoryItem 생성
	UInv_InventoryItem* NewItem = ManifestCopy.Manifest(GetOwner());
	if (!IsValid(NewItem))
	{
		return nullptr;
	}

	// FastArray에 추가
	InventoryList.AddEntry(NewItem);

	// 스택 수량 1 (부착물은 스택 안 됨)
	NewItem->SetTotalStackCount(1);

	// ⭐ 부착 상태 플래그 설정 — 그리드에서 숨김
	int32 LastIdx = InventoryList.Entries.Num() - 1;
	InventoryList.Entries[LastIdx].bIsAttachedToWeapon = true;
	InventoryList.Entries[LastIdx].GridIndex = INDEX_NONE;
	InventoryList.MarkItemDirty(InventoryList.Entries[LastIdx]);

	// ⭐ OnItemAdded 브로드캐스트 안 함! (부착된 아이템은 그리드에 표시하지 않음)

	return NewItem;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 9] RestoreFromSaveData — 저장 데이터로 인벤토리 복원 (서버 전용)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 핵심:
//    1) 기존 아이템 전부 제거 (중복 방지)
//    2) SaveData로부터 아이템 재구축 (부착물 + Fragment 역직렬화 포함)
//    3) 장착 상태 복원 (DediServer 전용)
//    4) 멱등성 보장 (bInventoryRestored 플래그)
//
// 📌 이전 위치: SaveGameMode.cpp LoadAndSendInventoryToClient() lines 464-686
//    → InventoryComponent가 자기 상태를 소유하도록 캡슐화
//
// ════════════════════════════════════════════════════════════════════════════════
void UInv_InventoryComponent::RestoreFromSaveData(
	const FInv_PlayerSaveData& SaveData,
	const FInv_ItemTemplateResolver& TemplateResolver)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (bInventoryRestored) return;  // 멱등성 가드

	// ── 1) 기존 아이템 전부 제거 ──
	InventoryList.ClearAllEntries();

	// ── 2) 저장 데이터에서 아이템 복원 ──
	for (const FInv_SavedItemData& ItemData : SaveData.Items)
	{
		if (!ItemData.ItemType.IsValid()) continue;

		// 템플릿 리졸빙 (게임별 DataTable 매핑)
		UInv_ItemComponent* Template = TemplateResolver.Execute(ItemData.ItemType);
		if (!Template) continue;

		// Manifest 복사 (CDO 템플릿은 수정 금지!)
		FInv_ItemManifest ManifestCopy = Template->GetItemManifest();

		// ── 부착물 복원 (CDO 기반 — SpawnActor 없음!) ──
		if (ItemData.Attachments.Num() > 0)
		{
			FInv_AttachmentHostFragment* HostFrag = ManifestCopy.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
			if (HostFrag)
			{
				for (const FInv_SavedAttachmentData& AttSave : ItemData.Attachments)
				{
					UInv_ItemComponent* AttachTemplate = TemplateResolver.Execute(AttSave.AttachmentItemType);
					if (!AttachTemplate) continue;

					// FInv_AttachedItemData 구성
					FInv_AttachedItemData AttachedData;
					AttachedData.SlotIndex = AttSave.SlotIndex;
					AttachedData.AttachmentItemType = AttSave.AttachmentItemType;
					AttachedData.ItemManifestCopy = AttachTemplate->GetItemManifest(); // 값 복사

					// 부착물 Fragment 역직렬화
					if (AttSave.SerializedManifest.Num() > 0)
					{
						if (!AttachedData.ItemManifestCopy.DeserializeAndApplyFragments(AttSave.SerializedManifest))
						{
							UE_LOG(LogTemp, Warning,
								TEXT("[RestoreFromSaveData] ⚠️ 부착물 Fragment 역직렬화 실패 → CDO 기본값 사용 | AttachType=%s"),
								*AttSave.AttachmentItemType.ToString());
						}
					}

					HostFrag->AttachItem(AttSave.SlotIndex, AttachedData);
				}
			}
		}

		// ── 메인 아이템 Fragment 역직렬화 ──
		if (ItemData.SerializedManifest.Num() > 0)
		{
			const bool bDeserializeOk = ManifestCopy.DeserializeAndApplyFragments(ItemData.SerializedManifest);
			if (!bDeserializeOk)
			{
				// [Fix26] 역직렬화 실패 → CDO 기본 Fragment로 계속 진행 (아이템 유실 방지)
				UE_LOG(LogTemp, Warning,
					TEXT("[RestoreFromSaveData] ⚠️ Fragment 역직렬화 실패 → CDO 기본값 사용 | ItemType=%s | SerializedSize=%d"),
					*ItemData.ItemType.ToString(), ItemData.SerializedManifest.Num());
			}
		}

		// ── 디자인타임 전용 값 복원 (CDO 템플릿에서 추출) ──
		{
			const FInv_ItemManifest& CDOManifest = Template->GetItemManifest();

			// SlotPosition 복원
			const FInv_AttachmentHostFragment* CDOHost = CDOManifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
			FInv_AttachmentHostFragment* LoadedHost = ManifestCopy.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
			if (CDOHost && LoadedHost)
			{
				LoadedHost->RestoreDesignTimeSlotPositions(CDOHost->GetSlotDefinitions());
			}

			// PreviewMesh 복원
			const FInv_EquipmentFragment* CDOEquip = CDOManifest.GetFragmentOfType<FInv_EquipmentFragment>();
			FInv_EquipmentFragment* LoadedEquip = ManifestCopy.GetFragmentOfTypeMutable<FInv_EquipmentFragment>();
			if (CDOEquip && LoadedEquip)
			{
				LoadedEquip->RestoreDesignTimePreview(*CDOEquip);
			}
		}

		// ── 인벤토리에 추가 ──
		UInv_InventoryItem* NewItem = AddItemFromManifest(ManifestCopy, ItemData.StackCount);
		if (!NewItem) continue;

		// ── 그리드 위치 복원 ──
		// [Fix30-A] GridPosition이 음수(-1,-1 등)일 때 INDEX_NONE으로 방어
		// 음수 좌표 → (-1)*8+(-1)=-9 같은 잘못된 인덱스 방지
		const int32 Columns = GridColumns > 0 ? GridColumns : 8;
		int32 SavedGridIndex;
		if (ItemData.GridPosition.X < 0 || ItemData.GridPosition.Y < 0)
		{
			SavedGridIndex = INDEX_NONE;
		}
		else
		{
			SavedGridIndex = ItemData.GridPosition.Y * Columns + ItemData.GridPosition.X;
		}
		SetLastEntryGridPosition(SavedGridIndex, ItemData.GridCategory);

		// ── R키 회전 상태 복원 ──
		if (InventoryList.Entries.Num() > 0)
		{
			InventoryList.Entries.Last().bRotated = ItemData.bRotated;
		}

		// ── 부착물 FastArray Entry 생성 + OriginalItem 연결 ──
		if (ItemData.Attachments.Num() > 0 && IsValid(NewItem))
		{
			FInv_AttachmentHostFragment* LoadedHostFrag =
				NewItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();

			if (LoadedHostFrag)
			{
				for (const FInv_SavedAttachmentData& AttSave : ItemData.Attachments)
				{
					UInv_ItemComponent* AttachTemplate = TemplateResolver.Execute(AttSave.AttachmentItemType);
					if (!AttachTemplate) continue;

					FInv_ItemManifest AttachManifest = AttachTemplate->GetItemManifest();

					// Fragment 역직렬화 (저장된 스탯 복원)
					if (AttSave.SerializedManifest.Num() > 0)
					{
						if (!AttachManifest.DeserializeAndApplyFragments(AttSave.SerializedManifest))
						{
							UE_LOG(LogTemp, Warning,
								TEXT("[RestoreFromSaveData] ⚠️ 부착물(Entry) Fragment 역직렬화 실패 → CDO 기본값 사용 | AttachType=%s"),
								*AttSave.AttachmentItemType.ToString());
						}
					}

					// FastArray에 Entry 추가 (그리드 숨김 상태)
					UInv_InventoryItem* AttachItem = AddAttachedItemFromManifest(AttachManifest);
					if (!AttachItem) continue;

					// HostFrag의 AttachedItemData에 OriginalItem 포인터 연결
					LoadedHostFrag->SetOriginalItemForSlot(AttSave.SlotIndex, AttachItem);
				}
			}
		}
	}

	// ── 3) 장착 상태 복원 (DediServer only) ──
	if (GetOwner()->GetNetMode() == NM_DedicatedServer)
	{
		TSet<UInv_InventoryItem*> ProcessedEquipItems;
		for (const FInv_SavedItemData& ItemData : SaveData.Items)
		{
			if (!ItemData.bEquipped || ItemData.WeaponSlotIndex < 0) continue;

			UInv_InventoryItem* FoundItem = FindItemByTypeExcluding(
				ItemData.ItemType, ProcessedEquipItems);
			if (FoundItem)
			{
				OnItemEquipped.Broadcast(FoundItem, ItemData.WeaponSlotIndex);
				ProcessedEquipItems.Add(FoundItem);
			}
		}

		// Fix 7: 장착된 아이템의 서버 FastArray Entry GridIndex 클리어 — Phase 5 저장 시 좌표 중복 방지
		// Fix 13: bIsEquipped 플래그 설정 — PostReplicatedAdd에서 그리드 배치 스킵
		// [Fix14] WeaponSlotIndex도 Entry에 기록 — 저장/전송 시 슬롯 정보 보존
		// [Fix40] Item 포인터 → SaveData 매핑 구축 (같은 ItemType 다중 장착 지원)
		TMap<UInv_InventoryItem*, int32> ItemToWeaponSlotMap;
		{
			TSet<int32> UsedSaveDataIndices;
			for (UInv_InventoryItem* EqItem : ProcessedEquipItems)
			{
				const FGameplayTag& EqType = EqItem->GetItemManifest().GetItemType();
				for (int32 s = 0; s < SaveData.Items.Num(); s++)
				{
					if (UsedSaveDataIndices.Contains(s)) continue;
					const FInv_SavedItemData& SD = SaveData.Items[s];
					if (SD.bEquipped && SD.ItemType == EqType)
					{
						ItemToWeaponSlotMap.Add(EqItem, SD.WeaponSlotIndex);
						UsedSaveDataIndices.Add(s);
						break;
					}
				}
			}
		}

		for (UInv_InventoryItem* EquippedItem : ProcessedEquipItems)
		{
			for (int32 i = 0; i < InventoryList.Entries.Num(); i++)
			{
				if (InventoryList.Entries[i].Item == EquippedItem)
				{
					InventoryList.Entries[i].GridIndex = INDEX_NONE;
					InventoryList.Entries[i].GridCategory = 0;
					InventoryList.Entries[i].bIsEquipped = true;

					// [Fix40] 미리 구축한 매핑에서 정확한 WeaponSlotIndex 사용
					const int32* MappedSlot = ItemToWeaponSlotMap.Find(EquippedItem);
					InventoryList.Entries[i].WeaponSlotIndex = MappedSlot ? *MappedSlot : 0;

					// MarkItemDirty 호출 금지! 리플리케이션 트리거 시 PostReplicatedChange → AddItem으로 아이템이 Grid에 다시 나타남
					// bIsEquipped는 이미 dirty 상태인 Entry에 포함되어 리플리케이션됨 (같은 프레임)
					UE_LOG(LogTemp, Warning, TEXT("[Fix14-Restore] 장착 아이템 bIsEquipped=true, WeaponSlotIndex=%d: %s (Entry[%d])"),
						InventoryList.Entries[i].WeaponSlotIndex,
						*EquippedItem->GetItemManifest().GetItemType().ToString(), i);
					break;
				}
			}
		}
	}

	bInventoryRestored = true;
}

bool UInv_InventoryComponent::Server_AddStacksToItem_Validate(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder)
{
	return StackCount >= 0 && Remainder >= 0;
}

void UInv_InventoryComponent::Server_AddStacksToItem_Implementation(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder) // 서버에서 아이템 스택 개수를 세어주는 역할.
{
	const FGameplayTag& ItemType = IsValid(ItemComponent) ? ItemComponent->GetItemManifest().GetItemType() : FGameplayTag::EmptyTag; // 아이템 유형 가져오기
	UInv_InventoryItem* Item = InventoryList.FindFirstItemByType(ItemType); // 동일한 유형의 아이템 찾기
	if (!IsValid(Item)) return;

	// ⭐ [MaxStackSize 검증] 초과분을 새 슬롯에 추가하도록 개선
	const int32 CurrentStack = Item->GetTotalStackCount();
	int32 MaxStackSize = 999; // 기본값
	
	// MaxStackSize 가져오기
	if (const FInv_StackableFragment* StackableFragment = Item->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>())
	{
		MaxStackSize = StackableFragment->GetMaxStackSize();
	}
	
	const int32 RoomInCurrentStack = MaxStackSize - CurrentStack; // 현재 스택에 추가 가능한 양
	const int32 AmountToAddToCurrentStack = FMath::Min(StackCount, RoomInCurrentStack); // 현재 스택에 실제로 추가할 양
	const int32 Overflow = StackCount - AmountToAddToCurrentStack; // 초과분 (새 슬롯으로 가야 함)
	
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[Server_AddStacksToItem] 현재: %d, 추가요청: %d, Max: %d, 추가가능: %d, 초과분: %d"),
		CurrentStack, StackCount, MaxStackSize, AmountToAddToCurrentStack, Overflow);
#endif

	// 1. 현재 스택에 추가 (MaxStackSize까지만)
	if (AmountToAddToCurrentStack > 0)
	{
		Item->SetTotalStackCount(CurrentStack + AmountToAddToCurrentStack);

		// ⚠️ MarkItemDirty — 데디서버 클라이언트에서 PostReplicatedChange가 트리거되어 UI 갱신
		const int32 StackEntryIdx = FindEntryIndexForItem(Item);
		if (InventoryList.Entries.IsValidIndex(StackEntryIdx))
		{
			InventoryList.MarkItemDirty(InventoryList.Entries[StackEntryIdx]);
		}

		// ════════════════════════════════════════════════════════════════
		// 🔧 리슨서버 호환 수정
		// ════════════════════════════════════════════════════════════════
		//
		// 📌 문제:
		//    기존 스택에 수량을 추가할 때 SetTotalStackCount만 호출
		//    리슨서버 호스트는 FastArray 콜백이 안 와서 UI 갱신 안 됨
		//
		// 📌 해결:
		//    리슨서버/스탠드얼론에서 OnItemAdded Broadcast로 UI에 알림
		//
		// 📌 데디서버 영향:
		//    없음 — FastArray 리플리케이션이 자동 처리
		//
		// ════════════════════════════════════════════════════════════════
		if (IsListenServerOrStandalone())
		{
			int32 EntryIndex = INDEX_NONE;
			for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
			{
				if (InventoryList.Entries[i].Item == Item)
				{
					EntryIndex = i;
					break;
				}
			}
			OnItemAdded.Broadcast(Item, EntryIndex);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("🔧 리슨서버 호스트: OnItemAdded 브로드캐스트 (기존 스택에 %d개 추가, EntryIndex=%d)"),
				AmountToAddToCurrentStack, EntryIndex);
#endif
		}

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[Server_AddStacksToItem] ✅ 기존 스택에 %d개 추가 → 총 %d개"),
			AmountToAddToCurrentStack, Item->GetTotalStackCount());
#endif
	}

	// 2. 초과분이 있으면 새 슬롯에 추가
	if (Overflow > 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[Server_AddStacksToItem] ⚠️ 초과분 %d개 → 새 슬롯에 추가!"), Overflow);
#endif

		// 새 아이템 생성 (기존 ItemComponent의 Manifest 사용)
		UInv_InventoryItem* NewItem = InventoryList.AddEntry(ItemComponent);
		if (IsValid(NewItem))
		{
			NewItem->SetTotalStackCount(Overflow);

			// ── 리슨서버/스탠드얼론: 초과분 새 Entry → FastArray 콜백 우회 ──
			if (IsListenServerOrStandalone())
			{
				int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
				OnItemAdded.Broadcast(NewItem, NewEntryIndex);
			}

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[Server_AddStacksToItem] ✅ 새 슬롯에 %d개 추가 완료!"), Overflow);
#endif
		}
#if INV_DEBUG_INVENTORY
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Server_AddStacksToItem] ❌ 새 슬롯 생성 실패! %d개 손실!"), Overflow);
		}
#endif
	}

	//0가 되면 아이템 파괴하는 부분
	if (Remainder == 0)
	{
		ItemComponent->PickedUp();
	}
	else if (FInv_StackableFragment* StackableFragment = ItemComponent->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>())
	{
		StackableFragment->SetStackCount(Remainder);
	}
}

bool UInv_InventoryComponent::Server_DropItem_Validate(UInv_InventoryItem* Item, int32 StackCount)
{
	// Validate failure disconnects the client. Runtime-sanitize malformed values
	// in _Implementation so UI timing issues do not force a connection loss.
	return !IsValid(Item) || IsInventoryItemOwnedByThisComponent(this, Item);
}

//아이템 드롭 상호작용을 누른 뒤 서버에서 어떻게 처리를 할지.
void UInv_InventoryComponent::Server_DropItem_Implementation(UInv_InventoryItem* Item, int32 StackCount)
{
	if (!IsValid(Item))
	{
		UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Server_DropItem ignored: invalid item"));
		return;
	}

	const int32 TrackedEntryIndex = FindOwnedInventoryEntryIndex(this, Item);
	if (TrackedEntryIndex == INDEX_NONE)
	{
		UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Server_DropItem ignored: item is not tracked by this inventory"));
		return;
	}

	const int32 TotalStackCount = FMath::Max(1, Item->GetTotalStackCount());
	int32 EffectiveStackCount = StackCount;
	if (EffectiveStackCount <= 0)
	{
		EffectiveStackCount = Item->IsStackable() ? TotalStackCount : 1;
		UE_LOG(LogInventory, Warning,
			TEXT("[InventoryComponent] Server_DropItem corrected invalid stack count. Item=%s Requested=%d Effective=%d Total=%d"),
			*Item->GetItemManifest().GetItemType().ToString(), StackCount, EffectiveStackCount, TotalStackCount);
	}
	EffectiveStackCount = FMath::Clamp(EffectiveStackCount, 1, TotalStackCount);

	const int32 NewStackCount = TotalStackCount - EffectiveStackCount;
	if (NewStackCount <= 0)
	{
		InventoryList.RemoveEntry(Item);
	}
	else
	{
		Item->SetTotalStackCount(NewStackCount);
		if (InventoryList.Entries.IsValidIndex(TrackedEntryIndex))
		{
			InventoryList.MarkItemDirty(InventoryList.Entries[TrackedEntryIndex]);
		}
	}

	SpawnDroppedItem(Item, EffectiveStackCount);
}

//무언가를 떨어뜨렸기 때문에 아이템도 생성 및 이벤트 효과들 보이게 하는 부분의 코드들
void UInv_InventoryComponent::SpawnDroppedItem(UInv_InventoryItem* Item, int32 StackCount)
{
	if (!IsValid(Item)) return;
	if (!OwningController.IsValid()) return;

	// TODO : 아이템을 버릴 시 월드에 소환하게 하는 부분 만들기
	const APawn* OwningPawn = OwningController->GetPawn();
	if (!IsValid(OwningPawn)) return;
	FVector RotatedForward = OwningPawn->GetActorForwardVector();
	RotatedForward = RotatedForward.RotateAngleAxis(FMath::FRandRange(DropSpawnAngleMin, DropSpawnAngleMax), FVector::UpVector); // 아이템이 빙글빙글 도는 부분
	FVector SpawnLocation = OwningPawn->GetActorLocation() + RotatedForward * FMath::FRandRange(DropSpawnDistanceMin, DropSpawnDistanceMax); // 아이템이 떨어지는 위치 설정
	SpawnLocation.Z -= RelativeSpawnElevation; // 스폰 위치를 아래로 밀어내는 부분
	const FRotator SpawnRotation = FRotator::ZeroRotator;
	
	// TODO : 아이템 매니패스트가 픽업 액터를 생성하도록 만드는 것 
	FInv_ItemManifest& ItemManifest = Item->GetItemManifestMutable(); // 아이템 매니페스트 가져오기
	if (FInv_StackableFragment* StackableFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_StackableFragment>()) // 스택 가능 프래그먼트 가져오기
	{
		StackableFragment->SetStackCount(StackCount); // 스택 수 설정
	}
	ItemManifest.SpawnPickupActor(this,SpawnLocation, SpawnRotation); // 아이템 매니페스트로 픽업 액터 생성
}

bool UInv_InventoryComponent::Server_ConsumeItem_Validate(UInv_InventoryItem* Item)
{
	return true;
}

// 아이템 소비 상호작용을 누른 뒤 서버에서 어떻게 처리를 할지.
void UInv_InventoryComponent::Server_ConsumeItem_Implementation(UInv_InventoryItem* Item)
{
	if (!IsValid(Item)) return;

	const int32 NewStackCount = Item->GetTotalStackCount() - 1;

	// ── Entry Index를 미리 찾아두기 (RemoveEntry 전에!) ──
	int32 ItemEntryIndex = INDEX_NONE;
	if (IsListenServerOrStandalone())
	{
		for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
		{
			if (InventoryList.Entries[i].Item == Item)
			{
				ItemEntryIndex = i;
				break;
			}
		}
	}

	if (NewStackCount <= 0) // 스택 카운트가 0일시.
	{
		InventoryList.RemoveEntry(Item);

		// ════════════════════════════════════════════════════════════════
		// 🔧 리슨서버 호환 수정
		// ════════════════════════════════════════════════════════════════
		//
		// 📌 문제:
		//    리슨서버 호스트는 자기 자신에게 FastArray 리플리케이션이 안 됨
		//    → PostReplicatedRemove 콜백이 불리지 않음
		//    → UI(Grid)에서 아이템이 사라지지 않음
		//
		// 📌 해결:
		//    리슨서버/스탠드얼론에서 OnItemRemoved를 직접 Broadcast
		//
		// 📌 데디서버 영향:
		//    없음 — FastArray가 자동으로 PostReplicatedRemove 호출
		//
		// ════════════════════════════════════════════════════════════════
		if (IsListenServerOrStandalone())
		{
			OnItemRemoved.Broadcast(Item, ItemEntryIndex);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("🔧 리슨서버 호스트: OnItemRemoved 브로드캐스트 (소비로 아이템 제거)"));
#endif
		}
	}
	else
	{
		Item->SetTotalStackCount(NewStackCount);

		// ── 리슨서버 호스트: 스택 수량 변경 UI 갱신 ──
		if (IsListenServerOrStandalone())
		{
			OnItemAdded.Broadcast(Item, ItemEntryIndex);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("🔧 리슨서버 호스트: OnItemAdded 브로드캐스트 (소비로 스택 수량 %d)"), NewStackCount);
#endif
		}
	}

	// 소비 프래그먼트를 가져와서 소비 함수 호출 (소비할 때 실제로 일어나는 일을 구현하자!)
	if (FInv_ConsumableFragment* ConsumableFragment = Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_ConsumableFragment>())
	{
		ConsumableFragment->OnConsume(OwningController.Get());
	}
}

bool UInv_InventoryComponent::Server_CraftItem_Validate(TSubclassOf<AActor> ItemActorClass)
{
	return ItemActorClass != nullptr;
}

// 크래프팅: 서버에서 아이템 생성 및 인벤토리 추가 (ItemManifest 직접 사용!)
void UInv_InventoryComponent::Server_CraftItem_Implementation(TSubclassOf<AActor> ItemActorClass)
{
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT] Server_CraftItem_Implementation 시작 ==="));
#endif

	// 서버 권한 체크
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 권한 없음! 서버에서만 실행 가능!"));
#endif
		return;
	}

	// ItemActorClass 유효성 체크
	if (!ItemActorClass)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemActorClass가 nullptr입니다!"));
#endif
		return;
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작할 아이템 Blueprint: %s"), *ItemActorClass->GetName());
#endif

	// ════════════════════════════════════════════════════════════════
	// 📌 [최적화] CDO/SCS 기반 — SpawnActor 제거
	// ════════════════════════════════════════════════════════════════
	// 이전: SpawnActorDeferred → FinishSpawning → Destroy (매번 액터 생성/파괴)
	// 이후: FindItemComponentTemplate(CDO) → Manifest 복사 (액터 생성 없음)
	// ════════════════════════════════════════════════════════════════

	UInv_ItemComponent* DefaultItemComp = AInv_SaveGameMode::FindItemComponentTemplate(ItemActorClass);

	if (!IsValid(DefaultItemComp))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ❌ CDO/SCS에서 ItemComponent를 찾을 수 없습니다!"));
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] Blueprint: %s"), *ItemActorClass->GetName());
#endif
		return;
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ CDO에서 ItemComponent 찾음! (SpawnActor 없음)"));
#endif

	// ItemManifest 복사 (CDO 템플릿은 수정 금지!)
	FInv_ItemManifest ItemManifest = DefaultItemComp->GetItemManifest();

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ItemManifest 복사 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   - 아이템 타입: %s"), *ItemManifest.GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   - 아이템 카테고리: %d"), (int32)ItemManifest.GetItemCategory());
#endif

	// ⭐ 공간 체크 (InventoryList 기반 - UI 없이 작동!)
	bool bHasRoom = HasRoomInInventoryList(ItemManifest);

	if (!bHasRoom)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ❌ 인벤토리에 공간이 없습니다!"));
#endif
		NoRoomInInventory.Broadcast();
		return;
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 인벤토리에 공간 있음!"));
#endif

	// InventoryList에 직접 추가 (PickUp 방식과 동일!)
	UInv_InventoryItem* NewItem = ItemManifest.Manifest(GetOwner());
	if (!IsValid(NewItem))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemManifest.Manifest() 실패!"));
#endif
		return;
	}

	// FastArray에 추가
	InventoryList.AddEntry(NewItem);

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] InventoryList.AddEntry 완료!"));
#endif

	// ── 리슨서버/스탠드얼론: FastArray 콜백 우회 ──
	if (IsListenServerOrStandalone())
	{
		int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ListenServer/Standalone - OnItemAdded 브로드캐스트 (EntryIndex=%d)"), NewEntryIndex);
#endif
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT] 크래프팅 완료! (CDO 기반 - SpawnActor 없음!) ==="));
#endif
}

// ⭐ [SERVER-ONLY] 서버의 InventoryList를 기준으로 실제 재료 보유 여부를 확인합니다.
bool UInv_InventoryComponent::HasRequiredMaterialsOnServer(const FGameplayTag& MaterialTag, int32 RequiredAmount) const
{
	// 유효하지 않은 태그나 수량 0은 항상 '재료 있음'으로 간주
	if (!MaterialTag.IsValid() || RequiredAmount <= 0)
	{
		return true;
	}

	// GetTotalMaterialCount는 이미 서버의 InventoryList를 사용하므로 안전합니다.
	const int32 CurrentAmount = GetTotalMaterialCount(MaterialTag);
	
	if (CurrentAmount < RequiredAmount)
	{
#if INV_DEBUG_INVENTORY
		// 이 로그는 서버에만 기록됩니다.
		UE_LOG(LogTemp, Warning, TEXT("[CHEAT DETECTION?] Server check failed for material %s. Required: %d, Has: %d"),
			*MaterialTag.ToString(), RequiredAmount, CurrentAmount);
#endif
		return false;
	}

	return true;
}

bool UInv_InventoryComponent::Server_CraftItemWithMaterials_Validate(
	TSubclassOf<AActor> ItemActorClass,
	const FGameplayTag& MaterialTag1, int32 Amount1,
	const FGameplayTag& MaterialTag2, int32 Amount2,
	const FGameplayTag& MaterialTag3, int32 Amount3,
	int32 CraftedAmount)
{
	return ItemActorClass != nullptr && Amount1 >= 0 && Amount2 >= 0 && Amount3 >= 0 && CraftedAmount > 0;
}

// ⭐ 크래프팅 통합 RPC: [안전성 강화] 서버 측 재료 검증 추가
void UInv_InventoryComponent::Server_CraftItemWithMaterials_Implementation(
	TSubclassOf<AActor> ItemActorClass,
	const FGameplayTag& MaterialTag1, int32 Amount1,
	const FGameplayTag& MaterialTag2, int32 Amount2,
	const FGameplayTag& MaterialTag3, int32 Amount3,
	int32 CraftedAmount)  // ⭐ 제작 개수 파라미터 추가!
{
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT WITH MATERIALS] 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작 개수: %d"), CraftedAmount);
#endif

	// 서버 권한 체크
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 권한 없음!"));
#endif
		return;
	}

	if (!ItemActorClass)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemActorClass가 nullptr!"));
#endif
		return;
	}

	// ========== ⭐ 1단계: [안전성 강화] 서버 측 재료 검증 ==========
	// 클라이언트의 요청을 신뢰하지 않고, 서버가 직접 재료 보유 여부를 확인합니다.
	if (!HasRequiredMaterialsOnServer(MaterialTag1, Amount1) ||
		!HasRequiredMaterialsOnServer(MaterialTag2, Amount2) ||
		!HasRequiredMaterialsOnServer(MaterialTag3, Amount3))
	{
#if INV_DEBUG_INVENTORY
		// 재료가 부족하므로 제작을 중단합니다. 클라이언트에는 별도 알림 없이, 서버 로그만 기록합니다.
		// 이는 비정상적인 요청(치트 등)일 가능성이 높습니다.
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 재료 부족! 클라이언트 요청 거부. (잠재적 치트 시도)"));
#endif
		return;
	}
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Log, TEXT("[SERVER CRAFT] ✅ 서버 측 재료 검증 통과."));
#endif


	// ════════════════════════════════════════════════════════════════
	// 📌 [최적화] 2단계: CDO/SCS 기반 — SpawnActor 제거
	// ════════════════════════════════════════════════════════════════
	UInv_ItemComponent* DefaultItemComp = AInv_SaveGameMode::FindItemComponentTemplate(ItemActorClass);
	if (!IsValid(DefaultItemComp))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ❌ CDO/SCS에서 ItemComponent를 찾을 수 없음!"));
#endif
		return;
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ CDO에서 ItemComponent 찾음! (SpawnActor 없음)"));
#endif

	FInv_ItemManifest ItemManifest = DefaultItemComp->GetItemManifest();
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작할 아이템: %s (카테고리: %d)"),
		*ItemManifest.GetItemType().ToString(), (int32)ItemManifest.GetItemCategory());
#endif

	// ========== 3단계: 공간 체크 ==========
	bool bHasRoom = HasRoomInInventoryList(ItemManifest);

	if (!bHasRoom)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ❌ 인벤토리에 공간이 없습니다!"));
#endif
		NoRoomInInventory.Broadcast(); // 클라이언트에 공간 없음 알림
		return; // 재료 차감 없이 중단!
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 인벤토리에 공간 있음!"));
#endif


	// ========== 4단계: 재료 차감 (모든 검증 통과 후!) ==========
	// Server_ConsumeMaterialsMultiStack은 서버에서만 호출 가능한 RPC이므로,
	// _Implementation을 직접 호출하여 서버 내에서 함수를 실행합니다.
	if (MaterialTag1.IsValid() && Amount1 > 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 재료1 차감: %s x %d"), *MaterialTag1.ToString(), Amount1);
#endif
		Server_ConsumeMaterialsMultiStack_Implementation(MaterialTag1, Amount1);
	}

	if (MaterialTag2.IsValid() && Amount2 > 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 재료2 차감: %s x %d"), *MaterialTag2.ToString(), Amount2);
#endif
		Server_ConsumeMaterialsMultiStack_Implementation(MaterialTag2, Amount2);
	}

	if (MaterialTag3.IsValid() && Amount3 > 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 재료3 차감: %s x %d"), *MaterialTag3.ToString(), Amount3);
#endif
		Server_ConsumeMaterialsMultiStack_Implementation(MaterialTag3, Amount3);
	}

	// ========== 5단계: 아이템 생성 (⭐ 여유 공간 있는 스택 검색 로직!) ==========
	FGameplayTag ItemType = ItemManifest.GetItemType();

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 🔍 여유 공간 있는 스택 검색 시작: ItemType=%s"), *ItemType.ToString());
#endif

	// ⭐ 여유 공간 있는 스택 찾기 (가득 찬 스택은 제외!)
	UInv_InventoryItem* ExistingItem = nullptr;

	for (const FInv_InventoryEntry& Entry : InventoryList.Entries)
	{
		if (!IsValid(Entry.Item))
			continue;

		if (Entry.Item->GetItemManifest().GetItemType() != ItemType)
			continue;

		// 같은 아이템 타입 발견!
		int32 CurrentCount = Entry.Item->GetTotalStackCount();

		if (!Entry.Item->IsStackable())
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]     Entry 발견 (Non-stackable) - 건너뜀"));
#endif
			continue;  // Non-stackable은 새 Entry 생성해야 함
		}

		const FInv_StackableFragment* Fragment = Entry.Item->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>();
		int32 MaxStackSize = Fragment ? Fragment->GetMaxStackSize() : 999;

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]     Entry 발견: %d / %d"),
			CurrentCount, MaxStackSize);
#endif

		// ⭐ 여유 공간 있는 스택 발견!
		if (CurrentCount < MaxStackSize)
		{
			ExistingItem = Entry.Item;
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ 여유 공간 있는 스택 발견! %d / %d (Item포인터: %p)"),
				CurrentCount, MaxStackSize, ExistingItem);
#endif
			break;  // 첫 번째 여유 공간 발견 시 중단
		}
#if INV_DEBUG_INVENTORY
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]     스택 가득 참 (%d/%d) - 다음 Entry 검색"),
				CurrentCount, MaxStackSize);
		}
#endif
	}

	// 여유 공간 있는 스택 발견 시
	if (IsValid(ExistingItem))
	{
		int32 CurrentCount = ExistingItem->GetTotalStackCount();
		const FInv_StackableFragment* StackableFragment = ExistingItem->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>();
		int32 MaxStackSize = StackableFragment ? StackableFragment->GetMaxStackSize() : 999;

		// ⭐ MaxStackSize 초과 시 Overflow 계산!
		int32 SpaceLeft = MaxStackSize - CurrentCount;  // 남은 공간
		int32 ToAdd = FMath::Min(CraftedAmount, SpaceLeft);  // 실제 추가할 개수
		int32 Overflow = CraftedAmount - ToAdd;  // 넘친 개수

		int32 NewCount = CurrentCount + ToAdd;

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ⭐ 기존 스택에 추가! %d → %d (추가량: %d/%d, Overflow: %d)"),
			CurrentCount, NewCount, ToAdd, CraftedAmount, Overflow);
#endif

		ExistingItem->SetTotalStackCount(NewCount);

		// Fragment도 동기화
		FInv_StackableFragment* MutableFragment = ExistingItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>();
		if (MutableFragment)
		{
			MutableFragment->SetStackCount(NewCount);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ StackableFragment도 업데이트: %d"), NewCount);
#endif
		}

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   📊 최종 확인: TotalStackCount=%d, Fragment.StackCount=%d"),
			NewCount, MutableFragment ? MutableFragment->GetStackCount() : -1);
#endif

		// 리플리케이션 활성화
		for (auto& Entry : InventoryList.Entries)
		{
			if (Entry.Item == ExistingItem)
			{
				InventoryList.MarkItemDirty(Entry);
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ MarkItemDirty 호출! 리플리케이션 활성화"));
#endif
				break;
			}
		}

		// ⭐⭐⭐ Overflow 처리: 넘친 개수만큼 새 Entry 생성!
		if (Overflow > 0)
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ⚠️ Overflow 발생! %d개가 MaxStackSize 초과 → 새 Entry 생성"), Overflow);
#endif

			// 새 Entry 생성을 위해 ItemManifest 다시 Manifest
			UInv_InventoryItem* OverflowItem = ItemManifest.Manifest(GetOwner());

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 🆕 Overflow Entry 생성 완료!"));
			UE_LOG(LogTemp, Warning, TEXT("    Item포인터: %p"), OverflowItem);
			UE_LOG(LogTemp, Warning, TEXT("    ItemType: %s"), *ItemType.ToString());
			UE_LOG(LogTemp, Warning, TEXT("    Overflow 개수: %d"), Overflow);
#endif

			// Overflow 개수로 초기화
			OverflowItem->SetTotalStackCount(Overflow);

			// Fragment도 동기화
			FInv_StackableFragment* OverflowMutableFragment = OverflowItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>();
			if (OverflowMutableFragment)
			{
				OverflowMutableFragment->SetStackCount(Overflow);
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("    ✅ Overflow StackableFragment도 업데이트: %d"), Overflow);
#endif
			}

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("    최종 TotalStackCount: %d"), Overflow);
#endif

			// InventoryList에 추가
			InventoryList.AddEntry(OverflowItem);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ Overflow Entry 추가 완료!"));
#endif

			// ── 리슨서버/스탠드얼론: Overflow 새 Entry → FastArray 콜백 우회 ──
			if (IsListenServerOrStandalone())
			{
				int32 OverflowEntryIndex = InventoryList.Entries.Num() - 1;
				OnItemAdded.Broadcast(OverflowItem, OverflowEntryIndex);
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ Overflow OnItemAdded 브로드캐스트 완료! (EntryIndex=%d)"), OverflowEntryIndex);
#endif
			}
		}

		// ── 리슨서버/스탠드얼론: 기존 스택 수량 변경 → MarkDirty 콜백 우회 ──
		if (IsListenServerOrStandalone())
		{
			// Entry Index 찾기
			int32 EntryIndex = INDEX_NONE;
			for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
			{
				if (InventoryList.Entries[i].Item == ExistingItem)
				{
					EntryIndex = i;
					break;
				}
			}

			OnItemAdded.Broadcast(ExistingItem, EntryIndex);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ OnItemAdded 브로드캐스트 완료! (EntryIndex=%d)"), EntryIndex);
#endif
		}

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 제작 완료! 기존 스택에 추가됨 (Overflow: %s)"),
			Overflow > 0 ? TEXT("새 Entry 생성됨") : TEXT("없음"));
		UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT WITH MATERIALS] 완료 ==="));
#endif
		return; // ⭐ 여기서 리턴! 새 Entry 생성하지 않음!
	}
#if INV_DEBUG_INVENTORY
	else
	{
		// ⭐ 여유 공간 없음 → 새 Entry 생성
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ⚠️ 모든 스택 가득 참 또는 기존 스택 없음, 새 Entry 생성"));
	}
#endif

	// ========== 기존 스택이 없거나 가득 찬 경우: 새 Entry 생성 ==========
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 🆕 새 Entry 생성: ItemType=%s"), *ItemType.ToString());

	// ⭐ Manifest 전 ItemManifest 상태 확인
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 📋 ItemManifest 상태 (Manifest 전):"));
	const FInv_StackableFragment* PreManifestFragment = ItemManifest.GetFragmentOfType<FInv_StackableFragment>();
	if (PreManifestFragment)
	{
		UE_LOG(LogTemp, Warning, TEXT("    Fragment->GetStackCount(): %d"), PreManifestFragment->GetStackCount());
		UE_LOG(LogTemp, Warning, TEXT("    Fragment->GetMaxStackSize(): %d"), PreManifestFragment->GetMaxStackSize());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("    StackableFragment 없음 (Non-stackable?)"));
	}
#endif

	UInv_InventoryItem* NewItem = ItemManifest.Manifest(GetOwner());
	if (!IsValid(NewItem))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemManifest.Manifest() 실패!"));
#endif
		// 여기서 재료를 롤백하는 로직을 추가할 수 있으나, 현재는 생략합니다.
		return;
	}

#if INV_DEBUG_INVENTORY
	// ⭐ 새 Item 생성 후 상태 확인
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 🆕 새 Entry 생성 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("    Item포인터: %p"), NewItem);
	UE_LOG(LogTemp, Warning, TEXT("    ItemType: %s"), *NewItem->GetItemManifest().GetItemType().ToString());
#endif

	int32 InitialCount = NewItem->GetTotalStackCount();
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("    TotalStackCount (초기값): %d"), InitialCount);
#endif

	const FInv_StackableFragment* NewItemFragment = NewItem->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>();
	if (NewItemFragment)
	{
		int32 FragmentCount = NewItemFragment->GetStackCount();
		int32 MaxStackSize = NewItemFragment->GetMaxStackSize();

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("    StackableFragment 발견!"));
		UE_LOG(LogTemp, Warning, TEXT("      Fragment->GetStackCount(): %d"), FragmentCount);
		UE_LOG(LogTemp, Warning, TEXT("      Fragment->GetMaxStackSize(): %d"), MaxStackSize);

		if (InitialCount != FragmentCount)
		{
			UE_LOG(LogTemp, Error, TEXT("    ❌ 불일치! TotalStackCount(%d) != Fragment.StackCount(%d)"),
				InitialCount, FragmentCount);
		}
#endif

		// ⭐ 스택을 CraftedAmount로 초기화!
		if (InitialCount == 0 || InitialCount != CraftedAmount)
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("    ⭐ TotalStackCount를 CraftedAmount(%d)로 초기화!"), CraftedAmount);
#endif
			NewItem->SetTotalStackCount(CraftedAmount);

			// Fragment도 업데이트
			FInv_ItemManifest& NewItemManifest = NewItem->GetItemManifestMutable();
			if (FInv_StackableFragment* MutableFrag = NewItemManifest.GetFragmentOfTypeMutable<FInv_StackableFragment>())
			{
				MutableFrag->SetStackCount(CraftedAmount);
			}

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("    최종 TotalStackCount: %d"), NewItem->GetTotalStackCount());
#endif
		}
	}
#if INV_DEBUG_INVENTORY
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("    ❌ StackableFragment가 없습니다 (Non-stackable 아이템)"));
	}
#endif

	InventoryList.AddEntry(NewItem);
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 제작 완료! 새 Entry 추가됨"));
#endif

	// ── 리슨서버/스탠드얼론: 새 Entry 추가 → FastArray 콜백 우회 ──
	if (IsListenServerOrStandalone())
	{
		int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT WITH MATERIALS] 완료 ==="));
#endif
}

bool UInv_InventoryComponent::Server_ConsumeMaterials_Validate(const FGameplayTag& MaterialTag, int32 Amount)
{
	return MaterialTag.IsValid() && Amount > 0;
}

// 재료 소비 (Building 시스템용) - Server_DropItem과 동일한 로직 사용
void UInv_InventoryComponent::Server_ConsumeMaterials_Implementation(const FGameplayTag& MaterialTag, int32 Amount)
{
	if (!MaterialTag.IsValid() || Amount <= 0) return;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== Server_ConsumeMaterials 호출됨 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);
#endif

	// 재료 찾기
	UInv_InventoryItem* Item = InventoryList.FindFirstItemByType(MaterialTag);
	if (!IsValid(Item))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("Server_ConsumeMaterials: Item not found! (%s)"), *MaterialTag.ToString());
#endif
		return;
	}

	// GetTotalStackCount() 사용 (Server_DropItem과 동일!)
	int32 CurrentCount = Item->GetTotalStackCount();
	int32 NewCount = CurrentCount - Amount;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("Server: 재료 차감 (%d → %d)"), CurrentCount, NewCount);
#endif

	// ⭐ Entry Index를 미리 찾아두기 (RemoveEntry 전에!)
	int32 ItemEntryIndex = INDEX_NONE;
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == Item)
		{
			ItemEntryIndex = i;
			break;
		}
	}

	if (NewCount <= 0)
	{
		// 재료를 다 썼으면 인벤토리에서 제거 (Server_DropItem과 동일!)
		InventoryList.RemoveEntry(Item);
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("Server: 재료 전부 소비! 인벤토리에서 제거됨: %s"), *MaterialTag.ToString());
#endif
	}
	else
	{
		// SetTotalStackCount() 사용 (Server_DropItem과 동일!)
		Item->SetTotalStackCount(NewCount);

		// StackableFragment도 업데이트 (완전한 동기화!)
		FInv_ItemManifest& ItemManifest = Item->GetItemManifestMutable();
		if (FInv_StackableFragment* StackableFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_StackableFragment>())
		{
			StackableFragment->SetStackCount(NewCount);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("Server: StackableFragment도 업데이트됨!"));
#endif
		}

		// FastArray에 변경 사항 알림 (리플리케이션 활성화!)
		for (auto& Entry : InventoryList.Entries)
		{
			if (Entry.Item == Item)
			{
				InventoryList.MarkItemDirty(Entry);
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("Server: MarkItemDirty 호출 완료! 리플리케이션 활성화!"));
#endif
				break;
			}
		}

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("Server: 재료 차감 완료: %s (%d → %d)"), *MaterialTag.ToString(), CurrentCount, NewCount);
#endif
	}

	// ⚠️ 리슨서버/스탠드얼론에서만 직접 브로드캐스트 — 데디서버 클라는 FastArray 콜백이 처리
	if (IsListenServerOrStandalone())
	{
		if (NewCount <= 0)
		{
			OnItemRemoved.Broadcast(Item, ItemEntryIndex);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("OnItemRemoved 브로드캐스트 완료 (EntryIndex=%d)"), ItemEntryIndex);
#endif
		}
		else
		{
			FInv_SlotAvailabilityResult Result;
			Result.Item = Item;
			Result.bStackable = true;
			Result.TotalRoomToFill = NewCount;
			Result.EntryIndex = ItemEntryIndex;
			OnStackChange.Broadcast(Result);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("OnStackChange 브로드캐스트 완료 (NewCount: %d)"), NewCount);
#endif
		}
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== Server_ConsumeMaterials 완료 ==="));
#endif
}

// 같은 타입의 모든 스택 개수 합산 (Building UI용)
int32 UInv_InventoryComponent::GetTotalMaterialCount(const FGameplayTag& MaterialTag) const
{
	if (!MaterialTag.IsValid()) return 0;

	// ⭐ InventoryList에서 읽기 (Split 대응: 같은 ItemType의 모든 Entry 합산!)
	int32 TotalCount = 0;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Verbose, TEXT("🔍 GetTotalMaterialCount(%s) 시작:"), *MaterialTag.ToString());
#endif

	for (const auto& Entry : InventoryList.Entries)
	{
		if (!IsValid(Entry.Item)) continue;

		if (Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag))
		{
			int32 EntryCount = Entry.Item->GetTotalStackCount();
			TotalCount += EntryCount;

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Verbose, TEXT("  Entry 발견: Item포인터=%p, TotalStackCount=%d, 누적합=%d"),
				Entry.Item.Get(), EntryCount, TotalCount);
#endif
		}
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Log, TEXT("✅ GetTotalMaterialCount(%s) = %d (InventoryList 전체 합산)"),
		*MaterialTag.ToString(), TotalCount);
#endif
	return TotalCount;
}

bool UInv_InventoryComponent::Server_ConsumeMaterialsMultiStack_Validate(const FGameplayTag& MaterialTag, int32 Amount)
{
	return MaterialTag.IsValid() && Amount > 0;
}

// 재료 소비 - 여러 스택에서 차감 (Building 시스템용)
void UInv_InventoryComponent::Server_ConsumeMaterialsMultiStack_Implementation(const FGameplayTag& MaterialTag, int32 Amount)
{
	if (!MaterialTag.IsValid() || Amount <= 0) return;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== Server_ConsumeMaterialsMultiStack 호출됨 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);

	// 🔍 디버깅: 차감 전 서버 상태 확인
	UE_LOG(LogTemp, Warning, TEXT("🔍 [서버] 차감 전 InventoryList.Entries 상태:"));
	int32 ServerTotalBefore = 0;
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		const auto& Entry = InventoryList.Entries[i];
		if (IsValid(Entry.Item) && Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag))
		{
			int32 Count = Entry.Item->GetTotalStackCount();
			ServerTotalBefore += Count;
			UE_LOG(LogTemp, Warning, TEXT("  Entry[%d]: Item포인터=%p, TotalStackCount=%d"),
				i, Entry.Item.Get(), Count);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("🔍 [서버] 차감 전 총량: %d"), ServerTotalBefore);
#endif

	// 1단계: 데이터(TotalStackCount) 차감 및 동기화
	int32 RemainingAmount = Amount;
	TArray<UInv_InventoryItem*> ItemsToRemove;  // ⚠️ Entry 포인터가 아닌 Item 포인터 수집 (RemoveEntry가 TArray를 변경하므로 Entry* 저장 시 댕글링)

	for (auto& Entry : InventoryList.Entries)
	{
		if (RemainingAmount <= 0) break;

		if (!IsValid(Entry.Item)) continue;

		// 같은 타입인지 확인
		if (!Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag)) continue;

		int32 CurrentCount = Entry.Item->GetTotalStackCount();
		int32 AmountToConsume = FMath::Min(CurrentCount, RemainingAmount);

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("🔧 [서버] 차감 시도: Item포인터=%p, CurrentCount=%d, AmountToConsume=%d, RemainingBefore=%d"),
			Entry.Item.Get(), CurrentCount, AmountToConsume, RemainingAmount);
#endif

		RemainingAmount -= AmountToConsume;
		int32 NewCount = CurrentCount - AmountToConsume;

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("🔧 [서버] 차감 계산: %d - %d = %d, RemainingAfter=%d"),
			CurrentCount, AmountToConsume, NewCount, RemainingAmount);
#endif

		if (NewCount <= 0)
		{
			// 제거 예약 — Item 포인터만 수집 (Entry 포인터는 RemoveEntry 후 무효화됨)
			ItemsToRemove.Add(Entry.Item);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("❌ [서버] Entry 제거 예약: Item포인터=%p"), Entry.Item.Get());
#endif
		}
		else
		{
			// TotalStackCount 업데이트
			Entry.Item->SetTotalStackCount(NewCount);

			// StackableFragment도 업데이트
			FInv_ItemManifest& ItemManifest = Entry.Item->GetItemManifestMutable();
			if (FInv_StackableFragment* StackableFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_StackableFragment>())
			{
				StackableFragment->SetStackCount(NewCount);
			}

			// FastArray에 변경 알림
			InventoryList.MarkItemDirty(Entry);

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("✅ [서버] Entry 업데이트 완료: %d → %d (Item포인터=%p)"),
				CurrentCount, NewCount, Entry.Item.Get());
#endif
		}
	}

#if INV_DEBUG_INVENTORY
	// 🔍 디버깅: 차감 후 서버 상태 확인 (제거 전)
	UE_LOG(LogTemp, Warning, TEXT("🔍 [서버] 차감 후 InventoryList.Entries 상태 (제거 전):"));
	int32 ServerTotalAfter = 0;
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		const auto& Entry = InventoryList.Entries[i];
		if (IsValid(Entry.Item) && Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag))
		{
			int32 Count = Entry.Item->GetTotalStackCount();
			ServerTotalAfter += Count;
			UE_LOG(LogTemp, Warning, TEXT("  Entry[%d]: Item포인터=%p, TotalStackCount=%d"),
				i, Entry.Item.Get(), Count);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("🔍 [서버] 차감 후 총량 (제거 전): %d (예상: %d)"), ServerTotalAfter, ServerTotalBefore - Amount);
#endif

	// 제거 예약된 아이템들 실제 제거
	for (UInv_InventoryItem* ItemToRemove : ItemsToRemove)
	{
		InventoryList.RemoveEntry(ItemToRemove);

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("InventoryList에서 제거됨: %s"), *MaterialTag.ToString());
#endif
	}

	// ════════════════════════════════════════════════════════════════
	// 🔧 리슨서버 호환 수정
	// ════════════════════════════════════════════════════════════════
	//
	// 📌 문제:
	//    리슨서버 호스트는 자기 자신에게 FastArray 리플리케이션이 안 됨
	//    → PostReplicatedChange 콜백이 불리지 않음
	//    → UI(Grid)에 재료 수량 변경이 반영되지 않음
	//
	// 📌 해결:
	//    NM_ListenServer || NM_Standalone일 때
	//    Multicast_ConsumeMaterialsUI_Implementation을 직접 호출하여 UI 갱신
	//
	// 📌 데디서버 영향:
	//    없음 — 데디서버에서는 FastArray 리플리케이션이 자동으로
	//    PostReplicatedChange를 호출하여 UI를 업데이트함
	//
	// ════════════════════════════════════════════════════════════════
	if (IsListenServerOrStandalone())
	{
		// 리슨서버 호스트의 UI에 재료 차감 반영
		Multicast_ConsumeMaterialsUI_Implementation(MaterialTag, Amount);
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("🔧 리슨서버 호스트 UI 갱신: Multicast_ConsumeMaterialsUI_Implementation(%s, %d)"),
			*MaterialTag.ToString(), Amount);
#endif
	}

#if INV_DEBUG_INVENTORY
	if (RemainingAmount > 0)
	{
		UE_LOG(LogTemp, Error, TEXT("재료가 부족합니다! 남은 차감량: %d"), RemainingAmount);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 재료 차감 완료! MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== Server_ConsumeMaterialsMultiStack 완료 ==="));
#endif
}

bool UInv_InventoryComponent::Server_UpdateItemStackCount_Validate(UInv_InventoryItem* Item, int32 NewStackCount)
{
	return NewStackCount > 0;
}

// Split 시 서버의 TotalStackCount 업데이트
void UInv_InventoryComponent::Server_UpdateItemStackCount_Implementation(UInv_InventoryItem* Item, int32 NewStackCount)
{
	if (!IsValid(Item))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("Server_UpdateItemStackCount: Item is invalid!"));
#endif
		return;
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("🔧 [서버] Server_UpdateItemStackCount 호출됨"));
	UE_LOG(LogTemp, Warning, TEXT("  Item포인터: %p, ItemType: %s"), Item, *Item->GetItemManifest().GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("  이전 TotalStackCount: %d → 새로운 값: %d"), Item->GetTotalStackCount(), NewStackCount);
#endif

	// 1단계: TotalStackCount 업데이트
	Item->SetTotalStackCount(NewStackCount);

	// 2단계: StackableFragment도 업데이트
	FInv_ItemManifest& ItemManifest = Item->GetItemManifestMutable();
	if (FInv_StackableFragment* StackableFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_StackableFragment>())
	{
		StackableFragment->SetStackCount(NewStackCount);
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("  StackableFragment도 %d로 업데이트됨"), NewStackCount);
#endif
	}

	// ⭐⭐⭐ 3단계: FastArray에 변경 알림 (리플리케이션 트리거!)
	int32 ItemEntryIndex = INDEX_NONE;
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == Item)
		{
			ItemEntryIndex = i;
			InventoryList.MarkItemDirty(InventoryList.Entries[i]);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("✅ FastArray에 Item 변경 알림 완료! 클라이언트로 리플리케이션됩니다."));
#endif
			break;
		}
	}

	// ── 리슨서버 호스트: 스택 수량 변경 UI 갱신 ──
	// MarkItemDirty는 리모트 클라이언트에만 리플리케이션됨
	// 리슨서버 호스트에서는 직접 Broadcast하여 UI 갱신
	if (IsListenServerOrStandalone())
	{
		OnItemAdded.Broadcast(Item, ItemEntryIndex);
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("🔧 리슨서버 호스트: OnItemAdded 브로드캐스트 (스택 수량 %d, EntryIndex=%d)"),
			NewStackCount, ItemEntryIndex);
#endif
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("✅ [서버] %s의 TotalStackCount를 %d로 업데이트 완료 (FastArray 갱신됨)"),
		*Item->GetItemManifest().GetItemType().ToString(), NewStackCount);
#endif
}

// Multicast: 모든 클라이언트의 UI 업데이트
void UInv_InventoryComponent::Multicast_ConsumeMaterialsUI_Implementation(const FGameplayTag& MaterialTag, int32 Amount)
{
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== Multicast_ConsumeMaterialsUI 호출됨 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);
#endif

	if (!IsValid(InventoryMenu))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("InventoryMenu is invalid!"));
#endif
		return;
	}

	// SpatialInventory의 해당 카테고리 Grid 찾아서 ConsumeItemsByTag 호출
	UInv_SpatialInventory* SpatialInv = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInv))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("SpatialInventory cast failed!"));
#endif
		return;
	}

	// 모든 그리드를 순회하되, 실제로 해당 아이템이 있는 Grid에서만 차감
	TArray<UInv_InventoryGrid*> AllGrids = {
		SpatialInv->GetGrid_Equippables(),
		SpatialInv->GetGrid_Consumables(),
		SpatialInv->GetGrid_Craftables()
	};

	int32 RemainingToConsume = Amount;
	
	for (UInv_InventoryGrid* Grid : AllGrids)
	{
		if (!IsValid(Grid)) continue;
		if (RemainingToConsume <= 0) break; // 이미 다 차감했으면 종료

		// 이 Grid의 카테고리 확인
		EInv_ItemCategory GridCategory = Grid->GetItemCategory();
		
		// MaterialTag가 이 Grid의 카테고리에 속하는지 확인
		// 예: GameItems.Craftables.FireFernFruit → Craftables 카테고리
		bool bMatchesCategory = false;
		
		if (MaterialTag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("GameItems.Craftables"))))
		{
			bMatchesCategory = (GridCategory == EInv_ItemCategory::Craftable);
		}
		else if (MaterialTag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("GameItems.Consumables"))))
		{
			bMatchesCategory = (GridCategory == EInv_ItemCategory::Consumable);
		}
		else if (MaterialTag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("GameItems.Equippables"))))
		{
			bMatchesCategory = (GridCategory == EInv_ItemCategory::Equippable);
		}

		// 카테고리가 맞으면 이 Grid에서만 차감
		if (bMatchesCategory)
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("Grid 카테고리 매칭! GridCategory: %d"), (int32)GridCategory);
#endif
			Grid->ConsumeItemsByTag(MaterialTag, RemainingToConsume);
			break; // 한 Grid에서만 차감!
		}
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("✅ UI(GridSlot) 차감 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("=== Multicast_ConsumeMaterialsUI 완료 ==="));
#endif
}

bool UInv_InventoryComponent::Server_EquipSlotClicked_Validate(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex)
{
	if (WeaponSlotIndex < 0 || WeaponSlotIndex > 1)
	{
		return false;
	}

	if (IsValid(ItemToEquip) && !IsInventoryItemOwnedByThisComponent(this, ItemToEquip))
	{
		return false;
	}

	if (IsValid(ItemToUnequip) && !IsInventoryItemOwnedByThisComponent(this, ItemToUnequip))
	{
		return false;
	}

	return true;
}

// 아이템 장착 상호작용을 누른 뒤 서버에서 어떻게 처리를 할지.
void UInv_InventoryComponent::Server_EquipSlotClicked_Implementation(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex)
{
	Multicast_EquipSlotClicked(ItemToEquip, ItemToUnequip, WeaponSlotIndex); // 멀티캐스트로 모든 클라이언트에 알리는 부분.
}

// 멀티캐스트로 아이템 장착 상호작용을 모든 클라이언트에 알리는 부분.
void UInv_InventoryComponent::Multicast_EquipSlotClicked_Implementation(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex)
{
	// Equipment Component will listen to these delegates
	// 장비 컴포넌트가 이 델리게이트를 수신 대기합니다.
	if (!IsValid(ItemToEquip) && !IsValid(ItemToUnequip))
	{
		UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Multicast_EquipSlotClicked ignored: no items supplied"));
		return;
	}

	if (IsValid(ItemToEquip))
	{
		const int32 EquipEntryIndex = FindOwnedInventoryEntryIndex(this, ItemToEquip);
		if (EquipEntryIndex == INDEX_NONE || !HasEquipmentFragment(ItemToEquip))
		{
			UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Multicast_EquipSlotClicked ignored: equip item is invalid or not tracked"));
			return;
		}

		if (InventoryList.Entries[EquipEntryIndex].bIsAttachedToWeapon)
		{
			UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Multicast_EquipSlotClicked ignored: equip item is attached to another weapon"));
			return;
		}
	}

	if (IsValid(ItemToUnequip))
	{
		const int32 UnequipEntryIndex = FindOwnedInventoryEntryIndex(this, ItemToUnequip);
		if (UnequipEntryIndex == INDEX_NONE || !HasEquipmentFragment(ItemToUnequip))
		{
			UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Multicast_EquipSlotClicked ignored: unequip item is invalid or not tracked"));
			return;
		}

		const FInv_InventoryEntry& UnequipEntry = InventoryList.Entries[UnequipEntryIndex];
		if (UnequipEntry.WeaponSlotIndex != INDEX_NONE && UnequipEntry.WeaponSlotIndex != WeaponSlotIndex)
		{
			UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Multicast_EquipSlotClicked ignored: slot mismatch for unequip item"));
			return;
		}
	}

	if (IsValid(ItemToEquip) && IsValid(ItemToUnequip) && ItemToEquip == ItemToUnequip)
	{
		UE_LOG(LogInventory, Warning, TEXT("[InventoryComponent] Multicast_EquipSlotClicked ignored: equip and unequip target are identical"));
		return;
	}

	OnItemEquipped.Broadcast(ItemToEquip, WeaponSlotIndex);
	OnItemUnequipped.Broadcast(ItemToUnequip, WeaponSlotIndex);

	// Fix 7: 장착 시 서버 FastArray Entry의 GridIndex 클리어 — Phase 5 좌표 중복 방지
	// [Fix14] 장착 상태 + 슬롯 인덱스 기록
	if (GetOwner() && GetOwner()->HasAuthority() && IsValid(ItemToEquip))
	{
		for (int32 i = 0; i < InventoryList.Entries.Num(); i++)
		{
			if (InventoryList.Entries[i].Item == ItemToEquip)
			{
				InventoryList.Entries[i].GridIndex = INDEX_NONE;
				InventoryList.Entries[i].GridCategory = 0;
				InventoryList.Entries[i].bIsEquipped = true;
				InventoryList.Entries[i].WeaponSlotIndex = WeaponSlotIndex;
				// MarkItemDirty 호출 금지! 리플리케이션 트리거 시 PostReplicatedChange → AddItem으로 아이템이 Grid에 다시 나타남
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[Fix14] 장착 아이템 bIsEquipped=true, WeaponSlotIndex=%d, GridIndex 클리어: %s (Entry[%d])"),
					WeaponSlotIndex, *ItemToEquip->GetItemManifest().GetItemType().ToString(), i);
#endif
				break;
			}
		}
	}

	// [Fix14] 해제되는 아이템의 장착 플래그 클리어
	if (GetOwner() && GetOwner()->HasAuthority() && IsValid(ItemToUnequip))
	{
		for (int32 i = 0; i < InventoryList.Entries.Num(); i++)
		{
			if (InventoryList.Entries[i].Item == ItemToUnequip)
			{
				InventoryList.Entries[i].bIsEquipped = false;
				InventoryList.Entries[i].WeaponSlotIndex = -1;
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[Fix14] 해제 아이템 bIsEquipped=false: %s (Entry[%d])"),
					*ItemToUnequip->GetItemManifest().GetItemType().ToString(), i);
#endif
				break;
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 [부착물 시스템 Phase 2] 부착물 장착 Server RPC
// ════════════════════════════════════════════════════════════════
// 호출 경로: Phase 3 UI → 드래그 앤 드롭 → 이 RPC
// 처리 흐름:
//   1. InventoryList에서 무기/부착물 아이템 찾기
//   2. 유효성 검증 (Fragment, 슬롯, 타입 호환)
//   3. 부착물 Manifest 사본 → 무기에 장착
//   4. InventoryList에서 부착물 제거
//   5. 무기 장비 슬롯 장착 중이면 부착물 스탯 적용
//   6. 리플리케이션 (MarkItemDirty + 리슨서버 분기)
// ════════════════════════════════════════════════════════════════
bool UInv_InventoryComponent::Server_AttachItemToWeapon_Validate(int32 WeaponEntryIndex, int32 AttachmentEntryIndex, int32 SlotIndex)
{
	return WeaponEntryIndex >= 0 && AttachmentEntryIndex >= 0 && SlotIndex >= 0;
}

void UInv_InventoryComponent::Server_AttachItemToWeapon_Implementation(int32 WeaponEntryIndex, int32 AttachmentEntryIndex, int32 SlotIndex)
{
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment] Server_AttachItemToWeapon: WeaponEntry=%d, AttachmentEntry=%d, Slot=%d"),
		WeaponEntryIndex, AttachmentEntryIndex, SlotIndex);
#endif

	// ── 1. 아이템 찾기 ──
	if (!InventoryList.Entries.IsValidIndex(WeaponEntryIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 잘못된 WeaponEntryIndex %d"), WeaponEntryIndex);
		return;
	}
	if (!InventoryList.Entries.IsValidIndex(AttachmentEntryIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 잘못된 AttachmentEntryIndex %d"), AttachmentEntryIndex);
		return;
	}

	UInv_InventoryItem* WeaponItem = InventoryList.Entries[WeaponEntryIndex].Item;
	UInv_InventoryItem* AttachmentItem = InventoryList.Entries[AttachmentEntryIndex].Item;

	if (!IsValid(WeaponItem) || !IsValid(AttachmentItem))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 아이템이 유효하지 않음"));
		return;
	}

	// ── 2. Fragment 유효성 검증 ──
	FInv_ItemManifest& WeaponManifest = WeaponItem->GetItemManifestMutable();
	FInv_AttachmentHostFragment* HostFragment = WeaponManifest.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
	if (!HostFragment)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 무기에 AttachmentHostFragment 없음"));
		return;
	}

	const FInv_ItemManifest& AttachManifest = AttachmentItem->GetItemManifest();
	const FInv_AttachableFragment* AttachableFragment = AttachManifest.GetFragmentOfType<FInv_AttachableFragment>();
	if (!AttachableFragment)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 부착물에 AttachableFragment 없음"));
		return;
	}

	// ── 3. 슬롯 유효성 검증 ──
	const FInv_AttachmentSlotDef* SlotDef = HostFragment->GetSlotDef(SlotIndex);
	if (!SlotDef)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 잘못된 SlotIndex %d (슬롯 수: %d)"),
			SlotIndex, HostFragment->GetSlotCount());
		return;
	}

	// 슬롯이 이미 점유되었는지 확인
	if (HostFragment->IsSlotOccupied(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 슬롯 %d 이미 점유됨"), SlotIndex);
		return;
	}

	// 부착물 타입과 슬롯 타입이 일치하는지 확인
	if (!AttachableFragment->CanAttachToSlot(*SlotDef))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 장착 실패: 타입 불일치 (부착물=%s, 슬롯=%s)"),
			*AttachableFragment->GetAttachmentType().ToString(), *SlotDef->SlotType.ToString());
		return;
	}

	// ── 4. 부착물 Manifest 사본 생성 → 무기에 장착 ──
	FInv_AttachedItemData AttachedData;
	AttachedData.SlotIndex = SlotIndex;
	AttachedData.AttachmentItemType = AttachManifest.GetItemType();
	AttachedData.ItemManifestCopy = AttachManifest; // Manifest 전체 사본
	AttachedData.OriginalItem = AttachmentItem; // ⭐ 원본 포인터 저장 (분리 시 복원용)

	HostFragment->AttachItem(SlotIndex, AttachedData);

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment] 부착물 장착 성공: %s → 슬롯 %d"),
		*AttachedData.AttachmentItemType.ToString(), SlotIndex);
#endif

	// ── 5. FastArray에서 부착물 Entry를 '부착됨' 상태로 표시 ──
	// ⭐ RemoveEntry 대신 플래그 방식 (인덱스 밀림 방지!)
	// Entry는 배열에 남아있지만 그리드에서만 숨김
	int32 RemovedEntryIndex = AttachmentEntryIndex;

	InventoryList.Entries[AttachmentEntryIndex].bIsAttachedToWeapon = true;
	InventoryList.Entries[AttachmentEntryIndex].GridIndex = INDEX_NONE; // 그리드 자리 반환
	InventoryList.MarkItemDirty(InventoryList.Entries[AttachmentEntryIndex]);

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment] 부착물 Entry[%d] bIsAttachedToWeapon=true (RemoveEntry 대신 플래그 방식)"),
		AttachmentEntryIndex);
#endif

	// 리슨서버 호스트: 부착물이 Grid에서 사라졌으므로 OnItemRemoved 방송
	if (IsListenServerOrStandalone())
	{
		OnItemRemoved.Broadcast(AttachmentItem, RemovedEntryIndex);
	}

	// ── 6. 무기 Entry를 dirty로 표시 (리플리케이션) ──
	// ⭐ bIsAttachedToWeapon 방식이므로 Entry 인덱스가 변경되지 않음
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == WeaponItem)
		{
#if INV_DEBUG_ATTACHMENT
			// ★ [부착진단-MarkDirty] MarkItemDirty 직전 Entry 상태 ★
			{
				UE_LOG(LogTemp, Error, TEXT("[부착진단-MarkDirty] MarkItemDirty 호출 직전"));
				UE_LOG(LogTemp, Error, TEXT("[부착진단-MarkDirty]   EntryIndex=%d, Item=%s"),
					i, *WeaponItem->GetItemManifest().GetItemType().ToString());
				const FInv_AttachmentHostFragment* PreHost =
					WeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
				UE_LOG(LogTemp, Error, TEXT("[부착진단-MarkDirty]   HostFrag=%s, AttachedItems=%d"),
					PreHost ? TEXT("유효") : TEXT("nullptr"),
					PreHost ? PreHost->GetAttachedItems().Num() : -1);
				if (PreHost)
				{
					for (int32 d = 0; d < PreHost->GetAttachedItems().Num(); d++)
					{
						const FInv_AttachedItemData& DiagData = PreHost->GetAttachedItems()[d];
						UE_LOG(LogTemp, Error, TEXT("[부착진단-MarkDirty]     [%d] Type=%s (Slot=%d), ManifestCopy.ItemType=%s"),
							d, *DiagData.AttachmentItemType.ToString(), DiagData.SlotIndex,
							*DiagData.ItemManifestCopy.GetItemType().ToString());
					}
				}
			}
#endif

			InventoryList.MarkItemDirty(InventoryList.Entries[i]);

#if INV_DEBUG_ATTACHMENT
			// ★ [부착진단-서버] 부착 완료 후 서버 상태 확인 ★
			{
				const FInv_AttachmentHostFragment* DiagHost =
					WeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
				UE_LOG(LogTemp, Error, TEXT("[부착진단-서버] 부착 완료 후 MarkItemDirty 직후: WeaponItem=%s, HostFrag=%s, AttachedItems=%d"),
					*WeaponItem->GetItemManifest().GetItemType().ToString(),
					DiagHost ? TEXT("유효") : TEXT("nullptr"),
					DiagHost ? DiagHost->GetAttachedItems().Num() : -1);
				if (DiagHost)
				{
					for (int32 d = 0; d < DiagHost->GetAttachedItems().Num(); d++)
					{
						const FInv_AttachedItemData& DiagData = DiagHost->GetAttachedItems()[d];
						UE_LOG(LogTemp, Error, TEXT("[부착진단-서버]   [%d] Type=%s (Slot=%d), ManifestCopy.ItemType=%s"),
							d, *DiagData.AttachmentItemType.ToString(), DiagData.SlotIndex,
							*DiagData.ItemManifestCopy.GetItemType().ToString());
					}
				}
			}
#endif

			break;
		}
	}

	// ── 7. 무기가 장비 슬롯에 장착 중이면 부착물 스탯 적용 ──
	const FInv_EquipmentFragment* EquipFragment = WeaponManifest.GetFragmentOfType<FInv_EquipmentFragment>();

#if INV_DEBUG_ATTACHMENT
	// ⭐ [Phase 7 디버그] bEquipped 상태 확인
	UE_LOG(LogTemp, Warning, TEXT("[Attachment Phase7 디버그] EquipFragment=%s, bEquipped=%s"),
		EquipFragment ? TEXT("있음") : TEXT("없음"),
		(EquipFragment && EquipFragment->bEquipped) ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	if (EquipFragment)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment Phase7 디버그] EquippedActor=%s"),
			IsValid(EquipFragment->GetEquippedActor()) ? *EquipFragment->GetEquippedActor()->GetName() : TEXT("nullptr"));
	}
	UE_LOG(LogTemp, Warning, TEXT("[Attachment Phase7 디버그] AttachableFragment->bIsSuppressor=%s"),
		AttachableFragment->GetIsSuppressor() ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
#endif

	if (EquipFragment && EquipFragment->bEquipped)
	{
		// 방금 장착한 부착물의 스탯만 적용 (ManifestCopy에서 가져옴)
		const FInv_AttachedItemData* JustAttached = HostFragment->GetAttachedItemData(SlotIndex);
		if (JustAttached)
		{
			FInv_AttachableFragment* MutableAttachable =
				const_cast<FInv_ItemManifest&>(JustAttached->ItemManifestCopy)
					.GetFragmentOfTypeMutable<FInv_AttachableFragment>();
			if (MutableAttachable)
			{
				MutableAttachable->OnEquip(OwningController.Get());
			}
		}

		// ════════════════════════════════════════════════════════════════
		// 📌 [Phase 5] 실시간 부착물 메시 추가 (무기가 장착 중일 때만)
		// ════════════════════════════════════════════════════════════════
		AInv_EquipActor* EquipActor = EquipFragment->GetEquippedActor();
		if (IsValid(EquipActor) && AttachableFragment->GetAttachmentMesh())
		{
			// 소켓 폴백: 무기 SlotDef → 부착물 AttachableFragment → NAME_None
			const FInv_AttachmentSlotDef* MeshSlotDef = HostFragment->GetSlotDef(SlotIndex);
			FName MeshSocketName = NAME_None;
			if (MeshSlotDef && !MeshSlotDef->AttachSocket.IsNone())
			{
				MeshSocketName = MeshSlotDef->AttachSocket;  // 1순위: 무기 SlotDef 오버라이드
			}
			else
			{
				MeshSocketName = AttachableFragment->GetAttachSocket();  // 2순위: 부착물 기본 소켓
			}
			EquipActor->AttachMeshToSocket(
				SlotIndex,
				AttachableFragment->GetAttachmentMesh(),
				MeshSocketName,
				AttachableFragment->GetAttachOffset()
			);
#if INV_DEBUG_ATTACHMENT
			UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 실시간 부착물 메시 추가: 슬롯 %d"), SlotIndex);
#endif
		}

		// ════════════════════════════════════════════════════════════════
		// [Phase 7] 부착물 효과 적용 (소음기/스코프/레이저)
		// ════════════════════════════════════════════════════════════════
		if (IsValid(EquipActor))
		{
			EquipActor->ApplyAttachmentEffects(AttachableFragment);
		}

		// ════════════════════════════════════════════════════════════════
		// 부착물 시각 변경 알림 → WeaponBridge가 HandWeapon에 Multicast 전파
		// EquipActor에만 반영된 부착물을 HandWeapon(손 무기)에도 동기화
		// ════════════════════════════════════════════════════════════════
		if (IsValid(EquipActor))
		{
			OnWeaponAttachmentVisualChanged.Broadcast(EquipActor);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 [부착물 시스템 Phase 2] 부착물 분리 Server RPC
// ════════════════════════════════════════════════════════════════
// 호출 경로: Phase 3 UI → 슬롯 우클릭 → 이 RPC
// 처리 흐름:
//   1. InventoryList에서 무기 아이템 찾기
//   2. 유효성 검증 (Fragment, 슬롯 점유 여부, Grid 빈 공간)
//   3. 무기에서 부착물 분리 → ManifestCopy 반환
//   4. ManifestCopy로 새 인벤토리 아이템 생성 (스탯 재랜덤 방지)
//   5. InventoryList에 추가
//   6. 무기 장비 슬롯 장착 중이면 부착물 스탯 해제
//   7. 리플리케이션
// ════════════════════════════════════════════════════════════════
bool UInv_InventoryComponent::Server_DetachItemFromWeapon_Validate(int32 WeaponEntryIndex, int32 SlotIndex)
{
	return WeaponEntryIndex >= 0 && SlotIndex >= 0;
}

void UInv_InventoryComponent::Server_DetachItemFromWeapon_Implementation(int32 WeaponEntryIndex, int32 SlotIndex)
{
#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment] Server_DetachItemFromWeapon: WeaponEntry=%d, Slot=%d"),
		WeaponEntryIndex, SlotIndex);
#endif

	// ── 1. 무기 아이템 찾기 ──
	if (!InventoryList.Entries.IsValidIndex(WeaponEntryIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 분리 실패: 잘못된 WeaponEntryIndex %d"), WeaponEntryIndex);
		return;
	}

	UInv_InventoryItem* WeaponItem = InventoryList.Entries[WeaponEntryIndex].Item;
	if (!IsValid(WeaponItem))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 분리 실패: 무기 아이템 유효하지 않음"));
		return;
	}

	// ── 2. Fragment 유효성 검증 ──
	FInv_ItemManifest& WeaponManifest = WeaponItem->GetItemManifestMutable();
	FInv_AttachmentHostFragment* HostFragment = WeaponManifest.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
	if (!HostFragment)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 분리 실패: 무기에 AttachmentHostFragment 없음"));
		return;
	}

	// 해당 SlotIndex에 부착물이 있는지 확인
	if (!HostFragment->IsSlotOccupied(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 분리 실패: 슬롯 %d에 부착물 없음"), SlotIndex);
		return;
	}

	// ── 3. 부착 데이터 및 원본 아이템 포인터 확인 ──
	const FInv_AttachedItemData* AttachedData = HostFragment->GetAttachedItemData(SlotIndex);
	if (!AttachedData)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attachment] 분리 실패: 슬롯 %d 데이터를 찾을 수 없음"), SlotIndex);
		return;
	}

	// ⭐ bIsAttachedToWeapon 방식: 원본 Entry가 FastArray에 남아있으므로 공간 체크 불필요
	if (!IsValid(AttachedData->OriginalItem))
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment] 분리 실패: OriginalItem 포인터가 nullptr (레거시 데이터?)"));
		return;
	}

	// ── 4. 무기가 장비 슬롯에 장착 중이면 부착물 스탯 해제 (분리 전에!) ──
	const FInv_EquipmentFragment* EquipFragment = WeaponManifest.GetFragmentOfType<FInv_EquipmentFragment>();
	if (EquipFragment && EquipFragment->bEquipped)
	{
		FInv_AttachableFragment* MutableAttachable =
			const_cast<FInv_ItemManifest&>(AttachedData->ItemManifestCopy)
				.GetFragmentOfTypeMutable<FInv_AttachableFragment>();
		if (MutableAttachable)
		{
			MutableAttachable->OnUnequip(OwningController.Get());
		}

		// ════════════════════════════════════════════════════════════════
		// 📌 [Phase 5] 실시간 부착물 메시 제거 (무기가 장착 중일 때만)
		// ════════════════════════════════════════════════════════════════
		AInv_EquipActor* EquipActor = EquipFragment->GetEquippedActor();

		// ════════════════════════════════════════════════════════════════
		// [Phase 7] 부착물 효과 해제 (분리 전, AttachedData가 아직 유효할 때)
		// ════════════════════════════════════════════════════════════════
		if (IsValid(EquipActor))
		{
			const FInv_AttachableFragment* DetachingAttachable =
				AttachedData->ItemManifestCopy.GetFragmentOfType<FInv_AttachableFragment>();
			if (DetachingAttachable)
			{
				EquipActor->RemoveAttachmentEffects(DetachingAttachable);
			}
		}

		// [Phase 5] 실시간 부착물 메시 제거
		if (IsValid(EquipActor))
		{
			EquipActor->DetachMeshFromSocket(SlotIndex);
#if INV_DEBUG_ATTACHMENT
			UE_LOG(LogTemp, Log, TEXT("[Attachment Visual] 실시간 부착물 메시 제거: 슬롯 %d"), SlotIndex);
#endif

			// ════════════════════════════════════════════════════════════════
			// 부착물 시각 변경 알림 → WeaponBridge가 HandWeapon에 Multicast 전파
			// DetachMeshFromSocket 직후 Broadcast해야 GetAttachmentVisualInfos()가
			// 제거된 슬롯을 제외한 결과를 반환한다
			// ════════════════════════════════════════════════════════════════
			OnWeaponAttachmentVisualChanged.Broadcast(EquipActor);
		}
	}

	// ── 5. 무기에서 부착물 분리 → FInv_AttachedItemData 반환 ──
	FInv_AttachedItemData DetachedData = HostFragment->DetachItem(SlotIndex);

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Attachment] 부착물 분리 성공: %s (슬롯 %d)"),
		*DetachedData.AttachmentItemType.ToString(), SlotIndex);
#endif

	// ── 6. 원본 Entry의 bIsAttachedToWeapon 플래그 해제 ──
	// ⭐ 새 아이템 생성 대신 원본 Entry를 복원 (인덱스 밀림 방지!)
	UInv_InventoryItem* OriginalItem = DetachedData.OriginalItem;
	int32 RestoredEntryIndex = INDEX_NONE;

	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == OriginalItem && InventoryList.Entries[i].bIsAttachedToWeapon)
		{
			InventoryList.Entries[i].bIsAttachedToWeapon = false;
			InventoryList.MarkItemDirty(InventoryList.Entries[i]);
			RestoredEntryIndex = i;

#if INV_DEBUG_ATTACHMENT
			UE_LOG(LogTemp, Log, TEXT("[Attachment] 원본 Entry[%d] bIsAttachedToWeapon=false 복원 완료: %s"),
				i, *OriginalItem->GetItemManifest().GetItemType().ToString());
#endif
			break;
		}
	}

	if (RestoredEntryIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[Attachment] 분리 실패: 원본 Entry를 FastArray에서 찾을 수 없음!"));
		return;
	}

	// 리슨서버 호스트: 아이템이 Grid에 다시 표시되므로 OnItemAdded 방송
	if (IsListenServerOrStandalone())
	{
		OnItemAdded.Broadcast(OriginalItem, RestoredEntryIndex);
	}

	// ── 7. 무기 Entry를 dirty로 표시 (리플리케이션) ──
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == WeaponItem)
		{
			InventoryList.MarkItemDirty(InventoryList.Entries[i]);
			break;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 [부착물 시스템 Phase 2] 호환성 체크 (읽기 전용)
// ════════════════════════════════════════════════════════════════
// 호출 경로: Phase 3 UI → 드래그 중 슬롯 하이라이트
// 장착 가능 여부만 확인 (실제 장착은 안 함)
// ════════════════════════════════════════════════════════════════
bool UInv_InventoryComponent::CanAttachToWeapon(int32 WeaponEntryIndex, int32 AttachmentEntryIndex, int32 SlotIndex) const
{
	// 무기 Entry 유효성
	if (!InventoryList.Entries.IsValidIndex(WeaponEntryIndex)) return false;

	// 부착물 Entry 유효성
	if (!InventoryList.Entries.IsValidIndex(AttachmentEntryIndex)) return false;

	const UInv_InventoryItem* WeaponItem = InventoryList.Entries[WeaponEntryIndex].Item;
	const UInv_InventoryItem* AttachmentItem = InventoryList.Entries[AttachmentEntryIndex].Item;
	if (!IsValid(WeaponItem) || !IsValid(AttachmentItem)) return false;

	// 무기에 AttachmentHostFragment 있는지
	const FInv_AttachmentHostFragment* HostFragment =
		WeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
	if (!HostFragment) return false;

	// 부착물에 AttachableFragment 있는지
	const FInv_AttachableFragment* AttachableFragment =
		AttachmentItem->GetItemManifest().GetFragmentOfType<FInv_AttachableFragment>();
	if (!AttachableFragment) return false;

	// SlotIndex 유효한지
	const FInv_AttachmentSlotDef* SlotDef = HostFragment->GetSlotDef(SlotIndex);
	if (!SlotDef) return false;

	// 슬롯이 비어있는지
	if (HostFragment->IsSlotOccupied(SlotIndex)) return false;

	// 타입 호환되는지
	return AttachableFragment->CanAttachToSlot(*SlotDef);
}

void UInv_InventoryComponent::ToggleInventoryMenu()
{
	if (bInventoryMenuOpen)
	{
		CloseInventoryMenu();
	}
	else
	{
		OpenInventoryMenu();
	}
	OnInventoryMenuToggled.Broadcast(bInventoryMenuOpen); // 인벤토리 메뉴 토글 방송
}

void UInv_InventoryComponent::AddRepSubObj(UObject* SubObj)
{
	if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && IsValid(SubObj)) // 복제 준비가 되었는지 확인
	{
		AddReplicatedSubObject(SubObj); // 복제된 하위 객체 추가
	}
}

void UInv_InventoryComponent::RemoveRepSubObj(UObject* SubObj)
{
	if (IsUsingRegisteredSubObjectList() && IsValid(SubObj))
	{
		RemoveReplicatedSubObject(SubObj); // 복제 하위 객체 등록 해제 (GC + 네트워크 누수 방지)
	}
}

// Called when the game starts
void UInv_InventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	ConstructInventory();
	
	// ⭐ InventoryMenu의 Grid 크기를 Component 설정에 동기화 (Blueprint Widget → Component)
	SyncGridSizesFromWidget();
}

// ⭐ Blueprint Widget의 Grid 크기를 Component 설정으로 가져오기
void UInv_InventoryComponent::SyncGridSizesFromWidget()
{
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] Grid 크기 참조 시작..."));
#endif

	// ⭐ 1순위: Blueprint에서 직접 선택한 Widget 참조
	if (IsValid(InventoryGridReference))
	{
		GridRows = InventoryGridReference->GetRows();
		GridColumns = InventoryGridReference->GetColumns();

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ✅ Grid (Blueprint 직접 참조): %d x %d = %d칸"),
			GridRows, GridColumns, GridRows * GridColumns);
#endif
	}
	// 2순위: InventoryMenu에서 자동으로 가져오기 (Grid_Equippables 사용)
	else if (IsValid(InventoryMenu))
	{
		UInv_SpatialInventory* SpatialInv = Cast<UInv_SpatialInventory>(InventoryMenu);
		if (IsValid(SpatialInv) && IsValid(SpatialInv->GetGrid_Equippables()))
		{
			GridRows = SpatialInv->GetGrid_Equippables()->GetRows();
			GridColumns = SpatialInv->GetGrid_Equippables()->GetColumns();

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ✅ Grid (InventoryMenu 자동 - Grid_Equippables): %d x %d = %d칸"),
				GridRows, GridColumns, GridRows * GridColumns);
#endif
		}
#if INV_DEBUG_INVENTORY
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ⚠️ Grid 참조 없음 - 기본값 사용: %d x %d = %d칸"),
				GridRows, GridColumns, GridRows * GridColumns);
		}
#endif
	}
#if INV_DEBUG_INVENTORY
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ⚠️ InventoryMenu 없음 - 기본값 사용: %d x %d = %d칸"),
				GridRows, GridColumns, GridRows * GridColumns);
	}

	UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] 완료! 모든 카테고리(Equippables/Consumables/Craftables)가 동일한 크기 사용"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
#endif
}


//인벤토리 메뉴 위젯 생성 함수
void UInv_InventoryComponent::ConstructInventory()
{
	OwningController = Cast<APlayerController>(GetOwner());
	if (!OwningController.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] ConstructInventory: Owner가 PlayerController가 아님! Owner=%s"), *GetNameSafe(GetOwner()));
		return;
	}
	if (!OwningController->IsLocalController()) return;

	//블루프린터 위젯 클래스가 설정되어 있는지 확인
	if (!InventoryMenuClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] ConstructInventory: InventoryMenuClass가 설정되지 않음!"));
		return;
	}
	InventoryMenu = CreateWidget<UInv_InventoryBase>(OwningController.Get(), InventoryMenuClass);
	if (!IsValid(InventoryMenu))
	{
		UE_LOG(LogTemp, Error, TEXT("[Inventory] ConstructInventory: CreateWidget 실패!"));
		return;
	}
	InventoryMenu->AddToViewport();
	CloseInventoryMenu();
}

//인벤토리 메뉴 열기/닫기 함수
void UInv_InventoryComponent::OpenInventoryMenu()
{
	if (!IsValid(InventoryMenu)) return;

	// 방안 B: 다른 위젯(BuildMenu, CraftingMenu) 열려있으면 먼저 닫기
	CloseOtherMenus();

	InventoryMenu->SetVisibility(ESlateVisibility::Visible);
	bInventoryMenuOpen = true;

	if (!OwningController.IsValid()) return;

	FInputModeGameAndUI InputMode;
	OwningController->SetInputMode(InputMode);
	OwningController->SetShowMouseCursor(true);
}

void UInv_InventoryComponent::CloseInventoryMenu()
{
	if (!IsValid(InventoryMenu)) return;

	InventoryMenu->SetVisibility(ESlateVisibility::Collapsed);
	bInventoryMenuOpen = false;

	// ⭐ BuildMenu와 CraftingMenu도 함께 닫기
	CloseOtherMenus();

	if (!OwningController.IsValid()) return;

	FInputModeGameOnly InputMode;
	OwningController->SetInputMode(InputMode);
	OwningController->SetShowMouseCursor(false);
}

void UInv_InventoryComponent::CloseOtherMenus()
{
	if (!OwningController.IsValid() || !GetWorld()) return;

	// BuildMode(고스트 액터) + BuildMenu 닫기
	UInv_BuildingComponent* BuildingComp = OwningController->FindComponentByClass<UInv_BuildingComponent>();
	if (IsValid(BuildingComp))
	{
		BuildingComp->ForceEndBuildMode();
		BuildingComp->CloseBuildMenu();
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Log, TEXT("CloseOtherMenus: BuildMode/BuildMenu 닫힘"));
#endif
	}

	// CraftingMenu 닫기 (위젯 검색 방식)
	TArray<UUserWidget*> FoundWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), FoundWidgets, UUserWidget::StaticClass(), false);
	for (UUserWidget* Widget : FoundWidgets)
	{
		if (!IsValid(Widget)) continue;
		const FString WidgetClassName = Widget->GetClass()->GetName();
		if (WidgetClassName.Contains(TEXT("CraftingMenu")) || WidgetClassName.Contains(TEXT("Repair")))
		{
			Widget->RemoveFromParent();
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Log, TEXT("CloseOtherMenus: 위젯 닫힘: %s"), *WidgetClassName);
#endif
		}
	}
}

// ⭐ InventoryList 기반 공간 체크 (서버 전용, UI 없이 작동!)
bool UInv_InventoryComponent::Server_SplitItemEntry_Validate(UInv_InventoryItem* OriginalItem, int32 OriginalNewStackCount, int32 SplitStackCount, int32 TargetGridIndex)
{
	return OriginalNewStackCount > 0 && SplitStackCount > 0;
}

// ⭐ Phase 8: Split 시 서버에서 새 Entry 생성 (포인터 분리)
void UInv_InventoryComponent::Server_SplitItemEntry_Implementation(UInv_InventoryItem* OriginalItem, int32 OriginalNewStackCount, int32 SplitStackCount, int32 TargetGridIndex)
{
#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [SERVER] Server_SplitItemEntry_Implementation           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════╣"));
#endif

	if (!IsValid(OriginalItem))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("║ ❌ OriginalItem이 유효하지 않음!"));
		UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("║ 원본 Item: %s"), *OriginalItem->GetItemManifest().GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("║ 원본 현재 개수: %d"), OriginalItem->GetTotalStackCount());
	UE_LOG(LogTemp, Warning, TEXT("║ 원본 새 개수: %d"), OriginalNewStackCount);
	UE_LOG(LogTemp, Warning, TEXT("║ Split할 개수: %d"), SplitStackCount);
	UE_LOG(LogTemp, Warning, TEXT("║ ⭐ 목표 Grid 위치: %d"), TargetGridIndex);
#endif

	// 1. 원본 Entry의 TotalStackCount 감소
	OriginalItem->SetTotalStackCount(OriginalNewStackCount);

	// 2. 원본 Entry 찾아서 MarkItemDirty
	int32 OriginalEntryIndex = -1;
	for (int32 i = 0; i < InventoryList.Entries.Num(); i++)
	{
		if (InventoryList.Entries[i].Item == OriginalItem)
		{
			OriginalEntryIndex = i;
			InventoryList.MarkItemDirty(InventoryList.Entries[i]);
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("║ ✅ 원본 Entry[%d] MarkItemDirty 완료"), i);
#endif
			break;
		}
	}

	if (OriginalEntryIndex < 0)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("║ ❌ 원본 Entry를 찾지 못함!"));
		UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

	// ── 리슨서버 호스트: 원본 아이템 스택 수량 변경 UI 갱신 ──
	// MarkItemDirty는 데디서버 클라이언트에게는 리플리케이션되지만,
	// 리슨서버 호스트에게는 안 되므로 직접 Broadcast 필요
	if (IsListenServerOrStandalone())
	{
		OnItemAdded.Broadcast(OriginalItem, OriginalEntryIndex);
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("║ 🔧 리슨서버 호스트: 원본 아이템 스택 변경 OnItemAdded 브로드캐스트 (EntryIndex=%d, NewCount=%d)"),
			OriginalEntryIndex, OriginalNewStackCount);
#endif
	}

	// 3. 새 Item 생성 (새 포인터!)
	UInv_InventoryItem* NewItem = NewObject<UInv_InventoryItem>(this);
	if (!IsValid(NewItem))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Error, TEXT("║ ❌ 새 Item 생성 실패!"));
		UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

	// 4. 새 Item 초기화 (원본 Manifest 복사)
	NewItem->SetItemManifest(OriginalItem->GetItemManifest());
	NewItem->SetTotalStackCount(SplitStackCount);

	// 5. 새 Entry를 FastArray에 추가 (AddEntry 내부에서 AddRepSubObj도 호출됨)
	InventoryList.AddEntry(NewItem);

	int32 NewEntryIndex = InventoryList.Entries.Num() - 1;

	// ⭐ 7. 새 Entry에 TargetGridIndex 설정 (클라이언트에서 마우스 위치에 배치하기 위함)
	if (InventoryList.Entries.IsValidIndex(NewEntryIndex))
	{
		InventoryList.Entries[NewEntryIndex].TargetGridIndex = TargetGridIndex;
		InventoryList.MarkItemDirty(InventoryList.Entries[NewEntryIndex]);
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("║ ✅ 새 Entry[%d]에 TargetGridIndex=%d 설정 완료"), NewEntryIndex, TargetGridIndex);
#endif
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ 새 Entry[%d] 생성 완료!"), NewEntryIndex);
	UE_LOG(LogTemp, Warning, TEXT("║    새 Item 포인터: %p"), NewItem);
	UE_LOG(LogTemp, Warning, TEXT("║    새 Item 개수: %d"), SplitStackCount);
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif

	// 8. OnItemAdded 브로드캐스트 (리슨서버/스탠드얼론에서만 — 데디서버 클라는 PostReplicatedAdd가 처리)
	if (IsListenServerOrStandalone())
	{
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
	}
}

bool UInv_InventoryComponent::Server_UpdateItemGridPosition_Validate(UInv_InventoryItem* Item, int32 GridIndex, uint8 GridCategory, bool bRotated)
{
	return GridIndex >= INDEX_NONE && GridIndex < InvValidation::MaxGridIndex && GridCategory <= 2;
}

bool UInv_InventoryComponent::ApplyItemGridPositionSync(UInv_InventoryItem* Item, int32 GridIndex, uint8 GridCategory, bool bRotated)
{
	if (!IsValid(Item))
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[Server_UpdateItemGridPosition] Item이 유효하지 않음!"));
#endif
		return false;
	}

	// Entry 찾아서 GridIndex, GridCategory 업데이트
	for (int32 i = 0; i < InventoryList.Entries.Num(); i++)
	{
		if (InventoryList.Entries[i].Item == Item)
		{
			InventoryList.Entries[i].GridIndex = GridIndex;
			InventoryList.Entries[i].GridCategory = GridCategory;
			InventoryList.Entries[i].bRotated = bRotated;
			// Fix 11: MarkItemDirty 제거 — Server_UpdateItemGridPosition은 항상 클라→서버 RPC이므로
			// 클라이언트가 이미 올바른 GridIndex를 알고 있음. 서버→클라 역리플리케이션은 불필요하며,
			// PostReplicatedChange → AddItem → UpdateGridSlots → RPC 순환 오염의 원인.

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Log, TEXT("[Server_UpdateItemGridPosition] Entry[%d] 업데이트: %s → Grid%d Index=%d (MarkItemDirty 스킵)"),
				i, *Item->GetItemManifest().GetItemType().ToString(), GridCategory, GridIndex);
#endif
			return true;
		}
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[Server_UpdateItemGridPosition] Entry를 찾지 못함: %s"), *Item->GetItemManifest().GetItemType().ToString());
#endif
	return false;
}

// ⭐ [Phase 4 방법2] 클라이언트 Grid 위치를 서버 Entry에 동기화
void UInv_InventoryComponent::Server_UpdateItemGridPosition_Implementation(UInv_InventoryItem* Item, int32 GridIndex, uint8 GridCategory, bool bRotated)
{
	ApplyItemGridPositionSync(Item, GridIndex, GridCategory, bRotated);
}

bool UInv_InventoryComponent::Server_UpdateItemGridPositionsBatch_Validate(const TArray<FInv_GridPositionSyncData>& SyncRequests)
{
	if (SyncRequests.Num() > 128)
	{
		return false;
	}

	for (const FInv_GridPositionSyncData& Request : SyncRequests)
	{
		if (Request.GridIndex < INDEX_NONE || Request.GridCategory > 2)
		{
			return false;
		}
	}

	return true;
}

void UInv_InventoryComponent::Server_UpdateItemGridPositionsBatch_Implementation(const TArray<FInv_GridPositionSyncData>& SyncRequests)
{
	int32 AppliedCount = 0;

	for (const FInv_GridPositionSyncData& Request : SyncRequests)
	{
		if (ApplyItemGridPositionSync(Request.Item, Request.GridIndex, Request.GridCategory, Request.bRotated))
		{
			++AppliedCount;
		}
	}

#if INV_DEBUG_INVENTORY
	if (SyncRequests.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[Server_UpdateItemGridPositionsBatch] 요청=%d 적용=%d"), SyncRequests.Num(), AppliedCount);
	}
#endif
}

bool UInv_InventoryComponent::HasRoomInInventoryList(const FInv_ItemManifest& Manifest) const
{
	EInv_ItemCategory Category = Manifest.GetItemCategory();
	FGameplayTag ItemType = Manifest.GetItemType();

	// GridFragment에서 아이템 크기 가져오기
	const FInv_GridFragment* GridFragment = Manifest.GetFragmentOfType<FInv_GridFragment>();
	FIntPoint ItemSize = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 제작할 아이템: %s"), *ItemType.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 아이템 카테고리: %d"), (int32)Category);
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 아이템 크기: %d x %d"), ItemSize.X, ItemSize.Y);
#endif

	// ⭐ Grid 크기 설정 (Component 설정에서 가져오기)
	int32 LocalGridRows = GridRows;  // ⭐ 지역 변수로 복사 (const 함수에서 수정 가능)
	int32 LocalGridColumns = GridColumns;
	int32 MaxSlots = LocalGridRows * LocalGridColumns;
	UInv_InventoryGrid* TargetGrid = nullptr;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Component 설정: %d x %d = %d칸 (모든 카테고리 공통)"),
		LocalGridRows, LocalGridColumns, MaxSlots);
#endif
	
	// ⭐ InventoryMenu가 있으면 실제 Grid의 HasRoomForItem 사용 (더 정확함!)
	if (IsValid(InventoryMenu))
	{
		UInv_SpatialInventory* SpatialInv = Cast<UInv_SpatialInventory>(InventoryMenu);
		if (IsValid(SpatialInv))
		{
			switch (Category)
			{
			case EInv_ItemCategory::Equippable:
				TargetGrid = SpatialInv->GetGrid_Equippables();
				break;
			case EInv_ItemCategory::Consumable:
				TargetGrid = SpatialInv->GetGrid_Consumables();
				break;
			case EInv_ItemCategory::Craftable:
				TargetGrid = SpatialInv->GetGrid_Craftables();
				break;
#if INV_DEBUG_INVENTORY
			default:
				UE_LOG(LogTemp, Warning, TEXT("[공간체크] ⚠️ 알 수 없는 카테고리: %d"), (int32)Category);
				break;
#endif
			}

			if (IsValid(TargetGrid))
			{
				LocalGridRows = TargetGrid->GetRows();  // ⭐ 지역 변수 사용
				LocalGridColumns = TargetGrid->GetColumns();
				MaxSlots = TargetGrid->GetMaxSlots();

#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[공간체크] Grid 설정: %d x %d = %d칸"),
					LocalGridRows, LocalGridColumns, MaxSlots);
#endif

				// ⭐⭐⭐ 실제 UI GridSlots 상태 기반 공간 체크! (플레이어가 옮긴 위치 반영!)
				bool bHasRoom = TargetGrid->HasRoomInActualGrid(Manifest);

#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[공간체크] 🔍 Grid->HasRoomInActualGrid() 결과: %s"),
					bHasRoom ? TEXT("✅ 실제 UI Grid에 공간 있음!") : TEXT("❌ UI Grid 꽉 참!"));
				UE_LOG(LogTemp, Warning, TEXT("========================================"));
#endif

				return bHasRoom;
			}
		}
	}
#if INV_DEBUG_INVENTORY
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] ⚠️ InventoryMenu가 nullptr - Fallback 로직 사용"));
	}

	// ========== Fallback: Virtual Grid 시뮬레이션 (서버 전용) ==========
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Fallback 모드: Virtual Grid 시뮬레이션 시작"));
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Grid 크기: %d x %d = %d칸"), LocalGridRows, LocalGridColumns, MaxSlots);
#endif
	
	// Virtual Grid 생성 (0 = 빈 칸, 1~ = 아이템 인덱스)
	TArray<int32> VirtualGrid;
	VirtualGrid.SetNum(MaxSlots);
	for (int32 i = 0; i < MaxSlots; i++)
	{
		VirtualGrid[i] = 0; // 모두 빈 칸으로 초기화
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 📋 Virtual Grid 초기화 완료 (%dx%d)"), LocalGridRows, LocalGridColumns);

	// 1. 현재 인벤토리의 아이템들을 Virtual Grid에 배치
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 현재 인벤토리 내용을 Grid에 배치 중..."));
#endif

	int32 ItemIndex = 1; // 0은 빈 칸이므로 1부터 시작
	int32 CurrentItemCount = 0;

	for (const auto& Entry : InventoryList.Entries)
	{
		if (!IsValid(Entry.Item)) continue;

		if (Entry.Item->GetItemManifest().GetItemCategory() == Category)
		{
			const FInv_GridFragment* ItemGridFragment = Entry.Item->GetItemManifest().GetFragmentOfType<FInv_GridFragment>();
			FIntPoint ExistingItemSize = ItemGridFragment ? ItemGridFragment->GetGridSize() : FIntPoint(1, 1);

			FGameplayTag EntryType = Entry.Item->GetItemManifest().GetItemType();
			int32 StackCount = Entry.Item->GetTotalStackCount();

			// ⭐ 실제 Grid 위치 사용! (없으면 순차 배치 Fallback)
			FIntPoint ActualPos = Entry.Item->GetGridPosition();

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("[공간체크]   - [%d] %s x%d (크기: %dx%d, 실제위치: [%d,%d])"),
				CurrentItemCount, *EntryType.ToString(), StackCount, ExistingItemSize.X, ExistingItemSize.Y,
				ActualPos.X, ActualPos.Y);
#endif
			
			// Virtual Grid에 배치
			bool bPlaced = false;
			
			// ⭐ 실제 위치가 있으면 그대로 사용!
			if (ActualPos.X >= 0 && ActualPos.Y >= 0 &&
				ActualPos.X + ExistingItemSize.X <= LocalGridColumns &&
				ActualPos.Y + ExistingItemSize.Y <= LocalGridRows)
			{
				// 실제 위치에 배치 가능한지 체크
				bool bCanPlace = true;
				for (int32 y = 0; y < ExistingItemSize.Y && bCanPlace; y++)
				{
					for (int32 x = 0; x < ExistingItemSize.X && bCanPlace; x++)
					{
						int32 CheckIndex = (ActualPos.Y + y) * LocalGridColumns + (ActualPos.X + x);
						if (VirtualGrid[CheckIndex] != 0) // 이미 점유됨
						{
							bCanPlace = false;
						}
					}
				}
				
				if (bCanPlace)
				{
					// 실제 위치에 배치!
					for (int32 y = 0; y < ExistingItemSize.Y; y++)
					{
						for (int32 x = 0; x < ExistingItemSize.X; x++)
						{
							int32 PlaceIndex = (ActualPos.Y + y) * LocalGridColumns + (ActualPos.X + x);
							VirtualGrid[PlaceIndex] = ItemIndex;
						}
					}
					bPlaced = true;
#if INV_DEBUG_INVENTORY
					UE_LOG(LogTemp, Warning, TEXT("[공간체크]     → ✅ 실제 위치 Grid[%d,%d]에 배치됨"), ActualPos.X, ActualPos.Y);
#endif
				}
			}

			// ⚠️ 실제 위치가 없거나 배치 실패하면 순차 배치 시도 (Fallback)
			if (!bPlaced)
			{
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[공간체크]     → ⚠️ 실제 위치 사용 불가! Fallback 순차 배치 시도..."));
#endif
				
				for (int32 Row = 0; Row <= LocalGridRows - ExistingItemSize.Y && !bPlaced; Row++)
				{
					for (int32 Col = 0; Col <= LocalGridColumns - ExistingItemSize.X && !bPlaced; Col++)
					{
						int32 StartIndex = Row * LocalGridColumns + Col;
						
						// 이 위치에 배치 가능한지 체크
						bool bCanPlace = true;
						for (int32 y = 0; y < ExistingItemSize.Y && bCanPlace; y++)
						{
							for (int32 x = 0; x < ExistingItemSize.X && bCanPlace; x++)
							{
								int32 CheckIndex = (Row + y) * LocalGridColumns + (Col + x);
								if (VirtualGrid[CheckIndex] != 0) // 이미 점유됨
								{
									bCanPlace = false;
								}
							}
						}
						
						// 배치 가능하면 Grid에 표시
						if (bCanPlace)
						{
							for (int32 y = 0; y < ExistingItemSize.Y; y++)
							{
								for (int32 x = 0; x < ExistingItemSize.X; x++)
								{
									int32 PlaceIndex = (Row + y) * LocalGridColumns + (Col + x);
									VirtualGrid[PlaceIndex] = ItemIndex;
								}
							}
							bPlaced = true;
#if INV_DEBUG_INVENTORY
							UE_LOG(LogTemp, Warning, TEXT("[공간체크]     → Fallback Grid[%d,%d]에 배치됨"), Col, Row);
#endif
						}
					}
				}
			}

#if INV_DEBUG_INVENTORY
			if (!bPlaced)
			{
				UE_LOG(LogTemp, Error, TEXT("[공간체크]     → ❌ 배치 실패! (Grid 시뮬레이션 오류 가능성)"));
			}
#endif

			ItemIndex++;
			CurrentItemCount++;
		}
	}

#if INV_DEBUG_INVENTORY
	// 2. Virtual Grid 상태 출력
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 📊 현재 Virtual Grid 상태:"));
	for (int32 Row = 0; Row < LocalGridRows; Row++)
	{
		FString RowStr = TEXT("  ");
		for (int32 Col = 0; Col < LocalGridColumns; Col++)
		{
			int32 Value = VirtualGrid[Row * LocalGridColumns + Col];
			RowStr += FString::Printf(TEXT("[%d]"), Value);
		}
		UE_LOG(LogTemp, Warning, TEXT("%s"), *RowStr);
	}
#endif

	// 3. 스택 가능 여부 체크
	const FInv_StackableFragment* StackableFragment = Manifest.GetFragmentOfType<FInv_StackableFragment>();
	bool bStackable = (StackableFragment != nullptr);

	if (bStackable)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] 🔍 스택 가능 아이템 - 기존 스택 찾기 중..."));
#endif
		for (const auto& Entry : InventoryList.Entries)
		{
			if (!IsValid(Entry.Item)) continue;

			if (Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(ItemType) &&
				Entry.Item->GetItemManifest().GetItemCategory() == Category)
			{
				int32 CurrentStack = Entry.Item->GetTotalStackCount();
				int32 MaxStack = StackableFragment->GetMaxStackSize();

				if (CurrentStack < MaxStack)
				{
#if INV_DEBUG_INVENTORY
					UE_LOG(LogTemp, Warning, TEXT("[공간체크] ✅ 스택 가능! (현재: %d / 최대: %d)"), CurrentStack, MaxStack);
					UE_LOG(LogTemp, Warning, TEXT("========================================"));
#endif
					return true;
				}
			}
		}
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] ⚠️ 스택 여유 없음 - 새 슬롯 필요"));
#endif
	}

#if INV_DEBUG_INVENTORY
	// 4. 새로운 아이템을 배치할 수 있는지 체크
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 🔍 새 아이템 배치 가능 여부 체크 (크기: %dx%d)"), ItemSize.X, ItemSize.Y);
#endif
	
	bool bHasRoom = false;
	for (int32 Row = 0; Row <= LocalGridRows - ItemSize.Y && !bHasRoom; Row++)
	{
		for (int32 Col = 0; Col <= LocalGridColumns - ItemSize.X && !bHasRoom; Col++)
		{
			bool bCanPlace = true;
			
			// 이 위치에 배치 가능한지 체크
			for (int32 y = 0; y < ItemSize.Y && bCanPlace; y++)
			{
				for (int32 x = 0; x < ItemSize.X && bCanPlace; x++)
				{
					int32 CheckIndex = (Row + y) * LocalGridColumns + (Col + x);
					if (VirtualGrid[CheckIndex] != 0) // 이미 점유됨
					{
						bCanPlace = false;
					}
				}
			}
			
			if (bCanPlace)
			{
				bHasRoom = true;
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("[공간체크] ✅ 배치 가능! Grid[%d,%d]부터 %dx%d 공간 확보됨"),
					Col, Row, ItemSize.X, ItemSize.Y);
#endif
			}
		}
	}

#if INV_DEBUG_INVENTORY
	if (!bHasRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] ❌ Grid 꽉 참! %dx%d 크기의 빈 공간 없음"), ItemSize.X, ItemSize.Y);
	}

	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Virtual Grid 결과: %s"),
		bHasRoom ? TEXT("✅ 공간 있음") : TEXT("❌ 공간 없음"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
#endif

	return bHasRoom;
}

// ============================================
// ⭐ [Phase 4 개선] 서버에서 직접 인벤토리 데이터 수집
// ============================================
// 
// 📌 목적: Logout 시 RPC 없이 즉시 저장 가능하게!
// 
// 📌 기존 문제점:
//    - 캐시에 의존 → 자동저장 전에 나가면 저장 안 됨
//    - Client RPC 필요 → 연결 끊기면 못 받음
// 
// 📌 해결책:
//    - 서버의 FastArray에서 직접 데이터 읽기
//    - GridIndex, GridCategory 모두 서버에 있음!
// 
// ============================================
// ⭐ [Phase 5 Fix] 마지막으로 추가된 Entry의 Grid 위치 설정
// 로드 시 저장된 위치를 Entry에 미리 설정하여 클라이언트가 올바른 위치에 배치하도록 함
// ============================================
void UInv_InventoryComponent::SetLastEntryGridPosition(int32 GridIndex, uint8 GridCategory)
{
	if (InventoryList.Entries.Num() > 0)
	{
		int32 LastEntryIndex = InventoryList.Entries.Num() - 1;
		FInv_InventoryEntry& Entry = InventoryList.Entries[LastEntryIndex];
		
		Entry.GridIndex = GridIndex;
		Entry.GridCategory = GridCategory;
		InventoryList.MarkItemDirty(Entry);

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Log, TEXT("[Phase 5 Fix] Entry[%d] GridIndex=%d, Category=%d set (saved pos)"),
			LastEntryIndex, GridIndex, GridCategory);
#endif
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 [부착물 시스템 Phase 3] Entry Index 검색 헬퍼
// ════════════════════════════════════════════════════════════════
int32 UInv_InventoryComponent::FindEntryIndexForItem(const UInv_InventoryItem* Item) const
{
	if (!IsValid(Item)) return INDEX_NONE;

	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == Item)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

// ============================================
// ============================================
// 🆕 [Phase 6] ItemType으로 아이템 찾기
// ============================================
UInv_InventoryItem* UInv_InventoryComponent::FindItemByType(const FGameplayTag& ItemType)
{
	return InventoryList.FindFirstItemByType(ItemType);
}

// 🆕 [Phase 6] 제외 목록을 사용한 아이템 검색 (같은 타입 다중 장착 지원)
UInv_InventoryItem* UInv_InventoryComponent::FindItemByTypeExcluding(const FGameplayTag& ItemType, const TSet<UInv_InventoryItem*>& ExcludeItems)
{
	const TArray<FInv_InventoryEntry>& Entries = InventoryList.Entries;
	
	for (const FInv_InventoryEntry& Entry : Entries)
	{
		if (!Entry.Item) continue;
		
		// 제외 목록에 있는 아이템은 건너뜀
		if (ExcludeItems.Contains(Entry.Item)) continue;
		
		// 타입 매칭
		if (Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(ItemType))
		{
			return Entry.Item;
		}
	}
	
	return nullptr;
}

TArray<FInv_SavedItemData> UInv_InventoryComponent::CollectInventoryDataForSave() const
{
	TArray<FInv_SavedItemData> Result;
	int32 SavedEquippedCount = 0;
	int32 SavedGridCount = 0;
	int32 SavedAttachmentCount = 0;
	int32 SkippedAttachedCount = 0;
	int32 SkippedNullCount = 0;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [Phase 4] CollectInventoryDataForSave - 서버 직접 수집     ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
#endif

	// FastArray의 Entries 순회
	const TArray<FInv_InventoryEntry>& Entries = InventoryList.Entries;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("║ Entry 개수: %d                                             ║"), Entries.Num());
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
#endif

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		const FInv_InventoryEntry& Entry = Entries[i];
		
		// Item 유효성 체크
		if (!Entry.Item)
		{
			++SkippedNullCount;
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("║ [%d] ⚠️ Item nullptr - 스킵                               ║"), i);
#endif
			continue;
		}

		// ⭐ [부착물 시스템] 무기에 부착된 아이템은 저장 스킵
		// 부착물 데이터는 무기의 SavedItem.Attachments에 이미 포함됨
		if (Entry.bIsAttachedToWeapon)
		{
			++SkippedAttachedCount;
#if INV_DEBUG_ATTACHMENT
			UE_LOG(LogTemp, Log, TEXT("║ [%d] bIsAttachedToWeapon=true → 저장 스킵 (무기 Attachments에 포함됨)"), i);
#endif
			continue;
		}

		// Item 데이터 추출
		const FInv_ItemManifest& Manifest = Entry.Item->GetItemManifest();
		FGameplayTag ItemType = Manifest.GetItemType();
		int32 StackCount = Entry.Item->GetTotalStackCount();
		int32 GridIndex = Entry.GridIndex;
		uint8 GridCategory = Entry.GridCategory;

		// ⭐ [Fix11] 비스택(장비) 아이템은 TotalStackCount가 0일 수 있음
		// Stackable Fragment가 없는 아이템은 "존재 = 1개"이므로 최소 1로 보정
		if (StackCount <= 0 && !Entry.Item->IsStackable())
		{
			StackCount = 1;
			UE_LOG(LogTemp, Log, TEXT("[Fix11] Entry[%d] %s: 비스택 아이템 StackCount 0→1 보정"), i, *ItemType.ToString());
		}

#if INV_DEBUG_ITEM_POINTER
		// ── [포인터 진단] Entry별 아이템 포인터 & 부착물 상태 추적 ──
		{
			const FInv_AttachmentHostFragment* DiagHostFrag = Manifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
			int32 DiagAttachedCount = DiagHostFrag ? DiagHostFrag->GetAttachedItems().Num() : -1;

			UE_LOG(LogTemp, Warning, TEXT("[ItemPointer] Entry[%d] %s | Item=%p | bIsEquipped=%s | bIsAttachedToWeapon=%s | HasSlots=%s | HostFrag=%p | AttachedItems=%d"),
				i, *ItemType.ToString(),
				Entry.Item.Get(),
				Entry.bIsEquipped ? TEXT("Y") : TEXT("N"),
				Entry.bIsAttachedToWeapon ? TEXT("Y") : TEXT("N"),
				Entry.Item->HasAttachmentSlots() ? TEXT("Y") : TEXT("N"),
				DiagHostFrag,
				DiagAttachedCount);

			if (DiagHostFrag && DiagAttachedCount > 0)
			{
				for (int32 d = 0; d < DiagHostFrag->GetAttachedItems().Num(); d++)
				{
					const FInv_AttachedItemData& DiagAtt = DiagHostFrag->GetAttachedItems()[d];
					UE_LOG(LogTemp, Warning, TEXT("[ItemPointer]   └ 부착물[%d] Slot=%d, Type=%s, OriginalItem=%p"),
						d, DiagAtt.SlotIndex,
						*DiagAtt.AttachmentItemType.ToString(),
						DiagAtt.OriginalItem.Get());
				}
			}
		}
#endif

		// GridIndex → GridPosition 변환 (Column = X, Row = Y)
		// 기본값 8 columns 사용 (서버에서는 실제 Grid 크기를 모를 수 있음)
		int32 LocalGridColumns = GridColumns > 0 ? GridColumns : 8;
		FIntPoint GridPosition;
		
		if (GridIndex != INDEX_NONE && GridIndex >= 0)
		{
			GridPosition.X = GridIndex % LocalGridColumns;  // Column
			GridPosition.Y = GridIndex / LocalGridColumns;  // Row
		}
		else
		{
			GridPosition = FIntPoint(-1, -1);  // 미배치
		}

		// [Fix10-Save진단] 저장 시 GridPosition + StackCount 출처 확인
		UE_LOG(LogTemp, Error, TEXT("[Fix10-Save진단] Entry[%d] %s: StackCount=%d, Entry.GridIndex=%d, GridColumns=%d, SavedItem.GridPosition=(%d,%d), GridCat=%d"),
			i, *ItemType.ToString(),
			StackCount,
			GridIndex, LocalGridColumns,
			GridPosition.X, GridPosition.Y,
			GridCategory);

		// FInv_SavedItemData 생성
		FInv_SavedItemData SavedItem(ItemType, StackCount, GridPosition, GridCategory);

		// [Fix14] Entry의 장착 상태를 SavedItem에 전파
		SavedItem.bEquipped = Entry.bIsEquipped;
		SavedItem.WeaponSlotIndex = Entry.WeaponSlotIndex;
		SavedItem.bRotated = Entry.bRotated;

		// ── [Phase 6 Attachment] 부착물 데이터 수집 ──
		// 무기 아이템인 경우 AttachmentHostFragment의 AttachedItems 수집
		if (Entry.Item->HasAttachmentSlots())
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("🔍 [SaveDiag] Entry[%d] %s - HasAttachmentSlots=TRUE"), i, *ItemType.ToString());
#endif
			const FInv_ItemManifest& ItemManifest = Entry.Item->GetItemManifest();
			const FInv_AttachmentHostFragment* HostFrag = ItemManifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
			if (HostFrag)
			{
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Error, TEXT("🔍 [SaveDiag] Entry[%d] HostFrag 유효! AttachedItems=%d"), i, HostFrag->GetAttachedItems().Num());
#endif
				for (const FInv_AttachedItemData& Attached : HostFrag->GetAttachedItems())
				{
					FInv_SavedAttachmentData AttSave;
					AttSave.AttachmentItemType = Attached.AttachmentItemType;
					AttSave.SlotIndex = Attached.SlotIndex;

					// AttachableFragment에서 AttachmentType 추출
					const FInv_AttachableFragment* AttachableFrag =
						Attached.ItemManifestCopy.GetFragmentOfType<FInv_AttachableFragment>();
					if (AttachableFrag)
					{
						AttSave.AttachmentType = AttachableFrag->GetAttachmentType();
					}

					SavedItem.Attachments.Add(AttSave);
				}

#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("║ [%d]   → 부착물 %d개 수집"), i, SavedItem.Attachments.Num());
#endif
			}
		}

		// ════════════════════════════════════════════════════════════════
		// 📌 [Phase 1 최적화] Fragment 직렬화 — 랜덤 스탯 보존
		// ════════════════════════════════════════════════════════════════
		// 아이템의 전체 Fragment 데이터를 바이너리로 직렬화
		// 로드 시 DeserializeAndApplyFragments()로 복원
		// ════════════════════════════════════════════════════════════════
		{
			const FInv_ItemManifest& ItemManifest = Entry.Item->GetItemManifest();
			SavedItem.SerializedManifest = ItemManifest.SerializeFragments();

#if INV_DEBUG_SAVE
			UE_LOG(LogTemp, Warning,
				TEXT("║ [%d] 📦 [Phase 1 최적화] Fragment 직렬화: %s → %d바이트"),
				i, *ItemType.ToString(), SavedItem.SerializedManifest.Num());
#endif

			// 부착물의 Fragment도 각각 직렬화
			const FInv_AttachmentHostFragment* SerializeHostFrag = ItemManifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
			if (SerializeHostFrag)
			{
				for (int32 AttIdx = 0; AttIdx < SavedItem.Attachments.Num(); ++AttIdx)
				{
					FInv_SavedAttachmentData& AttSave = SavedItem.Attachments[AttIdx];

					// HostFrag의 AttachedItems에서 해당 슬롯의 ManifestCopy를 찾아 직렬화
					const FInv_AttachedItemData* AttachedData = SerializeHostFrag->GetAttachedItemData(AttSave.SlotIndex);
					if (AttachedData)
					{
						AttSave.SerializedManifest = AttachedData->ItemManifestCopy.SerializeFragments();

#if INV_DEBUG_SAVE
						UE_LOG(LogTemp, Warning,
							TEXT("║ [%d]   📦 부착물[%d] Fragment 직렬화: %s → %d바이트"),
							i, AttIdx, *AttSave.AttachmentItemType.ToString(),
							AttSave.SerializedManifest.Num());
#endif
					}
				}
			}
		}

		Result.Add(SavedItem);
		if (SavedItem.bEquipped)
		{
			++SavedEquippedCount;
		}
		else
		{
			++SavedGridCount;
		}
		SavedAttachmentCount += SavedItem.Attachments.Num();

#if INV_DEBUG_ITEM_POINTER
		UE_LOG(LogTemp, Warning, TEXT("[ItemPointer] → 저장 완료: Entry[%d] %s | Attachments=%d | bEquipped=%s"),
			i, *ItemType.ToString(), SavedItem.Attachments.Num(),
			Entry.bIsEquipped ? TEXT("Y") : TEXT("N"));
#endif

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("║ [%d] %s x%d @ Grid%d [%d,%d] (Cat:%d)"),
			i,
			*ItemType.ToString(),
			StackCount,
			GridIndex,
			GridPosition.X, GridPosition.Y,
			GridCategory);
#endif
	}

#if INV_DEBUG_ITEM_POINTER
	// ── [포인터 진단] 최종 요약 ──
	{
		int32 TotalAttachments = 0;
		int32 EquippedCount = 0;
		for (const FInv_SavedItemData& S : Result)
		{
			TotalAttachments += S.Attachments.Num();
			if (S.bEquipped) EquippedCount++;
		}
		UE_LOG(LogTemp, Warning, TEXT("[ItemPointer] ═══ 저장 요약: 아이템 %d개, 장착 %d개, 부착물 총 %d개 ═══"),
			Result.Num(), EquippedCount, TotalAttachments);
	}
#endif

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ 수집 완료! 총 %d개 아이템                                ║"), Result.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning,
		TEXT("[SaveFlow] CollectInventoryDataForSave | Owner=%s | Saved=%d | Grid=%d | Equipped=%d | Attachments=%d | SkippedNull=%d | SkippedAttached=%d"),
		*GetNameSafe(GetOwner()),
		Result.Num(),
		SavedGridCount,
		SavedEquippedCount,
		SavedAttachmentCount,
		SkippedNullCount,
		SkippedAttachedCount);
#endif
	return Result;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 4 Lobby] TransferItemTo — 크로스 컴포넌트 아이템 전송
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//   이 InvComp(Source)에서 아이템을 꺼내 TargetComp에 넣는다.
//   로비에서 Stash↔Loadout 간 아이템 이동에 사용.
//
// 📌 왜 이 함수가 플러그인(Inventory 모듈) 안에 있는가?
//   FInv_InventoryFastArray는 USTRUCT이며 INVENTORY_API 매크로가 없음
//   → InventoryList.Entries, Entry.Item 등 private 멤버에 외부 모듈(Helluna)에서 접근 불가
//   → LNK2019 unresolved external 에러 발생
//   → 해결: UInv_InventoryComponent는 INVENTORY_API UCLASS이므로 DLL export됨
//     + InventoryList의 friend 접근 가능
//     → 이 함수에서 FastArray 조작을 수행
//
// 📌 처리 순서:
//   1) Authority 체크 (서버에서만 FastArray 수정 가능)
//   2) InventoryList.Entries를 순회하여 유효한 아이템 목록 구축
//      - IsValid(Entry.Item) && !Entry.bIsAttachedToWeapon
//      - bIsAttachedToWeapon: 무기에 부착된 부착물은 전송 대상에서 제외
//   3) ItemIndex번째 유효 아이템의 Manifest + StackCount 추출
//   4) Manifest를 복사하여 TargetComp->AddItemFromManifest()로 대상에 추가
//      - 새 UInv_InventoryItem이 생성되고 FastArray에 추가됨
//      - 실패 시(공간 부족 등) nullptr 반환 → 전송 중단 (원본 유지)
//   5) Source의 InventoryList.RemoveEntry()로 원본 제거
//      - FastArray MarkItemDirty → 리플리케이션 트리거
//
// 📌 리플리케이션:
//   AddItemFromManifest + RemoveEntry가 각각 FastArray를 Dirty 마킹
//   → 다음 리플리케이션 프레임에 클라이언트에 자동 동기화
//   → 클라이언트의 Grid가 OnItemAdded/OnItemRemoved 델리게이트로 UI 자동 업데이트
//
// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
// ════════════════════════════════════════════════════════════════════════════════
bool UInv_InventoryComponent::TransferItemTo(int32 ItemIndex, UInv_InventoryComponent* TargetComp, int32 TargetGridIndex)
{
	// ── Authority 체크 (서버 전용) ──
	if (!TargetComp || !GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] TransferItemTo: 조건 미충족!"));
		UE_LOG(LogTemp, Warning, TEXT("[InvComp]   TargetComp=%s | Owner=%s | HasAuthority=%s"),
			TargetComp ? TEXT("O") : TEXT("X"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("nullptr"),
			(GetOwner() && GetOwner()->HasAuthority()) ? TEXT("Y") : TEXT("N (클라에서 직접 호출 불가!)"));
		return false;
	}

	// ── 1) 유효한 아이템 목록 구축 ──
	// Entries에서 무효(IsValid 실패) + 부착물(bIsAttachedToWeapon) 제외
	TArray<UInv_InventoryItem*> ValidItems;
	for (const FInv_InventoryEntry& Entry : InventoryList.Entries)
	{
		if (IsValid(Entry.Item) && !Entry.bIsAttachedToWeapon)
		{
			ValidItems.Add(Entry.Item);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[InvComp] TransferItemTo: 유효 아이템 %d개 (전체 Entry %d개) | 요청 Index=%d"),
		ValidItems.Num(), InventoryList.Entries.Num(), ItemIndex);

	// ── 2) 인덱스 검증 ──
	if (!ValidItems.IsValidIndex(ItemIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] TransferItemTo: 잘못된 ItemIndex=%d (유효 범위: 0~%d)"),
			ItemIndex, ValidItems.Num() - 1);
		return false;
	}

	UInv_InventoryItem* Item = ValidItems[ItemIndex];
	if (!IsValid(Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] TransferItemTo: Item이 IsValid 실패!"));
		return false;
	}

	// ── 3) 아이템 정보 추출 ──
	const FInv_ItemManifest& SourceManifest = Item->GetItemManifest();
	const int32 StackCount = Item->GetTotalStackCount();
	const FGameplayTag ItemType = SourceManifest.GetItemType();

	UE_LOG(LogTemp, Log, TEXT("[InvComp] TransferItemTo: %s x%d | Source=%s → Target=%s"),
		*ItemType.ToString(), StackCount, *GetName(), *TargetComp->GetName());

	// ── 3-1) 대상 인벤토리 용량 체크 ──
	// 꽉 찬 그리드에 아이템 전송 방지
	if (!TargetComp->HasRoomInInventoryList(SourceManifest))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] TransferItemTo: 대상 인벤토리 공간 부족! %s 전송 불가"), *ItemType.ToString());
		return false;
	}

	// ── 3-2) 전송 전 소스 Entry의 GridCategory 캡처 (Target Entry에 설정용) ──
	uint8 SourceGridCategory = 0;
	for (const FInv_InventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Item == Item)
		{
			SourceGridCategory = Entry.GridCategory;
			break;
		}
	}

	// ── 4) Manifest 복사 → Target에 추가 ──
	// AddItemFromManifest: 새 UInv_InventoryItem 생성 → FastArray 추가 → MarkDirty
	FInv_ItemManifest ManifestCopy = SourceManifest;
	UInv_InventoryItem* NewItem = TargetComp->AddItemFromManifest(ManifestCopy, StackCount);
	if (!NewItem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] TransferItemTo: Target 추가 실패!"));
		UE_LOG(LogTemp, Warning, TEXT("[InvComp]   → Target에 공간이 부족하거나 아이템 생성 실패"));
		UE_LOG(LogTemp, Warning, TEXT("[InvComp]   → ItemType=%s, StackCount=%d"), *ItemType.ToString(), StackCount);
		return false;
	}

	// ── 4-1) [Fix31] 새 Entry에 TargetGridIndex 설정 → 리플리케이션으로 클라이언트가 해당 위치에 배치 ──
	if (TargetGridIndex != INDEX_NONE)
	{
		bool bFound = false;
		for (int32 i = 0; i < TargetComp->InventoryList.Entries.Num(); ++i)
		{
			FInv_InventoryEntry& Entry = TargetComp->InventoryList.Entries[i];
			if (Entry.Item == NewItem)
			{
				Entry.GridIndex = TargetGridIndex;
				Entry.GridCategory = SourceGridCategory;
				TargetComp->InventoryList.MarkItemDirty(Entry);
				UE_LOG(LogTemp, Warning, TEXT("[InvComp-Fix31진단] TransferItemTo: Entry[%d] GridIndex=%d→%d, GridCategory=%d 설정 완료 | Target=%s"),
					i, Entry.GridIndex, TargetGridIndex, SourceGridCategory, *TargetComp->GetName());
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			UE_LOG(LogTemp, Error, TEXT("[InvComp-Fix31진단] TransferItemTo: NewItem을 TargetComp에서 찾지 못함! TargetGridIndex=%d"), TargetGridIndex);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp-Fix31진단] TransferItemTo: TargetGridIndex=INDEX_NONE → 자동 배치"));
	}

	// ── 5) Source에서 제거 ──
	// RemoveEntry: FastArray에서 해당 Entry 제거 → MarkDirty → 리플리케이션
	//
	// [Phase 4 Fix] EntryIndex를 미리 저장 — RemoveEntry 후 OnItemRemoved를 수동 broadcast
	// RemoveEntry는 OnItemRemoved를 broadcast하지 않음 (PreReplicatedRemove에 의존)
	// Standalone/ListenServer에서는 즉시 리플리케이션이 안 돌아서 Grid가 갱신 안 됨
	int32 RemovedEntryIndex = INDEX_NONE;
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == Item)
		{
			RemovedEntryIndex = i;
			break;
		}
	}

	InventoryList.RemoveEntry(Item);

	// 수동 broadcast — Grid가 즉시 아이템을 제거하도록
	if (RemovedEntryIndex != INDEX_NONE)
	{
		OnItemRemoved.Broadcast(Item, RemovedEntryIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("[InvComp] TransferItemTo 완료: %s x%d | %s → %s"),
		*ItemType.ToString(), StackCount, *GetName(), *TargetComp->GetName());
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════
// [CrossSwap] SwapItemWith — 두 InvComp 간 아이템 교환
// ════════════════════════════════════════════════════════════════════════════════
//
// TransferItemTo를 두 번 호출하면 HasRoomInInventoryList가 Swap을 차단함
// (꽉 찬 Grid에 아이템 추가 불가). 따라서:
//   1) 양쪽 Manifest 수집
//   2) 양쪽 아이템 제거
//   3) 교차 추가
//   4) 실패 시 롤백
//
// ════════════════════════════════════════════════════════════════════════════════
bool UInv_InventoryComponent::SwapItemWith(int32 MyItemIndex, UInv_InventoryComponent* OtherComp, int32 OtherItemIndex, int32 TargetGridIndex)
{
	if (!OtherComp || !GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] SwapItemWith: 조건 미충족!"));
		return false;
	}

	// ── 1) ValidItems 구축 (양쪽) ──
	auto BuildValidItems = [](UInv_InventoryComponent* Comp) -> TArray<UInv_InventoryItem*>
	{
		TArray<UInv_InventoryItem*> Items;
		for (const FInv_InventoryEntry& Entry : Comp->InventoryList.Entries)
		{
			if (IsValid(Entry.Item) && !Entry.bIsAttachedToWeapon)
			{
				Items.Add(Entry.Item);
			}
		}
		return Items;
	};

	TArray<UInv_InventoryItem*> MyValidItems = BuildValidItems(this);
	TArray<UInv_InventoryItem*> OtherValidItems = BuildValidItems(OtherComp);

	if (!MyValidItems.IsValidIndex(MyItemIndex) || !OtherValidItems.IsValidIndex(OtherItemIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] SwapItemWith: 인덱스 범위 초과! MyIndex=%d/%d, OtherIndex=%d/%d"),
			MyItemIndex, MyValidItems.Num(), OtherItemIndex, OtherValidItems.Num());
		return false;
	}

	UInv_InventoryItem* MyItem = MyValidItems[MyItemIndex];
	UInv_InventoryItem* OtherItem = OtherValidItems[OtherItemIndex];

	if (!IsValid(MyItem) || !IsValid(OtherItem))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InvComp] SwapItemWith: Item이 IsValid 실패!"));
		return false;
	}

	// ── 2) Manifest & StackCount 수집 (제거 전에!) ──
	FInv_ItemManifest MyManifest = MyItem->GetItemManifest();
	const int32 MyStackCount = MyItem->GetTotalStackCount();
	const FGameplayTag MyType = MyManifest.GetItemType();

	FInv_ItemManifest OtherManifest = OtherItem->GetItemManifest();
	const int32 OtherStackCount = OtherItem->GetTotalStackCount();
	const FGameplayTag OtherType = OtherManifest.GetItemType();

	// [Fix30-C] 제거 전 양쪽 Entry의 위치 정보 캡처 (교차 할당용)
	int32 MyGridIndex = INDEX_NONE;
	uint8 MyGridCategory = 0;
	bool bMyRotated = false;
	int32 OtherGridIndex = INDEX_NONE;
	uint8 OtherGridCategory = 0;
	bool bOtherRotated = false;

	for (const FInv_InventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Item == MyItem)
		{
			MyGridIndex = Entry.GridIndex;
			MyGridCategory = Entry.GridCategory;
			bMyRotated = Entry.bRotated;
			break;
		}
	}
	for (const FInv_InventoryEntry& Entry : OtherComp->InventoryList.Entries)
	{
		if (Entry.Item == OtherItem)
		{
			OtherGridIndex = Entry.GridIndex;
			OtherGridCategory = Entry.GridCategory;
			bOtherRotated = Entry.bRotated;
			break;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[InvComp] SwapItemWith: %s x%d (%s) ↔ %s x%d (%s)"),
		*MyType.ToString(), MyStackCount, *GetName(),
		*OtherType.ToString(), OtherStackCount, *OtherComp->GetName());
	UE_LOG(LogTemp, Log, TEXT("[InvComp] SwapItemWith 위치캡처: My(Grid=%d,Cat=%d,Rot=%d) Other(Grid=%d,Cat=%d,Rot=%d)"),
		MyGridIndex, MyGridCategory, bMyRotated, OtherGridIndex, OtherGridCategory, bOtherRotated);

	// ── 3) 양쪽 아이템 제거 ──
	// MyItem 제거
	int32 MyRemovedIdx = INDEX_NONE;
	for (int32 i = 0; i < InventoryList.Entries.Num(); ++i)
	{
		if (InventoryList.Entries[i].Item == MyItem)
		{
			MyRemovedIdx = i;
			break;
		}
	}
	InventoryList.RemoveEntry(MyItem);
	if (MyRemovedIdx != INDEX_NONE)
	{
		OnItemRemoved.Broadcast(MyItem, MyRemovedIdx);
	}

	// OtherItem 제거
	int32 OtherRemovedIdx = INDEX_NONE;
	for (int32 i = 0; i < OtherComp->InventoryList.Entries.Num(); ++i)
	{
		if (OtherComp->InventoryList.Entries[i].Item == OtherItem)
		{
			OtherRemovedIdx = i;
			break;
		}
	}
	OtherComp->InventoryList.RemoveEntry(OtherItem);
	if (OtherRemovedIdx != INDEX_NONE)
	{
		OtherComp->OnItemRemoved.Broadcast(OtherItem, OtherRemovedIdx);
	}

	// ── 4) 교차 추가 ──
	// MyItem의 Manifest → OtherComp에 추가
	UInv_InventoryItem* NewItemInOther = OtherComp->AddItemFromManifest(MyManifest, MyStackCount);
	// OtherItem의 Manifest → 이 Comp에 추가
	UInv_InventoryItem* NewItemInMe = AddItemFromManifest(OtherManifest, OtherStackCount);

	// ── 5) 실패 시 롤백 ──
	if (!NewItemInOther || !NewItemInMe)
	{
		UE_LOG(LogTemp, Error, TEXT("[InvComp] SwapItemWith: 교차 추가 실패! 롤백 시도"));

		// 추가된 것이 있으면 제거
		if (NewItemInOther)
		{
			OtherComp->InventoryList.RemoveEntry(NewItemInOther);
		}
		if (NewItemInMe)
		{
			InventoryList.RemoveEntry(NewItemInMe);
		}

		// 원본 복원
		AddItemFromManifest(MyManifest, MyStackCount);
		OtherComp->AddItemFromManifest(OtherManifest, OtherStackCount);

		UE_LOG(LogTemp, Error, TEXT("[InvComp] SwapItemWith: 롤백 완료 — 원본 상태 복원"));
		return false;
	}

	// [Fix30-C] 교차 위치 할당: 각 새 아이템은 같은 컴포넌트에서 제거된 아이템의 위치를 상속
	// NewItemInMe (OtherItem 데이터) → MyItem이 있던 자리 (MyGridIndex, MyGridCategory)
	for (FInv_InventoryEntry& Entry : InventoryList.Entries)
	{
		if (Entry.Item == NewItemInMe)
		{
			Entry.GridIndex = MyGridIndex;
			Entry.GridCategory = MyGridCategory;
			Entry.bRotated = bMyRotated;
			InventoryList.MarkItemDirty(Entry);
			break;
		}
	}
	// NewItemInOther (MyItem 데이터) → OtherItem이 있던 자리 (OtherGridIndex, OtherGridCategory)
	// [Fix31] TargetGridIndex가 지정되면 해당 위치로 오버라이드 (유저가 드롭한 위치)
	for (FInv_InventoryEntry& Entry : OtherComp->InventoryList.Entries)
	{
		if (Entry.Item == NewItemInOther)
		{
			Entry.GridIndex = (TargetGridIndex != INDEX_NONE) ? TargetGridIndex : OtherGridIndex;
			Entry.GridCategory = OtherGridCategory;
			Entry.bRotated = bOtherRotated;
			OtherComp->InventoryList.MarkItemDirty(Entry);
			UE_LOG(LogTemp, Log, TEXT("[InvComp] SwapItemWith: [Fix31] NewItemInOther.GridIndex=%d (Target=%d, Fallback=%d)"),
				Entry.GridIndex, TargetGridIndex, OtherGridIndex);
			break;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[InvComp] SwapItemWith 완료: %s x%d ↔ %s x%d (위치 교차 할당됨)"),
		*MyType.ToString(), MyStackCount, *OtherType.ToString(), OtherStackCount);
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 4 Fix] FindValidItemIndexByReplicationID
// ════════════════════════════════════════════════════════════════════════════════
// FastArray의 ReplicationID로 ValidItems 배열 인덱스를 찾는다.
// ReplicationID는 Entry가 추가될 때 부여되며, 다른 Entry의 제거로 배열이 밀려도 변하지 않는다.
// TransferItemTo()가 사용하는 ValidItems 인덱스(무효/부착물 제외)와 동일한 기준으로 필터링.
// ════════════════════════════════════════════════════════════════════════════════
int32 UInv_InventoryComponent::FindValidItemIndexByReplicationID(int32 InReplicationID) const
{
	int32 ValidIdx = 0;
	for (const FInv_InventoryEntry& Entry : InventoryList.Entries)
	{
		if (IsValid(Entry.Item) && !Entry.bIsAttachedToWeapon)
		{
			if (Entry.ReplicationID == InReplicationID)
			{
				return ValidIdx;
			}
			ValidIdx++;
		}
	}
	return INDEX_NONE;
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 9] 컨테이너 아이템 전송 RPC 구현
// ════════════════════════════════════════════════════════════════════════════════

// ⚠️ Fix18 준수: Validate에서 UObject IsValid() 금지, 값 타입만 검증
bool UInv_InventoryComponent::Server_TakeItemFromContainer_Validate(
	UInv_LootContainerComponent* Container,
	int32 ContainerEntryIndex,
	int32 TargetGridIndex)
{
	return ContainerEntryIndex >= 0 && ContainerEntryIndex < InvValidation::MaxEntryIndex
		&& (TargetGridIndex == INDEX_NONE || (TargetGridIndex >= 0 && TargetGridIndex < InvValidation::MaxGridIndex));
}

void UInv_InventoryComponent::Server_TakeItemFromContainer_Implementation(
	UInv_LootContainerComponent* Container,
	int32 ContainerEntryIndex,
	int32 TargetGridIndex)
{
	if (!IsValid(Container)) return;

	// 잠금 확인: 요청한 PC가 CurrentUser인지
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!IsValid(PC) || Container->CurrentUser != PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase 9] Server_TakeItemFromContainer: 잠금 불일치 (요청 PC != CurrentUser)"));
		return;
	}

	// 컨테이너 FastArray에서 아이템 검색
	FInv_InventoryFastArray& ContainerList = Container->ContainerInventoryList;
	if (!ContainerList.Entries.IsValidIndex(ContainerEntryIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase 9] Server_TakeItemFromContainer: 유효하지 않은 EntryIndex=%d"), ContainerEntryIndex);
		return;
	}

	FInv_InventoryEntry& ContainerEntry = ContainerList.Entries[ContainerEntryIndex];
	if (!IsValid(ContainerEntry.Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase 9] Server_TakeItemFromContainer: Entry[%d] Item이 nullptr"), ContainerEntryIndex);
		return;
	}

	// 내 인벤토리에 공간 확인
	FInv_ItemManifest Manifest = ContainerEntry.Item->GetItemManifest();
	if (!HasRoomInInventoryList(Manifest))
	{
		UE_LOG(LogTemp, Log, TEXT("[Phase 9] Server_TakeItemFromContainer: 인벤토리 공간 부족"));
		NoRoomInInventory.Broadcast();
		return;
	}

	// 아이템 정보 캐시 (RemoveEntry 전에 복사)
	UInv_InventoryItem* ItemToMove = ContainerEntry.Item;
	int32 StackCount = ItemToMove->GetTotalStackCount();
	FGameplayTag ItemType = Manifest.GetItemType();

	// 내 인벤토리에 추가
	FInv_InventoryEntry& NewEntry = InventoryList.Entries.AddDefaulted_GetRef();
	NewEntry.Item = ItemToMove;

	// Grid 위치 설정
	if (TargetGridIndex >= 0)
	{
		NewEntry.GridIndex = TargetGridIndex;
	}
	NewEntry.GridCategory = static_cast<uint8>(Manifest.GetItemCategory());

	// 리플리케이션 서브오브젝트 이전: 컨테이너에서 해제 → 내 InvComp에 등록
	if (Container->IsUsingRegisteredSubObjectList() && IsValid(ItemToMove))
	{
		Container->RemoveReplicatedSubObject(ItemToMove);
	}
	AddRepSubObj(ItemToMove);

	InventoryList.MarkItemDirty(NewEntry);
	InventoryList.RebuildItemTypeIndex();

	// 컨테이너에서 제거 (Entry만 제거, Item 객체는 유지 — 내 InvComp으로 이동했으므로)
	ContainerList.Entries.RemoveAt(ContainerEntryIndex);
	ContainerList.MarkArrayDirty();
	ContainerList.RebuildItemTypeIndex();

	UE_LOG(LogTemp, Log, TEXT("[Phase 9] Server_TakeItemFromContainer: %s (x%d) 가져옴"),
		*ItemType.ToString(), StackCount);

	// 리슨서버 호스트 UI 갱신
	if (IsListenServerOrStandalone())
	{
		OnItemAdded.Broadcast(ItemToMove, InventoryList.Entries.Num() - 1);
	}

	// 비어있으면 파괴 (설정에 따라)
	if (Container->bDestroyOwnerWhenEmpty && Container->IsEmpty())
	{
		AActor* ContainerOwner = Container->GetOwner();
		if (IsValid(ContainerOwner))
		{
			ContainerOwner->Destroy();
		}
	}
}

bool UInv_InventoryComponent::Server_PutItemInContainer_Validate(
	UInv_LootContainerComponent* Container,
	int32 PlayerEntryIndex,
	int32 TargetGridIndex)
{
	return PlayerEntryIndex >= 0 && PlayerEntryIndex < InvValidation::MaxEntryIndex
		&& (TargetGridIndex == INDEX_NONE || (TargetGridIndex >= 0 && TargetGridIndex < InvValidation::MaxGridIndex));
}

void UInv_InventoryComponent::Server_PutItemInContainer_Implementation(
	UInv_LootContainerComponent* Container,
	int32 PlayerEntryIndex,
	int32 TargetGridIndex)
{
	if (!IsValid(Container)) return;

	// 잠금 확인
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!IsValid(PC) || Container->CurrentUser != PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase 9] Server_PutItemInContainer: 잠금 불일치"));
		return;
	}

	// 내 FastArray에서 아이템 검색
	if (!InventoryList.Entries.IsValidIndex(PlayerEntryIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase 9] Server_PutItemInContainer: 유효하지 않은 EntryIndex=%d"), PlayerEntryIndex);
		return;
	}

	FInv_InventoryEntry& PlayerEntry = InventoryList.Entries[PlayerEntryIndex];
	if (!IsValid(PlayerEntry.Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase 9] Server_PutItemInContainer: Entry[%d] Item이 nullptr"), PlayerEntryIndex);
		return;
	}

	// 아이템 정보 캐시
	UInv_InventoryItem* ItemToMove = PlayerEntry.Item;
	int32 StackCount = ItemToMove->GetTotalStackCount();
	FGameplayTag ItemType = ItemToMove->GetItemManifest().GetItemType();

	// 컨테이너에 추가
	FInv_InventoryFastArray& ContainerList = Container->ContainerInventoryList;
	FInv_InventoryEntry& NewEntry = ContainerList.Entries.AddDefaulted_GetRef();
	NewEntry.Item = ItemToMove;

	if (TargetGridIndex >= 0)
	{
		NewEntry.GridIndex = TargetGridIndex;
	}

	// 리플리케이션 서브오브젝트 이전: 내 InvComp에서 해제 → 컨테이너에 등록
	RemoveRepSubObj(ItemToMove);
	if (Container->IsUsingRegisteredSubObjectList() && Container->IsReadyForReplication() && IsValid(ItemToMove))
	{
		Container->AddReplicatedSubObject(ItemToMove);
	}

	ContainerList.MarkItemDirty(NewEntry);
	ContainerList.RebuildItemTypeIndex();

	// 내 인벤토리에서 제거
	InventoryList.Entries.RemoveAt(PlayerEntryIndex);
	InventoryList.MarkArrayDirty();
	InventoryList.RebuildItemTypeIndex();

	UE_LOG(LogTemp, Log, TEXT("[Phase 9] Server_PutItemInContainer: %s (x%d) 넣음"),
		*ItemType.ToString(), StackCount);

	// 리슨서버 호스트 UI 갱신
	if (IsListenServerOrStandalone())
	{
		OnItemRemoved.Broadcast(ItemToMove, PlayerEntryIndex);
	}
}

bool UInv_InventoryComponent::Server_TakeAllFromContainer_Validate(UInv_LootContainerComponent* Container)
{
	return true;
}

void UInv_InventoryComponent::Server_TakeAllFromContainer_Implementation(
	UInv_LootContainerComponent* Container)
{
	if (!IsValid(Container)) return;

	// 잠금 확인
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!IsValid(PC) || Container->CurrentUser != PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Phase 9] Server_TakeAllFromContainer: 잠금 불일치"));
		return;
	}

	FInv_InventoryFastArray& ContainerList = Container->ContainerInventoryList;
	int32 TakenCount = 0;

	// 역순으로 가져오기 (인덱스 시프트 안전)
	for (int32 i = ContainerList.Entries.Num() - 1; i >= 0; --i)
	{
		if (!ContainerList.Entries.IsValidIndex(i)) continue;

		FInv_InventoryEntry& ContainerEntry = ContainerList.Entries[i];
		if (!IsValid(ContainerEntry.Item)) continue;

		// 공간 확인
		FInv_ItemManifest Manifest = ContainerEntry.Item->GetItemManifest();
		if (!HasRoomInInventoryList(Manifest))
		{
			UE_LOG(LogTemp, Log, TEXT("[Phase 9] Server_TakeAllFromContainer: 공간 부족으로 중단 (%d개 가져옴)"), TakenCount);
			break;
		}

		// 아이템 이동
		UInv_InventoryItem* ItemToMove = ContainerEntry.Item;

		FInv_InventoryEntry& NewEntry = InventoryList.Entries.AddDefaulted_GetRef();
		NewEntry.Item = ItemToMove;
		NewEntry.GridCategory = static_cast<uint8>(Manifest.GetItemCategory());

		// 리플리케이션 서브오브젝트 이전
		if (Container->IsUsingRegisteredSubObjectList() && IsValid(ItemToMove))
		{
			Container->RemoveReplicatedSubObject(ItemToMove);
		}
		AddRepSubObj(ItemToMove);

		InventoryList.MarkItemDirty(NewEntry);

		// 컨테이너에서 제거
		ContainerList.Entries.RemoveAt(i);
		TakenCount++;

		// 리슨서버 호스트 UI 갱신
		if (IsListenServerOrStandalone())
		{
			OnItemAdded.Broadcast(ItemToMove, InventoryList.Entries.Num() - 1);
		}
	}

	ContainerList.MarkArrayDirty();
	ContainerList.RebuildItemTypeIndex();
	InventoryList.RebuildItemTypeIndex();

	UE_LOG(LogTemp, Log, TEXT("[Phase 9] Server_TakeAllFromContainer: %d개 아이템 가져옴"), TakenCount);

	// 비어있으면 파괴
	if (Container->bDestroyOwnerWhenEmpty && Container->IsEmpty())
	{
		AActor* ContainerOwner = Container->GetOwner();
		if (IsValid(ContainerOwner))
		{
			ContainerOwner->Destroy();
		}
	}
}
