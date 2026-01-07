// Fill out your copyright notice in the Description page of Project Settings.

#include "Crafting/Actors/Inv_CraftingStation.h"
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
		UE_LOG(LogTemp, Log, TEXT("OnInteract: 로컬 컨트롤러가 아님 - UI 열기 무시"));
		return;
	}
	
	if (!IsValid(CraftingMenuClass))
	{
		UE_LOG(LogTemp, Error, TEXT("CraftingMenuClass가 설정되지 않았습니다! BP에서 위젯을 할당하세요."));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== 크래프팅 스테이션 상호작용! ==="));
	UE_LOG(LogTemp, Warning, TEXT("Station: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("Menu Class: %s"), *CraftingMenuClass->GetName());

	// 이미 메뉴가 열려있으면 닫기
	if (IsValid(CurrentCraftingMenu))
	{
		CurrentCraftingMenu->RemoveFromParent();
		CurrentCraftingMenu = nullptr;
		
		// 입력 모드를 게임으로 변경
		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(false);
		
		UE_LOG(LogTemp, Log, TEXT("크래프팅 메뉴 닫힘"));
		return;
	}

	// 크래프팅 메뉴 생성 및 표시
	CurrentCraftingMenu = CreateWidget<UUserWidget>(PlayerController, CraftingMenuClass);
	if (IsValid(CurrentCraftingMenu))
	{
		CurrentCraftingMenu->AddToViewport();
		
		// 입력 모드를 UI로 변경
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(CurrentCraftingMenu->TakeWidget());
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(true);
		
		UE_LOG(LogTemp, Log, TEXT("크래프팅 메뉴 열림!"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("크래프팅 메뉴 생성 실패!"));
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

