// Gihyeon's Inventory Project


#include "Building/Components/Inv_BuildingComponent.h"
#include "Inventory.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"


// Sets default values for this component's properties
UInv_BuildingComponent::UInv_BuildingComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	bIsInBuildMode = false;
	bCanPlaceBuilding = false;
	MaxPlacementDistance = 1000.0f;
	MaxGroundAngle = 45.0f; // 45도 이상 경사면에는 설치 불가
}


// Called when the game starts
void UInv_BuildingComponent::BeginPlay()
{
	Super::BeginPlay();

	// PlayerController 캐싱
	OwningPC = Cast<APlayerController>(GetOwner());
	if (!OwningPC.IsValid())
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildingComponent: Owner is not a PlayerController!"));
#endif
		return;
	}

	// 로컬 플레이어만 입력 등록
	if (!OwningPC->IsLocalController()) return;

	// ★ BuildingMenuMappingContext만 여기서 추가 (B키 - 항상 활성화)
	// ★ BuildingActionMappingContext는 BuildMode 진입 시에만 동적으로 추가됨
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(OwningPC->GetLocalPlayer()))
	{
		if (IsValid(BuildingMenuMappingContext))
		{
			Subsystem->AddMappingContext(BuildingMenuMappingContext, 0);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("BuildingMenuMappingContext added (B키 - 항상 활성화)"));
#endif
		}
		else
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("BuildingMenuMappingContext is not set!"));
#endif
		}
	}

	// Input Component 바인딩 - B키(빌드 메뉴 토글)만 항상 활성화
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(OwningPC->InputComponent))
	{
		if (IsValid(IA_Building))
		{
			// B키: 빌드 메뉴 토글 (항상 활성화)
			EnhancedInputComponent->BindAction(IA_Building, ETriggerEvent::Started, this, &UInv_BuildingComponent::ToggleBuildMenu);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("IA_Building bound to ToggleBuildMenu."));
#endif
		}
		else
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("IA_Building is not set."));
#endif
		}

		// ★ IA_BuildingAction, IA_CancelBuilding은 여기서 바인딩하지 않음!
		// ★ BuildMode 진입 시에만 동적으로 바인딩됨 (EnableBuildModeInput)
	}
	else
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Enhanced Input Component not found!"));
#endif
	}
}


// Called every frame
void UInv_BuildingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 빌드 모드일 때 고스트 메시 위치 업데이트
	if (bIsInBuildMode && IsValid(GhostActorInstance) && OwningPC.IsValid())
	{
		// 플레이어가 바라보는 방향으로 라인 트레이스
		FVector CameraLocation;
		FRotator CameraRotation;
		OwningPC->GetPlayerViewPoint(CameraLocation, CameraRotation);

		const FVector TraceStart = CameraLocation;
		const FVector TraceEnd = TraceStart + (CameraRotation.Vector() * MaxPlacementDistance);

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwningPC->GetPawn()); // 플레이어 폰 무시
		QueryParams.AddIgnoredActor(GhostActorInstance); // 고스트 액터 자신 무시

		// 라인 트레이스 실행 (ECC_Visibility 채널 사용)
		if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
		{
			// 히트된 위치에 고스트 액터 배치
			GhostActorInstance->SetActorLocation(HitResult.Location);
			
			// 바닥 법선 각도 체크 - 너무 가파른 경사면인지 확인
			const FVector UpVector = FVector::UpVector;
			const float DotProduct = FVector::DotProduct(HitResult.ImpactNormal, UpVector);
			const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(DotProduct));
			
			// 설치 가능 여부 판단
			bCanPlaceBuilding = (AngleDegrees <= MaxGroundAngle);
			
			// 디버그 라인 (빨강: 불가능, 초록: 가능)
			const FColor DebugColor = bCanPlaceBuilding ? FColor::Green : FColor::Red;
			DrawDebugLine(GetWorld(), TraceStart, HitResult.Location, DebugColor, false, 0.0f, 0, 2.0f);
			DrawDebugPoint(GetWorld(), HitResult.Location, 10.0f, DebugColor, false, 0.0f);
		}
		else
		{
			// 바닥을 못 찾으면 설치 불가능
			bCanPlaceBuilding = false;
			
			// 고스트를 트레이스 끝 지점에 배치 (공중)
			GhostActorInstance->SetActorLocation(TraceEnd);
			
			// 디버그 라인 (회색: 바닥 없음)
			DrawDebugLine(GetWorld(), TraceStart, TraceEnd, FColor::Silver, false, 0.0f, 0, 1.0f);
		}
	}
}

