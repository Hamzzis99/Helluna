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
#include "Widgets/Building/Inv_BuildModeHUD.h"
#include "Building/Actor/Inv_BuildingActor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/MeshComponent.h"


// Sets default values for this component's properties
UInv_BuildingComponent::UInv_BuildingComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// RPC(Server/Multicast) 사용을 위해 컴포넌트 리플리케이션 활성화
	SetIsReplicatedByDefault(true);

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
		UWorld* World = GetWorld();
		if (!World) return;
		if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
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
			DrawDebugLine(World, TraceStart, HitResult.Location, DebugColor, false, 0.0f, 0, 2.0f);
			DrawDebugPoint(World, HitResult.Location, 10.0f, DebugColor, false, 0.0f);
		}
		else
		{
			// 바닥을 못 찾으면 설치 불가능
			bCanPlaceBuilding = false;

			// 고스트를 트레이스 끝 지점에 배치 (공중)
			GhostActorInstance->SetActorLocation(TraceEnd);

			// 디버그 라인 (회색: 바닥 없음)
			DrawDebugLine(World, TraceStart, TraceEnd, FColor::Silver, false, 0.0f, 0, 1.0f);
		}

		// ★ 건물 회전 적용 (위치와 무관하게 항상 처리)
		if (bIsRotatingRight)
		{
			CurrentBuildRotationYaw += ContinuousRotationSpeed * DeltaTime;
		}
		if (bIsRotatingLeft)
		{
			CurrentBuildRotationYaw -= ContinuousRotationSpeed * DeltaTime;
		}
		// Yaw 정규화 (0~360)
		CurrentBuildRotationYaw = FMath::Fmod(CurrentBuildRotationYaw + 360.f, 360.f);
		GhostActorInstance->SetActorRotation(FRotator(0.f, CurrentBuildRotationYaw, 0.f));

		// ★ HUD 배치 상태 실시간 업데이트
		if (IsValid(BuildModeHUDInstance))
		{
			BuildModeHUDInstance->UpdatePlacementStatus(bCanPlaceBuilding);
		}
	}
}

void UInv_BuildingComponent::StartBuildMode()
{
	if (!OwningPC.IsValid() || !GetWorld()) return;

	bIsInBuildMode = true;
	bIsRotatingRight = false;
	bIsRotatingLeft = false;
	// CurrentBuildRotationYaw는 유지 — 같은 각도로 연속 배치 가능
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== Build Mode STARTED (RotationYaw: %.1f) ==="), CurrentBuildRotationYaw);
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
	APawn* OwnerPawn = OwningPC->GetPawn();
	if (!OwnerPawn) return;
	FVector SpawnLocation = OwnerPawn->GetActorLocation() + (OwnerPawn->GetActorForwardVector() * 300.0f);
	FRotator SpawnRotation = FRotator::ZeroRotator;

	UWorld* World = GetWorld();
	if (!World) return;
	GhostActorInstance = World->SpawnActor<AActor>(SelectedGhostClass, SpawnLocation, SpawnRotation, SpawnParams);

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

	// ★ 빌드 모드 HUD 표시 (로컬 클라이언트만!)
	UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] StartBuildMode → ShowBuildModeHUD 호출 직전. IsLocalController=%s"),
		OwningPC->IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	if (OwningPC->IsLocalController())
	{
		ShowBuildModeHUD();
	}
	UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] StartBuildMode → ShowBuildModeHUD 호출 완료"));
}

