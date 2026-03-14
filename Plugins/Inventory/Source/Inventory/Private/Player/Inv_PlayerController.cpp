#include "Player/Inv_PlayerController.h"
#include "Inventory.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Interaction/Inv_Highlightable.h"
#include "Crafting/Interfaces/Inv_CraftingInterface.h"
#include "Crafting/Actors/Inv_CraftingStation.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/HUD/Inv_HUDWidget.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Widgets/Inventory/GridSlots/Inv_EquippedGridSlot.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Widgets/Inventory/SlottedItems/Inv_EquippedSlottedItem.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "InventoryManagement/Components/Inv_LootContainerComponent.h"
#include "Widgets/Inventory/Container/Inv_ContainerWidget.h"
#include "Interfaces/Inv_Interface_Primary.h"

AInv_PlayerController::AInv_PlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	TraceLength = 500.0;
	ItemTraceChannel = ECC_GameTraceChannel1;
}

void AInv_PlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (InventoryComponent.IsValid() && InventoryComponent->IsMenuOpen()) return;
	if (bIsViewingContainer) return;
	TraceForInteractables();
}

void AInv_PlayerController::ToggleInventory()
{
	// Phase 9: 컨테이너 열려있으면 닫기
	if (bIsViewingContainer)
	{
		Server_CloseContainer();
		return;
	}

	if (!InventoryComponent.IsValid()) return;
	InventoryComponent->ToggleInventoryMenu();

	if (InventoryComponent->IsMenuOpen())
	{
		// U10: HUDWidget null 체크
		if (IsValid(HUDWidget))
		{
			HUDWidget->SetVisibility(ESlateVisibility::Hidden);
		}

#if INV_DEBUG_ATTACHMENT
		// ★ [부착진단-UI] 인벤토리 열기 시 InventoryList 아이템 부착물 상태 확인 ★
		{
			TArray<UInv_InventoryItem*> DiagAllItems = InventoryComponent->GetInventoryList().GetAllItems();
			UE_LOG(LogTemp, Error, TEXT("[부착진단-UI] 인벤토리 열기: InventoryList 총 아이템=%d"), DiagAllItems.Num());
			for (int32 d = 0; d < DiagAllItems.Num(); d++)
			{
				UInv_InventoryItem* DiagItem = DiagAllItems[d];
				if (!IsValid(DiagItem)) continue;
				if (!DiagItem->HasAttachmentSlots()) continue;

				const FInv_AttachmentHostFragment* DiagHost =
					DiagItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
				UE_LOG(LogTemp, Error, TEXT("[부착진단-UI]   [%d] %s, HasSlots=Y, HostFrag=%s, AttachedItems=%d"),
					d,
					*DiagItem->GetItemManifest().GetItemType().ToString(),
					DiagHost ? TEXT("유효") : TEXT("nullptr"),
					DiagHost ? DiagHost->GetAttachedItems().Num() : -1);
				if (DiagHost)
				{
					for (int32 a = 0; a < DiagHost->GetAttachedItems().Num(); a++)
					{
						const FInv_AttachedItemData& DiagData = DiagHost->GetAttachedItems()[a];
						UE_LOG(LogTemp, Error, TEXT("[부착진단-UI]     [%d] Type=%s (Slot=%d), ManifestCopy.ItemType=%s"),
							a, *DiagData.AttachmentItemType.ToString(), DiagData.SlotIndex,
							*DiagData.ItemManifestCopy.GetItemType().ToString());
					}
				}
			}
		}
#endif
	}
	else
	{
		// U10: HUDWidget null 체크
		if (IsValid(HUDWidget))
		{
			HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void AInv_PlayerController::BeginPlay()
{
	Super::BeginPlay();

#if INV_DEBUG_PLAYER
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
#endif

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
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] EquipmentComponent 찾음"));
#endif
	}

	CreateHUDWidget();
}

void AInv_PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [Inv_PlayerController] EndPlay                             ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ EndPlayReason: %d"), static_cast<int32>(EndPlayReason));
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// Phase 9: 컨테이너 잠금 해제
	if (ActiveContainerComp.IsValid())
	{
		ActiveContainerComp->ClearCurrentUser();
		ActiveContainerComp.Reset();
	}
	bIsViewingContainer = false;

	// ⭐ 서버에서만 처리 (인벤토리 저장 + 로그아웃)
	if (HasAuthority())
	{
		// 인벤토리 데이터 수집
		TArray<FInv_SavedItemData> CollectedData;

		if (InventoryComponent.IsValid())
		{
			CollectedData = InventoryComponent->CollectInventoryDataForSave();
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ InventoryComponent에서 %d개 아이템 수집"), CollectedData.Num());
#endif

			// ============================================
			// 🆕 [Phase 6] EquipmentComponent에서 장착 상태 추가
			// ============================================
			if (EquipmentComponent.IsValid())
			{
				// 🔧 수정: SlotIndex → ItemType 맵 (같은 타입 다중 장착 지원)
				TMap<int32, FGameplayTag> SlotToItemMap;
				const TArray<TObjectPtr<AInv_EquipActor>>& EquippedActors = EquipmentComponent->GetEquippedActors();

#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] 🔍 [Phase 6] EquippedActors 개수: %d"), EquippedActors.Num());
#endif

				for (const TObjectPtr<AInv_EquipActor>& EquipActor : EquippedActors)
				{
					if (EquipActor.Get())
					{
						FGameplayTag ItemType = EquipActor->GetEquipmentType();
						int32 SlotIndex = EquipActor->GetWeaponSlotIndex();
						SlotToItemMap.Add(SlotIndex, ItemType);

#if INV_DEBUG_PLAYER
						UE_LOG(LogTemp, Warning, TEXT("[EndPlay]    ⚔️ 장착됨: Slot %d → %s"),
							SlotIndex, *ItemType.ToString());
#endif
					}
				}

				// CollectedData에 장착 상태 추가 (슬롯별로 매칭)
				int32 EquippedCount = 0;
				for (auto& Pair : SlotToItemMap)
				{
					int32 SlotIndex = Pair.Key;
					FGameplayTag& ItemType = Pair.Value;

					// 같은 ItemType이고 아직 장착 표시 안 된 아이템 찾기
					for (FInv_SavedItemData& Item : CollectedData)
					{
						if (Item.ItemType == ItemType && !Item.bEquipped)
						{
							Item.bEquipped = true;
							Item.WeaponSlotIndex = SlotIndex;
							EquippedCount++;

#if INV_DEBUG_PLAYER
							UE_LOG(LogTemp, Warning, TEXT("[EndPlay]    ✅ %s → bEquipped=true, WeaponSlotIndex=%d"),
								*Item.ItemType.ToString(), SlotIndex);
#endif
							break;  // 하나만 매칭하고 다음 슬롯으로
						}
					}
				}

#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] 🆕 [Phase 6] 장착 상태 추가 완료: %d개"), EquippedCount);
#endif
			}
			else
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ EquipmentComponent가 nullptr - 장착 상태 추가 불가"));
#endif
			}
		}
		else
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ InventoryComponent가 nullptr"));
#endif
		}

		// 델리게이트 브로드캐스트 (GameMode에서 저장 + 로그아웃 처리)
		if (OnControllerEndPlay.IsBound())
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ OnControllerEndPlay 델리게이트 브로드캐스트"));
#endif
			OnControllerEndPlay.Broadcast(this, CollectedData);
		}
		else
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ OnControllerEndPlay 바인딩 안 됨"));
#endif
		}
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif

	// [Fix26] 람다 타이머 핸들 해제
	GetWorldTimerManager().ClearTimer(GridRestoreTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AInv_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// [Fix26] CastChecked → Cast + null 체크 (프로세스 종료 방지)
	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInputComponent)
	{
		UE_LOG(LogInventory, Error, TEXT("[InvPlayerController] InputComponent가 UEnhancedInputComponent가 아닙니다! 입력 바인딩 스킵"));
		return;
	}

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

	// Phase 9: 컨테이너 상호작용
	UInv_LootContainerComponent* ContainerComp = ThisActor->FindComponentByClass<UInv_LootContainerComponent>();
	if (IsValid(ContainerComp))
	{
		if (bIsViewingContainer)
		{
			Server_CloseContainer();
		}
		else
		{
			Server_OpenContainer(ContainerComp);
		}
		return;
	}

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

