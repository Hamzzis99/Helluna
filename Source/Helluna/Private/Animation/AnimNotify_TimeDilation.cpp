#include "Animation/AnimNotify_TimeDilation.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

FString UAnimNotify_TimeDilation::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("TimeDilation: %.2fx %.2fs"), Dilation, Duration);
}

void UAnimNotify_TimeDilation::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	UWorld* World = MeshComp->GetWorld();
	if (!World) return;

	// 머신 로컬 슬로우 적용 — 각 클라가 자기 월드 시뮬 속도만 변경.
	UGameplayStatics::SetGlobalTimeDilation(World, FMath::Clamp(Dilation, 0.05f, 1.0f));

	// 슬로우 동안 실시간으로 Duration 만큼 흐른 뒤 복구.
	// SetTimer 은 Time Dilation 영향을 받으므로, Real-time 기준으로 보정해 입력 Duration
	// 그대로 "언슬로우 기준" 으로 흐르게 하려면 Duration / Dilation 만큼 시뮬 시간을 줘야 함.
	// 다만 일반적인 슬로우 모션 연출에서는 "슬로우 자체가 길게 느껴지는" 게 의도라
	// 입력값을 그대로 시뮬 타이머로 사용 → 실제 체감 시간은 Duration / Dilation 초.
	const float Restore = 1.0f;
	const float DilatedDuration = FMath::Max(0.05f, Duration);

	FTimerHandle TempHandle;
	TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(TempHandle, [WeakWorld, Restore]()
	{
		if (UWorld* W = WeakWorld.Get())
		{
			UGameplayStatics::SetGlobalTimeDilation(W, Restore);
		}
	}, DilatedDuration, false);

	UE_LOG(LogTemp, Warning,
		TEXT("[TimeDilationNotify] Dilation=%.2f Duration(sim)=%.2fs"),
		Dilation, DilatedDuration);
}