void UInv_BuildingComponent::EndBuildMode()
{
	bIsInBuildMode = false;
	bIsRotatingRight = false;
	bIsRotatingLeft = false;
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== Build Mode ENDED ==="));
#endif

	// ★ 빌드 모드 HUD 제거 (로컬 클라이언트만!)
	if (OwningPC.IsValid() && OwningPC->IsLocalController())
	{
		HideBuildModeHUD();
	}

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

	// 방안 B: 인벤토리 열려있으면 닫기
	UInv_InventoryComponent* InvComp = OwningPC->FindComponentByClass<UInv_InventoryComponent>();
	if (IsValid(InvComp) && InvComp->IsMenuOpen())
	{
		InvComp->ToggleInventoryMenu();
	}

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

void UInv_BuildingComponent::ForceEndBuildMode()
{
	if (bIsInBuildMode)
	{
		EndBuildMode();
	}
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

		// 클래스 이름에 "CraftingMenu" 또는 "Repair"가 포함되어 있으면 제거
		FString WidgetClassName = Widget->GetClass()->GetName();
		if (WidgetClassName.Contains(TEXT("CraftingMenu")) || WidgetClassName.Contains(TEXT("Repair")))
		{
			Widget->RemoveFromParent();
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Log, TEXT("위젯 닫힘: %s (BuildMenu 열림)"), *WidgetClassName);
#endif
		}
	}
}

void UInv_BuildingComponent::OnBuildingSelectedFromWidget(const FInv_BuildingSelectionInfo& Info)
{
	if (!Info.GhostClass || !Info.ActualBuildingClass)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Error, TEXT("OnBuildingSelectedFromWidget: Invalid class parameters!"));
#endif
		return;
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("=== BUILDING SELECTED FROM WIDGET ==="));
	UE_LOG(LogTemp, Warning, TEXT("BuildingName: %s, BuildingID: %d"), *Info.BuildingName.ToString(), Info.BuildingID);

	if (Info.MaterialTag1.IsValid() && Info.MaterialAmount1 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Required Material 1: %s x %d"), *Info.MaterialTag1.ToString(), Info.MaterialAmount1);
	}
	if (Info.MaterialTag2.IsValid() && Info.MaterialAmount2 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Required Material 2: %s x %d"), *Info.MaterialTag2.ToString(), Info.MaterialAmount2);
	}
	if (Info.MaterialTag3.IsValid() && Info.MaterialAmount3 > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Required Material 3: %s x %d"), *Info.MaterialTag3.ToString(), Info.MaterialAmount3);
	}
#endif

	// 통합 정보 저장 (HUD에서 사용)
	CurrentBuildingInfo = Info;

	// 개별 변수에도 저장 (기존 로직 호환)
	SelectedGhostClass = Info.GhostClass;
	SelectedBuildingClass = Info.ActualBuildingClass;
	CurrentBuildingID = Info.BuildingID;
	CurrentMaterialTag = Info.MaterialTag1;
	CurrentMaterialAmount = Info.MaterialAmount1;
	CurrentMaterialTag2 = Info.MaterialTag2;
	CurrentMaterialAmount2 = Info.MaterialAmount2;
	CurrentMaterialTag3 = Info.MaterialTag3;
	CurrentMaterialAmount3 = Info.MaterialAmount3;

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
			EndBuildMode();
			return;
		}
	}

	// 재료 3 체크
	if (CurrentMaterialTag3.IsValid() && CurrentMaterialAmount3 > 0)
	{
		if (!HasRequiredMaterials(CurrentMaterialTag3, CurrentMaterialAmount3))
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("=== 배치 실패: 재료3이 부족합니다! ==="));
			UE_LOG(LogTemp, Warning, TEXT("필요한 재료3: %s x %d"), *CurrentMaterialTag3.ToString(), CurrentMaterialAmount3);
#endif
			EndBuildMode();
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

	// 서버에 실제 건물 배치 요청 (재료 3개 정보 함께 전달!)
	Server_PlaceBuilding(SelectedBuildingClass, BuildingLocation, BuildingRotation,
		CurrentMaterialTag, CurrentMaterialAmount,
		CurrentMaterialTag2, CurrentMaterialAmount2,
		CurrentMaterialTag3, CurrentMaterialAmount3);

	// 건물 배치 성공 시 빌드 모드 종료
	EndBuildMode();
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("Building placed! Exiting build mode."));
#endif
}

