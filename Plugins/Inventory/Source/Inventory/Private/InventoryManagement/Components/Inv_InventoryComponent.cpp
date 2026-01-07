// Gihyeon's Inventory Project


#include "InventoryManagement/Components/Inv_InventoryComponent.h"

#include "Items/Components/Inv_ItemComponent.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Net/UnrealNetwork.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Building/Components/Inv_BuildingComponent.h"

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
	UE_LOG(LogTemp, Warning, TEXT("=== [PICKUP] TryAddItem 시작 ==="));
	
	// 디버깅: ItemComponent 정보 출력
	if (!IsValid(ItemComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("[PICKUP] ItemComponent가 nullptr입니다!"));
		return;
	}

	AActor* OwnerActor = ItemComponent->GetOwner();
	if (IsValid(OwnerActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 픽업할 Actor: %s"), *OwnerActor->GetName());
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] Actor Blueprint 클래스: %s"), *OwnerActor->GetClass()->GetName());
	}

	const FInv_ItemManifest& Manifest = ItemComponent->GetItemManifest();
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 아이템 정보:"));
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - ItemType (GameplayTag): %s"), *Manifest.GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - ItemCategory: %d"), (int32)Manifest.GetItemCategory());
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - PickupMessage: %s"), *ItemComponent->GetPickupMessage());
	
	// PickupActorClass 정보 추가 (크래프팅에서 사용할 Blueprint 확인용!)
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 📦 이 아이템의 PickupActorClass (크래프팅에 사용해야 하는 Blueprint):"));
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]    Blueprint 클래스: %s"), *OwnerActor->GetClass()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[PICKUP]    전체 경로: %s"), *OwnerActor->GetClass()->GetPathName());

	FInv_SlotAvailabilityResult Result = InventoryMenu->HasRoomForItem(ItemComponent); // 인벤토리에 아이템을 추가할 수 있는지 확인하는 부분.

	UInv_InventoryItem* FoundItem = InventoryList.FindFirstItemByType(ItemComponent->GetItemManifest().GetItemType()); // 동일한 유형의 아이템이 이미 있는지 확인하는 부분.
	Result.Item = FoundItem; // 찾은 아이템을 결과에 설정.

	if (Result.TotalRoomToFill == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 인벤토리에 공간이 없습니다!"));
		NoRoomInInventory.Broadcast(); // 나 인벤토리 꽉찼어! 이걸 알려주는거야! 방송 삐용삐용 모두 알아둬라!
		return;
	}

	// 아이템 스택 가능 정보를 전달하는 것? 서버 RPC로 해보자.
	if (Result.Item.IsValid() && Result.bStackable) // 유효한지 검사하는 작업. 쌓을 수 있다면 다음 부분들을 진행.
	{
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 스택 가능 아이템! 기존 스택에 추가합니다."));
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 추가할 개수: %d"), Result.TotalRoomToFill);
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 남은 개수: %d"), Result.Remainder);
		
		// 이미 존재하는 아이템에 스택을 추가하는 부분. 
		OnStackChange.Broadcast(Result); // 스택 변경 사항 방송
		Server_AddStacksToItem(ItemComponent, Result.TotalRoomToFill, Result.Remainder); // 아이템을 추가하는 부분.
	}
	// 서버에서 아이템 등록
	else if (Result.TotalRoomToFill > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP] 새로운 아이템 추가!"));
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 스택 개수: %d"), Result.bStackable ? Result.TotalRoomToFill : 0);
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP]   - 남은 개수: %d"), Result.Remainder);
		
		// This item type dosen't exist in the inventory. Create a new one and update all partient slots.
		Server_AddNewItem(ItemComponent, Result.bStackable ? Result.TotalRoomToFill : 0, Result.Remainder); //쌓을 수 있다면 채울 수 있는 공간 이런 문법은 또 처음 보네
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [PICKUP] TryAddItem 완료 ==="));
}