bool AInv_PlayerController::Server_Interact_Validate(AActor* TargetActor)
{
	if (!TargetActor) return true;
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return true;
	const double MaxDist = TraceLength * 2.0;
	return FVector::DistSquared(MyPawn->GetActorLocation(), TargetActor->GetActorLocation()) <= FMath::Square(MaxDist);
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
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HUDWidgetClass가 설정되지 않음"));
#endif
		return;
	}

	if (!HUDWidget)
	{
		HUDWidget = CreateWidget<UInv_HUDWidget>(this, HUDWidgetClass);
		if (IsValid(HUDWidget))
		{
			HUDWidget->AddToViewport();
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HUD 위젯 생성됨"));
#endif
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
	// [Fix26] GetWorld() null 체크 (레벨 전환 중 Tick 크래시 방지)
	UWorld* TraceWorld = GetWorld();
	if (!TraceWorld) return;
	TraceWorld->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ItemTraceChannel);

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
		if (LastActor.IsValid())
		{
			if (UActorComponent* Highlightable = LastActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
			{
				IInv_Highlightable::Execute_UnHighlight(Highlightable);
			}
		}
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
				// Phase 9: 컨테이너 표시 이름
				UInv_LootContainerComponent* LootComp = ThisActor->FindComponentByClass<UInv_LootContainerComponent>();
				if (IsValid(LootComp))
				{
					HUDWidget->ShowPickupMessage(LootComp->ContainerDisplayName.ToString());
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

#if INV_DEBUG_INVENTORY
	// ── 🔍 [진단] CollectGridState 호출 컨텍스트 확인 ──
	UE_LOG(LogTemp, Error, TEXT("🔍 [CollectGridState 진단] Controller=%s, IsLocal=%s, HasAuth=%s, Role=%d"),
		*GetName(),
		IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"),
		HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"),
		(int32)GetLocalRole());

	if (InventoryComponent.IsValid())
	{
		UInv_InventoryBase* DiagMenu = InventoryComponent->GetInventoryMenu();
		UE_LOG(LogTemp, Error, TEXT("🔍 [진단] InvComp=유효, Menu=%s, Menu주소=%p"),
			IsValid(DiagMenu) ? *DiagMenu->GetName() : TEXT("nullptr"), DiagMenu);

		if (IsValid(DiagMenu))
		{
			UInv_SpatialInventory* DiagSpatial = Cast<UInv_SpatialInventory>(DiagMenu);
			if (IsValid(DiagSpatial))
			{
				UInv_InventoryGrid* DG0 = DiagSpatial->GetGrid_Equippables();
				UInv_InventoryGrid* DG1 = DiagSpatial->GetGrid_Consumables();
				UInv_InventoryGrid* DG2 = DiagSpatial->GetGrid_Craftables();
				UE_LOG(LogTemp, Error, TEXT("🔍 [진단] Grid_Equippables=%p SlottedItems=%d, Grid_Consumables=%p SlottedItems=%d, Grid_Craftables=%p SlottedItems=%d"),
					DG0, DG0 ? DG0->GetSlottedItemCount() : -1,
					DG1, DG1 ? DG1->GetSlottedItemCount() : -1,
					DG2, DG2 ? DG2->GetSlottedItemCount() : -1);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("🔍 [진단] InventoryComponent=INVALID ❌"));
	}
#endif

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║          [Phase 3] CollectInventoryGridState() - 인벤토리 상태 수집           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 호출 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: UI의 Grid 상태를 수집하여 서버로 전송할 데이터 생성                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	// ============================================
	// Step 1: InventoryComponent 유효성 검사
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] InventoryComponent 확인"));
#endif

	if (!InventoryComponent.IsValid())
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent가 nullptr입니다!"));
		UE_LOG(LogTemp, Error, TEXT("     → PlayerController에 InventoryComponent가 없거나 초기화 안 됨"));
#endif
		return Result;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryComponent 유효함"));
#endif

	// ============================================
	// Step 2: InventoryMenu 가져오기
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] InventoryMenu(위젯) 가져오기"));
#endif

	UInv_InventoryBase* InventoryMenu = InventoryComponent->GetInventoryMenu();
	if (!IsValid(InventoryMenu))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryMenu가 nullptr입니다!"));
		UE_LOG(LogTemp, Error, TEXT("     → 인벤토리 위젯이 생성되지 않았거나 파괴됨"));
		UE_LOG(LogTemp, Error, TEXT("     → InventoryComponent::BeginPlay()에서 위젯 생성 확인 필요"));
#endif
		return Result;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryMenu 유효함: %s"), *InventoryMenu->GetName());
#endif

	// ============================================
	// Step 3: SpatialInventory로 캐스트
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] SpatialInventory로 캐스트"));
#endif

	UInv_SpatialInventory* SpatialInventory = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInventory))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("  ❌ SpatialInventory로 캐스트 실패!"));
		UE_LOG(LogTemp, Error, TEXT("     → InventoryMenu 클래스: %s"), *InventoryMenu->GetClass()->GetName());
		UE_LOG(LogTemp, Error, TEXT("     → UInv_SpatialInventory 상속 확인 필요"));
#endif
		return Result;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ✅ SpatialInventory 캐스트 성공"));
#endif

	// ============================================
	// [BugFix] Step 3.5: 장착 아이템 포인터 Set 구성 (Step 4에서 이중 수집 방지)
	// ============================================
	TSet<UInv_InventoryItem*> EquippedItemPtrs;
	{
		const TArray<TObjectPtr<UInv_EquippedGridSlot>>& PreEquippedSlots = SpatialInventory->GetEquippedGridSlots();
		for (const TObjectPtr<UInv_EquippedGridSlot>& Slot : PreEquippedSlots)
		{
			if (IsValid(Slot.Get()))
			{
				UInv_InventoryItem* EqItem = Slot->GetInventoryItem().Get();
				if (IsValid(EqItem))
				{
					EquippedItemPtrs.Add(EqItem);
				}
			}
		}
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Error, TEXT("[Step3.5진단] EquippedItemPtrs 구성: %d개"), EquippedItemPtrs.Num());
	for (UInv_InventoryItem* EqDiagPtr : EquippedItemPtrs)
	{
		UE_LOG(LogTemp, Error, TEXT("[Step3.5진단]   -> 포인터=%p, Type=%s"),
			EqDiagPtr, *EqDiagPtr->GetItemManifest().GetItemType().ToString());
	}