bool UInv_BuildingComponent::Server_PlaceBuilding_Validate(
	TSubclassOf<AActor> BuildingClass,
	FVector Location,
	FRotator Rotation,
	FGameplayTag MaterialTag1,
	int32 MaterialAmount1,
	FGameplayTag MaterialTag2,
	int32 MaterialAmount2,
	FGameplayTag MaterialTag3,
	int32 MaterialAmount3)
{
	return BuildingClass != nullptr && MaterialAmount1 >= 0 && MaterialAmount2 >= 0 && MaterialAmount3 >= 0 && !Location.ContainsNaN() && !Rotation.ContainsNaN();
}

void UInv_BuildingComponent::Server_PlaceBuilding_Implementation(
	TSubclassOf<AActor> BuildingClass,
	FVector Location,
	FRotator Rotation,
	FGameplayTag MaterialTag1,
	int32 MaterialAmount1,
	FGameplayTag MaterialTag2,
	int32 MaterialAmount2,
	FGameplayTag MaterialTag3,
	int32 MaterialAmount3)
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

	// 재료 3 검증
	if (MaterialTag3.IsValid() && MaterialAmount3 > 0)
	{
		if (!HasRequiredMaterials(MaterialTag3, MaterialAmount3))
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Error, TEXT("❌ Server: 재료3 부족! 건설 차단. (Tag: %s, 필요: %d)"),
				*MaterialTag3.ToString(), MaterialAmount3);
#endif
			return; // 건설 중단!
		}
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("✅ Server: 재료3 검증 통과 (Tag: %s, 필요: %d)"),
			*MaterialTag3.ToString(), MaterialAmount3);
