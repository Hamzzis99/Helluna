// Source/Helluna/Private/Conponent/Outline/HellunaTeamOutlineComponent.cpp
// L4D식 아군 외곽선 — CustomDepth/Stencil 토글 구현부

#include "Conponent/Outline/HellunaTeamOutlineComponent.h"

#include "Character/HellunaHeroCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Helluna.h"

UHellunaTeamOutlineComponent::UHellunaTeamOutlineComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false); // 클라이언트 시각 효과
}

void UHellunaTeamOutlineComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(GetOwner()))
	{
		CachedMeshComponent = Hero->GetMesh();
	}

	if (!CachedMeshComponent.IsValid())
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[TeamOutline] Owner SkeletalMesh not found — outline disabled. Owner=%s"),
			*GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	// 데디서버는 렌더 없음 — 즉시 비활성
	const ENetMode NetMode = GetNetMode();
	if (NetMode == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}
}

void UHellunaTeamOutlineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CachedMeshComponent.IsValid())
	{
		CachedMeshComponent->SetRenderCustomDepth(false);
	}
	Super::EndPlay(EndPlayReason);
}

void UHellunaTeamOutlineComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bDisabled)
	{
		if (CurrentState != EHellunaOutlineState::None)
		{
			ApplyOutlineState(EHellunaOutlineState::None);
		}
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();
	if (Now - LastEvaluationTime < EvaluationInterval) return;
	LastEvaluationTime = Now;

	EvaluateAndApply();
}

void UHellunaTeamOutlineComponent::SetDownedHint(bool bInDowned)
{
	bDownedHint = bInDowned;
}

void UHellunaTeamOutlineComponent::EvaluateAndApply()
{
	if (!CachedMeshComponent.IsValid())
	{
		ApplyOutlineState(EHellunaOutlineState::None);
		return;
	}

	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		ApplyOutlineState(EHellunaOutlineState::None);
		return;
	}

	APawn* LocalPawn = GetLocalPlayerPawn();
	if (!LocalPawn)
	{
		ApplyOutlineState(EHellunaOutlineState::None);
		return;
	}

	// 로컬 본인은 외곽선 그리지 않음 (자기 메시는 카메라가 가리는 일이 거의 없음)
	if (LocalPawn == Owner)
	{
		ApplyOutlineState(EHellunaOutlineState::None);
		return;
	}

	// Hero 끼리만 아군 — 추후 PlayerState/Party 비교로 확장 예정
	const AHellunaHeroCharacter* OwnerHero = Cast<AHellunaHeroCharacter>(Owner);
	const AHellunaHeroCharacter* LocalHero = Cast<AHellunaHeroCharacter>(LocalPawn);
	if (!OwnerHero || !LocalHero)
	{
		ApplyOutlineState(EHellunaOutlineState::None);
		return;
	}

	const float DistanceSq = FVector::DistSquared(LocalPawn->GetActorLocation(),
		Owner->GetActorLocation());
	const float MaxSq = MaxOutlineDistance * MaxOutlineDistance;
	if (DistanceSq > MaxSq)
	{
		ApplyOutlineState(EHellunaOutlineState::None);
		return;
	}

	const EHellunaOutlineState NewState = bDownedHint
		? EHellunaOutlineState::AllyDowned
		: EHellunaOutlineState::Ally;

	ApplyOutlineState(NewState);
}

void UHellunaTeamOutlineComponent::ApplyOutlineState(EHellunaOutlineState NewState)
{
	if (NewState == CurrentState) return;
	if (!CachedMeshComponent.IsValid()) return;

	USkeletalMeshComponent* Mesh = CachedMeshComponent.Get();

	switch (NewState)
	{
	case EHellunaOutlineState::None:
		Mesh->SetRenderCustomDepth(false);
		break;
	case EHellunaOutlineState::Ally:
		Mesh->SetRenderCustomDepth(true);
		Mesh->SetCustomDepthStencilValue(AllyStencilValue);
		break;
	case EHellunaOutlineState::AllyDowned:
		Mesh->SetRenderCustomDepth(true);
		Mesh->SetCustomDepthStencilValue(AllyDownedStencilValue);
		break;
	default:
		break;
	}

	CurrentState = NewState;
}

APawn* UHellunaTeamOutlineComponent::GetLocalPlayerPawn() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	APlayerController* LocalPC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	return LocalPC ? LocalPC->GetPawn() : nullptr;
}