#endif

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3.5] 장착 아이템 필터 Set 구성: %d개"), EquippedItemPtrs.Num());
#endif

	// ============================================
	// Step 4: 3개 Grid 접근 및 상태 수집
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 4] 3개 Grid에서 아이템 수집 (장착 아이템 제외)"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
#endif

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
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │"));
		UE_LOG(LogTemp, Warning, TEXT("  ├─ [Grid %d] %s"), GridInfo.Category, GridInfo.Name);
#endif

		if (!IsValid(GridInfo.Grid))
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │    ⚠️ Grid가 nullptr! 건너뜀"));
#endif
			continue;
		}

		// 각 Grid의 상태 수집 (장착 아이템은 제외)
		TArray<FInv_SavedItemData> GridItems = GridInfo.Grid->CollectGridState(&EquippedItemPtrs);

#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │    📦 수집된 아이템: %d개"), GridItems.Num());

		for (int32 i = 0; i < GridItems.Num(); ++i)
		{
			const FInv_SavedItemData& Item = GridItems[i];
			UE_LOG(LogTemp, Warning, TEXT("  │      [%d] %s x%d @ Pos(%d,%d)"),
				i, *Item.ItemType.ToString(), Item.StackCount,
				Item.GridPosition.X, Item.GridPosition.Y);
		}
#endif

		TotalCollected += GridItems.Num();
		Result.Append(GridItems);
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │"));
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// Step 5: 장착 슬롯(EquippedGridSlots) 상태 디버깅
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 5] 장착 슬롯(EquippedGridSlots) 상태 확인"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("  │ ⚔️ 장착 슬롯 디버깅 (Phase 6 준비)                          │"));
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
#endif

	const TArray<TObjectPtr<UInv_EquippedGridSlot>>& EquippedSlots = SpatialInventory->GetEquippedGridSlots();
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │ 총 장착 슬롯 개수: %d                                       │"), EquippedSlots.Num());
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
#endif

	int32 EquippedItemCount = 0;
	for (int32 i = 0; i < EquippedSlots.Num(); ++i)
	{
		UInv_EquippedGridSlot* Slot = EquippedSlots[i].Get();
		if (!IsValid(Slot))
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │ [%d] ❌ 슬롯 nullptr                                       │"), i);
#endif
			continue;
		}

		int32 WeaponSlotIndex = Slot->GetWeaponSlotIndex();
		UInv_InventoryItem* EquippedItem = Slot->GetInventoryItem().Get();

		if (IsValid(EquippedItem))
		{
			FGameplayTag ItemType = EquippedItem->GetItemManifest().GetItemType();
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │ [%d] ✅ WeaponSlot=%d │ %s"),
				i, WeaponSlotIndex, *ItemType.ToString());
#endif
			EquippedItemCount++;

			// ============================================
			// 🆕 Phase 6: 장착 아이템을 Result에 추가
			// ============================================
			FInv_SavedItemData EquippedData(
				EquippedItem->GetItemManifest().GetItemType(),
				1,  // 장비는 스택 1
				Slot->GetWeaponSlotIndex()
			);

			// ════════════════════════════════════════════════════════════════
			// 📌 [Phase 1 최적화] 장착 아이템 Fragment 직렬화
			// ════════════════════════════════════════════════════════════════
			{
				const FInv_ItemManifest& EquipManifest = EquippedItem->GetItemManifest();
				EquippedData.SerializedManifest = EquipManifest.SerializeFragments();

#if INV_DEBUG_SAVE
				UE_LOG(LogTemp, Warning,
					TEXT("  │      📦 [Phase 1 최적화] 장착 아이템 Fragment 직렬화 (클라이언트): %s → %d바이트"),
					*EquippedData.ItemType.ToString(), EquippedData.SerializedManifest.Num());
#endif

				// 부착물 데이터 수집 + 직렬화
				const FInv_AttachmentHostFragment* HostFrag = EquipManifest.GetFragmentOfType<FInv_AttachmentHostFragment>();
				if (HostFrag)
				{
					for (const FInv_AttachedItemData& Attached : HostFrag->GetAttachedItems())
					{
						FInv_SavedAttachmentData AttSave;
						AttSave.AttachmentItemType = Attached.AttachmentItemType;
						AttSave.SlotIndex = Attached.SlotIndex;

						const FInv_AttachableFragment* AttachableFrag =
							Attached.ItemManifestCopy.GetFragmentOfType<FInv_AttachableFragment>();
						if (AttachableFrag)
						{
							AttSave.AttachmentType = AttachableFrag->GetAttachmentType();
						}

						AttSave.SerializedManifest = Attached.ItemManifestCopy.SerializeFragments();

#if INV_DEBUG_SAVE
						UE_LOG(LogTemp, Warning,
							TEXT("  │        📦 부착물 Fragment 직렬화: %s → %d바이트"),
							*AttSave.AttachmentItemType.ToString(), AttSave.SerializedManifest.Num());
#endif

						EquippedData.Attachments.Add(AttSave);
					}
				}
			}

			Result.Add(EquippedData);
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │      → ✅ Result에 추가됨!"));
#endif
		}
		else
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │ [%d] ⬜ WeaponSlot=%d │ (비어있음)                        │"),
				i, WeaponSlotIndex);
#endif
		}
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 📊 장착된 아이템: %d개 → Result에 추가됨!                    │"), EquippedItemCount);
	UE_LOG(LogTemp, Warning, TEXT("  │ ✅ Phase 6 완료: 장착 아이템 저장 로직 구현됨                 │"));
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// 최종 결과 출력
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║                        📊 수집 결과 요약                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 총 수집된 아이템: %d개                                                        "), Result.Num());
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
#endif

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

#if INV_DEBUG_PLAYER
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
#endif

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning,
		TEXT("[SaveFlow] CollectInventoryGridState | Controller=%s | Total=%d | Grid=%d | Equipped=%d | GridEquippable=%d | GridConsumable=%d | GridCraftable=%d"),
		*GetName(),
		Result.Num(),
		TotalCollected,
		EquippedItemCount,
		EquipCount,
		ConsumeCount,
		CraftCount);
#endif
	return Result;
}

/**
 * 저장된 상태로 인벤토리 Grid 복원
 */
