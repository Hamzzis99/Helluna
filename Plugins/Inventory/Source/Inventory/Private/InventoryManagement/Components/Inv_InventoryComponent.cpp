// Gihyeon's Inventory Project


#include "InventoryManagement/Components/Inv_InventoryComponent.h"

#include "Items/Components/Inv_ItemComponent.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Net/UnrealNetwork.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_ItemFragment.h"

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
	FInv_SlotAvailabilityResult Result = InventoryMenu->HasRoomForItem(ItemComponent); // 인벤토리에 아이템을 추가할 수 있는지 확인하는 부분.

	UInv_InventoryItem* FoundItem = InventoryList.FindFirstItemByType(ItemComponent->GetItemManifest().GetItemType()); // 동일한 유형의 아이템이 이미 있는지 확인하는 부분.
	Result.Item = FoundItem; // 찾은 아이템을 결과에 설정.

	if (Result.TotalRoomToFill == 0)
	{
		NoRoomInInventory.Broadcast(); // 나 인벤토리 꽉찼어! 이걸 알려주는거야! 방송 삐용삐용 모두 알아둬라!
		return;
	}
	// TODO : 실제로 인벤토리에 추가하는 부분을 만들 것. (일단 나중에)

	// 아이템 스택 가능 정보를 전달하는 것? 서버 RPC로 해보자.
	if (Result.Item.IsValid() && Result.bStackable) // 유효한지 검사하는 작업. 쌓을 수 있다면 다음 부분들을 진행.
	{
		// 이미 존재하는 아이템에 스택을 추가하는 부분. 
		// 슬롯 가능 여부 결과를 방송할 위임자를 만들기 (Broadcast)
		// Add stacks to an item that already exists in the inventory. We only want to update the stack count,
		// not create a new item of this type.
		OnStackChange.Broadcast(Result); // 스택 변경 사항 방송
		Server_AddStacksToItem(ItemComponent, Result.TotalRoomToFill, Result.Remainder); // 아이템을 추가하는 부분.
	}
	// 서버에서 아이템 등록 우와.... 자살하고 싶어진다.
	else if (Result.TotalRoomToFill > 0)
	{
		// This item type dosen't exist in the inventory. Create a new one and update all partient slots.
		Server_AddNewItem(ItemComponent, Result.bStackable ? Result.TotalRoomToFill : 0, Result.Remainder); //쌓을 수 있다면 채울 수 있는 공간 이런 문법은 또 처음 보네
	}
}

void UInv_InventoryComponent::Server_AddNewItem_Implementation(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder) // 서버에서 새로운 아이템 추가 구현
{
	UInv_InventoryItem* NewItem = InventoryList.AddEntry(ItemComponent); // 여기서 아이템을정상적으로 줍게 된다면? 추가를 한다.
	NewItem->SetTotalStackCount(StackCount);

	if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone) // 이 부분이 복제할 클라이언트가 없기 때문에 배열 복제 안 되는 거 (데디 서버로 변경할 때 참고해라)
	{
		OnItemAdded.Broadcast(NewItem);
	}

	// 아이템 개수가 인벤토리 개수보다 많아져도 파괴되지 않게 안전장치를 걸기.
	if (Remainder == 0)
	{
		ItemComponent->PickedUp();
	}
	else if (FInv_StackableFragment* StackableFragment = ItemComponent->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>()) // 복사본이 아니라 실제 참조본을 가져오는 것.
	{
		StackableFragment->SetStackCount(Remainder);
	}
	
}

void UInv_InventoryComponent::Server_AddStacksToItem_Implementation(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder) // 서버에서 아이템 스택 개수를 세어주는 역할.
{
	const FGameplayTag& ItemType = IsValid(ItemComponent) ? ItemComponent->GetItemManifest().GetItemType() : FGameplayTag::EmptyTag; // 아이템 유형 가져오기
	UInv_InventoryItem* Item = InventoryList.FindFirstItemByType(ItemType); // 동일한 유형의 아이템 찾기
	if (!IsValid(Item)) return;

	//아이템 스택수 불러오기 (이미 있는 항목에 추가로 등록)
	Item->SetTotalStackCount(Item->GetTotalStackCount() + StackCount);

	//0가 되면 아이템 파괴하는 부분
	// TODO : Destroy the item if the Remainder is zero.
	// Otherwise, update the stack count for the item pickup.

	if (Remainder == 0)
	{
		ItemComponent->PickedUp();
	}
	else if (FInv_StackableFragment* StackableFragment = ItemComponent->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>())
	{
		StackableFragment->SetStackCount(Remainder);
	}
}

