// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/HellunaEnemyCharacter_Boss.h"

#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Controller/HellunaHeroController.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Kismet/GameplayStatics.h"

#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AHellunaEnemyCharacter_Boss::AHellunaEnemyCharacter_Boss()
{
	// 보스 등급 기본값 (BP CDO 에서 SemiBoss/Boss 로 추가 조정 가능)
	EnemyGrade = EEnemyGrade::Boss;

	// [BossPhase2_DefaultV1] 보스 서브클래스의 기본값을 true 로 강제.
	//   C++ 부모 필드 초기값 false + BP CDO 에 한 번씩 직접 켜야 하는 구조로는
	//   리페어런팅/리팩터링 후 silently false 로 빠지는 사고가 반복됨 (실제 발생).
	//   이 클래스를 상속받은 BP 는 기본적으로 2페이즈 활성, 비활성이 필요한 보스만
	//   BP CDO 에서 명시적으로 false 로 끄도록 정책 변경.
	bHasPhase2 = true;
}

void AHellunaEnemyCharacter_Boss::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHellunaEnemyCharacter_Boss, bInPhase2);
}

// ============================================================
// [BossPhase2V1] HP 0 도달 가로채기 → 2페이즈 전환
// ============================================================
bool AHellunaEnemyCharacter_Boss::TryInterceptDeathForPhase2(float OldHealth, float NewHealth)
{
	// [BossPhase2_Diag] LiveCoding 검증용 진입 로그 — 어떤 조건에서 막히는지 추적
	UE_LOG(LogTemp, Warning,
		TEXT("[BossPhase2_Diag] TryIntercept ENTER Boss=%s HasAuth=%d bHasPhase2=%d bInPhase2=%d Old=%.1f New=%.1f HC=%d"),
		*GetNameSafe(this),
		HasAuthority() ? 1 : 0,
		bHasPhase2 ? 1 : 0,
		bInPhase2 ? 1 : 0,
		OldHealth, NewHealth,
		HealthComponent ? 1 : 0);

	if (!HasAuthority()) return false;
	if (!bHasPhase2) return false;
	if (bInPhase2) return false;              // 이미 2페이즈면 진짜 사망
	if (NewHealth > KINDA_SMALL_NUMBER) return false; // HP 0 도달이 아니면 스킵
	if (!HealthComponent) return false;

	UE_LOG(LogTemp, Warning,
		TEXT("[BossPhase2V1] %s HP depleted — intercepting death, entering Phase 2"),
		*GetNameSafe(this));

	EnterBossPhase2();
	return true;
}

void AHellunaEnemyCharacter_Boss::EnterBossPhase2()
{
	if (!HasAuthority()) return;
	if (bInPhase2) return;

	bInPhase2 = true;

	// 1) HP 회복 — OnHealthChanged 콜백 시점엔 아직 Internal_SetHealth의 OnDeath 브로드캐스트 전이라 bDead=false.
	if (HealthComponent)
	{
		const float MaxHP = HealthComponent->GetMaxHealth();
		const float RestoreTo = FMath::Clamp(Phase2HealthRestoreRatio, 0.1f, 1.f) * MaxHP;
		HealthComponent->SetHealth(RestoreTo);
	}

	// 2) 진행 중이던 모든 패턴(GA) 즉시 취소 — 공격/스킬/존 스폰 전부 중단
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}

	// 3) AI 브레인 정지 — StateTree가 다음 패턴 고르지 못하도록 시네마틱 동안 중단
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* Brain = AIC->GetBrainComponent())
		{
			Brain->StopLogic(TEXT("BossPhase2Transition"));
			bAIStoppedForPhase2 = true;
		}
	}

	// 4) 진행 중이던 공격 락/Focus 정리 + Movement 일시 정지 (중력은 유지)
	UnlockMovement();
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->SetMovementMode(MOVE_Walking);
	}

	// 5) 공격/피격 몽타주 전부 블렌드 아웃
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			constexpr float BlendOutTime = 0.2f;
			AnimInst->StopAllMontages(BlendOutTime);
		}
	}

	// 6) 시네마틱 무적 — 타이머 종료 시 무적 해제 + Brain 재시작
	SetCanBeDamaged(false);
	GetWorldTimerManager().SetTimer(Phase2InvulnerabilityTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SetCanBeDamaged(true);

			// 2페이즈 전투 재개 — Brain 재가동 시 StateTree가 새 패턴 선택
			if (bAIStoppedForPhase2)
			{
				if (AAIController* AIC = Cast<AAIController>(GetController()))
				{
					if (UBrainComponent* Brain = AIC->GetBrainComponent())
					{
						Brain->StartLogic();
					}
				}
				bAIStoppedForPhase2 = false;
			}

			UE_LOG(LogTemp, Warning, TEXT("[BossPhase2V1] Invulnerability ended — Phase 2 combat active"));
		}),
		FMath::Max(Phase2InvulnerabilityDuration, 0.1f), false);

	// 7) Enrage 스탯 재활용 — bEnraged로 공격력/쿨다운 배율 적용
	bEnraged = true;
	EnrageDamageMultiplier = FMath::Max(Phase2AttackMultiplier, EnrageDamageMultiplier);
	EnrageCooldownMultiplier = FMath::Min(Phase2CooldownMultiplier, EnrageCooldownMultiplier);

	UE_LOG(LogTemp, Warning,
		TEXT("[BossPhase2V1] Entered Phase 2 — HP restored=%.0f%%, AtkMul=%.2f, CoolMul=%.2f — patterns cancelled, brain paused"),
		Phase2HealthRestoreRatio * 100.f, Phase2AttackMultiplier, Phase2CooldownMultiplier);

	// 8) Multicast 연출 (VFX/쉐이크/오라/HP바 델리게이트)
	Multicast_PlayBossPhase2Transition();
}

