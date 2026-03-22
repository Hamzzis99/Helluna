// Source/Helluna/Private/Combat/MeleeTraceComponent.cpp

#include "Combat/MeleeTraceComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"

DEFINE_LOG_CATEGORY(LogMeleeTrace);

// ═══════════════════════════════════════════════════════════
// 생성자
// ═══════════════════════════════════════════════════════════

UMeleeTraceComponent::UMeleeTraceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 기본은 Tick OFF — StartTrace 호출 시 활성화
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

// ═══════════════════════════════════════════════════════════
// StartTrace / StopTrace
// ═══════════════════════════════════════════════════════════

void UMeleeTraceComponent::StartTrace(FName InSocketName)
{
	if (InSocketName != NAME_None)
	{
		TraceSocketName = InSocketName;
	}

	bTracing = true;
	PreviousSocketLocation = FVector::ZeroVector;
	AlreadyHitActors.Empty();
	SetComponentTickEnabled(true);

	UE_LOG(LogMeleeTrace, Log, TEXT("[MeleeTrace] StartTrace — Socket=%s, Radius=%.1f"),
		*TraceSocketName.ToString(), TraceSphereRadius);
}

void UMeleeTraceComponent::StopTrace()
{
	bTracing = false;
	PreviousSocketLocation = FVector::ZeroVector;
	AlreadyHitActors.Empty();
	SetComponentTickEnabled(false);

	UE_LOG(LogMeleeTrace, Log, TEXT("[MeleeTrace] StopTrace"));
}

// ═══════════════════════════════════════════════════════════
// TickComponent — 소켓 간 SphereTrace
// ═══════════════════════════════════════════════════════════

void UMeleeTraceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bTracing) return;

	// 소유자의 SkeletalMeshComponent에서 소켓 위치 얻기
	const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar) return;

	USkeletalMeshComponent* Mesh = OwnerChar->GetMesh();
	if (!Mesh) return;

	const FVector CurrentLoc = Mesh->GetSocketLocation(TraceSocketName);

	// 첫 틱: 이전 위치 초기화만
	if (PreviousSocketLocation.IsZero())
	{
		PreviousSocketLocation = CurrentLoc;
		return;
	}

	// 이전→현재 소켓 위치 사이 SphereTrace
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());

	// 이미 히트한 액터 무시
	for (AActor* Already : AlreadyHitActors)
	{
		if (Already)
		{
			Params.AddIgnoredActor(Already);
		}
	}

	// Pawn 타입 오브젝트만 감지 (Floor/벽 히트 방지)
	const bool bHit = GetWorld()->SweepSingleByObjectType(
		Hit,
		PreviousSocketLocation,
		CurrentLoc,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(TraceSphereRadius),
		Params);

	if (bHit && Hit.GetActor())
	{
		AlreadyHitActors.Add(Hit.GetActor());
		OnMeleeHit.Broadcast(Hit.GetActor(), Hit);

		UE_LOG(LogMeleeTrace, Log, TEXT("[MeleeTrace] Hit! Actor=%s, ImpactPoint=%s"),
			*Hit.GetActor()->GetName(), *Hit.ImpactPoint.ToString());

		// 디버그: 히트 위치에 빨간 구체
		if (bDrawDebugTrace)
		{
			DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 15.f, 8, FColor::Red, false, 1.0f);
		}
	}

	// 디버그: Sweep 경로 시각화
	if (bDrawDebugTrace)
	{
		DrawDebugLine(GetWorld(), PreviousSocketLocation, CurrentLoc,
			bHit ? FColor::Red : FColor::Green, false, 0.1f);
	}

	PreviousSocketLocation = CurrentLoc;
}