void UInv_BuildingComponent::StartBuildMode()
{
	if (!OwningPC.IsValid() || !GetWorld()) return;

	bIsInBuildMode = true;
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== Build Mode STARTED ==="));
#endif

	// 선택된 고스트 액터 클래스가 있는지 확인
	if (!SelectedGhostClass)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("SelectedGhostClass is not set! Please select a building from the menu first."));
#endif
		return;
	}

	// ★ BuildMode 진입 시 입력 활성화 (IMC 추가 + 바인딩)
	EnableBuildModeInput();

	// 이미 고스트 액터가 있다면 제거
	if (IsValid(GhostActorInstance))
	{
		GhostActorInstance->Destroy();
		GhostActorInstance = nullptr;
	}

	// 고스트 액터 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningPC.Get();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 플레이어 앞에 스폰
	FVector SpawnLocation = OwningPC->GetPawn()->GetActorLocation() + (OwningPC->GetPawn()->GetActorForwardVector() * 300.0f);
	FRotator SpawnRotation = FRotator::ZeroRotator;

	GhostActorInstance = GetWorld()->SpawnActor<AActor>(SelectedGhostClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (IsValid(GhostActorInstance))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("Ghost Actor spawned successfully! (BuildingID: %d)"), CurrentBuildingID);
#endif

		// 고스트 액터의 충돌 비활성화
		GhostActorInstance->SetActorEnableCollision(false);
	}
	else
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn Ghost Actor!"));
#endif
	}
}

void UInv_BuildingComponent::EndBuildMode()
{
	bIsInBuildMode = false;
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== Build Mode ENDED ==="));
#endif

	// ★ BuildMode 종료 시 입력 비활성화 (IMC 제거 + 바인딩 해제)
	DisableBuildModeInput();

	// 고스트 메시 제거
	if (IsValid(GhostActorInstance))
	{
		GhostActorInstance->Destroy();
		GhostActorInstance = nullptr;
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("Ghost Actor destroyed."));
#endif
	}
}

void UInv_BuildingComponent::CancelBuildMode()
{
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("CancelBuildMode called. bIsInBuildMode: %s"), bIsInBuildMode ? TEXT("TRUE") : TEXT("FALSE"));
#endif

	// 빌드 모드일 때만 취소
	if (!bIsInBuildMode)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("Not in build mode - ignoring cancel request."));
#endif
		return; // 빌드 모드가 아니면 무시
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== BUILD MODE CANCELLED (Right Click) ==="));
#endif
	
	// 빌드 모드 종료
	EndBuildMode();
}

void UInv_BuildingComponent::ToggleBuildMenu()
{
	if (!OwningPC.IsValid()) return;

	if (IsValid(BuildMenuInstance))
	{
		// 위젯이 열려있으면 닫기
		CloseBuildMenu();
	}
	else
	{
		// 위젯이 없으면 열기
		OpenBuildMenu();
	}
}

void UInv_BuildingComponent::OpenBuildMenu()
{
	if (!OwningPC.IsValid()) return;

	// 이미 열려있으면 무시
	if (IsValid(BuildMenuInstance)) return;

	// 빌드 메뉴 위젯 클래스가 설정되어 있는지 확인
	if (!BuildMenuWidgetClass)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("BuildMenuWidgetClass is not set! Please set WBP_BuildMenu in BP_Inv_BuildingComponent."));
#endif
		return;
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== OPENING BUILD MENU ==="));
#endif

	// Crafting Menu가 열려있으면 닫기
	CloseCraftingMenuIfOpen();

	// 위젯 생성
	BuildMenuInstance = CreateWidget<UUserWidget>(OwningPC.Get(), BuildMenuWidgetClass);
	if (!IsValid(BuildMenuInstance))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Failed to create Build Menu Widget!"));
