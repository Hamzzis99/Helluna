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
#include "Kismet/GameplayStatics.h"

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

	// [Phase 21] 다운된 팀원 근처면 수리 UI를 열지 않음 (F키 Revive 우선)
	if (Hero->FindNearestDownedHero() != nullptr)
	{
		UE_LOG(LogHelluna, Log, TEXT("[Repair] 근처에 다운 팀원 있음 → 수리 UI 스킵 (Revive 우선)"));
		return;
	}

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

	// [수리창 미표시 버그 수정]
	// 클라이언트에서 우주선이 막 스트리밍되어 들어온 직후엔 GameState의 복제 포인터
	// (SpaceShip)가 아직 null일 수 있어, 게이지(InRepair)는 떠도 F 수리창은 안 열렸음.
	// 실제 수리 RPC(Server_RepairSpaceShip)와 동일하게 "SpaceShip" 태그로 폴백 검색한다.
	AHellunaDefenseGameState* GS = GetWorld()->GetGameState<AHellunaDefenseGameState>();
	if (!GS)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair][BuildDiag] STOP@GS-null: GameState cast to AHellunaDefenseGameState failed. WorldGS=%s"),
			GetWorld()->GetGameState() ? *GetWorld()->GetGameState()->GetClass()->GetName() : TEXT("nullptr"));
		return;
	}

	AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
	if (!Ship)
	{
		TArray<AActor*> FoundShips;
		UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName("SpaceShip"), FoundShips);
		if (FoundShips.Num() > 0)
		{
			Ship = Cast<AResourceUsingObject_SpaceShip>(FoundShips[0]);
		}
		UE_LOG(LogTemp, Warning, TEXT("[Repair] GS->GetSpaceShip() null → 태그 폴백 결과: %s"),
			Ship ? *Ship->GetName() : TEXT("여전히 NULL"));
	}
	if (!Ship)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair][BuildDiag] STOP@Ship-null: GS->GetSpaceShip() + 태그폴백 모두 nullptr. Authority=%d NetMode=%d"),
			GS->HasAuthority() ? 1 : 0, (int32)GetWorld()->GetNetMode());
		return;
	}

	URepairComponent* RepairComp = Ship->FindComponentByClass<URepairComponent>();
	if (!RepairComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair][BuildDiag] STOP@RepairComp-null: Ship=%s has no URepairComponent"), *Ship->GetName());
		return;
	}

	APlayerController* PC = Hero->GetController<APlayerController>();
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair][BuildDiag] STOP@PC-null: Hero->GetController<APlayerController>() returned nullptr. Hero=%s"), *Hero->GetName());
		return;
	}

	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PC);
	if (!InvComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair][BuildDiag] STOP@InvComp-null: GetInventoryComponent(PC=%s) returned nullptr"), *PC->GetName());
		return;
	}

	// [메뉴 상호 배타] F(수리)를 열기 전에 E(회복) 메뉴가 떠 있으면 먼저 닫는다.
	Hero->CloseShipHealMenu();

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
		UE_LOG(LogTemp, Error, TEXT("[Repair][BuildDiag] STOP@WidgetClass-null: RepairMaterialWidgetClass not set on ability CDO"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Repair][BuildDiag] PRE-CreateWidget: Class=%s"), *RepairMaterialWidgetClass->GetName());

	CurrentWidget = CreateWidget<UUserWidget>(PC, RepairMaterialWidgetClass);
	if (!CurrentWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[Repair][BuildDiag] STOP@CreateWidget-null: CreateWidget returned nullptr for class %s"),
			*RepairMaterialWidgetClass->GetName());
		return;
	}

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
