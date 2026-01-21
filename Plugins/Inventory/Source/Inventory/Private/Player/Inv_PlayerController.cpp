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


AInv_PlayerController::AInv_PlayerController()
{
	// 추적길이 초기화 (추적길이란? 내 시점에서 얼마만큼 거리를 두고 라인트레이스를 할 것인지)
	PrimaryActorTick.bCanEverTick = true;
	TraceLength = 500.0;
	ItemTraceChannel = ECC_GameTraceChannel1; //커스텀 채널 1번을 사용
}

void AInv_PlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TraceForInteractables(); // 아이템과 크래프팅 스테이션 통합 감지
}

// I키를 누르면 점을 삭제 시키는 것인데. 
void AInv_PlayerController::ToggleInventory()
{
	//약한 포인터가 유효한지? 이거로 참조하여 메뉴를 만든다? WeakPtr은 좀 공부를 해야겠다.
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

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (IsValid(Subsystem))
	{
		for (UInputMappingContext* CurrentContext : DefaultIMCs)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}
	}

	InventoryComponent = FindComponentByClass<UInv_InventoryComponent>();
	
	// ============================================
	// ⭐ [WeaponBridge] EquipmentComponent 참조 가져오기
	// ============================================
	EquipmentComponent = FindComponentByClass<UInv_EquipmentComponent>();
	if (EquipmentComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] PlayerController - EquipmentComponent 찾음"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] PlayerController - EquipmentComponent 없음!"));
	}
	
	CreateHUDWidget(); // 위젯 생성 호출
}

void AInv_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent);

	//키 활성 바인딩 부분.
	EnhancedInputComponent->BindAction(PrimaryInteractAction, ETriggerEvent::Started, this, &AInv_PlayerController::PrimaryInteract);
	EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &AInv_PlayerController::ToggleInventory);
	
	// ============================================
	// ⭐ [WeaponBridge] 주무기 전환 InputAction 바인딩
	// ============================================
	if (PrimaryWeaponAction)
	{
		EnhancedInputComponent->BindAction(PrimaryWeaponAction, ETriggerEvent::Started, this, &AInv_PlayerController::HandlePrimaryWeapon);
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 전환 액션 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 주무기 전환 액션이 설정되지 않음 - Blueprint에서 지정 필요"));
	}

	// ============================================
	// ⭐ [WeaponBridge] 보조무기 전환 InputAction 바인딩
	// ============================================
	if (SecondaryWeaponAction)
	{
		EnhancedInputComponent->BindAction(SecondaryWeaponAction, ETriggerEvent::Started, this, &AInv_PlayerController::HandleSecondaryWeapon);
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 전환 액션 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 보조무기 전환 액션이 설정되지 않음 - Blueprint에서 지정 필요"));
	}
}

void AInv_PlayerController::PrimaryInteract()
{
	if (!ThisActor.IsValid()) return;

	// ==========================================================
	// [0순위] 단순 상호작용 (인터페이스 검사) -> [수정] 서버 RPC 호출
	// ==========================================================
	if (ThisActor->Implements<UInv_Interface_Primary>())
	{
		// "서버야, 이 문 좀 열어줘!" (여기서 서버로 보냄)
		Server_Interact(ThisActor.Get());
       
		// 서버로 보냈으니 클라이언트는 할 일 끝! 종료.
		return; 
	}
	
	// 1순위: 크래프팅 스테이션 상호작용
	if (CurrentCraftingStation.IsValid() && CurrentCraftingStation == ThisActor)
	{
		if (ThisActor->Implements<UInv_CraftingInterface>())
		{
			UE_LOG(LogTemp, Warning, TEXT("=== E키로 크래프팅 스테이션 상호작용! ==="));
			UE_LOG(LogTemp, Warning, TEXT("Station: %s"), *ThisActor->GetName());
			
			IInv_CraftingInterface::Execute_OnInteract(ThisActor.Get(), this);
			return;
		}
	}

	// 2순위: 아이템 줍기
	UInv_ItemComponent* ItemComp = ThisActor->FindComponentByClass<UInv_ItemComponent>();
	if (!IsValid(ItemComp) || !InventoryComponent.IsValid()) return;

	InventoryComponent->TryAddItem(ItemComp);
}

