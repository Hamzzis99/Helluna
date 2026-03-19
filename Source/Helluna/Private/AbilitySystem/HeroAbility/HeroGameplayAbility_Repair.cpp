// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Repair.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Character/HellunaHeroCharacter.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Component/RepairComponent.h"
#include "Widgets/RepairWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Building/Components/Inv_BuildingComponent.h"

#include "DebugHelper.h"
#include "Helluna.h"

UHeroGameplayAbility_Repair::UHeroGameplayAbility_Repair()
{
	// InstancedPerActor: 액터당 1개 인스턴스 → CurrentWidget 멤버 변수가 유지됨
	// NonInstanced(기본값)이면 CDO를 공유하므로 CurrentWidget 토글이 불가능!
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
}

void UHeroGameplayAbility_Repair::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	Repair(ActorInfo);

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

void UHeroGameplayAbility_Repair::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_Repair::Repair(const FGameplayAbilityActorInfo* ActorInfo)
{
	AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
	if (!Hero) return;

	if (!Hero->IsLocallyControlled()) return;

	UE_LOG(LogTemp, Warning, TEXT("[Repair] Repair() called. this=%p, CurrentWidget=%p, IsValid=%s"),
		this, CurrentWidget.Get(),
		IsValid(CurrentWidget) ? TEXT("true") : TEXT("false"));

	// F키 토글: Widget이 이미 열려있으면 닫기
	const bool bIsInViewport = IsValid(CurrentWidget) && CurrentWidget->IsInViewport();
	UE_LOG(LogTemp, Warning, TEXT("[Repair] Toggle detected, widget is in viewport: %s"),
		bIsInViewport ? TEXT("true") : TEXT("false"));

	if (bIsInViewport)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Repair] Closing widget, InputMode -> GameOnly"));
		if (URepairWidget* RepairWidget = Cast<URepairWidget>(CurrentWidget))
		{
			RepairWidget->CloseWidget();
		}
		else
		{
			APlayerController* PC = Hero->GetController<APlayerController>();
			if (PC)
			{
				PC->SetInputMode(FInputModeGameOnly());
				PC->bShowMouseCursor = false;
			}
			CurrentWidget->RemoveFromParent();
		}
		CurrentWidget = nullptr;
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Repair] Opening widget, InputMode -> GameAndUI"));

	AHellunaDefenseGameState* GS = GetWorld()->GetGameState<AHellunaDefenseGameState>();
	if (!GS) return;

	AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
	if (!Ship) return;

	URepairComponent* RepairComp = Ship->FindComponentByClass<URepairComponent>();
	if (!RepairComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair] RepairComponent not found!"));
		return;
	}

	APlayerController* PC = Hero->GetController<APlayerController>();
	if (!PC) return;

	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PC);
	if (!InvComp) return;

	// 방안 B: 다른 위젯 열려있으면 먼저 닫기
	if (InvComp->IsMenuOpen())
	{
		InvComp->ToggleInventoryMenu();
	}

	UInv_BuildingComponent* BuildComp = PC->FindComponentByClass<UInv_BuildingComponent>();
	if (IsValid(BuildComp))
	{
		BuildComp->ForceEndBuildMode();
		BuildComp->CloseBuildMenu();
	}

	// CraftingMenu 열려있으면 닫기
	if (UWorld* RepairWorld = GetWorld())
	{
		TArray<UUserWidget*> FoundWidgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(RepairWorld, FoundWidgets, UUserWidget::StaticClass(), false);
		for (UUserWidget* Widget : FoundWidgets)
		{
			if (!IsValid(Widget)) continue;
			if (Widget->GetClass()->GetName().Contains(TEXT("CraftingMenu")))
			{
				Widget->RemoveFromParent();
			}
		}
	}

	if (!RepairMaterialWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair] RepairMaterialWidgetClass not set!"));
		return;
	}

	CurrentWidget = CreateWidget<UUserWidget>(PC, RepairMaterialWidgetClass);
	if (!CurrentWidget) return;

	UE_LOG(LogTemp, Warning, TEXT("[Repair] Widget created: %p (%s)"),
		CurrentWidget.Get(), *CurrentWidget->GetClass()->GetName());

	// RepairWidget이면 InitializeWidget 호출
	if (URepairWidget* RepairWidget = Cast<URepairWidget>(CurrentWidget))
	{
		RepairWidget->InitializeWidget(RepairComp, InvComp);
	}

	CurrentWidget->AddToViewport(100);

	PC->FlushPressedKeys();
	PC->SetInputMode(FInputModeGameAndUI());
	PC->bShowMouseCursor = true;

	UE_LOG(LogTemp, Warning, TEXT("[Repair] Widget added to viewport. CurrentWidget=%p"), CurrentWidget.Get());
}
