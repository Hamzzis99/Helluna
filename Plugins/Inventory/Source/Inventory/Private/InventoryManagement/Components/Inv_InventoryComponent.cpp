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
		// ⭐ Entry Index 계산 (새로 추가된 항목은 맨 뒤)
		int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
		UE_LOG(LogTemp, Warning, TEXT("[SERVER PICKUP] ListenServer/Standalone 모드 - OnItemAdded 델리게이트 브로드캐스트 (EntryIndex=%d)"), NewEntryIndex);
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
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

	// ⭐ 공간 체크 (InventoryList 기반 - UI 없이 작동!)
	bool bHasRoom = HasRoomInInventoryList(ItemManifest);
	
	if (!bHasRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ❌ 인벤토리에 공간이 없습니다!"));
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작 취소! NoRoomInInventory 델리게이트 브로드캐스트"));
		
		// 임시 인스턴스 파괴
		TempActor->Destroy();
		
		// NoRoomInInventory 델리게이트 재사용!
		NoRoomInInventory.Broadcast();
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 인벤토리에 공간 있음!"))


	// 임시 인스턴스 파괴 (ItemManifest 복사 완료 & 공간 체크 완료!)
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
		// ⭐ Entry Index 계산 (새로 추가된 항목은 맨 뒤)
		int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ListenServer/Standalone 모드 - OnItemAdded 델리게이트 브로드캐스트 (EntryIndex=%d)"), NewEntryIndex);
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] Dedicated Server 모드 - FastArray 리플리케이션에 의존"));
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT] 인벤토리에 아이템 추가 완료! (임시 Actor 스폰 없음!) ==="));
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
		// 이 로그는 서버에만 기록됩니다.
		UE_LOG(LogTemp, Warning, TEXT("[CHEAT DETECTION?] Server check failed for material %s. Required: %d, Has: %d"), 
			*MaterialTag.ToString(), RequiredAmount, CurrentAmount);
		return false;
	}

	return true;
}