void UInv_InventoryComponent::Server_AddNewItem_Implementation(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder) // 서버에서 새로운 아이템 추가 구현
{
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER PICKUP] Server_AddNewItem_Implementation 시작 ==="));
	
	if (!IsValid(ItemComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER PICKUP] ItemComponent가 nullptr입니다!"));
		return;
	}

	AActor* OwnerActor = ItemComponent->GetOwner();
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

	UInv_InventoryItem* NewItem = InventoryList.AddEntry(ItemComponent); // 여기서 아이템을정상적으로 줍게 된다면? 추가를 한다.
	
	if (!IsValid(NewItem))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER PICKUP] InventoryList.AddEntry 실패!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] InventoryList.AddEntry 성공! NewItem 생성됨"));
	
	NewItem->SetTotalStackCount(StackCount);

	if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone) // 이 부분이 복제할 클라이언트가 없기 때문에 배열 복제 안 되는 거 (데디 서버로 변경할 때 참고해라)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] ListenServer/Standalone 모드 - OnItemAdded 델리게이트 브로드캐스트"));
		OnItemAdded.Broadcast(NewItem);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] Dedicated Server 모드 - FastArray 리플리케이션에 의존"));
	}

	// 아이템 개수가 인벤토리 개수보다 많아져도 파괴되지 않게 안전장치를 걸기.
	if (Remainder == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] Remainder == 0, ItemComponent->PickedUp() 호출 (Actor 파괴)"));
		ItemComponent->PickedUp();
	}
	else if (FInv_StackableFragment* StackableFragment = ItemComponent->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>()) // 복사본이 아니라 실제 참조본을 가져오는 것.
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] Remainder > 0 (%d), StackCount 업데이트"), Remainder);
		StackableFragment->SetStackCount(Remainder);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER PICKUP] Server_AddNewItem_Implementation 완료 ==="));
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
	
	// 소비 프래그먼트를 가져와서 소비 함수 호출 (소비할 때 실제로 일어나는 일을 구현하자!)
	if (FInv_ConsumableFragment* ConsumableFragment = Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_ConsumableFragment>())
	{
		ConsumableFragment->OnConsume(OwningController.Get());
	}
}

