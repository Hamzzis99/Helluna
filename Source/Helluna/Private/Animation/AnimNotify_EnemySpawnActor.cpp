/**
 * AnimNotify_EnemySpawnActor.cpp
 *
 * @author 김민우
 */

#include "Animation/AnimNotify_EnemySpawnActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UAnimNotify_EnemySpawnActor::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !SpawnedActorClass) return;

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;

	UWorld* World = Owner->GetWorld();
	if (!World) return;

	// [SpawnActorNotifyV1] 멀티플레이어 중복 스폰 방지.
	if (bServerOnly && !Owner->HasAuthority())
	{
		return;
	}

	// 기준 위치/회전 계산.
	FTransform BaseTransform = Owner->GetActorTransform();
	if (SocketName != NAME_None && MeshComp->DoesSocketExist(SocketName))
	{
		BaseTransform = MeshComp->GetSocketTransform(SocketName, RTS_World);
	}

	// 오프셋 적용 (본 로컬 vs 월드).
	FVector FinalLocation;
	if (bOffsetInBoneSpace)
	{
		FinalLocation = BaseTransform.TransformPosition(LocationOffset);
	}
	else
	{
		FinalLocation = BaseTransform.GetLocation() + LocationOffset;
	}

	const FRotator FinalRotation = BaseTransform.Rotator() + RotationOffset;

	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.Instigator = Cast<APawn>(Owner);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* SpawnedActor = World->SpawnActor<AActor>(SpawnedActorClass, FinalLocation, FinalRotation, Params);
	if (!SpawnedActor) return;

	if (UniformScale > 0.f && !FMath::IsNearlyEqual(UniformScale, 1.f))
	{
		SpawnedActor->SetActorScale3D(FVector(UniformScale));
	}

	if (bAttachToOwner)
	{
		FAttachmentTransformRules Rules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, true);
		if (SocketName != NAME_None)
		{
			SpawnedActor->AttachToComponent(MeshComp, Rules, SocketName);
		}
		else
		{
			SpawnedActor->AttachToActor(Owner, Rules);
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[SpawnActorNotifyV1] Owner=%s Spawned=%s Loc=(%.0f,%.0f,%.0f) Socket=%s Scale=%.2f Attach=%d"),
		*Owner->GetName(),
		*SpawnedActor->GetName(),
		FinalLocation.X, FinalLocation.Y, FinalLocation.Z,
		*SocketName.ToString(), UniformScale, bAttachToOwner ? 1 : 0);
}

FString UAnimNotify_EnemySpawnActor::GetNotifyName_Implementation() const
{
	if (SpawnedActorClass)
	{
		return FString::Printf(TEXT("SpawnActor: %s"), *SpawnedActorClass->GetName());
	}
	return TEXT("SpawnActor (no class)");
}
