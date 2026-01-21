// Capstone Project Helluna


#include "Character/HeroComponent/Helluna_FindResourceComponent.h"


#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "DebugHelper.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "HellunaGameplayTags.h"
#include "Components/WidgetComponent.h"
#include "Net/UnrealNetwork.h"
	
#include "Interaction/Inv_HighlightableStaticMesh.h"
#include "Interaction/Inv_Highlightable.h"
// ============================================================
// Helluna_FindResourceComponent (Simplified)
// - 카메라 정면으로 SphereSweepSingle(1개만)로 대상 결정
// - FocusedActor에만 Highlight/Prompt 적용
// - 파밍 가능 상태는 FocusedActor + 거리 조건으로만 토글
// - 로컬 플레이어만 Tick에서 실행(연출/UI 성격)
// ============================================================

UHelluna_FindResourceComponent::UHelluna_FindResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// ✅ BP 상속 컴포넌트 디테일 표시 안정화(이전 이슈 재발 방지)
	bEditableWhenInherited = true;
}

void UHelluna_FindResourceComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UHelluna_FindResourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) return;

	// ✅ 멀티 핵심: 연출/포커스 계산은 로컬 플레이어만
	if (!Pawn->IsLocallyControlled())
		return;

	UpdateFocus();
}

bool UHelluna_FindResourceComponent::IsValidTargetActor(AActor* Actor) const
{
	if (!Actor) return false;

	if (!RequiredActorTag.IsNone() && !Actor->ActorHasTag(RequiredActorTag))
		return false;

	return FindHighlightMesh(Actor) != nullptr;
}

UInv_HighlightableStaticMesh* UHelluna_FindResourceComponent::FindHighlightMesh(AActor* Actor) const
{
	if (!Actor) return nullptr;
	return Actor->FindComponentByClass<UInv_HighlightableStaticMesh>();
}

UWidgetComponent* UHelluna_FindResourceComponent::FindPromptWidget(AActor* Actor) const
{
	if (!Actor) return nullptr;
	return Actor->FindComponentByClass<UWidgetComponent>();
}

void UHelluna_FindResourceComponent::SetPromptVisible(AActor* Actor, bool bVisible)
{
	if (!Actor) return;

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
		return;

	if (UWidgetComponent* Prompt = FindPromptWidget(Actor))
	{
		Prompt->SetHiddenInGame(!bVisible);
		Prompt->SetVisibility(bVisible, true);

		FocusedPromptWidget = bVisible ? Prompt : nullptr;
	}
}

void UHelluna_FindResourceComponent::UpdateFocus()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) return;

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	const FVector ViewDir = ViewRot.Vector();

	// ✅ 아주 작은 전진 오프셋(비용 거의 0, 큰 콜리전에서 StartPenetrating 완화용)
	constexpr float StartForwardOffset = 10.f;

	const FVector Start = ViewLoc + ViewDir * StartForwardOffset;
	const FVector End = Start + ViewDir * TraceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MineFocusSingle), false);
	Params.AddIgnoredActor(Pawn);

	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(
		Hit,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(TraceRadius),
		Params
	);

	AActor* HitActor = bHit ? Hit.GetActor() : nullptr;

	// 1) Focus 결정 (딱 1개)
	if (HitActor && IsValidTargetActor(HitActor))
	{
		if (HitActor != FocusedActor.Get())
		{
			ApplyFocus(HitActor, FindHighlightMesh(HitActor));
		}
	}
	else
	{
		if (FocusedActor.IsValid())
		{
			ClearFocus();
		}
	}

	// 2) 파밍 가능 상태 토글 (FocusedActor + 거리 조건만)
	AActor* FocusActor = FocusedActor.Get();
	if (!FocusActor)
	{
		if (bFarmingApplied)
		{
			bFarmingApplied = false;
			ClearFarming();
		}
		return;
	}

	const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), FocusActor->GetActorLocation());
	const float RangeSq = InteractRange * InteractRange;
	const bool bInRange = (DistSq <= RangeSq);

	if (bInRange && !bFarmingApplied)
	{
		bFarmingApplied = true;
		ApplyFarming();
	}
	else if (!bInRange && bFarmingApplied)
	{
		bFarmingApplied = false;
		ClearFarming();
	}
}