// ⭐ 크래프팅 통합 RPC: [안전성 강화] 서버 측 재료 검증 추가
void UInv_InventoryComponent::Server_CraftItemWithMaterials_Implementation(
	TSubclassOf<AActor> ItemActorClass,
	const FGameplayTag& MaterialTag1, int32 Amount1,
	const FGameplayTag& MaterialTag2, int32 Amount2,
	const FGameplayTag& MaterialTag3, int32 Amount3,
	int32 CraftedAmount)  // ⭐ 제작 개수 파라미터 추가!
{
	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT WITH MATERIALS] 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작 개수: %d"), CraftedAmount);

	// 서버 권한 체크
	if (!GetOwner()->HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 권한 없음!"));
		return;
	}

	if (!ItemActorClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemActorClass가 nullptr!"));
		return;
	}

	// ========== ⭐ 1단계: [안전성 강화] 서버 측 재료 검증 ==========
	// 클라이언트의 요청을 신뢰하지 않고, 서버가 직접 재료 보유 여부를 확인합니다.
	if (!HasRequiredMaterialsOnServer(MaterialTag1, Amount1) ||
		!HasRequiredMaterialsOnServer(MaterialTag2, Amount2) ||
		!HasRequiredMaterialsOnServer(MaterialTag3, Amount3))
	{
		// 재료가 부족하므로 제작을 중단합니다. 클라이언트에는 별도 알림 없이, 서버 로그만 기록합니다.
		// 이는 비정상적인 요청(치트 등)일 가능성이 높습니다.
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 재료 부족! 클라이언트 요청 거부. (잠재적 치트 시도)"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[SERVER CRAFT] ✅ 서버 측 재료 검증 통과."));


	// ========== 2단계: 임시 Actor 스폰 및 ItemManifest 추출 ==========
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;
	
	FVector TempLocation = FVector(0, 0, -50000);
	FRotator TempRotation = FRotator::ZeroRotator;
	FTransform TempTransform(TempRotation, TempLocation);
	
	AActor* TempActor = GetWorld()->SpawnActorDeferred<AActor>(ItemActorClass, TempTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(TempActor))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] 임시 Actor 생성 실패!"));
		return;
	}

	TempActor->FinishSpawning(TempTransform);

	UInv_ItemComponent* DefaultItemComp = TempActor->FindComponentByClass<UInv_ItemComponent>();
	if (!IsValid(DefaultItemComp))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemComponent를 찾을 수 없음!"));
		TempActor->Destroy();
		return;
	}

	FInv_ItemManifest ItemManifest = DefaultItemComp->GetItemManifest();
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 제작할 아이템: %s (카테고리: %d)"), 
		*ItemManifest.GetItemType().ToString(), (int32)ItemManifest.GetItemCategory());

	// ========== 3단계: 공간 체크 ==========
	bool bHasRoom = HasRoomInInventoryList(ItemManifest);
	
	if (!bHasRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ❌ 인벤토리에 공간이 없습니다!"));
		TempActor->Destroy();
		NoRoomInInventory.Broadcast(); // 클라이언트에 공간 없음 알림
		return; // 재료 차감 없이 중단!
	}

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 인벤토리에 공간 있음!"));

	// 임시 Actor 파괴
	TempActor->Destroy();

	// ========== 4단계: 재료 차감 (모든 검증 통과 후!) ==========
	// Server_ConsumeMaterialsMultiStack은 서버에서만 호출 가능한 RPC이므로,
	// _Implementation을 직접 호출하여 서버 내에서 함수를 실행합니다.
	if (MaterialTag1.IsValid() && Amount1 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 재료1 차감: %s x %d"), *MaterialTag1.ToString(), Amount1);
		Server_ConsumeMaterialsMultiStack_Implementation(MaterialTag1, Amount1);
	}

	if (MaterialTag2.IsValid() && Amount2 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 재료2 차감: %s x %d"), *MaterialTag2.ToString(), Amount2);
		Server_ConsumeMaterialsMultiStack_Implementation(MaterialTag2, Amount2);
	}

	if (MaterialTag3.IsValid() && Amount3 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 재료3 차감: %s x %d"), *MaterialTag3.ToString(), Amount3);
		Server_ConsumeMaterialsMultiStack_Implementation(MaterialTag3, Amount3);
	}

	// ========== 5단계: 아이템 생성 (⭐ 여유 공간 있는 스택 검색 로직!) ==========
	FGameplayTag ItemType = ItemManifest.GetItemType();

	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 🔍 여유 공간 있는 스택 검색 시작: ItemType=%s"), *ItemType.ToString());

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
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]     Entry 발견 (Non-stackable) - 건너뜀"));
			continue;  // Non-stackable은 새 Entry 생성해야 함
		}

		const FInv_StackableFragment* Fragment = Entry.Item->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>();
		int32 MaxStackSize = Fragment ? Fragment->GetMaxStackSize() : 999;

		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]     Entry 발견: %d / %d"),
			CurrentCount, MaxStackSize);

		// ⭐ 여유 공간 있는 스택 발견!
		if (CurrentCount < MaxStackSize)
		{
			ExistingItem = Entry.Item;
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ 여유 공간 있는 스택 발견! %d / %d (Item포인터: %p)"),
				CurrentCount, MaxStackSize, ExistingItem);
			break;  // 첫 번째 여유 공간 발견 시 중단
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]     스택 가득 참 (%d/%d) - 다음 Entry 검색"),
				CurrentCount, MaxStackSize);
		}
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

		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ⭐ 기존 스택에 추가! %d → %d (추가량: %d/%d, Overflow: %d)"),
			CurrentCount, NewCount, ToAdd, CraftedAmount, Overflow);

		ExistingItem->SetTotalStackCount(NewCount);

		// Fragment도 동기화
		FInv_StackableFragment* MutableFragment = ExistingItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>();
		if (MutableFragment)
		{
			MutableFragment->SetStackCount(NewCount);
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ StackableFragment도 업데이트: %d"), NewCount);
		}

		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   📊 최종 확인: TotalStackCount=%d, Fragment.StackCount=%d"),
			NewCount, MutableFragment ? MutableFragment->GetStackCount() : -1);

		// 리플리케이션 활성화
		for (auto& Entry : InventoryList.Entries)
		{
			if (Entry.Item == ExistingItem)
			{
				InventoryList.MarkItemDirty(Entry);
				UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ MarkItemDirty 호출! 리플리케이션 활성화"));
				break;
			}
		}

		// ⭐⭐⭐ Overflow 처리: 넘친 개수만큼 새 Entry 생성!
		if (Overflow > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ⚠️ Overflow 발생! %d개가 MaxStackSize 초과 → 새 Entry 생성"), Overflow);

			// 새 Entry 생성을 위해 ItemManifest 다시 Manifest
			UInv_InventoryItem* OverflowItem = ItemManifest.Manifest(GetOwner());

			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 🆕 Overflow Entry 생성 완료!"));
			UE_LOG(LogTemp, Warning, TEXT("    Item포인터: %p"), OverflowItem);
			UE_LOG(LogTemp, Warning, TEXT("    ItemType: %s"), *ItemType.ToString());
			UE_LOG(LogTemp, Warning, TEXT("    Overflow 개수: %d"), Overflow);

			// Overflow 개수로 초기화
			OverflowItem->SetTotalStackCount(Overflow);

			// Fragment도 동기화
			FInv_StackableFragment* OverflowMutableFragment = OverflowItem->GetItemManifestMutable().GetFragmentOfTypeMutable<FInv_StackableFragment>();
			if (OverflowMutableFragment)
			{
				OverflowMutableFragment->SetStackCount(Overflow);
				UE_LOG(LogTemp, Warning, TEXT("    ✅ Overflow StackableFragment도 업데이트: %d"), Overflow);
			}

			UE_LOG(LogTemp, Warning, TEXT("    최종 TotalStackCount: %d"), Overflow);

			// InventoryList에 추가
			InventoryList.AddEntry(OverflowItem);
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ Overflow Entry 추가 완료!"));

			// ListenServer/Standalone에서는 OnItemAdded 델리게이트 브로드캐스트
			if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone)
			{
				int32 OverflowEntryIndex = InventoryList.Entries.Num() - 1;
				OnItemAdded.Broadcast(OverflowItem, OverflowEntryIndex);
				UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ Overflow OnItemAdded 브로드캐스트 완료! (EntryIndex=%d)"), OverflowEntryIndex);
			}
		}

		// ListenServer/Standalone에서는 기존 스택 업데이트도 브로드캐스트
		if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone)
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
			UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ✅ OnItemAdded 브로드캐스트 완료! (EntryIndex=%d)"), EntryIndex);
		}

		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 제작 완료! 기존 스택에 추가됨 (Overflow: %s)"),
			Overflow > 0 ? TEXT("새 Entry 생성됨") : TEXT("없음"));
		UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT WITH MATERIALS] 완료 ==="));
		return; // ⭐ 여기서 리턴! 새 Entry 생성하지 않음!
	}
	else
	{
		// ⭐ 여유 공간 없음 → 새 Entry 생성
		UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT]   ⚠️ 모든 스택 가득 참 또는 기존 스택 없음, 새 Entry 생성"));
	}

	// ========== 기존 스택이 없거나 가득 찬 경우: 새 Entry 생성 ==========
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

	UInv_InventoryItem* NewItem = ItemManifest.Manifest(GetOwner());
	if (!IsValid(NewItem))
	{
		UE_LOG(LogTemp, Error, TEXT("[SERVER CRAFT] ItemManifest.Manifest() 실패!"));
		// 여기서 재료를 롤백하는 로직을 추가할 수 있으나, 현재는 생략합니다.
		return;
	}

	// ⭐ 새 Item 생성 후 상태 확인
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] 🆕 새 Entry 생성 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("    Item포인터: %p"), NewItem);
	UE_LOG(LogTemp, Warning, TEXT("    ItemType: %s"), *NewItem->GetItemManifest().GetItemType().ToString());

	int32 InitialCount = NewItem->GetTotalStackCount();
	UE_LOG(LogTemp, Warning, TEXT("    TotalStackCount (초기값): %d"), InitialCount);

	const FInv_StackableFragment* NewItemFragment = NewItem->GetItemManifest().GetFragmentOfType<FInv_StackableFragment>();
	if (NewItemFragment)
	{
		int32 FragmentCount = NewItemFragment->GetStackCount();
		int32 MaxStackSize = NewItemFragment->GetMaxStackSize();

		UE_LOG(LogTemp, Warning, TEXT("    StackableFragment 발견!"));
		UE_LOG(LogTemp, Warning, TEXT("      Fragment->GetStackCount(): %d"), FragmentCount);
		UE_LOG(LogTemp, Warning, TEXT("      Fragment->GetMaxStackSize(): %d"), MaxStackSize);

		if (InitialCount != FragmentCount)
		{
			UE_LOG(LogTemp, Error, TEXT("    ❌ 불일치! TotalStackCount(%d) != Fragment.StackCount(%d)"),
				InitialCount, FragmentCount);
		}

		// ⭐ 스택을 CraftedAmount로 초기화!
		if (InitialCount == 0 || InitialCount != CraftedAmount)
		{
			UE_LOG(LogTemp, Warning, TEXT("    ⭐ TotalStackCount를 CraftedAmount(%d)로 초기화!"), CraftedAmount);
			NewItem->SetTotalStackCount(CraftedAmount);

			// Fragment도 업데이트
			FInv_ItemManifest& NewItemManifest = NewItem->GetItemManifestMutable();
			if (FInv_StackableFragment* MutableFrag = NewItemManifest.GetFragmentOfTypeMutable<FInv_StackableFragment>())
			{
				MutableFrag->SetStackCount(CraftedAmount);
			}

			UE_LOG(LogTemp, Warning, TEXT("    최종 TotalStackCount: %d"), NewItem->GetTotalStackCount());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("    ❌ StackableFragment가 없습니다 (Non-stackable 아이템)"));
	}

	InventoryList.AddEntry(NewItem);
	UE_LOG(LogTemp, Warning, TEXT("[SERVER CRAFT] ✅ 제작 완료! 새 Entry 추가됨"));

	// ListenServer/Standalone에서는 델리게이트 브로드캐스트
	if (GetOwner()->GetNetMode() == NM_ListenServer || GetOwner()->GetNetMode() == NM_Standalone)
	{
		// ⭐ Entry Index 계산 (새로 추가된 항목은 맨 뒤)
		int32 NewEntryIndex = InventoryList.Entries.Num() - 1;
		OnItemAdded.Broadcast(NewItem, NewEntryIndex);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [SERVER CRAFT WITH MATERIALS] 완료 ==="));
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
		// 아이템 제거됨 - ⭐ Entry Index 전달!
		OnItemRemoved.Broadcast(Item, ItemEntryIndex);
		UE_LOG(LogTemp, Warning, TEXT("OnItemRemoved 브로드캐스트 완료 (EntryIndex=%d)"), ItemEntryIndex);
	}
	else
	{
		// 스택 개수만 변경됨 - OnStackChange 브로드캐스트
		FInv_SlotAvailabilityResult Result;
		Result.Item = Item;
		Result.bStackable = true;
		Result.TotalRoomToFill = NewCount;
		Result.EntryIndex = ItemEntryIndex; // ⭐ Entry Index 추가

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
	
	// ⭐ InventoryMenu의 Grid 크기를 Component 설정에 동기화 (Blueprint Widget → Component)
	SyncGridSizesFromWidget();
}

