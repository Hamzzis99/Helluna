// Fill out your copyright notice in the Description page of Project Settings.

#include "Crafting/Actors/Inv_CraftingStation.h"
#include "Inventory.h"
#include "Blueprint/UserWidget.h"
#include "Components/StaticMeshComponent.h"

AInv_CraftingStation::AInv_CraftingStation()
{
	PrimaryActorTick.bCanEverTick = false;

	// 스테이션 메시 컴포넌트 생성
	StationMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StationMesh"));
	RootComponent = StationMesh;
}

void AInv_CraftingStation::BeginPlay()
{
	Super::BeginPlay();
}

void AInv_CraftingStation::OnInteract_Implementation(APlayerController* PlayerController)
{
	if (!IsValid(PlayerController)) return;
	
	// 멀티플레이 체크: 로컬 컨트롤러만 UI 열기
	if (!PlayerController->IsLocalController())
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Log, TEXT("OnInteract: 로컬 컨트롤러가 아님 - UI 열기 무시"));
#endif
		return;
	}

	if (!IsValid(CraftingMenuClass))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("CraftingMenuClass가 설정되지 않았습니다! BP에서 위젯을 할당하세요."));
#endif
		return;
	}

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Warning, TEXT("=== 크래프팅 스테이션 상호작용! ==="));
	UE_LOG(LogTemp, Warning, TEXT("Player: %s"), *PlayerController->GetName());
	UE_LOG(LogTemp, Warning, TEXT("Station: %s"), *GetName());
#endif

	// 이미 이 플레이어의 메뉴가 열려있으면 닫기
	if (PlayerMenuMap.Contains(PlayerController))
	{
		ForceCloseMenu(PlayerController);
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Log, TEXT("크래프팅 메뉴 닫힘 (Player: %s)"), *PlayerController->GetName());
#endif
		return;
	}

	// 크래프팅 메뉴 생성 및 표시
	UUserWidget* NewMenu = CreateWidget<UUserWidget>(PlayerController, CraftingMenuClass);
	if (IsValid(NewMenu))
	{
		NewMenu->AddToViewport();
		
		// 입력 모드를 UI로 변경
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(NewMenu->TakeWidget());
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(true);

		// PlayerMenuMap에 저장
		PlayerMenuMap.Add(PlayerController, NewMenu);

		// Timer 시작 (Lambda로 PlayerController 전달)
		FTimerHandle TimerHandle;
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUFunction(this, FName("CheckDistanceToPlayer"), PlayerController);
		
		GetWorld()->GetTimerManager().SetTimer(
			TimerHandle,
			TimerDelegate,
			DistanceCheckInterval,
			true  // 반복
		);
		
		PlayerTimerMap.Add(PlayerController, TimerHandle);

#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Log, TEXT("크래프팅 메뉴 열림! 거리 체크 시작 (%.2f초마다, Player: %s)"),
			DistanceCheckInterval, *PlayerController->GetName());
#endif
	}
	else
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("크래프팅 메뉴 생성 실패!"));
#endif
	}
}

TSubclassOf<UUserWidget> AInv_CraftingStation::GetCraftingMenuClass_Implementation() const
{
	return CraftingMenuClass;
}

float AInv_CraftingStation::GetInteractionDistance_Implementation() const
{
	return InteractionDistance;
}

void AInv_CraftingStation::CheckDistanceToPlayer(APlayerController* PC)
{
	// PlayerController가 유효한지 확인
	if (!IsValid(PC))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("PlayerController가 유효하지 않음. 메뉴 강제 닫기."));
#endif
		ForceCloseMenu(PC);
		return;
	}

	// 메뉴가 여전히 열려있는지 확인
	if (!PlayerMenuMap.Contains(PC))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Log, TEXT("메뉴가 이미 닫혔음. Timer 정지. (Player: %s)"), *PC->GetName());
#endif
		
		// Timer 정지
		if (PlayerTimerMap.Contains(PC))
		{
			GetWorld()->GetTimerManager().ClearTimer(PlayerTimerMap[PC]);
			PlayerTimerMap.Remove(PC);
		}
		return;
	}

	// PlayerController의 Pawn 가져오기
	APawn* PlayerPawn = PC->GetPawn();
	if (!IsValid(PlayerPawn))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("PlayerPawn이 유효하지 않음. 메뉴 강제 닫기. (Player: %s)"), *PC->GetName());
#endif
		ForceCloseMenu(PC);
		return;
	}

	// 거리 계산
	float Distance = FVector::Dist(GetActorLocation(), PlayerPawn->GetActorLocation());
	
	// InteractionDistance보다 멀어지면 메뉴 닫기
	if (Distance > InteractionDistance)
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("플레이어가 너무 멀어짐! (거리: %.1f / 최대: %.1f) 메뉴 자동 닫기 (Player: %s)"),
			Distance, InteractionDistance, *PC->GetName());
#endif
		ForceCloseMenu(PC);
	}
}

void AInv_CraftingStation::ForceCloseMenu(APlayerController* PC)
{
	if (!IsValid(PC)) return;

	// 메뉴 제거
	if (PlayerMenuMap.Contains(PC))
	{
		UUserWidget* Menu = PlayerMenuMap[PC];
		if (IsValid(Menu))
		{
			Menu->RemoveFromParent();
		}
		PlayerMenuMap.Remove(PC);
	}

	// 입력 모드 복구
	FInputModeGameOnly InputMode;
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(false);

	// Timer 정지
	if (PlayerTimerMap.Contains(PC))
	{
		GetWorld()->GetTimerManager().ClearTimer(PlayerTimerMap[PC]);
		PlayerTimerMap.Remove(PC);
	}

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Log, TEXT("크래프팅 메뉴 강제 닫힘. Timer 정지됨. (Player: %s)"), *PC->GetName());
#endif
}
