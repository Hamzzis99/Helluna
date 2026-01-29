#include "Player/Inv_PlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Interaction/Inv_Highlightable.h"
#include "Crafting/Interfaces/Inv_CraftingInterface.h"
#include "Crafting/Actors/Inv_CraftingStation.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/HUD/Inv_HUDWidget.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Interfaces/Inv_Interface_Primary.cpp"

AInv_PlayerController::AInv_PlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	TraceLength = 500.0;
	ItemTraceChannel = ECC_GameTraceChannel1;
}

void AInv_PlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TraceForInteractables();
}

void AInv_PlayerController::ToggleInventory()
{
	if (!InventoryComponent.IsValid()) return;
	InventoryComponent->ToggleInventoryMenu();
	
	if (InventoryComponent->IsMenuOpen())
	{
		HUDWidget->SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		
		// ============================================
		// ⭐ [Phase 3 테스트] 인벤토리 닫을 때 Grid 상태 수집
		// ============================================
		// TODO: Phase 4 완료 후 이 코드 제거 (RPC로 대체)
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("🧪 [Phase 3 테스트] 인벤토리 닫힘 → CollectInventoryGridState() 호출"));
		
		TArray<FInv_SavedItemData> CollectedData = CollectInventoryGridState();
		
		UE_LOG(LogTemp, Warning, TEXT("🧪 [Phase 3 테스트] 수집 완료! %d개 아이템"), CollectedData.Num());
	}
}

void AInv_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [Inv_PlayerController] BeginPlay                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("║ Pawn: %s"), GetPawn() ? *GetPawn()->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (IsValid(Subsystem))
	{
		for (UInputMappingContext* CurrentContext : DefaultIMCs)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}
	}

	InventoryComponent = FindComponentByClass<UInv_InventoryComponent>();
	EquipmentComponent = FindComponentByClass<UInv_EquipmentComponent>();

	if (EquipmentComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] EquipmentComponent 찾음"));
	}

	CreateHUDWidget();
}

void AInv_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent);

	EnhancedInputComponent->BindAction(PrimaryInteractAction, ETriggerEvent::Started, this, &AInv_PlayerController::PrimaryInteract);
	EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &AInv_PlayerController::ToggleInventory);
	
	if (PrimaryWeaponAction)
	{
		EnhancedInputComponent->BindAction(PrimaryWeaponAction, ETriggerEvent::Started, this, &AInv_PlayerController::HandlePrimaryWeapon);
	}

	if (SecondaryWeaponAction)
	{
		EnhancedInputComponent->BindAction(SecondaryWeaponAction, ETriggerEvent::Started, this, &AInv_PlayerController::HandleSecondaryWeapon);
	}
}

void AInv_PlayerController::PrimaryInteract()
{
	if (!ThisActor.IsValid()) return;

	if (ThisActor->Implements<UInv_Interface_Primary>())
	{
		Server_Interact(ThisActor.Get());
		return; 
	}
	
	if (CurrentCraftingStation.IsValid() && CurrentCraftingStation == ThisActor)
	{
		if (ThisActor->Implements<UInv_CraftingInterface>())
		{
			IInv_CraftingInterface::Execute_OnInteract(ThisActor.Get(), this);
			return;
		}
	}

	UInv_ItemComponent* ItemComp = ThisActor->FindComponentByClass<UInv_ItemComponent>();
	if (!IsValid(ItemComp) || !InventoryComponent.IsValid()) return;

	InventoryComponent->TryAddItem(ItemComp);
}

void AInv_PlayerController::Server_Interact_Implementation(AActor* TargetActor)
{
	if (!TargetActor) return;

	if (TargetActor->Implements<UInv_Interface_Primary>())
	{
		IInv_Interface_Primary::Execute_ExecuteInteract(TargetActor, this);
	}
}

void AInv_PlayerController::CreateHUDWidget()
{
	if (!IsLocalController()) return;
	
	if (!HUDWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HUDWidgetClass가 설정되지 않음"));
		return;
	}

	if (!HUDWidget)
	{
		HUDWidget = CreateWidget<UInv_HUDWidget>(this, HUDWidgetClass);
		if (IsValid(HUDWidget))
		{
			HUDWidget->AddToViewport();
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HUD 위젯 생성됨"));
		}
	}
}