#endif
	}

	// 서버에서 실제 건물 액터 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwningPC.Get();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	UWorld* World = GetWorld();
	if (!World) return;
	AActor* PlacedBuilding = World->SpawnActor<AActor>(BuildingClass, Location, Rotation, SpawnParams);

	if (IsValid(PlacedBuilding))
	{
		// 건설 중 상태: 충돌 비활성 (스캔 완료 후 활성화)
		PlacedBuilding->SetActorEnableCollision(false);

		// AInv_BuildingActor는 BeginPlay에서 즉시 스캔 VFX 시작 → 숨길 필요 없음
		// 터렛 등 나머지 액터는 Multicast 도착까지 숨김 처리
		const bool bIsBuildingActor = Cast<AInv_BuildingActor>(PlacedBuilding) != nullptr;
		if (!bIsBuildingActor)
		{
			PlacedBuilding->SetActorHiddenInGame(true);
		}

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

		// 재료 3 차감
		if (MaterialTag3.IsValid() && MaterialAmount3 > 0)
		{
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("재료3 차감 조건 만족! ConsumeMaterials 호출..."));
#endif
			ConsumeMaterials(MaterialTag3, MaterialAmount3);
#if INV_DEBUG_BUILD
			UE_LOG(LogTemp, Warning, TEXT("Server: 재료3 차감 완료! (%s x %d)"), *MaterialTag3.ToString(), MaterialAmount3);
#endif
		}

		// 재료가 하나도 설정되지 않은 경우 로그
		if (!MaterialTag1.IsValid() && !MaterialTag2.IsValid() && !MaterialTag3.IsValid())
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

		// 리플리케이션 대기 후 Multicast 호출
		// (SpawnActor 직후에는 클라이언트에 액터가 아직 도착하지 않아 파라미터가 NULL)
		constexpr float ReplicationDelay = 0.5f;
		if (World)
		{
			TWeakObjectPtr<AActor> WeakBuilding = PlacedBuilding;
			TWeakObjectPtr<UInv_BuildingComponent> WeakThis = this;

			// 0.5초 후 Multicast (액터 리플리케이션 완료 대기)
			FTimerHandle ScanDelayTimer;
			World->GetTimerManager().SetTimer(ScanDelayTimer,
				FTimerDelegate::CreateLambda([WeakThis, WeakBuilding]()
				{
					if (WeakThis.IsValid() && WeakBuilding.IsValid())
					{
						WeakThis->Multicast_OnBuildingPlaced(WeakBuilding.Get());
					}
				}),
				ReplicationDelay, false);

			// 스캔 완료 후 건물 활성화
			// AInv_BuildingActor: BeginPlay 즉시 시작 → ScanDuration만 대기
			// 터렛 등: Multicast 경유 → ReplicationDelay + ScanDuration 대기
			const float ActivationDelay = bIsBuildingActor
				? DefaultScanDuration
				: (ReplicationDelay + DefaultScanDuration);
			FTimerHandle ActivationTimer;
			World->GetTimerManager().SetTimer(ActivationTimer,
				FTimerDelegate::CreateLambda([WeakBuilding]()
				{
					if (WeakBuilding.IsValid())
					{
						WeakBuilding->SetActorEnableCollision(true);
#if INV_DEBUG_BUILD
						UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Construction complete — collision enabled: %s"),
							*WeakBuilding->GetName());
#endif
					}
				}),
				ActivationDelay, false);
		}
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
	// 디버그: Multicast 도달 여부 확인 (Actor가 null이어도 출력)
	const bool bIsServer = GetOwner() && GetOwner()->HasAuthority();
	UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] === Multicast received === IsServer=%d, Actor=%s, Material=%s"),
		bIsServer,
		PlacedBuilding ? *PlacedBuilding->GetName() : TEXT("NULL"),
		DefaultPlacementScanMaterial ? *DefaultPlacementScanMaterial->GetName() : TEXT("NULL"));

	if (!IsValid(PlacedBuilding)) return;

	// 숨김 해제 — 스캔 머티리얼이 적용되면서 바닥부터 물질화
	PlacedBuilding->SetActorHiddenInGame(false);

	// 배치 스캔 VFX 시작 (모든 클라이언트에서 재생)
	if (AInv_BuildingActor* BuildingActor = Cast<AInv_BuildingActor>(PlacedBuilding))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Path: Inv_BuildingActor, Material: %s"),
			BuildingActor->PlacementScanMaterial ? *BuildingActor->PlacementScanMaterial->GetName() : TEXT("NULL"));
		BuildingActor->StartPlacementScanVFX();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Path: Fallback, DefaultMaterial: %s"),
			DefaultPlacementScanMaterial ? *DefaultPlacementScanMaterial->GetName() : TEXT("NULL"));
		ApplyScanVFXToAnyActor(PlacedBuilding);
	}

	// 스캔 머티리얼이 바닥부터 물질화를 제어 (Masked 블렌드로 위쪽 자동 마스킹)

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

		// R키: 오른쪽 연속 회전 (Started=시작, Completed=종료)
		if (IsValid(IA_RotateBuildingRight) && RotateRightStartHandle == 0)
		{
			RotateRightStartHandle = EnhancedInputComponent->BindAction(
				IA_RotateBuildingRight, ETriggerEvent::Started, this, &UInv_BuildingComponent::OnRotateRightStarted
			).GetHandle();
			RotateRightStopHandle = EnhancedInputComponent->BindAction(
				IA_RotateBuildingRight, ETriggerEvent::Completed, this, &UInv_BuildingComponent::OnRotateRightCompleted
			).GetHandle();
		}

		// Q키: 왼쪽 연속 회전 (Started=시작, Completed=종료)
		if (IsValid(IA_RotateBuildingLeft) && RotateLeftStartHandle == 0)
		{
			RotateLeftStartHandle = EnhancedInputComponent->BindAction(
				IA_RotateBuildingLeft, ETriggerEvent::Started, this, &UInv_BuildingComponent::OnRotateLeftStarted
			).GetHandle();
			RotateLeftStopHandle = EnhancedInputComponent->BindAction(
				IA_RotateBuildingLeft, ETriggerEvent::Completed, this, &UInv_BuildingComponent::OnRotateLeftCompleted
			).GetHandle();
		}

		// G키: 스냅 회전 (Started만 — 탭 한 번에 SnapRotationAngle도 회전)
		if (IsValid(IA_SnapRotateBuilding) && SnapRotateHandle == 0)
		{
			SnapRotateHandle = EnhancedInputComponent->BindAction(
				IA_SnapRotateBuilding, ETriggerEvent::Started, this, &UInv_BuildingComponent::OnSnapRotate
			).GetHandle();
		}
	}
}