void AInv_PlayerController::RestoreInventoryFromState(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║          [Phase 5] RestoreInventoryFromState() - 인벤토리 상태 복원           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 호출 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: 저장된 Grid 위치로 아이템 배치 복원                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	if (SavedItems.Num() == 0)
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 복원할 아이템이 없습니다."));
#endif
		return;
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 복원할 아이템 목록 (%d개):"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// Step 1: InventoryComponent 접근
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] InventoryComponent 접근"));
#endif

	if (!InventoryComponent.IsValid())
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("   ❌ [실패] InventoryComponent가 유효하지 않습니다!"));
		UE_LOG(LogTemp, Error, TEXT("         Controller: %s"), *GetName());
		UE_LOG(LogTemp, Error, TEXT("         → BeginPlay()에서 InventoryComponent 초기화 확인"));
		UE_LOG(LogTemp, Error, TEXT("         → PlayerController BP에 Component 추가 확인"));
#endif
		return;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("   ✅ InventoryComponent 유효함"));
#endif

	// ============================================
	// Step 2: SpatialInventory 접근
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] SpatialInventory 접근"));
#endif

	UInv_InventoryBase* InventoryMenu = InventoryComponent->GetInventoryMenu();
	if (!IsValid(InventoryMenu))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("   ❌ [실패] InventoryMenu가 nullptr!"));
		UE_LOG(LogTemp, Error, TEXT("         → InventoryComponent::BeginPlay()에서 위젯 생성 확인"));
		UE_LOG(LogTemp, Error, TEXT("         → InventoryMenuClass가 설정되어 있는지 확인"));
#endif
		return;
	}

	UInv_SpatialInventory* SpatialInventory = Cast<UInv_SpatialInventory>(InventoryMenu);
	if (!IsValid(SpatialInventory))
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Error, TEXT("   ❌ [실패] SpatialInventory로 캐스트 실패!"));
		UE_LOG(LogTemp, Error, TEXT("         InventoryMenu 클래스: %s"), *InventoryMenu->GetClass()->GetName());
		UE_LOG(LogTemp, Error, TEXT("         → InventoryMenu가 UInv_SpatialInventory 상속 확인"));
#endif
		return;
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("   ✅ SpatialInventory 접근 성공"));
#endif

	// ============================================
	// [FIX] 장착 아이템을 Grid에서 선제거 (Step 3 전에!)
	// PostReplicatedAdd가 모든 아이템을 Grid에 자동 배치했으므로
	// 장착 아이템이 Grid 공간을 차지하고 있음.
	// Step 3에서 Grid 위치를 복원하기 전에 제거해야 겹침 방지.
	// [BugFix] WeaponSlotIndex → 아이템 포인터를 저장하여 Phase 6에서 재사용
	// ============================================
	TMap<int32, UInv_InventoryItem*> PreRemovedEquippedItems;
	{
		UInv_InventoryGrid* PreEquipGrid = SpatialInventory->GetGrid_Equippables();
		if (IsValid(PreEquipGrid))
		{
			TSet<UInv_InventoryItem*> TempProcessedEquip;
			for (const FInv_SavedItemData& EqItem : SavedItems)
			{
				if (!EqItem.bEquipped) continue;

				UInv_InventoryItem* Found = InventoryComponent->FindItemByTypeExcluding(
					EqItem.ItemType, TempProcessedEquip);
				if (IsValid(Found))
				{
					bool bPreRemoved = PreEquipGrid->RemoveSlottedItemByPointer(Found);
					TempProcessedEquip.Add(Found);
					PreRemovedEquippedItems.Add(EqItem.WeaponSlotIndex, Found);
#if INV_DEBUG_PLAYER
					UE_LOG(LogTemp, Warning, TEXT("[Pre-Equip] Grid에서 장착 아이템 선제거: %s (슬롯 %d) → %s (포인터 저장됨)"),
						*EqItem.ItemType.ToString(), EqItem.WeaponSlotIndex,
						bPreRemoved ? TEXT("성공") : TEXT("실패"));
#endif
				}
			}
		}
	}

	// ============================================
	// Step 3: 각 Grid에 위치 복원 요청
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] 각 Grid에 위치 복원 요청"));
#endif

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
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("   📦 %s"), GridInfo.Name);
#endif

		if (!IsValid(GridInfo.Grid))
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("      ⚠️ Grid가 nullptr! 건너뜀"));
#endif
			continue;
		}

		int32 RestoredInGrid = GridInfo.Grid->RestoreItemPositions(SavedItems);
		TotalRestored += RestoredInGrid;

#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("      → %d개 복원됨"), RestoredInGrid);
#endif
	}

	// ============================================
	// 최종 결과 요약
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 📊 [Phase 5] Grid 위치 복원 결과                            │"));
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 요청: %3d개 아이템                                          │"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("  │ 복원: %3d개 성공 ✅                                         │"), TotalRestored);
#endif

	// ============================================
	// 🆕 [Phase 6] 장착 아이템 복원
	// ============================================
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ ⚔️ [Phase 6] 장착 아이템 복원 시작...                        │"));

	// 🔍 디버깅: 전체 SavedItems에서 bEquipped 상태 확인
	UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 🔍 [디버깅] SavedItems bEquipped 상태:                       │"));
#endif
	int32 EquippedCount = 0;
	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │   [%d] %s"), i, *Item.ItemType.ToString());
		UE_LOG(LogTemp, Warning, TEXT("  │       bEquipped=%s, WeaponSlotIndex=%d, GridPos=(%d,%d)"),
			Item.bEquipped ? TEXT("TRUE ✅") : TEXT("false"),
			Item.WeaponSlotIndex,
			Item.GridPosition.X, Item.GridPosition.Y);
#endif
		if (Item.bEquipped) EquippedCount++;
	}
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │ 🔍 bEquipped=true 아이템: %d개                              │"), EquippedCount);
	UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
#endif

	int32 EquippedRestored = 0;
	TSet<UInv_InventoryItem*> ProcessedEquipItems;  // 🆕 이미 장착 처리한 아이템 추적

	// SpatialInventory에서 장착 슬롯 가져오기
	// SpatialInventory는 이미 위에서 선언됨 - 유효성만 체크
	if (IsValid(SpatialInventory))
	{
		// 🆕 [Phase 7] EquippedGridSlots가 비어있으면 강제 수집
		SpatialInventory->CollectEquippedGridSlots();

		const TArray<TObjectPtr<UInv_EquippedGridSlot>>& EquippedSlots = SpatialInventory->GetEquippedGridSlots();
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │ 🔍 EquippedSlots 개수: %d                                   │"), EquippedSlots.Num());
#endif

		// 🆕 [Phase 7] 디버깅: EquippedSlots가 비어있으면 경고
		if (EquippedSlots.Num() == 0)
		{
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Error, TEXT("  │ ❌❌❌ [Phase 7] EquippedSlots가 비어있음! 위젯 초기화 문제! ❌❌❌ │"));
#endif
		}

		for (const FInv_SavedItemData& ItemData : SavedItems)
		{
			// 🔍 디버깅: 각 아이템의 bEquipped 체크
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
			UE_LOG(LogTemp, Warning, TEXT("  │   📌 처리 중: %s"), *ItemData.ItemType.ToString());
			UE_LOG(LogTemp, Warning, TEXT("  │       bEquipped=%s, WeaponSlotIndex=%d"),
				ItemData.bEquipped ? TEXT("TRUE") : TEXT("FALSE"), ItemData.WeaponSlotIndex);
#endif

			if (!ItemData.bEquipped)
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       → 건너뜀 (Grid 아이템)"));
#endif
				continue;  // Grid 아이템 건너뛰기
			}
			if (ItemData.WeaponSlotIndex < 0)
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       → 건너뜀 (유효하지 않은 슬롯 인덱스)"));
#endif
				continue;  // 유효하지 않은 슬롯
			}