void UHelluna_FindResourceComponent::ApplyFocus(AActor* NewActor, UInv_HighlightableStaticMesh* NewMesh)  //하이라이트 적용
{
	// ✅ 이전 프롬프트 OFF (한 번에 1개만)
	if (FocusedActor.IsValid() && FocusedActor.Get() != NewActor)
	{
		SetPromptVisible(FocusedActor.Get(), false);
	}

	// 이전 하이라이트 OFF
	if (FocusedMesh.IsValid())
	{
		UObject* Obj = FocusedMesh.Get();
		if (Obj && Obj->GetClass()->ImplementsInterface(UInv_Highlightable::StaticClass()))
		{
			IInv_Highlightable::Execute_UnHighlight(Obj);
		}
	}

	// 디버그 (Old -> New)
	const FString OldName = FocusedActor.IsValid() ? FocusedActor->GetName() : TEXT("None");
	const FString NewName = NewActor ? NewActor->GetName() : TEXT("None");
	Debug::Print(FString::Printf(TEXT("목표물 변경 : %s -> %s"), *OldName, *NewName), FColor::Green);

	FocusedActor = NewActor;
	FocusedMesh = NewMesh;

	// 새 하이라이트 ON
	if (FocusedMesh.IsValid())
	{
		UObject* Obj = FocusedMesh.Get();
		if (Obj && Obj->GetClass()->ImplementsInterface(UInv_Highlightable::StaticClass()))
		{
			IInv_Highlightable::Execute_Highlight(Obj);
		}
	}

	// ✅ 이미 파밍 상태(거리 안)에서 시선만 바뀐 경우 프롬프트를 새 대상으로 옮김
	if (bFarmingApplied && FocusedActor.IsValid())
	{
		if (APawn* Pawn = Cast<APawn>(GetOwner()))
		{
			const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), FocusedActor->GetActorLocation());
			if (DistSq <= InteractRange * InteractRange)
			{
				SetPromptVisible(FocusedActor.Get(), true);
				ServerSetCanFarming(true, NewActor);
			}
		}
	}
}

void UHelluna_FindResourceComponent::ClearFocus()  //하이라이트 해제
{
	if (FocusedActor.IsValid())
	{
		Debug::Print(FString::Printf(TEXT("목표물 벗어남 : %s"), *FocusedActor->GetName()), FColor::Yellow);
		SetPromptVisible(FocusedActor.Get(), false);
	}

	if (FocusedMesh.IsValid())
	{
		UObject* Obj = FocusedMesh.Get();
		if (Obj && Obj->GetClass()->ImplementsInterface(UInv_Highlightable::StaticClass()))
		{
			IInv_Highlightable::Execute_UnHighlight(Obj);
		}
	}

	// 포커스 해제 시 파밍도 같이 정리
	if (bFarmingApplied)
	{
		bFarmingApplied = false;
		ClearFarming();
	}

	FocusedActor = nullptr;
	FocusedMesh = nullptr;
	FocusedPromptWidget = nullptr;
}

void UHelluna_FindResourceComponent::ApplyFarming()  //파밍 가능 상태 적용 및 UI 띄우기, 회의 후 HUD로 수정 가능
{
	// ✅ 로컬: 현재 포커스 대상 1개에만 프롬프트 ON
	if (FocusedActor.IsValid())
	{
		SetPromptVisible(FocusedActor.Get(), true);
	}

	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetOwner()))
	{
		if (UHellunaAbilitySystemComponent* ASC = Hero->GetHellunaAbilitySystemComponent())
		{
			ASC->AddStateTag(HellunaGameplayTags::Player_status_Can_Farming);
			Debug::Print(TEXT("G키를 누르세요"), FColor::Green);
			ServerSetCanFarming(true, FocusedActor.Get());
		}
	}
}

void UHelluna_FindResourceComponent::ClearFarming() //상태 해제, UI 끄기
{
	// ✅ 프롬프트 OFF
	if (FocusedPromptWidget.IsValid())
	{
		UWidgetComponent* Prompt = FocusedPromptWidget.Get();
		Prompt->SetHiddenInGame(true);
		Prompt->SetVisibility(false, true);
		FocusedPromptWidget = nullptr;
	}
	else if (FocusedActor.IsValid())
	{
		SetPromptVisible(FocusedActor.Get(), false);
	}

	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetOwner()))
	{
		if (UHellunaAbilitySystemComponent* ASC = Hero->GetHellunaAbilitySystemComponent())
		{
			ASC->RemoveStateTag(HellunaGameplayTags::Player_status_Can_Farming);
			Debug::Print(TEXT("가까이 다가가세요"), FColor::Green);
			ServerSetCanFarming(false, nullptr);
		}
	}
}

void UHelluna_FindResourceComponent::ServerSetCanFarming_Implementation(bool bEnable, AActor* FarmingTarget)  //서버에 파밍 가능 상태 동기화
{
	ServerFarmingTarget = bEnable ? FarmingTarget : nullptr;

	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetOwner()))
	{
		if (UHellunaAbilitySystemComponent* ASC = Hero->GetHellunaAbilitySystemComponent())
		{
			if (bEnable)
				ASC->AddStateTag(HellunaGameplayTags::Player_status_Can_Farming);
			else
				ASC->RemoveStateTag(HellunaGameplayTags::Player_status_Can_Farming);
		}
	}
}

