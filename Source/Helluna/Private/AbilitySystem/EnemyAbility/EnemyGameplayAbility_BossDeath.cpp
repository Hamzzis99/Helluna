// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_BossDeath.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaEnemyCharacter_Boss.h"
#include "Character/EnemyComponent/BossDissolveComponent.h"
#include "GameMode/HellunaDefenseGameMode.h"

UEnemyGameplayAbility_BossDeath::UEnemyGameplayAbility_BossDeath()
{
	InstancingPolicy   = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UEnemyGameplayAbility_BossDeath::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UE_LOG(LogTemp, Warning,
		TEXT("[BossDeathV1] ActivateAbility — Avatar=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()));

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAnimMontage* DeathMontage = Enemy->DeathMontage;
	if (!DeathMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossDeathV1] No DeathMontage — finishing immediately"));
		HandleBossDeathFinished();
		return;
	}

	// 자연 종료까지 기다림 — EarlyFinishTimer 없음.
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, DeathMontage, 1.f, NAME_None, false);

	if (!MontageTask)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossDeathV1] MontageTask creation failed"));
		HandleBossDeathFinished();
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_BossDeath::OnMontageCompleted);
	MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_BossDeath::OnMontageCancelled);
	MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_BossDeath::OnMontageCancelled);
	MontageTask->ReadyForActivation();

	UE_LOG(LogTemp, Warning,
		TEXT("[BossDeathV1] Montage task started — Length=%.2f"),
		DeathMontage->GetPlayLength());

	// [BossDeathMeshLiftV1] 사망 몽타주 누운 자세에서 spine/hand 본이 mesh root(=floor) 아래로
	//   내려가는 authored data 보정 — SkelMesh.RelativeLocation.Z 를 lerp 으로 lift. Multicast 라
	//   모든 머신(서버/리슨/리모트)에서 동일하게 적용.
	if (AHellunaEnemyCharacter_Boss* BossLift = Cast<AHellunaEnemyCharacter_Boss>(Enemy))
	{
		BossLift->Multicast_StartDeathMeshLift();
	}

	// [BossEarlyHideV1] 사망 몽타주의 85% 시점에 mesh hide — 마지막 frame (idle locomotion 으로
	//   transition 직전) 도달 전에 사라짐. position/length fraction 사용 → actor TimeDilation
	//   슬로우 영향 안 받음 (anim time 기준).
	//   SetActorHiddenInGame 은 server 호출 시 client 자동 동기화 (bHidden replicated).
	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		TSharedPtr<FTimerHandle> SharedTimer = MakeShared<FTimerHandle>();
		TWeakObjectPtr<AHellunaEnemyCharacter> WeakEnemy(Enemy);
		TWeakObjectPtr<UAnimMontage> WeakMontage(DeathMontage);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		constexpr float HideAtFraction = 0.85f;

		World->GetTimerManager().SetTimer(*SharedTimer,
			FTimerDelegate::CreateLambda([WeakEnemy, WeakMontage, WeakWorld, SharedTimer]()
			{
				AHellunaEnemyCharacter* E = WeakEnemy.Get();
				UAnimMontage* M = WeakMontage.Get();
				UWorld* W = WeakWorld.Get();
				if (!E || !M || !W)
				{
					if (W && SharedTimer.IsValid()) W->GetTimerManager().ClearTimer(*SharedTimer);
					return;
				}
				if (E->IsHidden()) return;  // 이미 hide → 추가 작업 불필요

				USkeletalMeshComponent* SK = E->GetMesh();
				UAnimInstance* AI = SK ? SK->GetAnimInstance() : nullptr;
				if (!AI)
				{
					W->GetTimerManager().ClearTimer(*SharedTimer);
					return;
				}
				FAnimMontageInstance* Inst = AI->GetActiveInstanceForMontage(M);
				if (!Inst)
				{
					// montage 종료/cancel 됐으면 timer 정리 (HandleFinished 가 mesh hide 처리)
					W->GetTimerManager().ClearTimer(*SharedTimer);
					return;
				}

				const float Pos = Inst->GetPosition();
				const float Len = M->GetPlayLength();
				if (Len > KINDA_SMALL_NUMBER && (Pos / Len) >= 0.85f)
				{
					// [BossDissolveComponentV1] 85% 시점 = 사망 몽타주 끝부분 (넘어진 후).
					//   보스의 BossDissolveComponent 가 dissolve 효과 모두 캡슐화 처리 (mesh swap +
					//   Niagara + slowmo + timer + destroy). GA 는 한 줄 trigger 만.
					AHellunaEnemyCharacter_Boss* BossB = Cast<AHellunaEnemyCharacter_Boss>(E);
					if (BossB && BossB->DissolveComponent)
					{
						BossB->DissolveComponent->TriggerDissolve();
						UE_LOG(LogTemp, Warning,
							TEXT("[BossDissolveComponentV1] TriggerDissolve at %.0f%% montage progress (Pos=%.2f Len=%.2f)"),
							(Pos / Len) * 100.f, Pos, Len);
					}
					else
					{
						UE_LOG(LogTemp, Warning,
							TEXT("[BossDissolveComponentV1] DissolveComponent missing on %s — fallback hide"),
							*GetNameSafe(E));
						E->SetActorHiddenInGame(true);
					}
					W->GetTimerManager().ClearTimer(*SharedTimer);
				}
			}),
			0.1f, true);
	}
}

void UEnemyGameplayAbility_BossDeath::OnMontageCompleted()
{
	UE_LOG(LogTemp, Warning, TEXT("[BossDeathV1] Montage Completed (natural)"));
	HandleBossDeathFinished();
}

void UEnemyGameplayAbility_BossDeath::OnMontageCancelled()
{
	UE_LOG(LogTemp, Warning, TEXT("[BossDeathV1] Montage Cancelled/Interrupted"));
	HandleBossDeathFinished();
}

void UEnemyGameplayAbility_BossDeath::HandleBossDeathFinished()
{
	if (bDeathHandled) return;
	bDeathHandled = true;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	UE_LOG(LogTemp, Warning,
		TEXT("[BossDeathV1] HandleFinished — Enemy=%s WorldTime=%.3f (boss visual/destroy is BossDissolveComponent's job)"),
		*GetNameSafe(Enemy), World->GetTimeSeconds());

	// [BossDissolveComponentV1] 보스 visual 종료 / Mass entity 정리 / actor destroy 는
	//   BossDissolveComponent 가 책임 (85% 시점에 이미 TriggerDissolve 호출됨).
	//   여기서는 GameMode 통보 + GA EndAbility 만 처리.

	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(
		UGameplayStatics::GetGameMode(World)))
	{
		GM->NotifyMonsterDied(Enemy);
	}

	const FGameplayAbilitySpecHandle H = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* AI = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo AV = GetCurrentActivationInfo();
	EndAbility(H, AI, AV, true, false);
}