void UInv_BuildingComponent::ShowBuildModeHUD()
{
	if (!OwningPC.IsValid()) return;

	// BuildModeHUDClass가 NULL이면 직접 로드 시도 (BP CDO 저장 실패 대비)
	if (!BuildModeHUDClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] BuildModeHUDClass가 NULL! 직접 로드 시도..."));
		UClass* LoadedClass = LoadClass<UInv_BuildModeHUD>(nullptr, TEXT("/Inventory/Widgets/Building/WBP_Inv_BuildModeHUD.WBP_Inv_BuildModeHUD_C"));
		if (LoadedClass)
		{
			BuildModeHUDClass = LoadedClass;
			UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] 직접 로드 성공: %s"), *LoadedClass->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[BuildModeHUD-Debug] 직접 로드도 실패! HUD를 표시할 수 없습니다."));
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] ShowBuildModeHUD 진입. OwningPC=%s, BuildModeHUDClass=%s"),
		TEXT("Valid"), *BuildModeHUDClass->GetName());

	// 이미 표시 중이면 건물 정보만 업데이트 (NativeConstruct 중복 실행 방지)
	if (IsValid(BuildModeHUDInstance))
	{
		BuildModeHUDInstance->SetBuildingInfo(
			CurrentBuildingInfo.BuildingName,
			CurrentBuildingInfo.BuildingIcon,
			CurrentBuildingInfo.MaterialIcon1, CurrentBuildingInfo.MaterialAmount1, CurrentBuildingInfo.MaterialTag1,
			CurrentBuildingInfo.MaterialIcon2, CurrentBuildingInfo.MaterialAmount2, CurrentBuildingInfo.MaterialTag2,
			CurrentBuildingInfo.MaterialIcon3, CurrentBuildingInfo.MaterialAmount3, CurrentBuildingInfo.MaterialTag3
		);
		UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] 기존 HUD 인스턴스 재사용. BuildingName=%s"), *CurrentBuildingInfo.BuildingName.ToString());
		return;
	}

	BuildModeHUDInstance = CreateWidget<UInv_BuildModeHUD>(OwningPC.Get(), BuildModeHUDClass);

	UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] CreateWidget 결과: %s"),
		IsValid(BuildModeHUDInstance) ? TEXT("성공") : TEXT("실패!"));

	if (!IsValid(BuildModeHUDInstance)) return;

	// 건물 정보 설정
	BuildModeHUDInstance->SetBuildingInfo(
		CurrentBuildingInfo.BuildingName,
		CurrentBuildingInfo.BuildingIcon,
		CurrentBuildingInfo.MaterialIcon1, CurrentBuildingInfo.MaterialAmount1, CurrentBuildingInfo.MaterialTag1,
		CurrentBuildingInfo.MaterialIcon2, CurrentBuildingInfo.MaterialAmount2, CurrentBuildingInfo.MaterialTag2,
		CurrentBuildingInfo.MaterialIcon3, CurrentBuildingInfo.MaterialAmount3, CurrentBuildingInfo.MaterialTag3
	);

	BuildModeHUDInstance->AddToViewport();

	UE_LOG(LogTemp, Warning, TEXT("[BuildModeHUD-Debug] AddToViewport 완료! BuildingName=%s"), *CurrentBuildingInfo.BuildingName.ToString());
}

