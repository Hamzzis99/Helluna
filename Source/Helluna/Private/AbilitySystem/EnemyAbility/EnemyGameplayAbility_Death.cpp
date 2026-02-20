// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Death.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"

#include "Character/HellunaEnemyCharacter.h"
#include "GameMode/HellunaDefenseGameMode.h"

#include "DebugHelper.h"

// =============================================================
// 생성자
// =============================================================
UEnemyGameplayAbility_Death::UEnemyGameplayAbility_Death()
{
	// 몬스터당 1개 인스턴스 (사망은 1회만 발생)
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 서버에서만 실행 (몽타주는 Multicast_PlayDeath로 모든 클라에 전파)
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

// =============================================================
// ActivateAbility - GA 발동 진입점
// =============================================================
void UEnemyGameplayAbility_Death::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 부모 호출 (필수)
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// EnemyCharacter 가져오기
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		Debug::Print(TEXT("[GA_Death] FAIL: EnemyCharacter null"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ── 1. 이동 정지 / 콜리전 비활성화 ──────────────────────────
	if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
	{
		MoveComp->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// ── 2. 사망 몽타주 재생 ──────────────────────────────────────
	UAnimMontage* DeathMontage = Enemy->DeathMontage;
	
	Debug::Print(FString::Printf(TEXT("[GA_Death] DeathMontage: %s"),
		DeathMontage ? *DeathMontage->GetName() : TEXT("null")), FColor::Cyan);

	if (!DeathMontage)
	{
		// 몽타주가 없으면 바로 후처리
		Debug::Print(TEXT("[GA_Death] DeathMontage 없음 - 즉시 후처리"), FColor::Yellow);
		HandleDeathFinished();
		return;
	}

	Debug::Print(TEXT("[GA_Death] PlayMontageAndWait 태스크 생성 시작"), FColor::Cyan);

	// AbilityTask: PlayMontageAndWait
	// 몽타주가 끝나면 OnCompleted / OnCancelled / OnInterrupted 중 하나가 호출됨
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this,
		NAME_None,          // TaskInstanceName (고유 이름 필요 없으면 None)
		DeathMontage,
		1.f,                // PlayRate
		NAME_None,          // StartSection
		false               // bStopWhenAbilityEnds: false = 어빌리티가 끝나도 몽타주 유지
	);

	if (!MontageTask)
	{
		Debug::Print(TEXT("[GA_Death] FAIL: MontageTask 생성 실패"), FColor::Red);
		HandleDeathFinished();
		return;
	}

	Debug::Print(TEXT("[GA_Death] MontageTask 콜백 바인딩"), FColor::Cyan);

	// 완료/취소/중단 모두 동일하게 처리 (어떤 방식으로 끝나든 후처리)
	MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_Death::OnMontageCompleted);
	MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_Death::OnMontageCancelled);
	MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_Death::OnMontageCancelled);

	// ── 3. 몽타주 길이 확인 후 조기 삭제 타이머 설정 ─────────────────
	// 사망 몽타주가 완전히 끝날 때까지 기다리면 "일어난 후 사라짐" 문제 발생
	// 해결: 몽타주 길이의 70~80% 지점에서 미리 후처리 시작
	const float MontageDuration = DeathMontage->GetPlayLength();
	const float EarlyFinishRatio = 0.7f; // 70% 지점에서 삭제 (조절 가능)
	const float EarlyFinishTime = MontageDuration * EarlyFinishRatio;

	Debug::Print(FString::Printf(TEXT("[GA_Death] 몽타주 길이: %.2fs, 조기 삭제 시간: %.2fs"),
		MontageDuration, EarlyFinishTime), FColor::Cyan);

	// 타이머: 몽타주 70% 지점에서 강제 완료 처리
	FTimerHandle EarlyFinishTimer;
	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		const float StartTime = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(
			EarlyFinishTimer,
			[this, StartTime, World]()
			{
				const float CurrentTime = World->GetTimeSeconds();
				const float Elapsed = CurrentTime - StartTime;
				Debug::Print(FString::Printf(TEXT("[GA_Death] 조기 삭제 타이머 발동 (경과: %.2fs)"), Elapsed), FColor::Yellow);
				this->HandleDeathFinished();
			},
			EarlyFinishTime,
			false  // 1회만
		);
	}

	MontageTask->ReadyForActivation();

	Debug::Print(TEXT("[GA_Death] MontageTask ReadyForActivation 완료 - 몽타주 재생됨"), FColor::Cyan);

	// PlayMontageAndWait가 이미 서버+클라 동기화를 담당하므로
	// Multicast_PlayDeath 호출 불필요 (중복 재생 방지)
	// Enemy->Multicast_PlayDeath();  // ← 제거

	Debug::Print(TEXT("[GA_Death] 사망 몽타주 재생 시작"), FColor::Orange);
}

// =============================================================
// 몽타주 정상 완료
// =============================================================
void UEnemyGameplayAbility_Death::OnMontageCompleted()
{
	UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : 0.f;
	
	Debug::Print(FString::Printf(TEXT("[GA_Death] OnMontageCompleted 호출 (Time: %.2f)"), CurrentTime), FColor::Orange);
	HandleDeathFinished();
}

// =============================================================
// 몽타주 취소/중단 (피격 등으로 끊겼을 때)
// =============================================================
void UEnemyGameplayAbility_Death::OnMontageCancelled()
{
	Debug::Print(TEXT("[GA_Death] 몽타주 취소/중단 - 강제 후처리"), FColor::Yellow);
	HandleDeathFinished();
}

// =============================================================
// 사망 후처리 - 몽타주 종료 후 공통 로직
// =============================================================
void UEnemyGameplayAbility_Death::HandleDeathFinished()
{
	// 중복 실행 방지 (타이머 + 몽타주 완료 콜백 동시 발생 가능)
	if (bDeathHandled)
	{
		Debug::Print(TEXT("[GA_Death] 이미 후처리 완료됨 - SKIP"), FColor::Yellow);
		return;
	}
	bDeathHandled = true;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	// ── 1. StateTree DeathTask에 완료 알림 ───────────────────────
	// DeathTask가 bMontageFinished = true 로 설정 → Tick에서 감지
	Enemy->OnDeathMontageFinished.ExecuteIfBound();

	// ── 2. GameMode 통지 (킬 카운트 등) ─────────────────────────
	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(
		UGameplayStatics::GetGameMode(World)))
	{
		GM->NotifyMonsterDied(Enemy);
	}

	// ── 3. Entity 제거 (Mass) + Actor 삭제 ──────────────────────
	Enemy->DespawnMassEntityOnServer(TEXT("GA_Death"));

	// MassAgentComp가 없는 경우 (테스트 몬스터 등) Actor 직접 제거
	Enemy->SetLifeSpan(0.1f);

	Debug::Print(TEXT("[GA_Death] 사망 후처리 완료 - Actor 제거 예약"), FColor::Red);

	// GA 종료
	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// =============================================================
// EndAbility
// =============================================================
void UEnemyGameplayAbility_Death::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
