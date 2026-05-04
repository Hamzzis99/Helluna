// Capstone Project Helluna

#include "Animation/AnimNotifyState_ActorTimeDilation.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UAnimNotifyState_ActorTimeDilation::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (!MeshComp) return;
	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;
	// [MultiplyV1] 중첩 지원 — 현재 dilation 에 multiply (Notify 두 개 overlap 시 곱해짐).
	const float Mult = FMath::Clamp(TimeDilation, 0.05f, 1.f);
	Owner->CustomTimeDilation *= Mult;
}

void UAnimNotifyState_ActorTimeDilation::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (!MeshComp) return;
	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;
	// 역수 — Begin 의 multiply 취소.
	const float Mult = FMath::Clamp(TimeDilation, 0.05f, 1.f);
	Owner->CustomTimeDilation /= Mult;
	// 부동소수점 누적 보정 — 1.0 근처면 정확히 1.0
	if (FMath::IsNearlyEqual(Owner->CustomTimeDilation, 1.f, 0.001f))
	{
		Owner->CustomTimeDilation = 1.f;
	}
}

FString UAnimNotifyState_ActorTimeDilation::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Slow-Mo %.2fx"), TimeDilation);
}