#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │   → ⚔️ 장착 복원 시도: %s (슬롯 %d)"),
				*ItemData.ItemType.ToString(), ItemData.WeaponSlotIndex);
#endif

			// 해당 WeaponSlotIndex를 가진 장착 슬롯 찾기
			UInv_EquippedGridSlot* TargetSlot = nullptr;
			for (const TObjectPtr<UInv_EquippedGridSlot>& Slot : EquippedSlots)
			{
				if (IsValid(Slot) && Slot->GetWeaponSlotIndex() == ItemData.WeaponSlotIndex)
				{
					TargetSlot = Slot.Get();
					break;
				}
			}

			if (!IsValid(TargetSlot))
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ❌ WeaponSlot %d를 찾을 수 없음!"), ItemData.WeaponSlotIndex);
#endif
				continue;
			}
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │       ✅ TargetSlot 찾음: %s"), *TargetSlot->GetName());
#endif

			// [BugFix] Pre-remove에서 저장한 정확한 인스턴스 사용, fallback으로 FindItemByTypeExcluding
			UInv_InventoryItem* FoundItem = nullptr;
			UInv_InventoryItem** PreRemovedPtr = PreRemovedEquippedItems.Find(ItemData.WeaponSlotIndex);
			if (PreRemovedPtr && IsValid(*PreRemovedPtr))
			{
				FoundItem = *PreRemovedPtr;
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       ✅ Pre-remove 포인터 사용: %s (슬롯 %d)"),
					*FoundItem->GetItemManifest().GetItemType().ToString(), ItemData.WeaponSlotIndex);
#endif
			}
			else
			{
				// fallback: Pre-remove에서 못 찾은 경우 (타이밍 이슈 등)
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       ⚠️ Pre-remove 포인터 없음 → FindItemByTypeExcluding fallback: %s"), *ItemData.ItemType.ToString());
#endif
				FoundItem = InventoryComponent->FindItemByTypeExcluding(ItemData.ItemType, ProcessedEquipItems);
			}
			if (!IsValid(FoundItem))
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ❌ 아이템을 찾을 수 없음: %s"), *ItemData.ItemType.ToString());
#endif
				continue;
			}
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │       ✅ FoundItem: %s"), *FoundItem->GetItemManifest().GetItemType().ToString());
#endif

			// 🆕 [Phase 6] SpatialInventory의 RestoreEquippedItem 사용 (델리게이트 바인딩 포함)
#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("  │       🔧 RestoreEquippedItem 호출 (델리게이트 바인딩 포함)"));
#endif
			UInv_EquippedSlottedItem* EquippedSlottedItem = SpatialInventory->RestoreEquippedItem(TargetSlot, FoundItem);
			if (IsValid(EquippedSlottedItem))
			{
				EquippedRestored++;
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ✅ 장착 복원 성공: %s → 슬롯 %d"),
					*ItemData.ItemType.ToString(), ItemData.WeaponSlotIndex);
#endif

				// [BugFix] Grid에서 장착 아이템 제거 — Pre-remove에서 이미 제거했으면 스킵
				if (!PreRemovedPtr || !IsValid(*PreRemovedPtr))
				{
					// fallback 경로: Pre-remove를 안 거쳤으므로 여기서 Grid 제거 시도
					UInv_InventoryGrid* EquipGrid = SpatialInventory->GetGrid_Equippables();
					if (IsValid(EquipGrid))
					{
						bool bRemoved = EquipGrid->RemoveSlottedItemByPointer(FoundItem);
#if INV_DEBUG_PLAYER
						UE_LOG(LogTemp, Warning, TEXT("  │       🗑️ Grid 제거 (fallback): %s"),
							bRemoved ? TEXT("성공") : TEXT("실패"));
#endif
					}
				}
#if INV_DEBUG_PLAYER
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("  │       ✅ Grid 제거 스킵 (Pre-remove에서 이미 처리됨)"));
				}
#endif

				// 장착 델리게이트 브로드캐스트 (무기 Actor 스폰용)
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │       📡 OnItemEquipped 브로드캐스트 (WeaponSlotIndex=%d)"), ItemData.WeaponSlotIndex);
#endif
				InventoryComponent->OnItemEquipped.Broadcast(FoundItem, ItemData.WeaponSlotIndex);

				// 🆕 이미 처리한 아이템으로 표시 (같은 타입 다중 장착 시 다른 아이템 찾기 위함)
				ProcessedEquipItems.Add(FoundItem);
			}
			else
			{
#if INV_DEBUG_PLAYER
				UE_LOG(LogTemp, Warning, TEXT("  │   ❌ OnItemEquipped 실패 (EquippedSlottedItem=nullptr)"));
#endif
			}
		}
	}
	else
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("  │   ⚠️ SpatialInventory를 찾을 수 없음!"));
#endif
	}

	// ============================================
	// Fix 10: 복원 완료 후 서버에 올바른 Grid 위치 동기화
	// ============================================
	// PostReplicatedAdd 시 장착 아이템(GridIndex=INDEX_NONE)이 "first available" 슬롯에
	// 배치되면서 Server_UpdateItemGridPosition RPC가 서버의 GridIndex를 덮어씀.
	// MoveItemByCurrentIndex는 RPC를 보내지 않으므로, 복원 후 명시적으로 동기화 필요.
	// Step 4: 복원 직후 경로만 배치 RPC로 묶고, 일반 드래그/드롭 단건 RPC는 유지한다.
	TArray<FInv_GridPositionSyncData> RestoreSyncRequests;
	for (const FGridRestoreInfo& GridInfo : Grids)
	{
		if (IsValid(GridInfo.Grid))
		{
			const int32 AddedCount = GridInfo.Grid->AppendItemPositionSyncRequests(RestoreSyncRequests);
			UE_LOG(LogTemp, Warning, TEXT("[Fix10] %s 위치 서버 동기화 요청 추가: %d개"), GridInfo.Name, AddedCount);
		}
	}

	// Fix 10: 장착 아이템의 GridIndex를 서버에서 INDEX_NONE으로 재설정
	// PostReplicatedAdd → AddItem → UpdateGridSlots의 RPC가 서버의 GridIndex를 덮어썼으므로 복구
	if (InventoryComponent.IsValid())
	{
		for (const auto& [SlotIdx, EquippedItem] : PreRemovedEquippedItems)
		{
			if (IsValid(EquippedItem))
			{
				RestoreSyncRequests.Emplace(EquippedItem, INDEX_NONE, 0, false);
				UE_LOG(LogTemp, Warning, TEXT("[Fix10] 장착 아이템 GridIndex 서버 클리어: %s (WeaponSlot=%d)"),
					*EquippedItem->GetItemManifest().GetItemType().ToString(), SlotIdx);
			}
		}

		if (RestoreSyncRequests.Num() > 0)
		{
			InventoryComponent->Server_UpdateItemGridPositionsBatch(RestoreSyncRequests);
			UE_LOG(LogTemp, Warning, TEXT("[Step4] 복원 후 배치 위치 서버 동기화 완료: %d개 요청"), RestoreSyncRequests.Num());
		}
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("  │                                                              │"));
	UE_LOG(LogTemp, Warning, TEXT("  │ ⚔️ 장착 아이템 복원 완료: %d개 (예상: %d개)                    │"), EquippedRestored, EquippedCount);
	UE_LOG(LogTemp, Warning, TEXT("  │ 실패: %3d개 ❌                                              │"), SavedItems.Num() - TotalRestored - EquippedRestored);
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif
}