// ⭐ Blueprint Widget의 Grid 크기를 Component 설정으로 가져오기
void UInv_InventoryComponent::SyncGridSizesFromWidget()
{
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] Grid 크기 참조 시작..."));
	
	// ⭐ 1순위: Blueprint에서 직접 선택한 Widget 참조
	if (IsValid(InventoryGridReference))
	{
		GridRows = InventoryGridReference->GetRows();
		GridColumns = InventoryGridReference->GetColumns();
		
		UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ✅ Grid (Blueprint 직접 참조): %d x %d = %d칸"), 
			GridRows, GridColumns, GridRows * GridColumns);
	}
	// 2순위: InventoryMenu에서 자동으로 가져오기 (Grid_Equippables 사용)
	else if (IsValid(InventoryMenu))
	{
		UInv_SpatialInventory* SpatialInv = Cast<UInv_SpatialInventory>(InventoryMenu);
		if (IsValid(SpatialInv) && IsValid(SpatialInv->GetGrid_Equippables()))
		{
			GridRows = SpatialInv->GetGrid_Equippables()->GetRows();
			GridColumns = SpatialInv->GetGrid_Equippables()->GetColumns();
			
			UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ✅ Grid (InventoryMenu 자동 - Grid_Equippables): %d x %d = %d칸"), 
				GridRows, GridColumns, GridRows * GridColumns);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ⚠️ Grid 참조 없음 - 기본값 사용: %d x %d = %d칸"), 
				GridRows, GridColumns, GridRows * GridColumns);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] ⚠️ InventoryMenu 없음 - 기본값 사용: %d x %d = %d칸"), 
				GridRows, GridColumns, GridRows * GridColumns);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[Grid 동기화] 완료! 모든 카테고리(Equippables/Consumables/Craftables)가 동일한 크기 사용"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
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