// 크래프팅: 서버에서 아이템 생성 및 인벤토리 추가 (ItemManifest 직접 사용!)
void UInv_InventoryComponent::Server_CraftItem_Implementation(TSubclassOf<AActor> ItemActorClass)
{
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT] Server_CraftItem_Implementation 시작 ==="));

	// 서버 권한 체크
	if (!GetOwner()->HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 권한 없음! 서버에서만 실행 가능!"));
		return;
	}

	// ItemActorClass 유효성 체크
	if (!ItemActorClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemActorClass가 nullptr입니다!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작할 아이템 Blueprint: %s"), *ItemActorClass->GetName());
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ItemActorClass 전체 경로: %s"), *ItemActorClass->GetPathName());
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ItemActorClass 클래스 이름: %s"), *ItemActorClass.Get()->GetName());

	// Blueprint 컴포넌트 접근을 위해 임시 인스턴스 생성
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;
	
	FVector TempLocation = FVector(0, 0, -50000); // 매우 아래쪽
	FRotator TempRotation = FRotator::ZeroRotator;
	FTransform TempTransform(TempRotation, TempLocation);
	
	AActor* TempActor = GetWorld()->SpawnActorDeferred<AActor>(ItemActorClass, TempTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(TempActor))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 임시 인스턴스 생성 실패!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 임시 인스턴스 생성 성공: %s"), *TempActor->GetName());
	
	// FinishSpawning 호출 - Blueprint 컴포넌트 초기화! (BeginPlay는 호출되지 않음)
	TempActor->FinishSpawning(TempTransform);
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] FinishSpawning 호출 완료 - 컴포넌트 초기화됨!"));

	// ItemComponent 찾기 (Blueprint 컴포넌트 포함)
	UInv_ItemComponent* DefaultItemComp = nullptr;
	
	// 방법 1: FindComponentByClass
	DefaultItemComp = TempActor->FindComponentByClass<UInv_ItemComponent>();
	
	if (!IsValid(DefaultItemComp))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] FindComponentByClass 실패, GetComponents로 재시도..."));
		
		// 방법 2: GetComponents (Blueprint 컴포넌트 포함)
		TArray<UInv_ItemComponent*> ItemComponents;
		TempActor->GetComponents<UInv_ItemComponent>(ItemComponents);
		
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] GetComponents 결과: %d개 컴포넌트 발견"), ItemComponents.Num());
		
		if (ItemComponents.Num() > 0)
		{
			DefaultItemComp = ItemComponents[0];
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ItemComponent 찾음! (GetComponents 사용)"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ItemComponent 찾음! (FindComponentByClass 사용)"));
	}

	// 최종 검증
	if (!IsValid(DefaultItemComp))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ❌ ItemComponent를 찾을 수 없습니다!"));
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] Blueprint: %s"), *ItemActorClass->GetName());
		
		// 모든 컴포넌트 목록 출력 (디버깅)
		TArray<UActorComponent*> AllComponents;
		TempActor->GetComponents(AllComponents);
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 전체 컴포넌트 목록 (%d개):"), AllComponents.Num());
		for (UActorComponent* Comp : AllComponents)
		{
			if (IsValid(Comp))
			{
				UE_LOG(LogTemp, Warning, TEXT("  - %s (클래스: %s)"), *Comp->GetName(), *Comp->GetClass()->GetName());
			}
		}
		
		// 임시 인스턴스 파괴
		TempActor->Destroy();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ItemComponent: %s (클래스: %s)"), 
		*DefaultItemComp->GetName(), *DefaultItemComp->GetClass()->GetName());

	// ItemManifest 복사
	FInv_ItemManifest ItemManifest = DefaultItemComp->GetItemManifest();

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ItemManifest 가져옴!"));
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작할 아이템 정보:"));
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   - 아이템 타입 (GameplayTag): %s"), *ItemManifest.GetItemType().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   - 아이템 카테고리: %d"), (int32)ItemManifest.GetItemCategory());

	// 임시 인스턴스 파괴 (ItemManifest 복사 완료!)
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 임시 인스턴스 파괴 중..."));
	TempActor->Destroy();
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 임시 인스턴스 파괴 완료!"));

	// InventoryList에 직접 추가 (PickUp 방식과 동일!)
	UInv_InventoryItem* NewItem = ItemManifest.Manifest(GetOwner());
	if (!IsValid(NewItem))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemManifest.Manifest() 실패!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] UInv_InventoryItem 생성 성공!"));

	// FastArray에 추가 (PickUp의 AddEntry(ItemComponent)와 동일한 방식!)
	InventoryList.AddEntry(NewItem);

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] InventoryList.AddEntry 완료!"));

	// ListenServer/Standalone에서는 델리게이트 브로드캐스트
	if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ListenServer/Standalone 모드 - OnItemAdded 델리게이트 브로드캐스트"));
		OnItemAdded.Broadcast(NewItem);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] Dedicated Server 모드 - FastArray 리플리케이션에 의존"));
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT] 인벤토리에 아이템 추가 완료! (임시 Actor 스폰 없음!) ==="));
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

	// BuildMenu 닫기
	UInv_BuildingComponent* BuildingComp = OwningController->FindComponentByClass<UInv_BuildingComponent>();
	if (IsValid(BuildingComp))
	{
		BuildingComp->CloseBuildMenu();
		UE_LOG(LogTemp, Log, TEXT("인벤토리 닫힘: BuildMenu도 함께 닫힘"));
	}

	// CraftingMenu는 거리 체크로 자동으로 닫힘 (Timer 방식)
}