#endif
		return;
	}

	// 화면에 추가
	BuildMenuInstance->AddToViewport();

	// 입력 모드 변경: UI와 게임 동시
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(BuildMenuInstance->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	OwningPC->SetInputMode(InputMode);

	// 마우스 커서 보이기
	OwningPC->SetShowMouseCursor(true);

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("Build Menu opened successfully!"));
#endif
}

void UInv_BuildingComponent::CloseBuildMenu()
{
	if (!OwningPC.IsValid()) return;

	if (!IsValid(BuildMenuInstance)) return;

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== CLOSING BUILD MENU ==="));
#endif

	// 위젯 제거
	BuildMenuInstance->RemoveFromParent();
	BuildMenuInstance = nullptr;

	// 입력 모드 변경: 게임만
	FInputModeGameOnly InputMode;
	OwningPC->SetInputMode(InputMode);

	// 마우스 커서 숨기기
	OwningPC->SetShowMouseCursor(false);

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("Build Menu closed."));
#endif
}

void UInv_BuildingComponent::CloseCraftingMenuIfOpen()
{
	if (!OwningPC.IsValid() || !GetWorld()) return;

	// 간단한 방법: 모든 CraftingMenu 타입 위젯을 찾아서 제거
	TArray<UUserWidget*> FoundWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), FoundWidgets, UUserWidget::StaticClass(), false);

	for (UUserWidget* Widget : FoundWidgets)
	{
		if (!IsValid(Widget)) continue;

		// 클래스 이름에 "CraftingMenu"가 포함되어 있으면 제거
		FString WidgetClassName = Widget->GetClass()->GetName();
		if (WidgetClassName.Contains(TEXT("CraftingMenu")))
		{
			Widget->RemoveFromParent();
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Log, TEXT("Crafting Menu 닫힘: %s (BuildMenu 열림)"), *WidgetClassName);
#endif
		}
	}
}

void UInv_BuildingComponent::OnBuildingSelectedFromWidget(
	TSubclassOf<AActor> GhostClass, 
	TSubclassOf<AActor> ActualBuildingClass, 
	int32 BuildingID, 
	FGameplayTag MaterialTag1, 
	int32 MaterialAmount1,
	FGameplayTag MaterialTag2,
	int32 MaterialAmount2)
{
	if (!GhostClass || !ActualBuildingClass)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("OnBuildingSelectedFromWidget: Invalid class parameters!"));
#endif
		return;
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== BUILDING SELECTED FROM WIDGET ==="));
	UE_LOG(LogTemp, Warning, TEXT("BuildingID: %d"), BuildingID);

	if (MaterialTag1.IsValid() && MaterialAmount1 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Required Material 1: %s x %d"), *MaterialTag1.ToString(), MaterialAmount1);
	}
	if (MaterialTag2.IsValid() && MaterialAmount2 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Required Material 2: %s x %d"), *MaterialTag2.ToString(), MaterialAmount2);
	}
#endif

	// 선택된 건물 정보 저장
	SelectedGhostClass = GhostClass;
	SelectedBuildingClass = ActualBuildingClass;
	CurrentBuildingID = BuildingID;
	CurrentMaterialTag = MaterialTag1;
	CurrentMaterialAmount = MaterialAmount1;
	CurrentMaterialTag2 = MaterialTag2;
	CurrentMaterialAmount2 = MaterialAmount2;

	// 빌드 메뉴 닫기
	CloseBuildMenu();

	// 빌드 모드 시작 (고스트 스폰)
	StartBuildMode();
}

void UInv_BuildingComponent::TryPlaceBuilding()
{
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("TryPlaceBuilding called. bIsInBuildMode: %s"), bIsInBuildMode ? TEXT("TRUE") : TEXT("FALSE"));
#endif

	// 빌드 모드가 아니면 무시 (버그 방지)
	if (!bIsInBuildMode)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("Not in build mode - ignoring placement request."));
