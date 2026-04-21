#include "Animation/AnimNotify_EnemyCameraShake.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Kismet/GameplayStatics.h"

FString UAnimNotify_EnemyCameraShake::GetNotifyName_Implementation() const
{
	if (ShakeClass)
	{
		return FString::Printf(TEXT("CamShake: %s"), *ShakeClass->GetName());
	}
	return TEXT("CamShake: (None)");
}

void UAnimNotify_EnemyCameraShake::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !ShakeClass) return;

	UWorld* World = MeshComp->GetWorld();
	if (!World) return;

	// 데디케이티드 서버엔 렌더 카메라가 없으므로 스킵.
	if (World->GetNetMode() == NM_DedicatedServer) return;

	// Origin 결정: 소켓 지정 시 소켓 위치, 아니면 MeshComp 루트 위치.
	FVector Origin = MeshComp->GetComponentLocation();
	if (!SocketName.IsNone() && MeshComp->DoesSocketExist(SocketName))
	{
		Origin = MeshComp->GetSocketLocation(SocketName);
	}
	Origin += LocationOffset;

	UGameplayStatics::PlayWorldCameraShake(
		World,
		ShakeClass,
		Origin,
		InnerRadius,
		OuterRadius,
		Falloff);

	UE_LOG(LogTemp, Warning,
		TEXT("[CamShakeNotifyV1] Enemy world camera shake — Shake=%s Origin=(%.0f,%.0f,%.0f) Inner=%.0f Outer=%.0f Falloff=%.2f"),
		*ShakeClass->GetName(),
		Origin.X, Origin.Y, Origin.Z,
		InnerRadius, OuterRadius, Falloff);
}