// ============================================
// 📌 인벤토리 저장 RPC 구현 (Phase 4)
// ============================================

/**
 * [서버 → 클라이언트] 인벤토리 상태 요청
 */
void AInv_PlayerController::Client_RequestInventoryState_Implementation()
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] Client_RequestInventoryState - 서버로부터 요청 수신           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 요청자: 서버 (자동저장/로그아웃)                                           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	// Step 1: 인벤토리 상태 수집
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] CollectInventoryGridState() 호출하여 UI 상태 수집..."));
#endif

	TArray<FInv_SavedItemData> CollectedData;
	if (InventoryComponent.IsValid())
	{
		CollectedData = InventoryComponent->CollectInventoryDataForSave();
#if INV_DEBUG_PLAYER || INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Step3] Client_RequestInventoryState authoritative collect | Controller=%s | Items=%d"),
			*GetName(), CollectedData.Num());
#endif

		// Step 3 안정화:
		// 패키지 클라이언트에서는 복제 타이밍에 따라 FastArray 기반 수집이 0개가 되는 경우가 있다.
		// 이 경우에만 기존 UI 수집 경로로 한 번 더 보정한다.
		if (CollectedData.Num() == 0)
		{
			TArray<FInv_SavedItemData> FallbackData = CollectInventoryGridState();
			if (FallbackData.Num() > 0)
			{
#if INV_DEBUG_PLAYER || INV_DEBUG_SAVE
				UE_LOG(LogTemp, Warning,
					TEXT("[Step3] Client_RequestInventoryState UI fallback 사용 | Controller=%s | Authoritative=0 | Fallback=%d"),
					*GetName(), FallbackData.Num());
#endif
				CollectedData = MoveTemp(FallbackData);
			}
			else
			{
#if INV_DEBUG_PLAYER || INV_DEBUG_SAVE
				UE_LOG(LogTemp, Warning,
					TEXT("[Step3] Client_RequestInventoryState fallback도 비어 있음 | Controller=%s"),
					*GetName());
#endif
			}
		}
	}
	else
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("[Step3] InventoryComponent invalid - CollectInventoryGridState() fallback 사용"));
#endif
		CollectedData = CollectInventoryGridState();
	}

	// Step 2: 서버로 전송
	// [Fix15] 청크 분할 전송 — 65KB RPC 번치 크기 제한 초과 방지
	// 아이템당 SerializedManifest(바이너리 BLOB)가 커서 12개 이상이면 65KB 초과 가능
	constexpr int32 ChunkSize = 5; // 아이템 5개씩 분할 (안전 마진 확보)

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] Server_ReceiveInventoryStateChunk() RPC로 서버에 청크 전송..."));
	UE_LOG(LogTemp, Warning, TEXT("   전송할 아이템: %d개, 청크 크기: %d"), CollectedData.Num(), ChunkSize);
#endif

	if (CollectedData.Num() <= ChunkSize)
	{
		// 소량 → 단일 청크로 전송
		Server_ReceiveInventoryStateChunk(CollectedData, true);
	}
	else
	{
		// 대량 → 분할 전송
		const int32 TotalItems = CollectedData.Num();
		for (int32 StartIdx = 0; StartIdx < TotalItems; StartIdx += ChunkSize)
		{
			const int32 EndIdx = FMath::Min(StartIdx + ChunkSize, TotalItems);
			const bool bIsLast = (EndIdx >= TotalItems);

			TArray<FInv_SavedItemData> Chunk;
			Chunk.Reserve(EndIdx - StartIdx);
			for (int32 i = StartIdx; i < EndIdx; i++)
			{
				Chunk.Add(CollectedData[i]);
			}

#if INV_DEBUG_PLAYER
			UE_LOG(LogTemp, Warning, TEXT("   청크 전송: [%d~%d] (%d개), bIsLast=%s"),
				StartIdx, EndIdx - 1, Chunk.Num(), bIsLast ? TEXT("true") : TEXT("false"));
#endif

			Server_ReceiveInventoryStateChunk(Chunk, bIsLast);
		}
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("✅ 클라이언트 → 서버 청크 전송 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

/**
 * [클라이언트 → 서버] Server RPC 검증 함수
 */
bool AInv_PlayerController::Server_ReceiveInventoryState_Validate(const TArray<FInv_SavedItemData>& SavedItems)
{
	// 기본 검증: 데이터 크기 제한 (악의적 대량 전송 방지)
	if (SavedItems.Num() > 1000)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server_ReceiveInventoryState_Validate] 아이템 수 초과: %d"), SavedItems.Num());
		return false;
	}
	return true;
}

/**
 * [클라이언트 → 서버] 수집된 인벤토리 상태 전송
 */
void AInv_PlayerController::Server_ReceiveInventoryState_Implementation(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
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
#endif

	// 델리게이트 브로드캐스트 (GameMode에서 바인딩하여 저장 처리)
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ OnInventoryStateReceived 델리게이트 브로드캐스트..."));
#endif

	if (OnInventoryStateReceived.IsBound())
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 델리게이트 바인딩됨! 브로드캐스트 실행"));
#endif
		OnInventoryStateReceived.Broadcast(this, SavedItems);
	}
	else
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 델리게이트 바인딩 안 됨! (GameMode에서 바인딩 필요)"));
#endif
	}

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("✅ 서버 수신 처리 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// [Fix15] Server_ReceiveInventoryStateChunk — 청크 분할 수신
// ════════════════════════════════════════════════════════════════════════════════
// 클라이언트가 인벤토리 데이터를 N개씩 나눠 보낼 때 사용.
// 각 청크를 PendingServerChunkItems에 누적, bIsLastChunk=true일 때 델리게이트 브로드캐스트.
// ════════════════════════════════════════════════════════════════════════════════

bool AInv_PlayerController::Server_ReceiveInventoryStateChunk_Validate(
	const TArray<FInv_SavedItemData>& ChunkItems, bool bIsLastChunk)
{
	// 청크당 최대 100개, 누적 최대 1000개
	if (ChunkItems.Num() > 100)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server_ReceiveInventoryStateChunk_Validate] 청크 아이템 수 초과: %d"), ChunkItems.Num());
		return false;
	}
	return true;
}