//아이템 드롭 상호작용을 누른 뒤 서버에서 어떻게 처리를 할지.
void UInv_InventoryComponent::Server_DropItem_Implementation(UInv_InventoryItem* Item, int32 StackCount)
{
	//단순히 항목을 제거하는지 단순 업데이트를 하는지
	const int32 NewStackCount = Item->GetTotalStackCount() - StackCount;
	if (NewStackCount <= 0)
	{
		InventoryList.RemoveEntry(Item);
	}
	else
	{
		Item->SetTotalStackCount(NewStackCount);
	}

	SpawnDroppedItem(Item, StackCount); // 떨어진 아이템 생성 함수 호출
	
}

//무언가를 떨어뜨렸기 때문에 아이템도 생성 및 이벤트 효과들 보이게 하는 부분의 코드들
void UInv_InventoryComponent::SpawnDroppedItem(UInv_InventoryItem* Item, int32 StackCount)
{
	// TODO : 아이템을 버릴 시 월드에 소환하게 하는 부분 만들기
	const APawn* OwningPawn = OwningController->GetPawn();
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

// 아이템 소비 상호작용을 누른 뒤 서버에서 어떻게 처리를 할지.
void UInv_InventoryComponent::Server_ConsumeItem_Implementation(UInv_InventoryItem* Item)
{
	const int32 NewStackCount = Item->GetTotalStackCount() - 1;
	if (NewStackCount <= 0) // 스택 카운트가 0일시.
	{
		InventoryList.RemoveEntry(Item);
	}
	else
	{
		Item->SetTotalStackCount(NewStackCount);
	}
	
	// TODO: Get the consumable fragment and call Consume()
	// 소비 프래그먼트를 가져와서 소비 함수 호출 (소비할 때 실제로 일어나는 일을 구현하자!)
	// (Actually create the Consumable Fragment)
	// (소모 프래그먼트 실제로 만들기) 
	
	// 아이템 매니페스트에서 소비 프래그먼트 가져오기
	if (FInv_ConsumableFragment* ConsumableFragment = Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_ConsumableFragment>())
	{
		ConsumableFragment->OnConsume(OwningController.Get()); // 소비 함수 호출
	}
}

// 재료 소비 (Building 시스템용) - Server_DropItem과 동일한 로직 사용
void UInv_InventoryComponent::Server_ConsumeMaterials_Implementation(const FGameplayTag& MaterialTag, int32 Amount)
{
	if (!MaterialTag.IsValid() || Amount <= 0) return;

	UE_LOG(LogTemp, Warning, TEXT("=== Server_ConsumeMaterials 호출됨 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);

	// 재료 찾기
	UInv_InventoryItem* Item = InventoryList.FindFirstItemByType(MaterialTag);
	if (!IsValid(Item))
	{
		UE_LOG(LogTemp, Error, TEXT("Server_ConsumeMaterials: Item not found! (%s)"), *MaterialTag.ToString());
		return;
	}

	// GetTotalStackCount() 사용 (Server_DropItem과 동일!)
	int32 CurrentCount = Item->GetTotalStackCount();
	int32 NewCount = CurrentCount - Amount;

	UE_LOG(LogTemp, Warning, TEXT("Server: 재료 차감 (%d → %d)"), CurrentCount, NewCount);

	if (NewCount <= 0)
	{
		// 재료를 다 썼으면 인벤토리에서 제거 (Server_DropItem과 동일!)
		InventoryList.RemoveEntry(Item);
		UE_LOG(LogTemp, Warning, TEXT("Server: 재료 전부 소비! 인벤토리에서 제거됨: %s"), *MaterialTag.ToString());
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
			UE_LOG(LogTemp, Warning, TEXT("Server: StackableFragment도 업데이트됨!"));
		}

		// FastArray에 변경 사항 알림 (리플리케이션 활성화!)
		for (auto& Entry : InventoryList.Entries)
		{
			if (Entry.Item == Item)
			{
				InventoryList.MarkItemDirty(Entry);
				UE_LOG(LogTemp, Warning, TEXT("Server: MarkItemDirty 호출 완료! 리플리케이션 활성화!"));
				break;
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("Server: 재료 차감 완료: %s (%d → %d)"), *MaterialTag.ToString(), CurrentCount, NewCount);
	}

	// UI 업데이트를 위해 델리게이트 브로드캐스트 (모든 클라이언트에서 실행)
	// 리플리케이션이 작동하면 각 클라이언트에서도 호출됨
	if (NewCount <= 0)
	{
		// 아이템 제거됨
		OnItemRemoved.Broadcast(Item);
		UE_LOG(LogTemp, Warning, TEXT("OnItemRemoved 브로드캐스트 완료"));
	}
	else
	{
		// 스택 개수만 변경됨 - OnStackChange 브로드캐스트
		FInv_SlotAvailabilityResult Result;
		Result.Item = Item;
		Result.bStackable = true;
		Result.TotalRoomToFill = NewCount;
		
		// 슬롯 정보는 비워두고 (InventoryGrid가 Item으로 슬롯을 찾음)
		OnStackChange.Broadcast(Result);
		UE_LOG(LogTemp, Warning, TEXT("OnStackChange 브로드캐스트 완료 (NewCount: %d)"), NewCount);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== Server_ConsumeMaterials 완료 ==="));
}

// 같은 타입의 모든 스택 개수 합산 (Building UI용)
int32 UInv_InventoryComponent::GetTotalMaterialCount(const FGameplayTag& MaterialTag) const
{
	if (!MaterialTag.IsValid()) return 0;

	// ⭐ InventoryList에서 읽기 (Split 대응: 같은 ItemType의 모든 Entry 합산!)
	int32 TotalCount = 0;
	
	UE_LOG(LogTemp, Verbose, TEXT("🔍 GetTotalMaterialCount(%s) 시작:"), *MaterialTag.ToString());
	
	for (const auto& Entry : InventoryList.Entries)
	{
		if (!IsValid(Entry.Item)) continue;

		if (Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag))
		{
			int32 EntryCount = Entry.Item->GetTotalStackCount();
			TotalCount += EntryCount;
			
			UE_LOG(LogTemp, Verbose, TEXT("  Entry 발견: Item포인터=%p, TotalStackCount=%d, 누적합=%d"), 
				Entry.Item.Get(), EntryCount, TotalCount);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("✅ GetTotalMaterialCount(%s) = %d (InventoryList 전체 합산)"), 
		*MaterialTag.ToString(), TotalCount);
	return TotalCount;
}

// 재료 소비 - 여러 스택에서 차감 (Building 시스템용)
void UInv_InventoryComponent::Server_ConsumeMaterialsMultiStack_Implementation(const FGameplayTag& MaterialTag, int32 Amount)
{
	if (!MaterialTag.IsValid() || Amount <= 0) return;

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

	// 1단계: 데이터(TotalStackCount) 차감 및 동기화
	int32 RemainingAmount = Amount;
	TArray<FInv_InventoryEntry*> EntriesToRemove;

	for (auto& Entry : InventoryList.Entries)
	{
		if (RemainingAmount <= 0) break;

		if (!IsValid(Entry.Item)) continue;

		// 같은 타입인지 확인
		if (!Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(MaterialTag)) continue;

		int32 CurrentCount = Entry.Item->GetTotalStackCount();
		int32 AmountToConsume = FMath::Min(CurrentCount, RemainingAmount);

		UE_LOG(LogTemp, Warning, TEXT("🔧 [서버] 차감 시도: Item포인터=%p, CurrentCount=%d, AmountToConsume=%d, RemainingBefore=%d"), 
			Entry.Item.Get(), CurrentCount, AmountToConsume, RemainingAmount);

		RemainingAmount -= AmountToConsume;
		int32 NewCount = CurrentCount - AmountToConsume;

		UE_LOG(LogTemp, Warning, TEXT("🔧 [서버] 차감 계산: %d - %d = %d, RemainingAfter=%d"), 
			CurrentCount, AmountToConsume, NewCount, RemainingAmount);

		if (NewCount <= 0)
		{
			// 제거 예약
			EntriesToRemove.Add(&Entry);
			UE_LOG(LogTemp, Warning, TEXT("❌ [서버] Entry 제거 예약: Item포인터=%p"), Entry.Item.Get());
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

			UE_LOG(LogTemp, Warning, TEXT("✅ [서버] Entry 업데이트 완료: %d → %d (Item포인터=%p)"), 
				CurrentCount, NewCount, Entry.Item.Get());
		}
	}

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

	// 제거 예약된 아이템들 실제 제거
	for (FInv_InventoryEntry* EntryPtr : EntriesToRemove)
	{
		UInv_InventoryItem* ItemToRemove = EntryPtr->Item;
		InventoryList.RemoveEntry(ItemToRemove);
		
		UE_LOG(LogTemp, Warning, TEXT("InventoryList에서 제거됨: %s"), *MaterialTag.ToString());
	}

	// ⭐ FastArray 리플리케이션이 자동으로 PostReplicatedChange를 호출하여 UI를 업데이트합니다!
	// Multicast_ConsumeMaterialsUI 호출 제거 - 이중 차감 방지!

	if (RemainingAmount > 0)
	{
		UE_LOG(LogTemp, Error, TEXT("재료가 부족합니다! 남은 차감량: %d"), RemainingAmount);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 재료 차감 완료! MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);
		UE_LOG(LogTemp, Warning, TEXT("FastArray 리플리케이션이 자동으로 클라이언트 UI를 업데이트합니다."));
	}

	UE_LOG(LogTemp, Warning, TEXT("=== Server_ConsumeMaterialsMultiStack 완료 ==="));
}

// Split 시 서버의 TotalStackCount 업데이트
void UInv_InventoryComponent::Server_UpdateItemStackCount_Implementation(UInv_InventoryItem* Item, int32 NewStackCount)
{
	if (!IsValid(Item))
	{
		UE_LOG(LogTemp, Error, TEXT("Server_UpdateItemStackCount: Item is invalid!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("🔧 [서버] Server_UpdateItemStackCount 호출됨"));
	UE_LOG(LogTemp, Warning, TEXT("  Item포인터: %p, ItemType: %s"), Item, *Item->GetItemManifest().GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("  이전 TotalStackCount: %d → 새로운 값: %d"), Item->GetTotalStackCount(), NewStackCount);

	// 1단계: TotalStackCount 업데이트
	Item->SetTotalStackCount(NewStackCount);

	// 2단계: StackableFragment도 업데이트
	FInv_ItemManifest& ItemManifest = Item->GetItemManifestMutable();
	if (FInv_StackableFragment* StackableFragment = ItemManifest.GetFragmentOfTypeMutable<FInv_StackableFragment>())
	{
		StackableFragment->SetStackCount(NewStackCount);
		UE_LOG(LogTemp, Warning, TEXT("  StackableFragment도 %d로 업데이트됨"), NewStackCount);
	}

	// ⭐⭐⭐ 3단계: FastArray에 변경 알림 (리플리케이션 트리거!)
	for (auto& Entry : InventoryList.Entries)
	{
		if (Entry.Item == Item)
		{
			InventoryList.MarkItemDirty(Entry);
			UE_LOG(LogTemp, Warning, TEXT("✅ FastArray에 Item 변경 알림 완료! 클라이언트로 리플리케이션됩니다."));
			break;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("✅ [서버] %s의 TotalStackCount를 %d로 업데이트 완료 (FastArray 갱신됨)"), 
		*Item->GetItemManifest().GetItemType().ToString(), NewStackCount);
}

// Multicast: 모든 클라이언트의 UI 업데이트
void UInv_InventoryComponent::Multicast_ConsumeMaterialsUI_Implementation(const FGameplayTag& MaterialTag, int32 Amount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== Multicast_ConsumeMaterialsUI 호출됨 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);

	if (!IsValid(InventoryMenu))
	{
		UE_LOG(LogTemp, Error, TEXT("InventoryMenu is invalid!"));
		return;
	}

	// SpatialInventory의 해당 카테고리 Grid 찾아서 ConsumeItemsByTag 호출
	UInv_SpatialInventory* SpatialInv = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInv))
	{
		UE_LOG(LogTemp, Error, TEXT("SpatialInventory cast failed!"));
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
			UE_LOG(LogTemp, Warning, TEXT("Grid 카테고리 매칭! GridCategory: %d"), (int32)GridCategory);
			Grid->ConsumeItemsByTag(MaterialTag, RemainingToConsume);
			break; // 한 Grid에서만 차감!
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("✅ UI(GridSlot) 차감 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("=== Multicast_ConsumeMaterialsUI 완료 ==="));
}

// 아이템 장착 상호작용을 누른 뒤 서버에서 어떻게 처리를 할지.
void UInv_InventoryComponent::Server_EquipSlotClicked_Implementation(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip)
{
	Multicast_EquipSlotClicked(ItemToEquip, ItemToUnequip); // 멀티캐스트로 모든 클라이언트에 알리는 부분.
}

// 멀티캐스트로 아이템 장착 상호작용을 모든 클라이언트에 알리는 부분.
void UInv_InventoryComponent::Multicast_EquipSlotClicked_Implementation(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip)
{
	// Equipment Component will listen to these delegates
	// 장비 컴포넌트가 이 델리게이트를 수신 대기합니다.
	OnItemEquipped.Broadcast(ItemToEquip);
	OnItemUnequipped.Broadcast(ItemToUnequip);
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

// Called when the game starts
void UInv_InventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	ConstructInventory();
	
}


//인벤토리 메뉴 위젯 생성 함수
void UInv_InventoryComponent::ConstructInventory()
{
	OwningController = Cast<APlayerController>(GetOwner());
	checkf(OwningController.IsValid(), TEXT("Inventory Component should have a Player Controller as Owner."))
	if (!OwningController->IsLocalController()) return;

	//블루프린터 위젯 클래스가 설정되어 있는지 확인
	InventoryMenu = CreateWidget<UInv_InventoryBase>(OwningController.Get(), InventoryMenuClass);
	InventoryMenu->AddToViewport();
	CloseInventoryMenu();
}

//인벤토리 메뉴 열기/닫기 함수
void UInv_InventoryComponent::OpenInventoryMenu()
{
	if (!IsValid(InventoryMenu)) return;

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

	if (!OwningController.IsValid()) return;

	FInputModeGameOnly InputMode;
	OwningController->SetInputMode(InputMode);
	OwningController->SetShowMouseCursor(false);
}