void AInv_PlayerController::TraceForInteractables()
{
	if (!IsValid(GEngine) || !IsValid(GEngine->GameViewport)) return;
	FVector2D ViewportSize;
	GEngine->GameViewport->GetViewportSize(ViewportSize);
	const FVector2D ViewportCenter = ViewportSize / 2.f;

	FVector TraceStart;
	FVector Forward;
	if (!UGameplayStatics::DeprojectScreenToWorld(this, ViewportCenter, TraceStart, Forward)) return;
	 
	const FVector TraceEnd = TraceStart + (Forward * TraceLength);
	FHitResult HitResult;
	GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ItemTraceChannel);

	LastActor = ThisActor;
	ThisActor = HitResult.GetActor();

	bool bIsCraftingStation = false;
	if (ThisActor.IsValid() && ThisActor->Implements<UInv_CraftingInterface>())
	{
		CurrentCraftingStation = ThisActor;
		bIsCraftingStation = true;
	}
	else
	{
		CurrentCraftingStation = nullptr;
	}

	if (!ThisActor.IsValid())
	{
		if (IsValid(HUDWidget))
		{
			HUDWidget->HidePickupMessage();
		}
		return;
	}

	if (ThisActor == LastActor) return;

	if (ThisActor.IsValid())
	{
		if (UActorComponent* Highlightable = ThisActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IInv_Highlightable::Execute_Highlight(Highlightable);
		}

		if (IsValid(HUDWidget))
		{
			if (bIsCraftingStation)
			{
				AInv_CraftingStation* CraftingStation = Cast<AInv_CraftingStation>(ThisActor.Get());
				if (IsValid(CraftingStation))
				{
					HUDWidget->ShowPickupMessage(CraftingStation->GetPickupMessage());
				}
			}
			else
			{
				UInv_ItemComponent* ItemComponent = ThisActor->FindComponentByClass<UInv_ItemComponent>();
				if (IsValid(ItemComponent))
				{
					HUDWidget->ShowPickupMessage(ItemComponent->GetPickupMessage());
				}
			}
		}
	}

	if (LastActor.IsValid())
	{
		if (UActorComponent* Highlightable = LastActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IInv_Highlightable::Execute_UnHighlight(Highlightable);
		}
	}
}

void AInv_PlayerController::HandlePrimaryWeapon()
{
	if (EquipmentComponent.IsValid())
	{
		EquipmentComponent->HandlePrimaryWeaponInput();
	}
}

void AInv_PlayerController::HandleSecondaryWeapon()
{
	if (EquipmentComponent.IsValid())
	{
		EquipmentComponent->HandleSecondaryWeaponInput();
	}
}

// ============================================
// 📌 인벤토리 저장/로드용 함수 (Phase 3)
// ============================================

/**
 * 현재 클라이언트 UI의 인벤토리 Grid 상태를 수집
 */