// 서버에서 실행되는 함수 구현 (맵 변경 함수)
void AInv_PlayerController::Server_Interact_Implementation(AActor* TargetActor)
{
	if (!TargetActor) return;

	// 서버가 다시 한 번 확인 (인터페이스가 있는지?)
	if (TargetActor->Implements<UInv_Interface_Primary>())
	{
		// 이제 '서버'에서 실행되므로 MoveMapActor::Interact() 내부의 권한 체크를 통과합니다!
		IInv_Interface_Primary::Execute_ExecuteInteract(TargetActor, this);
        
		UE_LOG(LogTemp, Log, TEXT("[Server] 문 상호작용 성공: %s"), *TargetActor->GetName());
	}
}

void AInv_PlayerController::CreateHUDWidget() // 위젯 생성 부분
{
	if (!IsLocalController()) return;
	HUDWidget = CreateWidget<UInv_HUDWidget>(this, HUDWidgetClass);
	if(IsValid(HUDWidget))
	{
		HUDWidget->AddToViewport();
	}
}

void AInv_PlayerController::TraceForInteractables()
{
	// 바라보는 뷰포트 좌표 반환
	if (!IsValid(GEngine) || !IsValid(GEngine->GameViewport)) return;
	FVector2D ViewportSize;
	GEngine->GameViewport->GetViewportSize(ViewportSize);
	const FVector2D ViewportCenter = ViewportSize / 2.f; // 뷰포트 중앙 좌표

	FVector TraceStart;
	FVector Forward; //당연히 법선벡터
	if (!UGameplayStatics::DeprojectScreenToWorld(this, ViewportCenter, TraceStart, Forward)) return;
	 
	const FVector TraceEnd = TraceStart + (Forward * TraceLength); //괄호는 쉽게 변경할 수 있는 매개변수	
	FHitResult HitResult;
	//라인트레이스는 한 번 봐야겠음
	//선 추적부분 기억하지 이제 매 프레임마다 라인트레이스를 한다. (ToonTanks 처럼) 
	GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ItemTraceChannel);

	LastActor = ThisActor;
	ThisActor = HitResult.GetActor();

	// 1단계: 크래프팅 스테이션 확인
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

	// 2단계: UI 메시지 처리 - 아무것도 감지되지 않을 때
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
		// 하이라이트 처리
		if (UActorComponent* Highlightable = ThisActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IInv_Highlightable::Execute_Highlight(Highlightable);
		}

		// UI 메시지 표시
		if (IsValid(HUDWidget))
		{
			if (bIsCraftingStation)
			{
				// ⭐ Crafting 메시지 표시 (ShowPickupMessage 재사용)
				AInv_CraftingStation* CraftingStation = Cast<AInv_CraftingStation>(ThisActor.Get());
				if (IsValid(CraftingStation))
				{
					HUDWidget->ShowPickupMessage(CraftingStation->GetPickupMessage());
					UE_LOG(LogTemp, Log, TEXT("크래프팅 스테이션 감지: %s"), *ThisActor->GetName());
				}
			}
			else
			{
				// 아이템 픽업 메시지 표시
				UInv_ItemComponent* ItemComponent = ThisActor->FindComponentByClass<UInv_ItemComponent>();
				if (IsValid(ItemComponent))
				{
					HUDWidget->ShowPickupMessage(ItemComponent->GetPickupMessage());
					UE_LOG(LogTemp, Warning, TEXT("Started tracing"))
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

// ============================================
// ⭐ [WeaponBridge] 주무기 입력 처리 함수
// ⭐ 1키 입력 시 호출됨
// ============================================
void AInv_PlayerController::HandlePrimaryWeapon()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] PlayerController - HandlePrimaryWeapon 호출됨 (1키 입력)"));
	
	if (EquipmentComponent.IsValid())
	{
		EquipmentComponent->HandlePrimaryWeaponInput();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] PlayerController - EquipmentComponent 없음! 주무기 전환 불가"));
	}
}

// ============================================
// ⭐ [WeaponBridge] 보조무기 입력 처리 함수
// ⭐ 2키 입력 시 호출됨
// ============================================
void AInv_PlayerController::HandleSecondaryWeapon()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] PlayerController - HandleSecondaryWeapon 호출됨 (2키 입력)"));
	
	if (EquipmentComponent.IsValid())
	{
		EquipmentComponent->HandleSecondaryWeaponInput();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] PlayerController - EquipmentComponent 없음! 보조무기 전환 불가"));
	}
}