#endif
		return; // 로그 추가
	}

	if (!bCanPlaceBuilding)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("Cannot place building - invalid placement location (too steep or no ground)!"));
#endif
		return;
	}

	if (!IsValid(GhostActorInstance))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Cannot place building - Ghost Actor is invalid!"));
#endif
		return;
	}

	if (!OwningPC.IsValid())
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Cannot place building - OwningPC is invalid!"));
#endif
		return;
	}

	if (!SelectedBuildingClass)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Cannot place building - SelectedBuildingClass is invalid!"));
#endif
		return;
	}

	// 재료 다시 체크 (빌드 모드 중에 소비됐을 수 있음!)
	// 재료 1 체크
	if (CurrentMaterialTag.IsValid() && CurrentMaterialAmount > 0)
	{
		if (!HasRequiredMaterials(CurrentMaterialTag, CurrentMaterialAmount))
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("=== 배치 실패: 재료1이 부족합니다! ==="));
			UE_LOG(LogTemp, Warning, TEXT("필요한 재료1: %s x %d"), *CurrentMaterialTag.ToString(), CurrentMaterialAmount);
#endif
			EndBuildMode(); // 빌드 모드 종료
			return;
		}
	}

	// 재료 2 체크
	if (CurrentMaterialTag2.IsValid() && CurrentMaterialAmount2 > 0)
	{
		if (!HasRequiredMaterials(CurrentMaterialTag2, CurrentMaterialAmount2))
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("=== 배치 실패: 재료2가 부족합니다! ==="));
			UE_LOG(LogTemp, Warning, TEXT("필요한 재료2: %s x %d"), *CurrentMaterialTag2.ToString(), CurrentMaterialAmount2);
#endif
			EndBuildMode(); // 빌드 모드 종료
			return;
		}
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== TRY PLACING BUILDING (Client Request) ==="));
	UE_LOG(LogTemp, Warning, TEXT("BuildingID: %d"), CurrentBuildingID);
#endif
	
	// 고스트 액터의 현재 위치와 회전 가져오기
	const FVector BuildingLocation = GhostActorInstance->GetActorLocation();
	const FRotator BuildingRotation = GhostActorInstance->GetActorRotation();
	
	// 서버에 실제 건물 배치 요청 (재료 정보도 함께 전달!)
	Server_PlaceBuilding(SelectedBuildingClass, BuildingLocation, BuildingRotation, CurrentMaterialTag, CurrentMaterialAmount, CurrentMaterialTag2, CurrentMaterialAmount2);

	// 건물 배치 성공 시 빌드 모드 종료
	EndBuildMode();
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("Building placed! Exiting build mode."));
#endif
}

void UInv_BuildingComponent::Server_PlaceBuilding_Implementation(
	TSubclassOf<AActor> BuildingClass, 
	FVector Location, 
	FRotator Rotation, 
	FGameplayTag MaterialTag1, 
	int32 MaterialAmount1,
	FGameplayTag MaterialTag2,
	int32 MaterialAmount2)
{
	if (!GetWorld())
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Server_PlaceBuilding: World is invalid!"));
#endif
		return;
	}

	if (!BuildingClass)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Server_PlaceBuilding: BuildingClass is invalid!"));
#endif
		return;
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== SERVER PLACING BUILDING ==="));
#endif

	// 서버에서 재료 검증 (반드시 통과해야 건설!)
	// GetTotalMaterialCount는 멀티스택을 모두 합산하므로 정확함
	
	// 재료 1 검증 (필수)
	if (MaterialTag1.IsValid() && MaterialAmount1 > 0)
	{
		if (!HasRequiredMaterials(MaterialTag1, MaterialAmount1))
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Error, TEXT("❌ Server: 재료1 부족! 건설 차단. (Tag: %s, 필요: %d)"),
				*MaterialTag1.ToString(), MaterialAmount1);
#endif
			// 클라이언트에게 실패 알림 (옵션)
			// Client_OnBuildingFailed(TEXT("재료가 부족합니다!"));
			return; // 건설 중단!
		}
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("✅ Server: 재료1 검증 통과 (Tag: %s, 필요: %d)"),
			*MaterialTag1.ToString(), MaterialAmount1);
