// Capstone Project Helluna


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Farming.h"

#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

#include "DebugHelper.h"

UHeroGameplayAbility_Farming::UHeroGameplayAbility_Farming()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UHeroGameplayAbility_Farming::ActivateAbility(	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	Debug::Print(TEXT("장상작동"), FColor::Red);

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* Target = nullptr;
	FHitResult Hit;
	const bool bFound = FindFarmingTarget(ActorInfo, Target, Hit);

	if (!bFound || !Target)
	{
		Debug::Print(TEXT("주변에 채집 대상(광물)이 없음"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 채집하는 순간 대상 바라보기(로컬 체감)
	FaceToTarget_LocalOnly(ActorInfo, Target->GetActorLocation());

	// 데미지 적용은 서버에서만 확정
	AActor* Avatar = ActorInfo->AvatarActor.Get();
	const bool bAuth = Avatar && Avatar->HasAuthority();

	if (bAuth)
	{
		const FVector ShotDir = (Target->GetActorLocation() - Avatar->GetActorLocation()).GetSafeNormal();

		UGameplayStatics::ApplyPointDamage(
			Target,
			FarmingDamage,
			ShotDir,
			Hit,
			nullptr,
			Avatar,
			UDamageType::StaticClass()
		);

		Debug::Print(TEXT("파밍 성공!"), FColor::Green);
	}
	
}

void UHeroGameplayAbility_Farming::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}


bool UHeroGameplayAbility_Farming::FindFarmingTarget(const FGameplayAbilityActorInfo* ActorInfo, AActor*& OutTarget, FHitResult& OutHit) const
{
	OutTarget = nullptr;
	OutHit = FHitResult{};

	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Avatar) return false;

	UWorld* World = Avatar->GetWorld();
	if (!World) return false;

	// 카메라(시점) 기준으로 Start/End 잡기
	FVector Start = Avatar->GetActorLocation();
	FRotator ViewRot = Avatar->GetActorRotation();

	if (APawn* Pawn = Cast<APawn>(Avatar))
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->GetPlayerViewPoint(Start, ViewRot);
		}
	}

	const FVector End = Start + ViewRot.Vector() * TraceDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Avatar);

	TArray<FHitResult> Hits;
	const bool bHitAny = World->SweepMultiByChannel(
		Hits,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(TraceRadius),
		Params
	);

	if (bDrawDebug)
	{
		DrawDebugLine(World, Start, End, FColor::Yellow, false, 0.2f, 0, 1.f);
		DrawDebugSphere(World, Start, TraceRadius, 16, FColor::Yellow, false, 0.2f);
	}

	if (!bHitAny) return false;

	// 여러 개 중 "Ore 태그"만 필터링해서 가장 가까운 것 선택
	float BestDistSq = TNumericLimits<float>::Max();
	bool bFoundOre = false;

	for (const FHitResult& H : Hits)
	{
		AActor* A = H.GetActor();
		if (!A) continue;

		if (!A->ActorHasTag(FarmTargetTag))
			continue;

		const float DistSq = FVector::DistSquared(Start, H.ImpactPoint);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutTarget = A;
			OutHit = H;
			bFoundOre = true;
		}
	}

	return bFoundOre;
}

void UHeroGameplayAbility_Farming::FaceToTarget_LocalOnly( const FGameplayAbilityActorInfo* ActorInfo, const FVector& TargetLocation) const
{

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
		return;

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	if (!Pawn)
		return;

	// 진짜 로컬(내가 조종하는 Pawn)일 때만 카메라 회전
	if (!Pawn->IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC)
	{
		return;
	}

	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	const FRotator LookAt = UKismetMathLibrary::FindLookAtRotation(ViewLoc, TargetLocation);

	const_cast<UHeroGameplayAbility_Farming*>(this)->StartSmoothLookAt(PC, LookAt.Yaw);
}

static float LerpYawShortest(float FromYaw, float ToYaw, float Alpha)
{
	const float Delta = FMath::FindDeltaAngleDegrees(FromYaw, ToYaw);
	return FromYaw + Delta * Alpha;
}

void UHeroGameplayAbility_Farming::StartSmoothLookAt(APlayerController* PC, float TargetYaw)
{
	if (!PC) return;

	// 이전 타이머가 있으면 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LookInterpTimerHandle);
	}

	CachedPC = PC;
	LookElapsed = 0.f;
	LookStartYaw = PC->GetControlRotation().Yaw;
	LookTargetYaw = TargetYaw;

	// 60fps 느낌으로 짧게 틱
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			LookInterpTimerHandle,
			this,
			&UHeroGameplayAbility_Farming::TickSmoothLook,
			0.016f,
			true
		);
	}
}

void UHeroGameplayAbility_Farming::TickSmoothLook()
{
	APlayerController* PC = CachedPC.Get();
	if (!PC)
	{
		if (UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(LookInterpTimerHandle);
		return;
	}

	const float Dt = 0.016f;
	LookElapsed += Dt;

	const float Duration = FMath::Max(0.01f, LookInterpDuration);
	const float Alpha = FMath::Clamp(LookElapsed / Duration, 0.f, 1.f);

	FRotator Rot = PC->GetControlRotation();
	Rot.Yaw = LerpYawShortest(LookStartYaw, LookTargetYaw, Alpha);
	PC->SetControlRotation(Rot);

	if (Alpha >= 1.f)
	{
		if (UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(LookInterpTimerHandle);
	}
}