// ⭐ InventoryList 기반 공간 체크 (서버 전용, UI 없이 작동!)
bool UInv_InventoryComponent::HasRoomInInventoryList(const FInv_ItemManifest& Manifest) const
{
	EInv_ItemCategory Category = Manifest.GetItemCategory();
	FGameplayTag ItemType = Manifest.GetItemType();
	
	// GridFragment에서 아이템 크기 가져오기
	const FInv_GridFragment* GridFragment = Manifest.GetFragmentOfType<FInv_GridFragment>();
	FIntPoint ItemSize = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 제작할 아이템: %s"), *ItemType.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 아이템 카테고리: %d"), (int32)Category);
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 아이템 크기: %d x %d"), ItemSize.X, ItemSize.Y);

	// ⭐ Grid 크기 설정 (Component 설정에서 가져오기)
	int32 LocalGridRows = GridRows;  // ⭐ 지역 변수로 복사 (const 함수에서 수정 가능)
	int32 LocalGridColumns = GridColumns;
	int32 MaxSlots = LocalGridRows * LocalGridColumns;
	UInv_InventoryGrid* TargetGrid = nullptr;
	
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Component 설정: %d x %d = %d칸 (모든 카테고리 공통)"), 
		LocalGridRows, LocalGridColumns, MaxSlots);
	
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
			default:
				UE_LOG(LogTemp, Warning, TEXT("[공간체크] ⚠️ 알 수 없는 카테고리: %d"), (int32)Category);
				break;
			}
			
			if (IsValid(TargetGrid))
			{
				LocalGridRows = TargetGrid->GetRows();  // ⭐ 지역 변수 사용
				LocalGridColumns = TargetGrid->GetColumns();
				MaxSlots = TargetGrid->GetMaxSlots();

				UE_LOG(LogTemp, Warning, TEXT("[공간체크] Grid 설정: %d x %d = %d칸"),
					LocalGridRows, LocalGridColumns, MaxSlots);

				// ⭐⭐⭐ 실제 UI GridSlots 상태 기반 공간 체크! (플레이어가 옮긴 위치 반영!)
				bool bHasRoom = TargetGrid->HasRoomInActualGrid(Manifest);

				UE_LOG(LogTemp, Warning, TEXT("[공간체크] 🔍 Grid->HasRoomInActualGrid() 결과: %s"),
					bHasRoom ? TEXT("✅ 실제 UI Grid에 공간 있음!") : TEXT("❌ UI Grid 꽉 참!"));
				UE_LOG(LogTemp, Warning, TEXT("========================================"));

				return bHasRoom;
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] ⚠️ InventoryMenu가 nullptr - Fallback 로직 사용"));
	}

	// ========== Fallback: Virtual Grid 시뮬레이션 (서버 전용) ==========
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Fallback 모드: Virtual Grid 시뮬레이션 시작"));
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Grid 크기: %d x %d = %d칸"), LocalGridRows, LocalGridColumns, MaxSlots);
	
	// Virtual Grid 생성 (0 = 빈 칸, 1~ = 아이템 인덱스)
	TArray<int32> VirtualGrid;
	VirtualGrid.SetNum(MaxSlots);
	for (int32 i = 0; i < MaxSlots; i++)
	{
		VirtualGrid[i] = 0; // 모두 빈 칸으로 초기화
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 📋 Virtual Grid 초기화 완료 (%dx%d)"), LocalGridRows, LocalGridColumns);
	
	// 1. 현재 인벤토리의 아이템들을 Virtual Grid에 배치
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 현재 인벤토리 내용을 Grid에 배치 중..."));
	
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
			
			UE_LOG(LogTemp, Warning, TEXT("[공간체크]   - [%d] %s x%d (크기: %dx%d, 실제위치: [%d,%d])"), 
				CurrentItemCount, *EntryType.ToString(), StackCount, ExistingItemSize.X, ExistingItemSize.Y,
				ActualPos.X, ActualPos.Y);
			
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
					UE_LOG(LogTemp, Warning, TEXT("[공간체크]     → ✅ 실제 위치 Grid[%d,%d]에 배치됨"), ActualPos.X, ActualPos.Y);
				}
			}
			
			// ⚠️ 실제 위치가 없거나 배치 실패하면 순차 배치 시도 (Fallback)
			if (!bPlaced)
			{
				UE_LOG(LogTemp, Warning, TEXT("[공간체크]     → ⚠️ 실제 위치 사용 불가! Fallback 순차 배치 시도..."));
				
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
							UE_LOG(LogTemp, Warning, TEXT("[공간체크]     → Fallback Grid[%d,%d]에 배치됨"), Col, Row);
						}
					}
				}
			}
			
			if (!bPlaced)
			{
				UE_LOG(LogTemp, Error, TEXT("[공간체크]     → ❌ 배치 실패! (Grid 시뮬레이션 오류 가능성)"));
			}
			
			ItemIndex++;
			CurrentItemCount++;
		}
	}
	
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
	
	// 3. 스택 가능 여부 체크
	const FInv_StackableFragment* StackableFragment = Manifest.GetFragmentOfType<FInv_StackableFragment>();
	bool bStackable = (StackableFragment != nullptr);

	if (bStackable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] 🔍 스택 가능 아이템 - 기존 스택 찾기 중..."));
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
					UE_LOG(LogTemp, Warning, TEXT("[공간체크] ✅ 스택 가능! (현재: %d / 최대: %d)"), CurrentStack, MaxStack);
					UE_LOG(LogTemp, Warning, TEXT("========================================"));
					return true;
				}
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] ⚠️ 스택 여유 없음 - 새 슬롯 필요"));
	}

	// 4. 새로운 아이템을 배치할 수 있는지 체크
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] 🔍 새 아이템 배치 가능 여부 체크 (크기: %dx%d)"), ItemSize.X, ItemSize.Y);
	
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
				UE_LOG(LogTemp, Warning, TEXT("[공간체크] ✅ 배치 가능! Grid[%d,%d]부터 %dx%d 공간 확보됨"), 
					Col, Row, ItemSize.X, ItemSize.Y);
			}
		}
	}
	
	if (!bHasRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("[공간체크] ❌ Grid 꽉 참! %dx%d 크기의 빈 공간 없음"), ItemSize.X, ItemSize.Y);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[공간체크] Virtual Grid 결과: %s"), 
		bHasRoom ? TEXT("✅ 공간 있음") : TEXT("❌ 공간 없음"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	return bHasRoom;
}
