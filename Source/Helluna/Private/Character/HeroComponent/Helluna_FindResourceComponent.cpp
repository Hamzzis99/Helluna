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
	
#include "Interaction/Inv_HighlightableStaticMesh.h"
#include "Interaction/Inv_Highlightable.h"

// ============================================================
// Helluna_FindResourceComponent
// - 플레이어가 "근처를 바라보면" 광물(또는 상호작용 대상)을 자동 탐지
// - 탐지된 대상의 Highlight(Overlay Material)를 켜/끄는 역할
// - 멀티플레이에서 "로컬 플레이어"만 실행 (연출/UI 성격)
// ============================================================

UHelluna_FindResourceComponent::UHelluna_FindResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UHelluna_FindResourceComponent::BeginPlay()
{
	Super::BeginPlay();

	// 로컬 플레이어만 실행(하이라이트는 연출)
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		SetComponentTickEnabled(false);
	}
}

void UHelluna_FindResourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateFocus(); //틱 마다 하이라이트 갱신 -> 후에 최적화를 위해 수정될 가능성 있음
}

bool UHelluna_FindResourceComponent::IsValidTargetActor(AActor* Actor) const
{
	if (!Actor) return false;

	// 태그가 있으면 검사
	if (!RequiredActorTag.IsNone() && !Actor->ActorHasTag(RequiredActorTag))
		return false;

	// “하이라이트 가능한 메시 컴포넌트”가 있는 액터만
	return FindHighlightMesh(Actor) != nullptr;
}

UInv_HighlightableStaticMesh* UHelluna_FindResourceComponent::FindHighlightMesh(AActor* Actor) const
{
	if (!Actor) return nullptr;

	// 액터에 여러 개 있어도 “첫 번째”만 써도 당장은 충분
	return Actor->FindComponentByClass<UInv_HighlightableStaticMesh>();
}

// ------------------------------------------------------------
// 카메라 정면(ViewDir) 기준으로 TargetLoc이 특정 각도(MaxFocusAngleDeg) 안에 있는지 판정
// - DotProduct 기반 콘(원뿔) 판정
// - Cos(각도)가 CosLimit 이상이면 "시야(근처) 안"이라고 본다.
// ------------------------------------------------------------

bool UHelluna_FindResourceComponent::IsInViewCone(const FVector& ViewLoc, const FVector& ViewDir, const FVector& TargetLoc) const
{
	const FVector ToTarget = (TargetLoc - ViewLoc).GetSafeNormal();
	const float Cos = FVector::DotProduct(ViewDir.GetSafeNormal(), ToTarget);
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(MaxFocusAngleDeg));
	return Cos >= CosLimit;
}



void UHelluna_FindResourceComponent::UpdateFocus()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) return;

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;

	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	// ------------------------------------------------------------