#endif
	}

	// 재료 2 검증 (필수)
	if (MaterialTag2.IsValid() && MaterialAmount2 > 0)
	{
		if (!HasRequiredMaterials(MaterialTag2, MaterialAmount2))
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Error, TEXT("❌ Server: 재료2 부족! 건설 차단. (Tag: %s, 필요: %d)"),
				*MaterialTag2.ToString(), MaterialAmount2);
#endif
			return; // 건설 중단!
		}
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("✅ Server: 재료2 검증 통과 (Tag: %s, 필요: %d)"),
			*MaterialTag2.ToString(), MaterialAmount2);
#endif
	}
	
	// 서버에서 실제 건물 액터 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningPC.Get();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AActor* PlacedBuilding = GetWorld()->SpawnActor<AActor>(BuildingClass, Location, Rotation, SpawnParams);

	if (IsValid(PlacedBuilding))
	{
		// 실제 건물이므로 충돌 활성화
		PlacedBuilding->SetActorEnableCollision(true);

#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("건물 스폰 성공! 재료 차감 시도..."));
#endif

		// 건물 배치 성공! 재료 차감 (여기서만!)
		// 재료 1 차감
		if (MaterialTag1.IsValid() && MaterialAmount1 > 0)
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("재료1 차감 조건 만족! ConsumeMaterials 호출..."));
#endif
			ConsumeMaterials(MaterialTag1, MaterialAmount1);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("Server: 재료1 차감 완료! (%s x %d)"), *MaterialTag1.ToString(), MaterialAmount1);
#endif
		}

		// 재료 2 차감
		if (MaterialTag2.IsValid() && MaterialAmount2 > 0)
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("재료2 차감 조건 만족! ConsumeMaterials 호출..."));
#endif
			ConsumeMaterials(MaterialTag2, MaterialAmount2);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("Server: 재료2 차감 완료! (%s x %d)"), *MaterialTag2.ToString(), MaterialAmount2);
#endif
		}

		// 재료가 하나도 설정되지 않은 경우 로그
		if (!MaterialTag1.IsValid() && !MaterialTag2.IsValid())
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("재료가 필요 없는 건물입니다."));
#endif
		}
		
		// 리플리케이션 활성화 (중요!)
		PlacedBuilding->SetReplicates(true);
		PlacedBuilding->SetReplicateMovement(true);
		
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("Server: Building placed successfully at location: %s"), *Location.ToString());
#endif
		
		// 모든 클라이언트에게 건물 배치 알림 (선택사항 - 추가 로직이 필요할 때)
		Multicast_OnBuildingPlaced(PlacedBuilding);
	}
	else
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("Server: Failed to spawn building actor!"));
#endif
	}
}

void UInv_BuildingComponent::Multicast_OnBuildingPlaced_Implementation(AActor* PlacedBuilding)
{
	if (!IsValid(PlacedBuilding)) return;

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("Multicast: Building placed notification received - %s"), *PlacedBuilding->GetName());
#endif
	
	// 여기에 건물 배치 후 추가 로직 (이펙트, 사운드 등) 추가 가능
}

bool UInv_BuildingComponent::HasRequiredMaterials(const FGameplayTag& MaterialTag, int32 RequiredAmount) const
{
	// 재료가 필요 없으면 true
	if (!MaterialTag.IsValid() || RequiredAmount <= 0)
	{
		return true;
	}

	if (!OwningPC.IsValid()) return false;

	// InventoryComponent 가져오기
	UInv_InventoryComponent* InvComp = OwningPC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return false;

	// 모든 스택 합산하여 체크 (멀티스택 지원!)
	int32 TotalAmount = InvComp->GetTotalMaterialCount(MaterialTag);
	return TotalAmount >= RequiredAmount;
}

void UInv_BuildingComponent::ConsumeMaterials(const FGameplayTag& MaterialTag, int32 Amount)
{
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeMaterials 호출됨 ==="));
	UE_LOG(LogTemp, Warning, TEXT("MaterialTag: %s, Amount: %d"), *MaterialTag.ToString(), Amount);
#endif

	if (!MaterialTag.IsValid() || Amount <= 0)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("ConsumeMaterials: Invalid MaterialTag or Amount <= 0"));