TArray<FInv_SavedItemData> AInv_PlayerController::CollectInventoryGridState()
{
	TArray<FInv_SavedItemData> Result;

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║          [Phase 3] CollectInventoryGridState() - 인벤토리 상태 수집           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 호출 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: UI의 Grid 상태를 수집하여 서버로 전송할 데이터 생성                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	// ============================================
	// Step 1: InventoryComponent 유효성 검사
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] InventoryComponent 확인"));
	
	if (!InventoryComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent가 nullptr입니다!"));
		UE_LOG(LogTemp, Error, TEXT("     → PlayerController에 InventoryComponent가 없거나 초기화 안 됨"));
		return Result;
	}
	UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryComponent 유효함"));

	// ============================================
	// Step 2: InventoryMenu 가져오기
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] InventoryMenu(위젯) 가져오기"));
	
	UInv_InventoryBase* InventoryMenu = InventoryComponent->GetInventoryMenu();
	if (!IsValid(InventoryMenu))
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryMenu가 nullptr입니다!"));
		UE_LOG(LogTemp, Error, TEXT("     → 인벤토리 위젯이 생성되지 않았거나 파괴됨"));
		UE_LOG(LogTemp, Error, TEXT("     → InventoryComponent::BeginPlay()에서 위젯 생성 확인 필요"));
		return Result;
	}
	UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryMenu 유효함: %s"), *InventoryMenu->GetName());

	// ============================================
	// Step 3: SpatialInventory로 캐스트
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] SpatialInventory로 캐스트"));
	
	UInv_SpatialInventory* SpatialInventory = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInventory))
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ SpatialInventory로 캐스트 실패!"));
		UE_LOG(LogTemp, Error, TEXT("     → InventoryMenu 클래스: %s"), *InventoryMenu->GetClass()->GetName());
		UE_LOG(LogTemp, Error, TEXT("     → UInv_SpatialInventory 상속 확인 필요"));
		return Result;
	}
	UE_LOG(LogTemp, Warning, TEXT("  ✅ SpatialInventory 캐스트 성공"));

	// ============================================
	// Step 4: 3개 Grid 접근 및 상태 수집
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 4] 3개 Grid에서 아이템 수집"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	// Grid 배열 구성
	struct FGridInfo
	{
		UInv_InventoryGrid* Grid;
		const TCHAR* Name;
		uint8 Category;
	};

	FGridInfo Grids[] = {
		{ SpatialInventory->GetGrid_Equippables(),  TEXT("Grid_Equippables (장비)"),   0 },
		{ SpatialInventory->GetGrid_Consumables(), TEXT("Grid_Consumables (소모품)"), 1 },
		{ SpatialInventory->GetGrid_Craftables(),  TEXT("Grid_Craftables (재료)"),    2 }
	};

	int32 TotalCollected = 0;

	for (const FGridInfo& GridInfo : Grids)
	{
		UE_LOG(LogTemp, Warning, TEXT("  │"));
		UE_LOG(LogTemp, Warning, TEXT("  ├─ [Grid %d] %s"), GridInfo.Category, GridInfo.Name);

		if (!IsValid(GridInfo.Grid))
		{
			UE_LOG(LogTemp, Warning, TEXT("  │    ⚠️ Grid가 nullptr! 건너뜀"));
			continue;
		}

		// 각 Grid의 상태 수집
		TArray<FInv_SavedItemData> GridItems = GridInfo.Grid->CollectGridState();
		
		UE_LOG(LogTemp, Warning, TEXT("  │    📦 수집된 아이템: %d개"), GridItems.Num());
		
		for (int32 i = 0; i < GridItems.Num(); ++i)
		{
			const FInv_SavedItemData& Item = GridItems[i];
			UE_LOG(LogTemp, Warning, TEXT("  │      [%d] %s x%d @ Pos(%d,%d)"), 
				i, *Item.ItemType.ToString(), Item.StackCount,
				Item.GridPosition.X, Item.GridPosition.Y);
		}

		TotalCollected += GridItems.Num();
		Result.Append(GridItems);
	}

	UE_LOG(LogTemp, Warning, TEXT("  │"));
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));

	// ============================================
	// 최종 결과 출력
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║                        📊 수집 결과 요약                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 총 수집된 아이템: %d개                                                        "), Result.Num());
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	
	// 카테고리별 통계
	int32 EquipCount = 0, ConsumeCount = 0, CraftCount = 0;
	for (const FInv_SavedItemData& Item : Result)
	{
		switch (Item.GridCategory)
		{
			case 0: EquipCount++; break;
			case 1: ConsumeCount++; break;
			case 2: CraftCount++; break;
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("║ 장비(0):   %d개                                                               "), EquipCount);
	UE_LOG(LogTemp, Warning, TEXT("║ 소모품(1): %d개                                                               "), ConsumeCount);
	UE_LOG(LogTemp, Warning, TEXT("║ 재료(2):   %d개                                                               "), CraftCount);
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	
	// 전체 아이템 목록
	for (int32 i = 0; i < Result.Num(); ++i)
	{
		const FInv_SavedItemData& Item = Result[i];
		UE_LOG(LogTemp, Warning, TEXT("║ [%02d] %s"), i, *Item.ToString());
	}
	
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	return Result;
}

/**
 * 저장된 상태로 인벤토리 Grid 복원
 */
void AInv_PlayerController::RestoreInventoryFromState(const TArray<FInv_SavedItemData>& SavedItems)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║          [Phase 3] RestoreInventoryFromState() - 인벤토리 상태 복원           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 호출 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: 저장된 Grid 위치로 아이템 배치 복원                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 상태: ⚠️ Phase 5에서 실제 로직 구현 예정                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
	
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 복원할 아이템 목록 (%d개):"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
	
	// ============================================
	// Phase 5에서 구현할 내용:
	// ============================================
	// 1. InventoryComponent → SpatialInventory 접근
	// 2. 각 SavedItem 순회:
	//    a. GridCategory로 해당 Grid 선택
	//       - 0 → Grid_Equippables
	//       - 1 → Grid_Consumables
	//       - 2 → Grid_Craftables
	//    b. ItemType으로 현재 UI에서 해당 아이템 찾기
	//    c. 아이템을 GridPosition으로 이동
	//    d. StackCount 설정 (Split 복원)
	// 3. 충돌 처리 (이미 다른 아이템이 있는 경우)

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("⚠️ [TODO] Phase 5에서 실제 복원 로직 구현 예정"));
	UE_LOG(LogTemp, Warning, TEXT("   현재는 FastArray 리플리케이션으로 자동 배치됨 (위치 복원 없음)"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}