void AInv_PlayerController::Server_ReceiveInventoryStateChunk_Implementation(
	const TArray<FInv_SavedItemData>& ChunkItems, bool bIsLastChunk)
{
	UE_LOG(LogTemp, Log, TEXT("[Fix15] Server_ReceiveInventoryStateChunk: 청크 수신 %d개, bIsLastChunk=%s, 누적=%d"),
		ChunkItems.Num(), bIsLastChunk ? TEXT("true") : TEXT("false"), PendingServerChunkItems.Num());

	PendingServerChunkItems.Append(ChunkItems);

	if (!bIsLastChunk)
	{
		return; // 아직 더 올 청크가 있음
	}

	// 마지막 청크 → 누적된 전체 데이터로 델리게이트 브로드캐스트
	UE_LOG(LogTemp, Log, TEXT("[Fix15] 마지막 청크 수신. 총 %d개 아이템 → 델리게이트 브로드캐스트"),
		PendingServerChunkItems.Num());

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Fix15] OnInventoryStateReceived 델리게이트 브로드캐스트 (청크 누적 완료)..."));
#endif

	if (OnInventoryStateReceived.IsBound())
	{
		OnInventoryStateReceived.Broadcast(this, PendingServerChunkItems);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Fix15] 델리게이트 바인딩 안 됨! (GameMode에서 바인딩 필요)"));
	}

	// 누적 버퍼 초기화
	PendingServerChunkItems.Empty();
}

// ============================================
// 📌 인벤토리 로드 RPC 구현 (Phase 5)
// ============================================

/**
 * [서버 → 클라이언트] 저장된 인벤토리 데이터 수신
 */
void AInv_PlayerController::Client_ReceiveInventoryData_Implementation(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 5] Client_ReceiveInventoryData - 서버로부터 인벤토리 데이터 수신  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 수신된 아이템: %d개                                                        "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	if (SavedItems.Num() == 0)
	{
#if INV_DEBUG_PLAYER
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 저장된 인벤토리 데이터가 없습니다. (신규 플레이어?)"));
		UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
		return;
	}

	// 수신된 아이템 목록 출력
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 수신된 아이템 목록:"));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));

	for (int32 i = 0; i < SavedItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = SavedItems[i];
		UE_LOG(LogTemp, Warning, TEXT("  │ [%02d] %s"), i, *Item.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));
#endif

	// ============================================
	// FastArray 리플리케이션 완료 대기 후 Grid 위치 복원
	// ============================================
	// 서버에서 아이템이 추가되면 FastArray가 클라이언트로 리플리케이트됨
	// 리플리케이션 완료 후 Grid 위치를 복원해야 하므로 딜레이 필요
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 0.5초 후 Grid 위치 복원 예약..."));
	UE_LOG(LogTemp, Warning, TEXT("   (FastArray 리플리케이션 완료 대기)"));
#endif

	// SavedItems 복사본 생성 (타이머 람다에서 사용)
	TArray<FInv_SavedItemData> SavedItemsCopy = SavedItems;

	// [Fix26] 로컬 핸들 → 멤버 변수 (EndPlay에서 해제 가능)
	GetWorldTimerManager().SetTimer(GridRestoreTimerHandle, [this, SavedItemsCopy]()
	{
		DelayedRestoreGridPositions(SavedItemsCopy);
	}, 0.5f, false);

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// [네트워크 최적화] Client_ReceiveInventoryDataChunk
// ════════════════════════════════════════════════════════════════════════════════
// 서버가 SavedItems를 청크로 나눠 보낼 때 사용.
// 각 청크를 PendingSavedItems에 누적, bIsLastChunk=true일 때 폴링 복원 시작.
// ════════════════════════════════════════════════════════════════════════════════
void AInv_PlayerController::Client_ReceiveInventoryDataChunk_Implementation(
	const TArray<FInv_SavedItemData>& ChunkItems, bool bIsLastChunk)
{
	UE_LOG(LogTemp, Log, TEXT("[InventoryChunk] 청크 수신: %d개, bIsLastChunk=%s, 누적=%d"),
		ChunkItems.Num(),
		bIsLastChunk ? TEXT("true") : TEXT("false"),
		PendingSavedItems.Num());

	// [Fix10-Recv진단] 청크 수신 데이터 확인
	for (int32 DiagIdx = 0; DiagIdx < ChunkItems.Num(); DiagIdx++)
	{
		const FInv_SavedItemData& DiagItem = ChunkItems[DiagIdx];
		UE_LOG(LogTemp, Error, TEXT("[Fix10-Recv진단] 수신 Item[%d] %s: GridPos=(%d,%d), bEquipped=%s, WeaponSlot=%d"),
			PendingSavedItems.Num() + DiagIdx, *DiagItem.ItemType.ToString(),
			DiagItem.GridPosition.X, DiagItem.GridPosition.Y,
			DiagItem.bEquipped ? TEXT("TRUE") : TEXT("FALSE"),
			DiagItem.WeaponSlotIndex);
	}

	PendingSavedItems.Append(ChunkItems);

	if (!bIsLastChunk)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[InventoryChunk] 마지막 청크 수신. 총 %d개 아이템 → 폴링 복원 시작"),
		PendingSavedItems.Num());

	// 폴링 복원 데이터 세팅
	PendingRestoreItems = MoveTemp(PendingSavedItems);
	PendingSavedItems.Empty();
	PendingRestoreRetryCount = 0;

	// 0.3초 간격 폴링 시작 (최대 15회 = 4.5초)
	GetWorldTimerManager().SetTimer(PendingRestoreTimerHandle, this,
		&AInv_PlayerController::PollAndRestoreInventory, 0.3f, true);
}

// ════════════════════════════════════════════════════════════════════════════════
// [네트워크 최적화] PollAndRestoreInventory
// ════════════════════════════════════════════════════════════════════════════════
// FastArray 리플리케이션이 완료될 때까지 폴링으로 대기.
// InventoryComponent의 아이템 수가 기대값에 도달하면 복원 실행.
// ════════════════════════════════════════════════════════════════════════════════
void AInv_PlayerController::PollAndRestoreInventory()
{
	PendingRestoreRetryCount++;

	// InventoryComponent에서 현재 도착한 아이템 수 확인
	int32 CurrentItemCount = 0;
	if (InventoryComponent.IsValid())
	{
		CurrentItemCount = InventoryComponent->GetInventoryList().GetAllItems().Num();

		// [진단] 폴링 시점의 InventoryComponent 주소 + 아이템 수
		UE_LOG(LogTemp, Error, TEXT("[폴링진단] InvComp=%p, GetAllItems=%d, Owner=%s"),
			InventoryComponent.Get(),
			CurrentItemCount,
			*GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[폴링진단] InventoryComponent가 INVALID! Controller=%s"), *GetName());
	}

	const int32 ExpectedCount = PendingRestoreItems.Num();
	const bool bAllArrived = (CurrentItemCount >= ExpectedCount);
	const bool bTimeout = (PendingRestoreRetryCount >= 15); // 최대 4.5초

	UE_LOG(LogTemp, Log, TEXT("[InventoryChunk] 폴링 %d/15: FastArray 아이템 %d/%d %s"),
		PendingRestoreRetryCount, CurrentItemCount, ExpectedCount,
		bAllArrived ? TEXT("도착 완료!") : TEXT("대기 중..."));

	if (!bAllArrived && !bTimeout)
	{
		return; // 다음 폴링 대기
	}

	// 폴링 타이머 정지
	GetWorldTimerManager().ClearTimer(PendingRestoreTimerHandle);

	if (bTimeout && !bAllArrived)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryChunk] 폴링 타임아웃! %d/%d개만 도착. 가진 만큼 복원 시도"),
			CurrentItemCount, ExpectedCount);
	}

	// 복원 실행
	TArray<FInv_SavedItemData> ItemsToRestore = MoveTemp(PendingRestoreItems);
	PendingRestoreItems.Empty();
	PendingRestoreRetryCount = 0;

	DelayedRestoreGridPositions(ItemsToRestore);
}