void UInv_BuildingComponent::HideBuildModeHUD()
{
	if (IsValid(BuildModeHUDInstance))
	{
		BuildModeHUDInstance->RemoveFromParent();
		BuildModeHUDInstance = nullptr;
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("BuildModeHUD: 제거됨"));
#endif
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

		// 회전 바인딩 해제
		if (RotateRightStartHandle != 0)
		{
			EnhancedInputComponent->RemoveBindingByHandle(RotateRightStartHandle);
			RotateRightStartHandle = 0;
		}
		if (RotateRightStopHandle != 0)
		{
			EnhancedInputComponent->RemoveBindingByHandle(RotateRightStopHandle);
			RotateRightStopHandle = 0;
		}
		if (RotateLeftStartHandle != 0)
		{
			EnhancedInputComponent->RemoveBindingByHandle(RotateLeftStartHandle);
			RotateLeftStartHandle = 0;
		}
		if (RotateLeftStopHandle != 0)
		{
			EnhancedInputComponent->RemoveBindingByHandle(RotateLeftStopHandle);
			RotateLeftStopHandle = 0;
		}
		if (SnapRotateHandle != 0)
		{
			EnhancedInputComponent->RemoveBindingByHandle(SnapRotateHandle);
			SnapRotateHandle = 0;
		}
	}
}

// ============================================================================
// 건물 회전 콜백
// ============================================================================

void UInv_BuildingComponent::OnRotateRightStarted()
{
	bIsRotatingRight = true;
}

void UInv_BuildingComponent::OnRotateRightCompleted()
{
	bIsRotatingRight = false;
}

void UInv_BuildingComponent::OnRotateLeftStarted()
{
	bIsRotatingLeft = true;
}

void UInv_BuildingComponent::OnRotateLeftCompleted()
{
	bIsRotatingLeft = false;
}

void UInv_BuildingComponent::OnSnapRotate()
{
	CurrentBuildRotationYaw += SnapRotationAngle;
	CurrentBuildRotationYaw = FMath::Fmod(CurrentBuildRotationYaw + 360.f, 360.f);

	// 즉시 고스트 회전 적용 (다음 Tick 전에 시각 피드백)
	if (IsValid(GhostActorInstance))
	{
		GhostActorInstance->SetActorRotation(FRotator(0.f, CurrentBuildRotationYaw, 0.f));
	}
}

// ============================================================================
// 범용 스캔 VFX (비-BuildingActor 건물용, BossEncounterCube 패턴)
// ============================================================================

void UInv_BuildingComponent::ApplyScanVFXToAnyActor(AActor* TargetActor)
{
	if (!IsValid(TargetActor) || !DefaultPlacementScanMaterial)
	{
		return;
	}

	// 모든 MeshComponent 수집 (StaticMesh + DynamicMesh + SkeletalMesh)
	TArray<UMeshComponent*> AllMeshes;
	TargetActor->GetComponents<UMeshComponent>(AllMeshes);

	if (AllMeshes.Num() == 0)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Log, TEXT("[BuildingScanVFX] No MeshComponent on %s — skipping"), *TargetActor->GetName());