// ✅ Sphere Sweep(반경 TraceRadius)로 "근처를 대충 바라봐도" 후보를 잡기
// Start -> End 구간을 구체로 쓸어서(Hit) 후보들을 수집한다.
// 후보수 줄이기, 정확히 바라보지 않아도 됨, 콜리전을 통해 벽 뒤에 있는 후보 제거 가능
// ------------------------------------------------------------

	const FVector ViewDir = ViewRot.Vector();

	// ✅ (중요) Start를 살짝 앞으로 밀어서 "카메라가 오브젝트에 겹친 상태(Initial Overlap)"에서
	// SphereSweep가 계속 Hit를 내는 현상을 줄인다.
	const float StartForwardOffset = 30.f; // 10~30 추천
	const FVector Start = ViewLoc + ViewDir * StartForwardOffset;
	const FVector End = Start + ViewDir * TraceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MineFocus), false);
	Params.AddIgnoredActor(Pawn);

	TArray<FHitResult> Hits;
	const bool bHit = GetWorld()->SweepMultiByChannel(
		Hits,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(TraceRadius),
		Params
	);

	// 후보 중 “카메라에서 가장 가까운 것” 선택
	AActor* BestActor = nullptr;
	UInv_HighlightableStaticMesh* BestMesh = nullptr;
	float BestDot = -1.f;
	float BestDistSq = TNumericLimits<float>::Max();

	for (const FHitResult& H : Hits)
	{
		AActor* A = H.GetActor();
		if (!A) continue;
		if (!IsValidTargetActor(A)) continue;

		const float DistSq = FVector::DistSquared(ViewLoc, H.ImpactPoint);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestActor = A;
			BestMesh = FindHighlightMesh(A);
		}
	}

	if (bHit)
	{
		for (const FHitResult& H : Hits)
		{
			// ✅ 시작부터 겹쳐서 생기는 히트는 제외(특정 위치에서 안 꺼지는 현상 방지)
			if (H.bStartPenetrating) continue;

			AActor* A = H.GetActor();
			if (!A) continue;
			if (!IsValidTargetActor(A)) continue;

			// ✅ 후보 선정 단계에서부터 "시야각(콘)" 필터 적용
			// ActorLocation 대신 ImpactPoint를 쓰면 "피벗이 이상한 액터"도 정확히 동작
			if (!IsInViewCone(ViewLoc, ViewDir, H.ImpactPoint))
				continue;

			// 정면 점수 (클수록 카메라 정면)
			const FVector ToHit = (H.ImpactPoint - ViewLoc).GetSafeNormal();
			const float Dot = FVector::DotProduct(ViewDir, ToHit);

			// 거리 (타이브레이크용)
			const float DistSq = FVector::DistSquared(ViewLoc, H.ImpactPoint);

			// Best 갱신 규칙:
			// 1) Dot이 더 큰 후보 우선
			// 2) Dot이 거의 같으면 더 가까운 후보 우선
			const bool bBetterDot = Dot > BestDot + 0.0001f;
			const bool bSimilarDotButCloser = FMath::Abs(Dot - BestDot) <= 0.0001f && DistSq < BestDistSq;

			if (bBetterDot || bSimilarDotButCloser)
			{
				BestDot = Dot;
				BestDistSq = DistSq;
				BestActor = A;
				BestMesh = FindHighlightMesh(A);
			}
		}
	}

	// ✅ Best가 없으면 포커스 해제
	if (!BestActor || !BestMesh)
	{
		// 이미 포커스가 있었으면 해제 처리
		if (FocusedActor.IsValid() || FocusedMesh.IsValid())
		{
			ClearFocus();
		}
		return;
	}

	// ✅ 같은 액터면 변화 없음
	if (BestActor == FocusedActor.Get())
	{
		return;
	}

	ApplyFocus(BestActor, BestMesh);
}

void UHelluna_FindResourceComponent::ApplyFocus(AActor* NewActor, UInv_HighlightableStaticMesh* NewMesh)
{
	// 포커스 대상이 벗어나면 하이라이트 끄기 
	if (FocusedMesh.IsValid())
	{
		UObject* Obj = FocusedMesh.Get();
		if (Obj && Obj->GetClass()->ImplementsInterface(UInv_Highlightable::StaticClass()))
		{
			IInv_Highlightable::Execute_UnHighlight(Obj);
		}
	}

	// ✅ 포커스 변경 디버그 (여기서 Old -> New)
	const FString OldName = FocusedActor.IsValid() ? FocusedActor->GetName() : TEXT("None");
	const FString NewName = NewActor ? NewActor->GetName() : TEXT("None");
	Debug::Print(FString::Printf(TEXT("목표물 변경 : %s -> %s"), *OldName, *NewName), FColor::Green);

	FocusedActor = NewActor;
	FocusedMesh = NewMesh;

	// 새 ON
	if (FocusedMesh.IsValid())
	{
		UObject* Obj = FocusedMesh.Get();
		if (Obj && Obj->GetClass()->ImplementsInterface(UInv_Highlightable::StaticClass()))
		{
			IInv_Highlightable::Execute_Highlight(Obj);
		}
	}

	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetOwner()))
	{
		if (UHellunaAbilitySystemComponent* ASC = Hero->GetHellunaAbilitySystemComponent())
		{
			ASC->AddStateTag(HellunaGameplayTags::Player_status_Can_Farming);
			ServerSetCanFarming(true);
		}
	}
}

void UHelluna_FindResourceComponent::ClearFocus()
{
	// ✅ 포커스 해제 디버그
	if (FocusedActor.IsValid())
	{
		Debug::Print(FString::Printf(TEXT("목표물 벗어남 : %s"), *FocusedActor->GetName()), FColor::Yellow);
	}

	if (FocusedMesh.IsValid())
	{
		UObject* Obj = FocusedMesh.Get();
		if (Obj && Obj->GetClass()->ImplementsInterface(UInv_Highlightable::StaticClass()))
		{
			IInv_Highlightable::Execute_UnHighlight(Obj);
		}
	}

	// ✅ 플레이어 상태태그 제거
	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetOwner()))
	{
		if (UHellunaAbilitySystemComponent* ASC = Hero->GetHellunaAbilitySystemComponent())
		{
			ASC->RemoveStateTag(HellunaGameplayTags::Player_status_Can_Farming);
			ServerSetCanFarming(false);
		}
	}

	FocusedActor = nullptr;
	FocusedMesh = nullptr;
}

void UHelluna_FindResourceComponent::ServerSetCanFarming_Implementation(bool bEnable)
{
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