/**
 * FastArray 리플리케이션 완료 후 Grid 위치 복원
 */
void AInv_PlayerController::DelayedRestoreGridPositions(const TArray<FInv_SavedItemData>& SavedItems)
{
#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 5] DelayedRestoreGridPositions - Grid 위치 복원 시작             ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 클라이언트                                                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 복원할 아이템: %d개                                                        "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));
#endif

	// RestoreInventoryFromState 호출하여 Grid 위치 복원
	RestoreInventoryFromState(SavedItems);

#if INV_DEBUG_PLAYER
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🎉 [Phase 5] 인벤토리 로드 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
#endif
}

// ════════════════════════════════════════════════════════════════
// 🆕 [Phase 7.5] 현재 활성 무기의 EquipActor 반환
// ════════════════════════════════════════════════════════════════
// [2026-02-18] 작업자: 김기현
// ────────────────────────────────────────────────────────────────
// EquipmentComponent가 유효한 경우에만 GetActiveWeaponActor()를 호출.
// EquipmentComponent는 BeginPlay에서 FindComponentByClass로 캐싱됨.
//
// 반환값:
//   - 현재 손에 든 무기의 AInv_EquipActor* (소음기/스코프/레이저 효과 프로퍼티 포함)
//   - EquipmentComponent가 없거나 맨손이면 nullptr
// ════════════════════════════════════════════════════════════════
AInv_EquipActor* AInv_PlayerController::GetCurrentEquipActor() const
{
	if (EquipmentComponent.IsValid())
	{
		return EquipmentComponent->GetActiveWeaponActor();
	}
	return nullptr;
}

// ════════════════════════════════════════════════════════════════
// Phase 9: 컨테이너 RPC 구현
// ════════════════════════════════════════════════════════════════

bool AInv_PlayerController::Server_OpenContainer_Validate(UInv_LootContainerComponent* Container)
{
	return true;
}

void AInv_PlayerController::Server_OpenContainer_Implementation(UInv_LootContainerComponent* Container)
{
	if (!IsValid(Container)) return;

	// 활성화 체크
	if (!Container->IsActivated())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] 컨테이너 미활성화 → 열기 거부"));
		return;
	}

	// 잠금 체크: 이미 다른 플레이어가 사용 중
	if (IsValid(Container->CurrentUser) && Container->CurrentUser != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] 컨테이너 잠금 → %s가 사용 중"),
			*Container->CurrentUser->GetName());
		return;
	}

	Container->SetCurrentUser(this);
	ActiveContainerComp = Container;

	Client_ShowContainerUI(Container);
}

bool AInv_PlayerController::Server_CloseContainer_Validate()
{
	return true;
}

void AInv_PlayerController::Server_CloseContainer_Implementation()
{
	if (ActiveContainerComp.IsValid())
	{
		ActiveContainerComp->ClearCurrentUser();
		ActiveContainerComp.Reset();
	}

	Client_HideContainerUI();
}

void AInv_PlayerController::Client_ShowContainerUI_Implementation(UInv_LootContainerComponent* Container)
{
	if (!IsValid(Container) || !InventoryComponent.IsValid()) return;

	// 1. 인벤토리 열기 (닫혀있으면) — SpatialInventory(풀 인벤토리 UI)를 함께 표시
	if (!InventoryComponent->IsMenuOpen())
	{
		InventoryComponent->ToggleInventoryMenu();
	}

	// 2. SpatialInventory 참조 확보 (크로스 링크용)
	UInv_SpatialInventory* SpatialInv = Cast<UInv_SpatialInventory>(InventoryComponent->GetInventoryMenu());
	if (!IsValid(SpatialInv))
	{
		UE_LOG(LogTemp, Error, TEXT("[Inv_PlayerController] SpatialInventory 캐스트 실패"));
		return;
	}

	// 3. 컨테이너 위젯 생성 (최초 1회)
	if (!IsValid(ContainerWidget))
	{
		if (!ContainerWidgetClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[Inv_PlayerController] ContainerWidgetClass가 설정되지 않음"));
			return;
		}
		ContainerWidget = CreateWidget<UInv_ContainerWidget>(this, ContainerWidgetClass);
	}

	if (!IsValid(ContainerWidget)) return;

	// 4. 이전 컨테이너 패널이 열려있으면 정리 후 재초기화
	if (ContainerWidget->IsInViewport())
	{
		ContainerWidget->CleanupPanels();
		ContainerWidget->RemoveFromParent();
	}

	// 5. 초기화 + 표시 (SpatialInventory 전달)
	ContainerWidget->InitializePanels(Container, InventoryComponent.Get(), SpatialInv);
	ContainerWidget->AddToViewport();

	bIsViewingContainer = true;
	ActiveContainerComp = Container;

	// HUD 숨기기
	if (IsValid(HUDWidget))
	{
		HUDWidget->SetVisibility(ESlateVisibility::Hidden);
	}

	// 마우스 커서 표시
	SetShowMouseCursor(true);
	SetInputMode(FInputModeGameAndUI());

	UE_LOG(LogTemp, Log, TEXT("[Inv_PlayerController] 컨테이너 UI 표시 (SpatialInventory 통합): %s"),
		*Container->ContainerDisplayName.ToString());
}

void AInv_PlayerController::Client_HideContainerUI_Implementation()
{
	// 1. ContainerWidget 정리 (SpatialInventory 링크 해제 포함)
	if (IsValid(ContainerWidget))
	{
		ContainerWidget->CleanupPanels();
		ContainerWidget->RemoveFromParent();
	}

	bIsViewingContainer = false;
	ActiveContainerComp.Reset();

	// 2. 인벤토리도 닫기 (열려있으면) — ToggleInventoryMenu가 HUD/커서/입력모드 처리
	if (InventoryComponent.IsValid() && InventoryComponent->IsMenuOpen())
	{
		InventoryComponent->ToggleInventoryMenu();
	}

	// 3. 안전을 위해 상태 보장 (ToggleInventoryMenu에서 이미 처리되지만 방어 코드)
	if (IsValid(HUDWidget))
	{
		HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	SetShowMouseCursor(false);
	SetInputMode(FInputModeGameOnly());

	UE_LOG(LogTemp, Log, TEXT("[Inv_PlayerController] 컨테이너 UI 닫기 (인벤토리 함께 닫기)"));
}
