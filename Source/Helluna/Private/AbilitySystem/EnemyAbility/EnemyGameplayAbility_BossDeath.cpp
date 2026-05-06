// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_BossDeath.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"

#include "Character/HellunaEnemyCharacter.h"
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
					E->SetActorHiddenInGame(true);
					W->GetTimerManager().ClearTimer(*SharedTimer);
					UE_LOG(LogTemp, Warning,
						TEXT("[BossEarlyHideV1] Boss hidden at %.0f%% montage progress (Pos=%.2f Len=%.2f)"),
						(Pos / Len) * 100.f, Pos, Len);
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
		TEXT("[BossDeathV1] HandleFinished — Enemy=%s WorldTime=%.3f"),
		*GetNameSafe(Enemy), World->GetTimeSeconds());

	// [BossFadeFallbackV1] 사망 몽타주 끝 시점 = 보스 visual 종료 시점.
	//   SetActorHiddenInGame 은 AActor::bHidden 이 replicated 이므로 server 에서 호출하면
	//   client 에도 자동 동기화 → 멀티플레이 안전. SkelMesh->SetVisibility 는 replicate 안 됨.
	//   timing: server-side PlayMontageAndWait OnCompleted 시점. UE montage replicate 가
	//   server/client 거의 동기화하므로 client-side 도 같은 시점에 사라짐 (몇 frame 차이).
	Enemy->SetActorHiddenInGame(true);
	UE_LOG(LogTemp, Warning, TEXT("[BossFadeFallbackV1] SetActorHiddenInGame(true) on %s — replicates to clients"),
		*GetNameSafe(Enemy));

	// GameMode 통보 — NotifyMonsterDied → NotifyBossDied 트리거. EndGame 4초 timer 는 이전 세션에서 이미 제거됨.
	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(
		UGameplayStatics::GetGameMode(World)))
	{
		GM->NotifyMonsterDied(Enemy);
	}

	// EndAbility 먼저 — Despawn 후엔 ActorInfo 무효화 가능
	const FGameplayAbilitySpecHandle H = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* AI = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo AV = GetCurrentActivationInfo();
	EndAbility(H, AI, AV, true, false);

	// Despawn — Mass entity 정리 + Destroy timer. EndGame 은 GameMode 5초 timer 가 처리.
	Enemy->DespawnMassEntityOnServer(TEXT("GA_BossDeath"));
}