#endif
		return;
	}

	if (!OwningPC.IsValid())
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("ConsumeMaterials: OwningPC is invalid!"));
#endif
		return;
	}

	// InventoryComponent 가져오기
	UInv_InventoryComponent* InvComp = OwningPC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp))
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("ConsumeMaterials: InventoryComponent not found!"));
#endif
		return;
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("InventoryComponent 찾음! Server_ConsumeMaterialsMultiStack RPC 호출..."));
#endif

	// 멀티스택 차감 RPC 호출 (여러 스택에서 차감 지원!)
	InvComp->Server_ConsumeMaterialsMultiStack(MaterialTag, Amount);

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== ConsumeMaterials 완료 ==="));
#endif
}

void UInv_BuildingComponent::EnableBuildModeInput()
{
	if (!OwningPC.IsValid()) return;

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== EnableBuildModeInput: 빌드 모드 입력 활성화 ==="));
#endif

	// 1. BuildingActionMappingContext 추가 (높은 우선순위로 GA 입력보다 먼저 처리)
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(OwningPC->GetLocalPlayer()))
	{
		if (IsValid(BuildingActionMappingContext))
		{
			Subsystem->AddMappingContext(BuildingActionMappingContext, BuildingMappingContextPriority);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("BuildingActionMappingContext ADDED (Priority: %d)"), BuildingMappingContextPriority);
#endif
		}
		else
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Error, TEXT("BuildingActionMappingContext is not set!"));
#endif
		}
	}

	// 2. 동적 액션 바인딩
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(OwningPC->InputComponent))
	{
		// 좌클릭: 건물 배치 (중복 방지)
		if (IsValid(IA_BuildingAction) && BuildActionBindingHandle == 0)
		{
			BuildActionBindingHandle = EnhancedInputComponent->BindAction(
				IA_BuildingAction, ETriggerEvent::Started, this, &UInv_BuildingComponent::TryPlaceBuilding
			).GetHandle();
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("IA_BuildingAction BOUND (Handle: %u)"), BuildActionBindingHandle);
#endif
		}

		// 우클릭: 빌드 모드 취소 (중복 방지)
		if (IsValid(IA_CancelBuilding) && CancelBuildingBindingHandle == 0)
		{
			CancelBuildingBindingHandle = EnhancedInputComponent->BindAction(
				IA_CancelBuilding, ETriggerEvent::Started, this, &UInv_BuildingComponent::CancelBuildMode
			).GetHandle();
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("IA_CancelBuilding BOUND (Handle: %u)"), CancelBuildingBindingHandle);
#endif
		}
	}
}

void UInv_BuildingComponent::DisableBuildModeInput()
{
	if (!OwningPC.IsValid()) return;

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== DisableBuildModeInput: 빌드 모드 입력 비활성화 ==="));
#endif

	// 1. BuildingActionMappingContext 제거
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(OwningPC->GetLocalPlayer()))
	{
		if (IsValid(BuildingActionMappingContext))
		{
			Subsystem->RemoveMappingContext(BuildingActionMappingContext);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("BuildingActionMappingContext REMOVED"));
#endif
		}
	}

	// 2. 동적 바인딩 해제
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(OwningPC->InputComponent))
	{
		if (BuildActionBindingHandle != 0)
		{
			EnhancedInputComponent->RemoveBindingByHandle(BuildActionBindingHandle);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("IA_BuildingAction UNBOUND (Handle: %u)"), BuildActionBindingHandle);
#endif
			BuildActionBindingHandle = 0;
		}

		if (CancelBuildingBindingHandle != 0)
		{
			EnhancedInputComponent->RemoveBindingByHandle(CancelBuildingBindingHandle);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("IA_CancelBuilding UNBOUND (Handle: %u)"), CancelBuildingBindingHandle);
#endif
			CancelBuildingBindingHandle = 0;
		}
	}
}

