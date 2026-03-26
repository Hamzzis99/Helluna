// Source/Helluna/Private/Puzzle/PuzzleShieldComponent.cpp

#include "Puzzle/PuzzleShieldComponent.h"
#include "Puzzle/PuzzleCubeActor.h"
#include "Net/UnrealNetwork.h"

UPuzzleShieldComponent::UPuzzleShieldComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UPuzzleShieldComponent::BeginPlay()
{
	Super::BeginPlay();

	// 서버에서만 퍼즐 큐브 이벤트 바인딩
	if (GetOwner() && GetOwner()->HasAuthority() && IsValid(LinkedPuzzleCube))
	{
		LinkedPuzzleCube->OnPuzzleLockChanged.AddUniqueDynamic(
			this, &UPuzzleShieldComponent::OnPuzzleLockChanged);

		UE_LOG(LogTemp, Warning,
			TEXT("[PuzzleShield] BeginPlay: Linked to %s on %s"),
			*GetNameSafe(LinkedPuzzleCube), *GetNameSafe(GetOwner()));
	}
	else if (GetOwner() && GetOwner()->HasAuthority() && !IsValid(LinkedPuzzleCube))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PuzzleShield] BeginPlay: LinkedPuzzleCube is null on %s — 에디터에서 할당 필요"),
			*GetNameSafe(GetOwner()));
	}
}

void UPuzzleShieldComponent::OnPuzzleLockChanged(bool bLocked)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bShieldActive = bLocked;

	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleShield] OnPuzzleLockChanged: bShieldActive=%s on %s"),
		bShieldActive ? TEXT("true (무적)") : TEXT("false (딜 가능)"),
		*GetNameSafe(GetOwner()));

	// 서버에서 OnRep 수동 호출
	OnRep_ShieldActive();
}

void UPuzzleShieldComponent::OnRep_ShieldActive()
{
	OnShieldStateChanged.Broadcast(bShieldActive);
}

float UPuzzleShieldComponent::FilterDamage(float InDamage) const
{
	return bShieldActive ? 0.f : InDamage;
}

void UPuzzleShieldComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPuzzleShieldComponent, bShieldActive);
}