void AHellunaEnemyCharacter_Boss::Multicast_PlayBossPhase2Transition_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[BossPhase2V1] Multicast Phase2 transition — %s"), *GetNameSafe(this));

	UWorld* World = GetWorld();
	if (!World) return;

	// 카메라 쉐이크 — 보스 위치 중심 월드 쉐이크
	if (Phase2TransitionShakeClass)
	{
		UGameplayStatics::PlayWorldCameraShake(
			World, Phase2TransitionShakeClass, GetActorLocation(),
			0.f, 4500.f, 1.f, false);
	}

	// 보스 발밑 충격파 VFX
	if (Phase2TransitionVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Phase2TransitionVFX,
			GetActorLocation(), GetActorRotation());
	}

	// 하늘에서 낙하 VFX — 여러 포인트
	if (Phase2SkyMeteorVFX && Phase2SkyMeteorCount > 0)
	{
		const FVector BossLoc = GetActorLocation();
		for (int32 i = 0; i < Phase2SkyMeteorCount; ++i)
		{
			const float Angle = FMath::FRandRange(0.f, 2.f * PI);
			const float Radius = FMath::FRandRange(0.f, Phase2SkyMeteorRadius);
			const FVector Offset(FMath::Cos(Angle) * Radius,
				FMath::Sin(Angle) * Radius,
				Phase2SkyMeteorHeight + FMath::FRandRange(-200.f, 200.f));
			const FVector Loc = BossLoc + Offset;
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Phase2SkyMeteorVFX,
				Loc, FRotator(-90.f, 0.f, 0.f)); // 아래 방향 기본
		}
	}

	// 보스 상시 오라 VFX attach (이미 있으면 스킵)
	if (Phase2AuraVFX && !ActivePhase2AuraComp)
	{
		USkeletalMeshComponent* BossMesh = GetMesh();
		UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Phase2AuraVFX, BossMesh ? (USceneComponent*)BossMesh : (USceneComponent*)GetRootComponent(),
			NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, false);
		ActivePhase2AuraComp = NC;
	}

	// BP/위젯/AI 알림
	OnBossEnterPhase2.Broadcast();

	// [BossPhase2CamV1] 로컬 PC 시네마틱 — 보스 주변에 CameraActor 스폰해서 전신을 프레이밍.
	//   EnterBossCinematic는 입력/HUD까지 자동 숨김 처리 (이미 BossSummonCinematic 경로에서 검증된 로직).

	// 보스 로컬 좌표 기준으로 카메라 위치 계산 (보스가 어디 보든 3/4 정면 각도 유지)
	const FVector BossLoc = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector Up = GetActorUpVector();

	const FVector CameraLoc =
		BossLoc
		+ Forward * Phase2CameraOffset.X
		+ Right * Phase2CameraOffset.Y
		+ Up * Phase2CameraOffset.Z;

	const FVector LookTarget = BossLoc + FVector(0.f, 0.f, Phase2CameraLookHeight);
	const FRotator LookAt = (LookTarget - CameraLoc).Rotation();

	FActorSpawnParameters CamSpawnParams;
	CamSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* CamActor = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), CameraLoc, LookAt, CamSpawnParams);

	if (CamActor)
	{
		// [Phase2CamFullScreenV1] 화면 양 사이드 검정 바(letterbox) 제거.
		//  ACameraActor 기본 CameraComponent 의 bConstrainAspectRatio 가 true 면 뷰포트가
		//  카메라의 AspectRatio 비율로 강제 매트되어 좌우 검정 바가 생김. false 로 두면 풀스크린.
		if (UCameraComponent* CamComp = CamActor->GetCameraComponent())
		{
			CamComp->SetConstraintAspectRatio(false);
		}

		// 보스에 부착 — 보스가 회전/이동해도 3/4 앵글 유지 (시네마틱 동안엔 보스가 StopMovement라 고정)
		FAttachmentTransformRules AttachRules(
			EAttachmentRule::KeepWorld,
			EAttachmentRule::KeepWorld,
			EAttachmentRule::KeepWorld,
			false);
		CamActor->AttachToActor(this, AttachRules);
	}

	AActor* ViewTarget = CamActor ? (AActor*)CamActor : (AActor*)this;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController()) continue;

		AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
		if (!HeroPC) continue;

		HeroPC->EnterBossCinematic(ViewTarget, FMath::Max(Phase2CameraBlendIn, 0.05f));

		// 무적 지속 시간 후 카메라 복귀 + CamActor 파괴
		TWeakObjectPtr<AHellunaHeroController> WeakPC = HeroPC;
		TWeakObjectPtr<ACameraActor> WeakCam = CamActor;
		const float ExitDelay = FMath::Max(Phase2InvulnerabilityDuration, 0.5f);
		const float BlendOut = Phase2CameraBlendOut;

		FTimerHandle LocalCamTimer;
		World->GetTimerManager().SetTimer(LocalCamTimer,
			FTimerDelegate::CreateWeakLambda(HeroPC, [WeakPC, WeakCam, BlendOut]()
			{
				if (AHellunaHeroController* PCLocal = WeakPC.Get())
				{
					PCLocal->ExitBossCinematic(BlendOut);
				}
				// 블렌드 아웃 중엔 카메라가 보여야 하므로 약간 늦게 파괴
				if (ACameraActor* Cam = WeakCam.Get())
				{
					FTimerHandle DestroyTimer;
					Cam->GetWorld()->GetTimerManager().SetTimer(DestroyTimer,
						FTimerDelegate::CreateWeakLambda(Cam, [WeakCam]()
						{
							if (ACameraActor* C = WeakCam.Get())
							{
								C->Destroy();
							}
						}),
						FMath::Max(BlendOut + 0.1f, 0.2f), false);
				}
			}),
			ExitDelay, false);

		break;
	}
}

