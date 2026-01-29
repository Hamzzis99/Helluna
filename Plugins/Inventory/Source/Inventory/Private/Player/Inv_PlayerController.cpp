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
	UE_LOG(LogTemp, Warning, TEXT("║          [Phase 5] RestoreInventoryFromState() - 인벤토리 상태 복원           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 호출 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: 저장된 Grid 위치로 아이템 배치 복원                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	if (SavedItems.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 복원할 아이템이 없습니다."));
		return;
	}

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
	// Step 1: InventoryComponent 접근
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] InventoryComponent 접근"));

	if (!InventoryComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ InventoryComponent가 유효하지 않습니다!"));
		return;
	}

	// ============================================
	// Step 2: SpatialInventory 접근
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] SpatialInventory 접근"));

	UInv_InventoryBase* InventoryMenu = InventoryComponent->GetInventoryMenu();
	if (!IsValid(InventoryMenu))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ InventoryMenu가 nullptr!"));
		return;
	}

	UInv_SpatialInventory* SpatialInventory = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInventory))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ SpatialInventory로 캐스트 실패!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("   ✅ SpatialInventory 접근 성공"));

	// ============================================
	// Step 3: 각 Grid에 위치 복원 요청
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] 각 Grid에 위치 복원 요청"));

	int32 TotalRestored = 0;

	// Grid 배열 구성
	struct FGridRestoreInfo
	{
		UInv_InventoryGrid* Grid;
		const TCHAR* Name;
	};

	FGridRestoreInfo Grids[] = {
		{ SpatialInventory->GetGrid_Equippables(),  TEXT("Grid_Equippables (장비)") },
		{ SpatialInventory->GetGrid_Consumables(), TEXT("Grid_Consumables (소모품)") },
		{ SpatialInventory->GetGrid_Craftables(),  TEXT("Grid_Craftables (재료)") }
	};

	for (const FGridRestoreInfo& GridInfo : Grids)
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("   📦 %s"), GridInfo.Name);

		if (!IsValid(GridInfo.Grid))
		{
			UE_LOG(LogTemp, Warning, TEXT("      ⚠️ Grid가 nullptr! 건너뜀"));
			continue;
		}

		int32 RestoredInGrid = GridInfo.Grid->RestoreItemPositions(SavedItems);
		TotalRestored += RestoredInGrid;

		UE_LOG(LogTemp, Warning, TEXT("      → %d개 복원됨"), RestoredInGrid);
	}

	// ============================================
	// 최종 결과
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║                        📊 복원 결과 요약                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 요청: %d개 아이템                                                             "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("║ 복원: %d개 성공                                                               "), TotalRestored);
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 인벤토리 저장 RPC 구현 (Phase 4)
// ============================================

/**
 * [서버 → 클라이언트] 인벤토리 상태 요청
 */
void AInv_PlayerController::Client_RequestInventoryState_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] Client_RequestInventoryState - 서버로부터 요청 수신           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 요청자: 서버 (자동저장/로그아웃)                                           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	// Step 1: 인벤토리 상태 수집
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] CollectInventoryGridState() 호출하여 UI 상태 수집..."));
	
	TArray<FInv_SavedItemData> CollectedData = CollectInventoryGridState();

	// Step 2: 서버로 전송
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] Server_ReceiveInventoryState() RPC로 서버에 전송..."));
	UE_LOG(LogTemp, Warning, TEXT("   전송할 아이템: %d개"), CollectedData.Num());

	Server_ReceiveInventoryState(CollectedData);

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("✅ 클라이언트 → 서버 전송 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

/**
 * [클라이언트 → 서버] 수집된 인벤토리 상태 전송
 */
void AInv_PlayerController::Server_ReceiveInventoryState_Implementation(const TArray<FInv_SavedItemData>& SavedItems)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] Server_ReceiveInventoryState - 클라이언트로부터 수신          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 서버                                                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 전송자: 클라이언트 (%s)                                                    "), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 수신된 아이템: %d개"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));

	// 델리게이트 브로드캐스트 (GameMode에서 바인딩하여 저장 처리)
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ OnInventoryStateReceived 델리게이트 브로드캐스트..."));

	if (OnInventoryStateReceived.IsBound())
	{
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 델리게이트 바인딩됨! 브로드캐스트 실행"));
		OnInventoryStateReceived.Broadcast(this, SavedItems);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 델리게이트 바인딩 안 됨! (GameMode에서 바인딩 필요)"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("✅ 서버 수신 처리 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

// ============================================
// 📌 인벤토리 로드 RPC 구현 (Phase 5)
// ============================================

/**
 * [서버 → 클라이언트] 저장된 인벤토리 데이터 수신
 */
void AInv_PlayerController::Client_ReceiveInventoryData_Implementation(const TArray<FInv_SavedItemData>& SavedItems)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 5] Client_ReceiveInventoryData - 서버로부터 인벤토리 데이터 수신  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 수신된 아이템: %d개                                                        "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	if (SavedItems.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 저장된 인벤토리 데이터가 없습니다. (신규 플레이어?)"));
		UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
		return;
	}

	// 수신된 아이템 목록 출력
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 수신된 아이템 목록:"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));

	// ============================================
	// FastArray 리플리케이션 완료 대기 후 Grid 위치 복원
	// ============================================
	// 서버에서 아이템이 추가되면 FastArray가 클라이언트로 리플리케이트됨
	// 리플리케이션 완료 후 Grid 위치를 복원해야 하므로 딜레이 필요
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 0.5초 후 Grid 위치 복원 예약..."));
	UE_LOG(LogTemp, Warning, TEXT("   (FastArray 리플리케이션 완료 대기)"));

	// SavedItems 복사본 생성 (타이머 람다에서 사용)
	TArray<FInv_SavedItemData> SavedItemsCopy = SavedItems;

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, SavedItemsCopy]()
	{
		DelayedRestoreGridPositions(SavedItemsCopy);
	}, 0.5f, false);

	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

/**
 * FastArray 리플리케이션 완료 후 Grid 위치 복원
 */
void AInv_PlayerController::DelayedRestoreGridPositions(const TArray<FInv_SavedItemData>& SavedItems)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 5] DelayedRestoreGridPositions - Grid 위치 복원 시작             ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 복원할 아이템: %d개                                                        "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	// RestoreInventoryFromState 호출하여 Grid 위치 복원
	RestoreInventoryFromState(SavedItems);

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🎉 [Phase 5] 인벤토리 로드 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}
