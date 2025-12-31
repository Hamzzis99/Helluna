#include "Player/Inv_PlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Interaction/Inv_Highlightable.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/HUD/Inv_HUDWidget.h"



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

	TraceForItem();
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
	CreateHUDWidget(); // 위젯 생성 호출
}

void AInv_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent);

	//키 활성 바인딩 부분.
	EnhancedInputComponent->BindAction(PrimaryInteractAction, ETriggerEvent::Started, this, &AInv_PlayerController::PrimaryInteract);
	EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &AInv_PlayerController::ToggleInventory);
}

void AInv_PlayerController::PrimaryInteract()
{
	if (!ThisActor.IsValid()) return;

	//이게 뭐하는 부분이지?
	UInv_ItemComponent* ItemComp = ThisActor->FindComponentByClass<UInv_ItemComponent>();
	if (!IsValid(ItemComp) || !InventoryComponent.IsValid()) return;

	InventoryComponent->TryAddItem(ItemComp);
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

void AInv_PlayerController::TraceForItem()
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

	//픽업 부분 관련의 코드들.
	if (!ThisActor.IsValid())
	{
		if (IsValid(HUDWidget)) HUDWidget->HidePickupMessage(); //블루프린트에서 구현할 것들.
	}

	if (ThisActor == LastActor) return;

	if (ThisActor.IsValid())
	{
		if (UActorComponent* Highlightable = ThisActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IInv_Highlightable::Execute_Highlight(Highlightable); //배우 컴포넌트?
		}

		UInv_ItemComponent* ItemComponent = ThisActor->FindComponentByClass<UInv_ItemComponent>();
		if (!IsValid(ItemComponent)) return;

		if (IsValid(HUDWidget)) HUDWidget->ShowPickupMessage(ItemComponent->GetPickupMessage()); //블루프린트에서 구현할 것들.
		UE_LOG(LogTemp, Warning, TEXT("Started tracing"))
	}

	if (LastActor.IsValid())
	{
		if (UActorComponent* Highlightable = LastActor->FindComponentByInterface(UInv_Highlightable::StaticClass()); IsValid(Highlightable))
		{
			IInv_Highlightable::Execute_UnHighlight(Highlightable); //배우 컴포넌트?
		}
	}
}