// ============================================================
// [HitStopV1] 히트 스톱
// ============================================================
void AHellunaEnemyCharacter_Boss::TriggerHitStop()
{
	if (!HasAuthority()) return;
	if (HitStopDuration <= KINDA_SMALL_NUMBER) return;

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastHitStopTime < HitStopCooldown) return;
	LastHitStopTime = Now;

	Multicast_TriggerHitStop(HitStopDuration, HitStopTimeDilation);
}

void AHellunaEnemyCharacter_Boss::Multicast_TriggerHitStop_Implementation(float Duration, float Dilation)
{
	CustomTimeDilation = FMath::Clamp(Dilation, 0.01f, 1.f);

	GetWorldTimerManager().ClearTimer(HitStopTimer);
	GetWorldTimerManager().SetTimer(HitStopTimer, this,
		&AHellunaEnemyCharacter_Boss::RestoreTimeDilationAfterHitStop,
		FMath::Max(Duration, 0.01f), false);
}

void AHellunaEnemyCharacter_Boss::RestoreTimeDilationAfterHitStop()
{
	CustomTimeDilation = 1.f;
}

// ============================================================
// Multicast_PlaySummonMontage_Implementation
// - 모든 머신에서 소환 몽타주 재생 (서버 + 클라)
// - 서버에서만 OnMontageEnded 바인딩 → BossEncounterCube에게 완료 알림
// ============================================================
void AHellunaEnemyCharacter_Boss::Multicast_PlaySummonMontage_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[BossSummon_LiveCodeCheck] Multicast_PlaySummonMontage — %s, Auth=%d, Montage=%s"),
		*GetName(), HasAuthority() ? 1 : 0,
		SummonMontage ? *SummonMontage->GetName() : TEXT("NULL"));

	if (!SummonMontage)
	{
		// 몽타주가 없으면 서버에서 즉시 완료 신호 — BossEncounterCube가 Failsafe로 처리
		if (HasAuthority())
		{
			OnSummonMontageFinished.ExecuteIfBound();
		}
		return;
	}

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh)
	{
		if (HasAuthority())
		{
			OnSummonMontageFinished.ExecuteIfBound();
		}
		return;
	}

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst)
	{
		if (HasAuthority())
		{
			OnSummonMontageFinished.ExecuteIfBound();
		}
		return;
	}

	// [RootMotionRestoreV1] 서버+클라 양쪽에 OnMontageEnded 바인딩 — 종료 시 RootMotionMode 를
	// 양 머신에서 복원하기 위함. OnSummonMontageFinished 델리게이트는 콜백 내부에서 서버만 발화.
	AnimInst->OnMontageEnded.RemoveDynamic(this, &AHellunaEnemyCharacter_Boss::OnSummonMontageEnded);
	AnimInst->OnMontageEnded.AddDynamic(this, &AHellunaEnemyCharacter_Boss::OnSummonMontageEnded);

	// [CinematicLocationLockV1] AM_Boss_Walk 의 시퀀스 Anim_FuturisticWarrior_walk_Real 가
	//   enable_root_motion=true 라 server 측 root motion 으로 보스가 월드 좌표를 타고 전진함.
	//   하지만 시네마틱 카메라(CineCam)가 KeepRelative 로 보스에 attach 돼 있어 화면상 보스는 제자리,
	//   종료 후 ViewTarget 이 player camera 로 돌아갈 때 새 위치가 드러나 "텔레포트" 처럼 보였음.
	//   해결: IgnoreRootMotion 모드 — 애니메이션의 root bone 키프레임은 그대로 추출되어 mesh 가
	//   actor pivot 에 anchor 되고 다리 모션은 정상 재생되지만, 그 delta 가 CMC velocity 로 적용되지 않아
	//   actor 는 월드에서 한 걸음도 못 움직임 → 시작/종료 위치 동일 → 텔레포트 없음 + 진동 없음.
	//   복원: OnSummonMontageEnded 에서 RootMotionFromMontagesOnly (기본값) 로 되돌림.
	AnimInst->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);

	// [BossSlowSummonV2] Montage layer 자체는 BS 위에 작동 중이지만 AM_Boss_Walk 가 BS 와 같은 walk
	//   시퀀스를 쓰는 탓에 RateScale 0.5x 만으로는 cadence 차이가 약함. InPlayRate 0.5 추가로 곱해
	//   effective 0.25x (RateScale 0.5 × InPlayRate 0.5) — 위엄 있는 슬로우 워크 톤 보장.
	constexpr float BossSummonExtraPlayRate = 0.5f;
	const float PlayedRate = AnimInst->Montage_Play(SummonMontage, BossSummonExtraPlayRate);
	UE_LOG(LogTemp, Warning,
		TEXT("[BossSummon_LiveCodeCheck] Montage_Play returned=%.3f — InPlayRate=%.2f, Asset.RateScale=%.3f, Comp.GlobalAnimRateScale=%.3f, Effective=%.3f"),
		PlayedRate, BossSummonExtraPlayRate, SummonMontage->RateScale, SkelMesh->GlobalAnimRateScale,
		BossSummonExtraPlayRate * SummonMontage->RateScale * SkelMesh->GlobalAnimRateScale);
}

// ============================================================
// OnSummonMontageEnded — 서버에서 소환 몽타주 완료 감지
// ============================================================
void AHellunaEnemyCharacter_Boss::OnSummonMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != SummonMontage) return;

	UE_LOG(LogTemp, Warning, TEXT("[BossSummon_LiveCodeCheck] OnSummonMontageEnded — %s, Interrupted=%d"),
		*GetName(), bInterrupted ? 1 : 0);

	// [RootMotionRestoreV1] Multicast_PlaySummonMontage 에서 켰던 RootMotionFromEverything 를
	// 양 머신(서버+클라) 모두에서 기본값으로 복원. 이걸 안 하면 시네마틱 후 AnimGraph 의 idle/
	// locomotion 블렌드까지 root motion 으로 간주되어 in-place 애니메이션이 속도 0 을 공급 →
	// CMC 의 input 기반 이동이 무시되어 보스가 그 자리에 멈춰있게 됨.
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			AnimInst->OnMontageEnded.RemoveDynamic(this, &AHellunaEnemyCharacter_Boss::OnSummonMontageEnded);
			AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
		}
	}

	if (HasAuthority())
	{
		OnSummonMontageFinished.ExecuteIfBound();
	}
}
