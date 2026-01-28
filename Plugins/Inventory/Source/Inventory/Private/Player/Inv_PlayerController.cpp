// Inv_PlayerController.cpp
// 
// ============================================
// 📌 수정일: 2025-01-28 (Phase B - 로그인 RPC 추가)
// ============================================

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
#include "Interfaces/Inv_Interface_Primary.cpp"

// ============================================
// 📌 [Phase B] 로그인 관련 include
// ============================================
#include "Login/HellunaLoginWidget.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Player/HellunaPlayerState.h"

AInv_PlayerController::AInv_PlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	TraceLength = 500.0;
	ItemTraceChannel = ECC_GameTraceChannel1;

	// ============================================
	// 📌 [Phase B] 마우스 커서 설정
	// 기본은 숨김, 로그인 UI 표시 시 보임
	// ============================================
	bShowMouseCursor = false;
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
	}
}

void AInv_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [Inv_PlayerController] BeginPlay                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// Input 설정
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

	// ============================================
	// 📌 [Phase B] 로그인 체크 및 UI 표시
	// 클라이언트에서만 실행
	// ============================================
	if (IsLocalController())
	{
		// PlayerState에서 로그인 여부 확인
		AHellunaPlayerState* PS = GetPlayerState<AHellunaPlayerState>();
		
		bool bIsLoggedIn = false;
		if (PS)
		{
			bIsLoggedIn = PS->IsLoggedIn();
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] PlayerState 로그인 상태: %s"), bIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HellunaPlayerState 없음!"));
		}

		if (!bIsLoggedIn)
		{
			// ============================================
			// 📌 로그인 안 됨 → 로그인 UI 표시
			// ============================================
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] → 로그인 필요! 로그인 UI 표시"));
			
			// 약간 딜레이 후 UI 표시 (네트워크 안정화)
			FTimerHandle TimerHandle;
			GetWorldTimerManager().SetTimer(TimerHandle, this, &AInv_PlayerController::ShowLoginUI, 0.5f, false);
		}
		else
		{
			// ============================================
			// 📌 로그인 됨 → HUD 위젯 생성
			// ============================================
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] → 로그인 완료! HUD 생성"));
			CreateHUDWidget();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
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

// ============================================
// 📌 [Phase B] 로그인 UI 표시
// ============================================
void AInv_PlayerController::ShowLoginUI()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [Inv_PlayerController] ShowLoginUI                         │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));

	if (!IsLocalController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] 로컬 컨트롤러 아님 → UI 표시 안 함"));
		return;
	}

	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Inv_PlayerController] ❌ LoginWidgetClass가 설정되지 않았습니다!"));
		UE_LOG(LogTemp, Error, TEXT("[Inv_PlayerController] BP_Inv_PlayerController에서 'Login Widget Class' 설정 필요!"));
		
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("❌ LoginWidgetClass가 설정되지 않았습니다! BP_Inv_PlayerController에서 설정 필요"));
		}
		return;
	}

	if (!LoginWidget)
	{
		LoginWidget = CreateWidget<UHellunaLoginWidget>(this, LoginWidgetClass);
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] 로그인 위젯 생성됨"));
	}

	if (LoginWidget && !LoginWidget->IsInViewport())
	{
		LoginWidget->AddToViewport(100);  // 높은 Z-Order로 다른 UI 위에 표시
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] ✅ 로그인 위젯 표시됨"));

		// UI 모드로 전환
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(LoginWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		bShowMouseCursor = true;
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] 로그인 UI 숨기기
// ============================================
void AInv_PlayerController::HideLoginUI()
{
	UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] HideLoginUI"));

	if (LoginWidget && LoginWidget->IsInViewport())
	{
		LoginWidget->RemoveFromParent();
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] ✅ 로그인 위젯 숨김"));
	}

	// 게임 모드로 전환
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

	// HUD 위젯 생성
	CreateHUDWidget();
}

// ============================================
// 📌 [Phase B] 로그인 버튼 클릭 시 호출
// ============================================
void AInv_PlayerController::OnLoginButtonClicked(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [Inv_PlayerController] OnLoginButtonClicked        ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ → Server_RequestLogin RPC 호출                             ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// 서버에 로그인 요청
	Server_RequestLogin(PlayerId, Password);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] Server RPC - 로그인 요청
// ============================================
void AInv_PlayerController::Server_RequestLogin_Implementation(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [Inv_PlayerController] Server_RequestLogin (서버)  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ → DefenseGameMode::ProcessLogin 호출                       ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// DefenseGameMode에서 로그인 처리
	AHellunaDefenseGameMode* GameMode = Cast<AHellunaDefenseGameMode>(GetWorld()->GetAuthGameMode());
	if (GameMode)
	{
		GameMode->ProcessLogin(this, PlayerId, Password);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Inv_PlayerController] ❌ DefenseGameMode를 찾을 수 없습니다!"));
		Client_LoginResult(false, TEXT("서버 오류: GameMode를 찾을 수 없습니다."));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] Client RPC - 로그인 결과
// ============================================
void AInv_PlayerController::Client_LoginResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [Inv_PlayerController] Client_LoginResult          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ bSuccess: %s"), bSuccess ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (bSuccess)
	{
		// 로그인 성공 → UI 숨기기
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] ✅ 로그인 성공! UI 숨기기"));

		if (LoginWidget)
		{
			LoginWidget->ShowMessage(TEXT("로그인 성공!"), false);
		}

		// 약간의 딜레이 후 UI 숨기기
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, this, &AInv_PlayerController::HideLoginUI, 0.5f, false);
	}
	else
	{
		// 로그인 실패 → 에러 메시지 표시
		UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] ❌ 로그인 실패: %s"), *ErrorMessage);

		if (LoginWidget)
		{
			LoginWidget->ShowMessage(ErrorMessage, true);
			LoginWidget->SetLoadingState(false);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] Client RPC - 로그인 UI 표시 요청
// ============================================
void AInv_PlayerController::Client_ShowLoginUI_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] Client_ShowLoginUI 호출됨"));
	ShowLoginUI();
}

// ============================================
// 📌 기존 함수들
// ============================================

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
			UE_LOG(LogTemp, Warning, TEXT("[Inv_PlayerController] ✅ HUD 위젯 생성됨"));
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