#endif
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// MeshComponent만의 바운딩 박스 합산 (DetectionSphere 등 비렌더링 컴포넌트 제외)
	FBox MeshBounds(ForceInit);
	for (UMeshComponent* MeshComp : AllMeshes)
	{
		if (MeshComp && MeshComp->IsVisible() && MeshComp->GetNumMaterials() > 0)
		{
			MeshBounds += MeshComp->Bounds.GetBox();
		}
	}
	if (!MeshBounds.IsValid)
	{
		return;
	}
	const float BottomZ = MeshBounds.Min.Z;
	const float TopZ = MeshBounds.Max.Z;
	const float MeshHeight = TopZ - BottomZ;
	if (MeshHeight < 1.f) return;

	const float BandWidth = FMath::Max(MeshHeight * DefaultScanBandRatio, 10.f);

	// 머티리얼 교체 데이터 구조체 (바닥→위 리빌 방식)
	struct FScanSwapEntry
	{
		TWeakObjectPtr<UMeshComponent> Mesh;
		TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;
		UMaterialInstanceDynamic* DMI = nullptr;
	};
	TSharedPtr<TArray<FScanSwapEntry>> Entries = MakeShared<TArray<FScanSwapEntry>>();

	// TODO: 디버그 확인 후 제거
	UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] Actor=%s MeshCount=%d BottomZ=%.0f TopZ=%.0f"),
		*TargetActor->GetName(), AllMeshes.Num(), BottomZ, TopZ);
	for (UMeshComponent* MeshComp : AllMeshes)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag]  Mesh=%s Class=%s Visible=%d NumMats=%d"),
			*MeshComp->GetName(), *MeshComp->GetClass()->GetName(),
			MeshComp->IsVisible(), MeshComp->GetNumMaterials());

		if (!MeshComp || !MeshComp->IsVisible()) continue;

		const int32 NumMats = MeshComp->GetNumMaterials();
		if (NumMats <= 0) continue;

		UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(DefaultPlacementScanMaterial, TargetActor);
		if (!DMI) continue;

		DMI->SetScalarParameterValue(TEXT("ScanHeight"), BottomZ);
		DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);

		FScanSwapEntry Entry;
		Entry.Mesh = MeshComp;
		Entry.DMI = DMI;

		for (int32 i = 0; i < NumMats; ++i)
		{
			Entry.OriginalMaterials.Add(MeshComp->GetMaterial(i));
			MeshComp->SetMaterial(i, DMI);
		}

		Entries->Add(MoveTemp(Entry));
	}

	if (Entries->Num() == 0) return;

	// 타이머 기반 애니메이션 (바닥→위 리빌)
	TSharedPtr<float> Progress = MakeShared<float>(0.f);
	const float CapturedDuration = DefaultScanDuration;
	const float CapturedBottomZ = BottomZ;
	const float CapturedTopZ = TopZ;
	const float CapturedBandRatio = DefaultScanBandRatio;
	const float TickInterval = 1.f / 30.f;

	FTimerHandle ScanTimerHandle;
	World->GetTimerManager().SetTimer(ScanTimerHandle,
		FTimerDelegate::CreateLambda([Entries, Progress, CapturedDuration, CapturedBottomZ, CapturedTopZ, CapturedBandRatio, TickInterval]()
		{
			*Progress += TickInterval / FMath::Max(CapturedDuration, 0.1f);

			if (*Progress >= 1.0f)
			{
				// 스캔 완료 — 원본 머티리얼 복원
				for (auto& Entry : *Entries)
				{
					if (Entry.Mesh.IsValid())
					{
						for (int32 i = 0; i < Entry.OriginalMaterials.Num(); ++i)
						{
							Entry.Mesh->SetMaterial(i, Entry.OriginalMaterials[i]);
						}
					}
				}
				Entries->Empty();
				return;
			}

			// 모든 DMI의 ScanHeight Lerp: BottomZ → TopZ
			const float CurrentZ = FMath::Lerp(CapturedBottomZ, CapturedTopZ, *Progress);
			const float Height = CapturedTopZ - CapturedBottomZ;
			const float BW = FMath::Max(Height * CapturedBandRatio, 10.f);

			for (auto& Entry : *Entries)
			{
				if (Entry.DMI)
				{
					Entry.DMI->SetScalarParameterValue(TEXT("ScanHeight"), CurrentZ);
					Entry.DMI->SetScalarParameterValue(TEXT("BandWidth"), BW);
				}
			}
		}),
		TickInterval, true);

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Fallback scan started: %s (%d meshes, Bottom=%.0f, Top=%.0f, Duration=%.1fs)"),
		*TargetActor->GetName(), Entries->Num(), BottomZ, TopZ, CapturedDuration);
#endif
}

