// Source/Helluna/Private/Conponent/Outline/HellunaTeamOutlineComponent.cpp
// L4D식 아군 외곽선 — CustomDepth/Stencil 토글 구현부

#include "Conponent/Outline/HellunaTeamOutlineComponent.h"

#include "Character/HellunaHeroCharacter.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Helluna.h"
#include "Materials/MaterialInterface.h"

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

	// 미할당 시 기본 경로에서 머티리얼 로드 시도
	if (!OutlineMaterial)
	{
		OutlineMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Gihyeon/Outline/M_TeamOutline_PP.M_TeamOutline_PP"));
	}

	// 로컬 카메라에 PP 등록 (LocallyControlled 일 때만)
	TryRegisterPostProcessOnLocalCamera();
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

	// PossessedBy 가 BeginPlay 보다 늦을 수 있음 — 등록 안 됐으면 매 평가마다 재시도
	if (!bPostProcessRegistered)
	{
		TryRegisterPostProcessOnLocalCamera();
	}

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

void UHellunaTeamOutlineComponent::TryRegisterPostProcessOnLocalCamera()
{
	if (bPostProcessRegistered) return;
	if (!OutlineMaterial)
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[TeamOutline] OutlineMaterial null — PP not registered. Owner=%s"),
			*GetNameSafe(GetOwner()));
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		// 원격 클라/서버 권위 인스턴스에서는 PP 등록 안 함 — 메시 토글만 수행
		return;
	}

	// 추가 가드: 진짜 로컬 플레이어가 controlling 하는 Pawn 만 — 캐릭터 셀렉트/프리뷰 등
	// IsLocallyControlled 가 true 인 다중 Hero 가 있을 수 있으므로 LocalPC->Pawn 일치 검사
	APawn* RealLocalPawn = GetLocalPlayerPawn();
	if (RealLocalPawn != OwnerPawn)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC)
	{
		// PossessedBy 미완료 — Tick 에서 재시도
		return;
	}

	// 동적 PostProcessComponent 부착 (Owner = OwnerPawn, Unbound)
	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(OwnerPawn,
		UPostProcessComponent::StaticClass(), TEXT("TeamOutlinePP_Dynamic"));
	if (!PP)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[TeamOutline] NewObject<UPostProcessComponent> failed."));
		return;
	}

	PP->SetupAttachment(OwnerPawn->GetRootComponent());
	PP->bUnbound = true;
	PP->BlendWeight = 1.0f;

	FWeightedBlendable WB(OutlineWeight, OutlineMaterial);
	PP->Settings.WeightedBlendables.Array.Add(WB);

	PP->RegisterComponent();

	SpawnedOutlinePP = PP;
	bPostProcessRegistered = true;

	UE_LOG(LogHelluna, Log,
		TEXT("[TeamOutline] Spawned PostProcessComponent (Unbound) on local Hero. Material=%s, Weight=%.2f"),
		*GetNameSafe(OutlineMaterial), OutlineWeight);
}
