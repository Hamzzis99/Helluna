// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/HellunaEnemyCharacter_Boss.h"

#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Controller/HellunaHeroController.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "HellunaGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Kismet/GameplayStatics.h"

#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"
#include "UObject/SoftObjectPath.h"
#include "DrawDebugHelpers.h"

// [BossArmorBreakV1]
#include "Animation/SkeletalMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/RotatingMovementComponent.h"

// [Phase2DescentChildV1]
#include "Components/ChildActorComponent.h"

// [Phase2RefactorV1]
#include "BossEvent/BossPhase2CinematicTrigger.h"

// [BossDeathCinematicV1]
#include "BossEvent/BossDeathCinematicTrigger.h"

// [DeathTimingV1] 보스 사망 몽타주 끝 → EndGame 직접 트리거.
#include "GameMode/HellunaDefenseGameMode.h"

// [BossDeathV1] 보스 전용 사망 GA — StateTree 우회로 직접 활성화.
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_BossDeath.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"

// [BossDissolveComponentV1]
#include "Character/EnemyComponent/BossDissolveComponent.h"

AHellunaEnemyCharacter_Boss::AHellunaEnemyCharacter_Boss()
{
	// [Phase2CamInterpV1] Tick 활성화 — 광폭화 시네마틱 동안 카메라 위→아래 lerp 처리에 필요.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 보스 등급 기본값 (BP CDO 에서 SemiBoss/Boss 로 추가 조정 가능)
	EnemyGrade = EEnemyGrade::Boss;

	// [BossAlwaysRelevantV1] 보스는 항상 모든 클라에 replicate.
	//   부모 AHellunaEnemyCharacter 는 NetCullDistance 60m (오픈월드 일반몹 최적화)인데,
	//   보스가 우주선에서 60m 넘게 떨어진 곳에 소환되면 remote client 에 보스 actor 가
	//   replicate 되지 않아 소환 시네마틱이 Boss=NULL 로 도착 → 시네마틱/포탈이 첫 플레이어
	//   (호스트)에게만 보이던 버그. 보스는 게임당 1마리라 always-relevant 부담은 무시 가능.
	bAlwaysRelevant = true;

	// [BossPhase2_DefaultV1] 보스 서브클래스의 기본값을 true 로 강제.
	//   C++ 부모 필드 초기값 false + BP CDO 에 한 번씩 직접 켜야 하는 구조로는
	//   리페어런팅/리팩터링 후 silently false 로 빠지는 사고가 반복됨 (실제 발생).
	//   이 클래스를 상속받은 BP 는 기본적으로 2페이즈 활성, 비활성이 필요한 보스만
	//   BP CDO 에서 명시적으로 false 로 끄도록 정책 변경.
	bHasPhase2 = true;

	// [BossDissolveComponentV1] dissolve 효과 컴포넌트.
	//   생성자에서 default subobject 로 보스 actor 에 attach.
	//   디자이너가 BP CDO 에서 dissolve 머티리얼/VFX/timing 직접 set.
	DissolveComponent = CreateDefaultSubobject<UBossDissolveComponent>(TEXT("DissolveComponent"));

	// [BossBGMV1] BGM 전용 2D 오디오 컴포넌트 — 거리감쇠 없이 각 머신 로컬에서 재생/정지.
	BGMAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("BGMAudioComponent"));
	BGMAudioComponent->SetupAttachment(RootComponent);
	BGMAudioComponent->bAutoActivate = false;
	BGMAudioComponent->bAllowSpatialization = false;
	BGMAudioComponent->bIsUISound = true;
}

void AHellunaEnemyCharacter_Boss::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHellunaEnemyCharacter_Boss, bInPhase2);
	// [Phase2HealthFillV3] widget polling 용 — server stage 변화를 client 에 push.
	DOREPLIFETIME(AHellunaEnemyCharacter_Boss, Phase2HealthFillStage);
	// [Phase2ExtraBarV1] client widget 이 별도 추가 바 percent 를 계산할 때 사용.
	DOREPLIFETIME(AHellunaEnemyCharacter_Boss, Phase2HealthFillOldMax);
	DOREPLIFETIME(AHellunaEnemyCharacter_Boss, Phase2HealthFillNewMax);
}

// [BossBGMV1] 모든 머신에서 BGM 정지 (보스 사망 시 서버가 호출).
void AHellunaEnemyCharacter_Boss::Multicast_StopBGM_Implementation()
{
	if (BGMAudioComponent && BGMAudioComponent->IsPlaying())
	{
		BGMAudioComponent->Stop();
	}
}

// ============================================================
// [BossDebugStartPhase2V1] 디버그 — 스폰 직후 2페이즈 강제 진입
// ============================================================
void AHellunaEnemyCharacter_Boss::BeginPlay()
{
	Super::BeginPlay();

	// bDebugStartInPhase2 가 켜져 있으면 1페이즈 전투를 건너뛰고 바로 2페이즈 보스/사망을
	//   테스트할 수 있게 스폰 직후 EnterBossPhase2 호출. AI possess / ASC / HealthComponent
	//   초기화를 기다리려 1초 지연. 서버 권한에서만.
	if (bDebugStartInPhase2 && bHasPhase2 && HasAuthority())
	{
		FTimerHandle DebugStartP2Timer;
		GetWorldTimerManager().SetTimer(DebugStartP2Timer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (bInPhase2) return;
				UE_LOG(LogTemp, Warning,
					TEXT("[BossDebugStartPhase2V1] %s — bDebugStartInPhase2=true → 스폰 직후 2페이즈 강제 진입"),
					*GetNameSafe(this));
				EnterBossPhase2();
			}),
			1.0f, false);
	}

	// [BossBGMV1] 1페이즈 BGM — 보스 등장 시 각 머신에서 재생(데디서버 제외, 2D).
	//   BeginPlay 는 모든 머신에서 호출되므로 Multicast 불필요.
	if (!IsRunningDedicatedServer() && BGMAudioComponent && Phase1BGM)
	{
		BGMAudioComponent->SetSound(Phase1BGM);
		BGMAudioComponent->Play();
	}
}

// ============================================================
// [BossCinematicFreezeV1] 시네마틱 중 무적
//   소환/페이즈2/사망 시네마틱이 재생 중이면 들어오는 데미지를 전부 무시(0).
//   포탑은 Tick 게이팅으로도 막지만, 플레이어 무기 등 다른 소스까지 막는 안전망.
//   데미지는 서버에서만 적용되므로 GameMode 쿼리는 항상 유효하다.
// ============================================================
float AHellunaEnemyCharacter_Boss::TakeDamage(float DamageAmount,
	FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (const AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		if (GM->IsAnyBossCinematicActive())
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[BossCinematicFreezeV1] 시네마틱 중 보스 무적 — 데미지 %.1f 무시 (Causer=%s)"),
				DamageAmount, *GetNameSafe(DamageCauser));
			return 0.f;
		}
	}

	return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
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

	// 1) [Phase2HealthFillV3] MaxHealth 확장도 Stage2 break-through 시점부터 점진 lerp.
	//    여기서는 MaxHealth 변경하지 않음 — OldMax 그대로 유지 + HP=1.
	//    회색 바(=MaxHealth widget width)가 즉시 커지지 않고, Stage2 동안 HP 와 같은 속도로 함께 확장.
	if (HealthComponent)
	{
		const float OldMax = HealthComponent->GetMaxHealth();
		const float NewMax = OldMax * FMath::Max(1.f, Phase2MaxHealthMultiplier);
		HealthComponent->SetHealth(1.f);  // 살아있음 유지 (cinematic 동안 HP 바 거의 비어 보이게)
		Phase2HealthFillOldMax = OldMax;
		Phase2HealthFillNewMax = NewMax;
		UE_LOG(LogTemp, Warning,
			TEXT("[Phase2HealthFillV3] OldMax=%.0f kept, NewMax=%.0f (×%.2f) deferred to Stage2 break-through, HP=1"),
			OldMax, NewMax, Phase2MaxHealthMultiplier);
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

	// 6) 시네마틱 무적 — 타이머 종료 시 무적 해제.
	//   [BossDebugStartPhase2V1] 디버그 시작 모드는 무적 지속을 12초로 단축 — 변신 연출(~9초)을
	//   덮어 변신 중 피격/스태거를 막되 곧 죽일 수 있게. (무적을 아예 끄면 HP=1 상태에서 HP-fill
	//   도중 피격돼 보스가 비정상 동작하므로 반드시 무적은 유지한다.)
	SetCanBeDamaged(false);
	GetWorldTimerManager().SetTimer(Phase2InvulnerabilityTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SetCanBeDamaged(true);
			// [Phase2BrainResumeV1] Brain 재가동 등은 시네마틱 트리거 책임. 여기서는 면역만 풀음.
			StopPhase2Shakes();
			UE_LOG(LogTemp, Warning, TEXT("[BossPhase2V1] Invulnerability ended — damage enabled, shake cleaned (server)"));
		}),
		bDebugStartInPhase2 ? 12.f : FMath::Max(Phase2InvulnerabilityDuration, 0.1f), false);

	// 7) [Phase2RefactorV1] EnterEnraged 호출은 Multicast 의 Stage3 timer (Phase2StunDuration 후) 에서.
	UE_LOG(LogTemp, Warning,
		TEXT("[BossPhase2_EnrageDiag] Phase 2 entry on %s — Stun=%.1fs, Total=%.1fs"),
		*GetNameSafe(this), Phase2StunDuration, Phase2InvulnerabilityDuration);

	// 8) Multicast 연출 (VFX/쉐이크/오라/HP바 델리게이트)
	Multicast_PlayBossPhase2Transition();
}

void AHellunaEnemyCharacter_Boss::Multicast_PlayBossPhase2Transition_Implementation()
{
	UE_LOG(LogTemp, Warning,
		TEXT("[Phase2RefactorV1] Multicast Phase2 transition start — %s (Stun=%.1f)"),
		*GetNameSafe(this), Phase2StunDuration);

	UWorld* World = GetWorld();
	if (!World) return;

	// [BossBGMV1] 2페이즈 전환 시작 — 1페이즈 BGM 을 끈다(무음). 2페이즈 BGM 은 이후 회오리
	//   발생 시점(Phase2_PlayStage3Visuals)에 재생된다. → "꺼졌다가 회오리에 맞춰 다시 시작".
	if (!IsRunningDedicatedServer() && BGMAudioComponent && BGMAudioComponent->IsPlaying())
	{
		BGMAudioComponent->Stop();
	}

	// [Phase2SkipFastForwardV1] 새 페이즈2 진입 — Stage3 가드 리셋 (모든 머신).
	bPhase2Stage3Triggered = false;

	// =========================================================
	// [Phase2RefactorV1] 카메라/대사/단계 시퀀스 — 트리거(ABossPhase2CinematicTrigger)에 위임.
	//   서버에서만 Spawn + TryActivate. 트리거가 Multicast 로 자기 카메라/대사 처리.
	// =========================================================
	// [BossDebugSkipV1] GameMode 통합 토글 OR 로컬 토글.
	//   [BossDebugStartPhase2V1] 디버그 시작 모드면 시네마틱도 무조건 스킵.
	bool bSkipPhase2Cinematic = bDebugSkipPhase2Cinematic || bDebugStartInPhase2;
	if (AHellunaDefenseGameMode* GM = World->GetAuthGameMode<AHellunaDefenseGameMode>())
	{
		bSkipPhase2Cinematic = bSkipPhase2Cinematic || GM->bDebugSkipBossPhase2Cinematic;
	}

	if (HasAuthority() && Phase2CinematicTriggerClass && !bSkipPhase2Cinematic)
	{
		FActorSpawnParameters TriggerParams;
		TriggerParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TriggerParams.Owner = this;
		ABossPhase2CinematicTrigger* Trigger = World->SpawnActor<ABossPhase2CinematicTrigger>(
			Phase2CinematicTriggerClass, GetActorLocation(), GetActorRotation(), TriggerParams);
		if (Trigger)
		{
			Trigger->TryActivate(this);
			UE_LOG(LogTemp, Warning, TEXT("[Phase2RefactorV1] Spawned cinematic trigger: %s"), *Trigger->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Phase2RefactorV1] Failed to spawn cinematic trigger — class=%s"),
				*GetNameSafe(Phase2CinematicTriggerClass));
		}
	}
	else if (bSkipPhase2Cinematic)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Phase2DebugSkipV1] Skipping Phase 2 cinematic trigger — Stage3 visuals + HP fill 만 진행"));
	}

	// =========================================================
	// 기절 몽타주 — 모든 머신
	// =========================================================
	// [BossDebugStartPhase2V1] 디버그 시작 모드면 기절 몽타주 스킵 — 바로 광폭화 비주얼로.
	if (Phase2StunMontage && !bDebugStartInPhase2)
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(Phase2StunMontage, 1.0f);
			}
		}
	}

	// =========================================================
	// Stage 3 timer — 광폭화/갑옷/본체 swap/VFX (Phase2StunDuration 후)
	// =========================================================
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfStage3(this);
		FTimerHandle Stage3Timer;
		World->GetTimerManager().SetTimer(Stage3Timer,
			FTimerDelegate::CreateLambda([WeakSelfStage3]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakSelfStage3.Get();
				if (!Self) return;
				// [Phase2EnrageDelayV2] EnterEnraged(광폭화 몽타주)는 Phase2_PlayStage3Visuals 안의
				//   지연 타이머(갑옷 메시 swap 과 동일 시점)로 이동했다 — 분리하면 메시 swap 이
				//   진행 중 기절 몽타주를 재시작해 첫 애니가 2번 재생되는 버그가 생긴다.
				Self->Phase2_PlayStage3Visuals();
			}),
			// [BossDebugStartPhase2V1] 디버그 시작 모드면 스턴 대기 없이 ~0.2초 후 광폭화 비주얼.
			bDebugStartInPhase2 ? 0.2f : FMath::Max(0.2f, Phase2StunDuration), false);
	}

	OnBossEnterPhase2.Broadcast();
	return;

	// 아래 ↓↓↓ 기존 코드 — 트리거로 이동, dead code (return 위에서 끝남)
	// 컴파일러 dead code warning 방지 위해 if(false) 안에 유지하지 않음. 그대로 return 위에서 종료.
	(void)World;
}

// [Phase2RefactorV1] 기존 카메라 spawn / 단계 1b/2/4 timer / 대사 / 시네마틱 종료 lambda 모두 제거됨.
//   트리거 (ABossPhase2CinematicTrigger) 가 처리.
#if 0
void AHellunaEnemyCharacter_Boss::Multicast_PlayBossPhase2Transition_Implementation_OLD()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// =========================================================
	// 단계 1 (즉시): 정면 카메라 spawn + EnterBossCinematic + 기절 몽타주 + 대사
	// =========================================================
	const FVector BossLoc = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector Up = GetActorUpVector();

	// [Phase2FaceCamV1] 시네마틱 시작 = 단계1a 얼굴 클로즈업.
	const FVector StartCamLoc = BossLoc
		+ Forward * Phase2CameraFaceOffset.X
		+ Right * Phase2CameraFaceOffset.Y
		+ Up * Phase2CameraFaceOffset.Z;
	const FVector StartLookTarget = BossLoc + FVector(0.f, 0.f, Phase2CameraFaceLookHeight);
	const FRotator StartLookAt = (StartLookTarget - StartCamLoc).Rotation();

	FActorSpawnParameters CamSpawnParams;
	CamSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* CamActor = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), StartCamLoc, StartLookAt, CamSpawnParams);
	if (CamActor)
	{
		if (UCameraComponent* CamComp = CamActor->GetCameraComponent())
		{
			CamComp->SetConstraintAspectRatio(false);
		}
		Phase2CamActor = CamActor;
		Phase2CamInterpElapsed = 0.f;
		bPhase2CameraInterpolating = false; // 단계 1a 동안 lerp 정지 (Tick 가 To 위치 추적).
		// 정지 카메라 위치 = Face — Tick 의 hold 분기가 이 값 사용 → 보스 추적.
		Phase2CamLerpFromOffset = Phase2CameraFaceOffset;
		Phase2CamLerpToOffset = Phase2CameraFaceOffset;
		Phase2CamLerpFromLookH = Phase2CameraFaceLookHeight;
		Phase2CamLerpToLookH = Phase2CameraFaceLookHeight;
		Phase2CamLerpDuration = 1.f;
	}
	AActor* ViewTarget = CamActor ? (AActor*)CamActor : (AActor*)this;

	// 로컬 PC 시네마틱 진입 + 단계1 대사 + 시네마틱 종료 timer
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController()) continue;
		AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
		if (!HeroPC) continue;

		HeroPC->EnterBossCinematic(ViewTarget, FMath::Max(Phase2CameraBlendIn, 0.05f));

		// 단계1 대사
		if (Phase2DialogueWidgetClass && !Phase2DialogueLine.IsEmpty()
			&& GetNetMode() != NM_DedicatedServer)
		{
			if (ActivePhase2DialogueWidget)
			{
				ActivePhase2DialogueWidget->HideDialogue();
				ActivePhase2DialogueWidget = nullptr;
			}
			ActivePhase2DialogueWidget = CreateWidget<UBossDialogueWidget>(PC, Phase2DialogueWidgetClass);
			if (ActivePhase2DialogueWidget)
			{
				ActivePhase2DialogueWidget->AddToViewport(50);
				ActivePhase2DialogueWidget->PlayDialogue(Phase2SpeakerName, Phase2DialogueLine);
				TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfDlg(this);
				GetWorldTimerManager().ClearTimer(Phase2DialogueHideTimer);
				GetWorldTimerManager().SetTimer(Phase2DialogueHideTimer,
					FTimerDelegate::CreateLambda([WeakSelfDlg]()
					{
						AHellunaEnemyCharacter_Boss* Self = WeakSelfDlg.Get();
						if (!Self || !Self->ActivePhase2DialogueWidget) return;
						Self->ActivePhase2DialogueWidget->HideDialogue();
						Self->ActivePhase2DialogueWidget = nullptr;
					}),
					FMath::Max(0.5f, Phase2DialogueDuration), false);
			}
		}

		// 시네마틱 종료 timer (Phase2InvulnerabilityDuration 후) — 카메라 복귀 + VFX/몽타주 정리
		TWeakObjectPtr<AHellunaHeroController> WeakPC = HeroPC;
		TWeakObjectPtr<ACameraActor> WeakCam = CamActor;
		const float ExitDelay = FMath::Max(Phase2InvulnerabilityDuration, 0.5f);
		const float BlendOut = Phase2CameraBlendOut;
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelf(this);
		FTimerHandle LocalCamTimer;
		World->GetTimerManager().SetTimer(LocalCamTimer,
			FTimerDelegate::CreateWeakLambda(HeroPC, [WeakPC, WeakCam, WeakSelf, BlendOut]()
			{
				if (AHellunaEnemyCharacter_Boss* Self = WeakSelf.Get())
				{
					Self->GetWorldTimerManager().ClearTimer(Self->Phase2ShakeRepeatTimer);
					Self->bPhase2CameraInterpolating = false;
					Self->Phase2CamActor.Reset();
					Self->bPhase2DescentScaling = false;

					TArray<UNiagaraComponent*> AllNCs;
					Self->GetComponents<UNiagaraComponent>(AllNCs);
					int32 DeactivatedNCs = 0;
					for (UNiagaraComponent* NC : AllNCs)
					{
						if (!NC || !NC->IsActive()) continue;
						// [Phase2LaserSplitV1] 회오리(Phase2Descent) + 레이저(Phase2Laser) 둘 다 정리.
						const bool bIsDescentOrLaser =
							NC->ComponentTags.Contains(FName(TEXT("Phase2Descent"))) ||
							NC->ComponentTags.Contains(FName(TEXT("Phase2Laser")));
						if (!bIsDescentOrLaser) continue;
						// [Phase2LaserLeftoverFixV1] Deactivate() 는 신규 spawn 만 멈춰 기존 파티클을 남긴다.
						//   시네마틱은 여기서 완전히 종료되므로 DeactivateImmediate 로 기존 파티클(레이저·회오리)까지
						//   즉시 제거 — 카메라 블렌드 아웃과 함께 깔끔히 사라짐.
						NC->DeactivateImmediate();
						++DeactivatedNCs;
					}

					bool bMontageStopped = false;
					if (Self->EnrageMontage)
					{
						if (USkeletalMeshComponent* SkelMesh = Self->GetMesh())
						{
							if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
							{
								if (AnimInst->Montage_IsPlaying(Self->EnrageMontage))
								{
									AnimInst->Montage_Stop(0.3f, Self->EnrageMontage);
									bMontageStopped = true;
								}
							}
						}
					}
					UE_LOG(LogTemp, Warning,
						TEXT("[Phase2LaserLeftoverFixV1] DeactivateImmediate NCs=%d MontageStopped=%d (잔존 레이저 즉시 제거)"),
						DeactivatedNCs, bMontageStopped ? 1 : 0);
				}
				if (AHellunaHeroController* PCLocal = WeakPC.Get())
				{
					PCLocal->ExitBossCinematic(BlendOut);
				}
				// 블렌드 아웃 중엔 카메라가 보여야 하므로 약간 늦게 파괴
				// [Fix:null-guard 2026-05-02] GetWorld() nullptr 체크 추가
				if (ACameraActor* Cam = WeakCam.Get())
				{
					if (UWorld* CamWorld = Cam->GetWorld())
					{
						FTimerHandle DestroyTimer;
						CamWorld->GetTimerManager().SetTimer(DestroyTimer,
							FTimerDelegate::CreateWeakLambda(Cam, [WeakCam]()
							{
								if (ACameraActor* C = WeakCam.Get())
								{
									C->Destroy();
								}
							}),
							FMath::Max(BlendOut + 0.1f, 0.2f), false);
					}
				}
			}),
			ExitDelay, false);
		break;
	}

	// 기절 몽타주 — 모든 머신
	if (Phase2StunMontage)
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(Phase2StunMontage, 1.0f);
			}
		}
	}

	// [Phase2HitSlowV2] 슬로우 모션은 AnimNotifyState_ActorTimeDilation 으로 anim 안에서 처리.
	//   Stun_Start 의 anim_hit 부분에 그 notify state drag → 자동 begin/end 시 보스 CustomTimeDilation set/restore.

	// =========================================================
	// 단계 1b timer (Phase2CameraFaceDuration 후): face → front lerp 시작
	// =========================================================
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfStage1b(this);
		FTimerHandle Stage1bTimer;
		World->GetTimerManager().SetTimer(Stage1bTimer,
			FTimerDelegate::CreateLambda([WeakSelfStage1b]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakSelfStage1b.Get();
				if (!Self) return;
				// face → front 전신 lerp
				Self->Phase2CamLerpFromOffset = Self->Phase2CameraFaceOffset;
				Self->Phase2CamLerpToOffset = Self->Phase2CameraStartOffset;
				Self->Phase2CamLerpFromLookH = Self->Phase2CameraFaceLookHeight;
				Self->Phase2CamLerpToLookH = Self->Phase2CameraStartLookHeight;
				Self->Phase2CamLerpDuration = FMath::Max(0.3f, Self->Phase2StunDuration - Self->Phase2CameraFaceDuration);
				Self->Phase2CamInterpElapsed = 0.f;
				Self->bPhase2CameraInterpolating = true;
				UE_LOG(LogTemp, Warning, TEXT("[Phase2StageV2] Stage 1b — face→front lerp start (%.1fs)"), Self->Phase2CamLerpDuration);
			}),
			FMath::Max(0.1f, Phase2CameraFaceDuration), false);
	}

	// =========================================================
	// 단계 2 timer (Phase2StunDuration 후): front → top lerp 시작
	// =========================================================
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfStage2(this);
		FTimerHandle Stage2Timer;
		World->GetTimerManager().SetTimer(Stage2Timer,
			FTimerDelegate::CreateLambda([WeakSelfStage2]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakSelfStage2.Get();
				if (!Self) return;
				Self->Phase2CamLerpFromOffset = Self->Phase2CameraStartOffset;
				Self->Phase2CamLerpToOffset = Self->Phase2CameraOffset;
				Self->Phase2CamLerpFromLookH = Self->Phase2CameraStartLookHeight;
				Self->Phase2CamLerpToLookH = Self->Phase2CameraLookHeight;
				Self->Phase2CamLerpDuration = FMath::Max(0.3f, Self->Phase2CameraRiseDuration);
				Self->Phase2CamInterpElapsed = 0.f;
				Self->bPhase2CameraInterpolating = true;
				UE_LOG(LogTemp, Warning, TEXT("[Phase2StageV2] Stage 2 — front→top lerp start"));
			}),
			FMath::Max(0.1f, Phase2StunDuration), false);
	}

	// =========================================================
	// 단계 3 timer (Phase2StunDuration + Phase2CameraRiseDuration 후): 광폭화 + VFX + 갑옷
	// =========================================================
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfStage3(this);
		// [Phase2StageV3] VFX 빨리 — 단계2 시작과 동시에 호출 (이전: 단계3 시작 = Stun+Rise).
		const float Stage3Delay = FMath::Max(0.2f, Phase2StunDuration);
		FTimerHandle Stage3Timer;
		World->GetTimerManager().SetTimer(Stage3Timer,
			FTimerDelegate::CreateLambda([WeakSelfStage3]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakSelfStage3.Get();
				if (!Self) return;
				if (Self->HasAuthority())
				{
					Self->EnterEnraged();
				}
				Self->Phase2_PlayStage3Visuals();
			}),
			Stage3Delay, false);
	}

	// =========================================================
	// 단계 4 timer (시네마틱 종료 - DescentDuration): 카메라 위→정면 reverse lerp
	// =========================================================
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfStage4(this);
		// [Phase2CamEndHoldV1] 단계4 후 2초 hold — 보스 전신 보이는 시간 확보.
		//   Stage4Delay = Invuln - Descent - Hold → Stage4 끝 = Stage4Delay + Descent → Hold = Invuln - Stage4 끝.
		const float Phase2CameraEndHoldHardcoded = 2.f;
		const float Stage4Delay = FMath::Max(0.2f, Phase2InvulnerabilityDuration - Phase2CameraDescentDuration - Phase2CameraEndHoldHardcoded);
		FTimerHandle Stage4Timer;
		World->GetTimerManager().SetTimer(Stage4Timer,
			FTimerDelegate::CreateLambda([WeakSelfStage4]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakSelfStage4.Get();
				if (!Self) return;
				// top → front 전신 lerp
				Self->Phase2CamLerpFromOffset = Self->Phase2CameraOffset;
				Self->Phase2CamLerpToOffset = Self->Phase2CameraStartOffset;
				Self->Phase2CamLerpFromLookH = Self->Phase2CameraLookHeight;
				Self->Phase2CamLerpToLookH = Self->Phase2CameraStartLookHeight;
				Self->Phase2CamLerpDuration = FMath::Max(0.3f, Self->Phase2CameraDescentDuration);
				Self->Phase2CamInterpElapsed = 0.f;
				Self->bPhase2CameraInterpolating = true;
				UE_LOG(LogTemp, Warning, TEXT("[Phase2StageV2] Stage 4 — top→front lerp start (위→정면)"));
			}),
			Stage4Delay, false);
	}

	// BP/위젯/AI 알림
	OnBossEnterPhase2.Broadcast();
}
#endif // 0 — Phase2RefactorV1 기존 코드 비활성화 끝

// ============================================================
// [BossDeathCinematicV1] 보스 사망 시점에 사망 시네마틱 트리거 spawn.
//   - HP 가 진짜 0 으로 떨어진 시점 (페이즈2 가 인터셉트 안 한 사망) 에 호출됨.
//   - 서버에서만 spawn — 트리거가 자체 Multicast 로 모든 머신 카메라 처리.
//   - Super::OnMonsterDeath 보다 먼저 트리거 spawn 해서 카메라가 사망 몽타주 시작과
//     같이 켜지도록 (StateTree → GA_Death → DeathMontage 가 곧 활성).
// ============================================================
void AHellunaEnemyCharacter_Boss::OnMonsterDeath(AActor* DeadActor, AActor* KillerActor)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[BossDeathV1][OnMonsterDeath] %s WorldTime=%.3f Auth=%d"),
		*GetNameSafe(this),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f,
		HasAuthority() ? 1 : 0);

	if (!HasAuthority())
	{
		// 클라는 부모 흐름 그대로 (필요 시 client-side cleanup).
		Super::OnMonsterDeath(DeadActor, KillerActor);
		return;
	}

	// [BossBGMV1] 보스 사망 — 모든 머신에서 BGM 정지.
	Multicast_StopBGM();

	// =========================================================================
	// [BossDeathV1] StateTree 우회 흐름 — Super::OnMonsterDeath 호출 안 함.
	//   Super 가 SendStateTreeEvent(Death) 호출 → STTask_Death → GA_Death → 70% EarlyFinish →
	//   STTask Succeed → ChooseAttack → 새 SpawnAttack 활성화 (사망 도중 TimeDistortionZone 발동) 버그.
	//   대신 부모 cleanup 만 직접 처리하고 BrainStop + GA_BossDeath 직접 활성화.
	// =========================================================================

	// 1) 부모 cleanup (SendStateTreeEvent 만 빼고 동일).
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAbilities();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (USkeletalMeshComponent* EnemyMesh = GetMesh())
	{
		EnemyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	// 2) BrainStop — StateTree 정지 (이미 활성된 어떤 STTask 도 멈춤, 새 transition 차단).
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* Brain = AIC->GetBrainComponent())
		{
			Brain->StopLogic(TEXT("BossDeathV1"));
			UE_LOG(LogTemp, Warning, TEXT("[BossDeathV1] BrainStopLogic on %s"),
				*GetNameSafe(this));
		}
	}

	// 3) 시네마틱 트리거 spawn.
	UWorld* World = GetWorld();
	if (World && DeathCinematicTriggerClass)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = this;
		ABossDeathCinematicTrigger* Trigger = World->SpawnActor<ABossDeathCinematicTrigger>(
			DeathCinematicTriggerClass, GetActorLocation(), GetActorRotation(), Params);
		if (Trigger)
		{
			Trigger->TryActivate(this);
			UE_LOG(LogTemp, Warning,
				TEXT("[BossDeathV1] Spawned death cinematic trigger: %s"),
				*Trigger->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BossDeathV1] Failed to spawn death cinematic — class=%s"),
				*GetNameSafe(DeathCinematicTriggerClass));
		}
	}

	// 4) GA_BossDeath 직접 활성화 (StateTree 우회). GA 가 자체 lifecycle 관리:
	//    몽타주 자연 종료 시 → DespawnMassEntity + EndGame trigger.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		bool bAlreadyHas = false;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetClass() == UEnemyGameplayAbility_BossDeath::StaticClass())
			{
				bAlreadyHas = true;
				break;
			}
		}
		if (!bAlreadyHas)
		{
			FGameplayAbilitySpec Spec(UEnemyGameplayAbility_BossDeath::StaticClass());
			Spec.SourceObject = this;
			Spec.Level = 1;
			ASC->GiveAbility(Spec);
		}

		const bool bActivated = ASC->TryActivateAbilityByClass(UEnemyGameplayAbility_BossDeath::StaticClass());
		UE_LOG(LogTemp, Warning,
			TEXT("[BossDeathV1] GA_BossDeath TryActivate=%d"), bActivated ? 1 : 0);

		if (!bActivated)
		{
			// Failsafe — GA 활성 실패 시 직접 destroy 처리.
			UE_LOG(LogTemp, Warning, TEXT("[BossDeathV1] Failsafe — direct DespawnMassEntityOnServer"));
			DespawnMassEntityOnServer(TEXT("BossDeath_GAFailsafe"));
		}
	}
}

// ============================================================
// [BossDeathV1] hold 시스템 제거 — GA_BossDeath 가 자체 lifecycle 관리.
//   override 자체는 멤버 함수 시그니처 보존을 위해 남기되 단순 pass-through.
// ============================================================
void AHellunaEnemyCharacter_Boss::DespawnMassEntityOnServer(const TCHAR* Where)
{
	Super::DespawnMassEntityOnServer(Where);
}

// ============================================================
// [BossDeathV1] 아래 두 함수는 GA_BossDeath 도입으로 더 이상 사용 안 함.
//   헤더 시그니처 보존 위해 빈 본문으로만 유지 (LiveCoding 대비). 호출 자체가 안 됨.
// ============================================================
void AHellunaEnemyCharacter_Boss::OnBossDeathMontageEnded_ServerSignal(UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
{
	// no-op — GA_BossDeath 가 자체 OnMontageCompleted/Cancelled 콜백으로 책임.
}

void AHellunaEnemyCharacter_Boss::ReleaseDeathCinematicHold(const TCHAR* /*Reason*/)
{
	// no-op — hold 시스템 제거됨. GA_BossDeath::HandleBossDeathFinished 가 처리.
}

// ============================================================
// [BossDeathMeshLiftV1] 사망 몽타주 동안 SkelMesh Z lift — 누운 자세가 바닥 뚫는 문제 보정.
//   Anim_FuturisticWarrior_death3 의 final pose 가 hand_l Z≈−10, foot_l Z≈−2 까지 내려가서
//   mesh root(=floor) 기준 음수 영역에 본들이 위치 → 시각적으로 바닥 관통.
//   여기서는 캡슐/콜리전 건드리지 않고 SkelMesh.RelativeLocation.Z 만 lerp 으로 +오프셋 적용.
// ============================================================
void AHellunaEnemyCharacter_Boss::Multicast_StartDeathMeshLift_Implementation()
{
	if (bDeathMeshLiftActive) return; // 중복 호출 방지

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	SavedDeathMeshRelLoc = SkelMesh->GetRelativeLocation();
	SavedDeathMeshRelZ = SavedDeathMeshRelLoc.Z;
	SavedDeathMeshRelRot = SkelMesh->GetRelativeRotation();
	DeathMeshLiftElapsed = 0.f;
	bDeathMeshLiftActive = true;

	// [BossDeathSlopeAlignV1] 죽는 순간 발밑 지형 캡처 (법선/각도/지면점). 정적 월드 기준이라
	//   이 Multicast 안에서 각 머신이 동일 결과를 얻는다 → 시체 정렬이 서버/클라 일치.
	CaptureDeathGroundSlope();

	UE_LOG(LogTemp, Warning,
		TEXT("[BossDeathMeshLiftV1] Start mesh Z lift — Auth=%d StartRelZ=%.2f Target=+%.2f over %.2fs"),
		HasAuthority() ? 1 : 0, SavedDeathMeshRelZ, DeathMontageMeshZOffset, DeathMontageMeshLiftDuration);
}

// ============================================================
// [BossDeathSlopeAlignV1] 죽는 순간 발밑으로 트레이스해 지형 법선/각도/지면점을 캡처.
//   - 정적 월드(ECC_WorldStatic: 랜드스케이프/스태틱메시) 기준 하향 LineTrace.
//   - 결과를 멤버에 저장하고, 검증용으로 로그 + 화면 메시지 + 디버그선(법선 화살표) 출력.
//   - 이동은 OnMonsterDeath 에서 이미 정지됐으므로 보스는 제자리. 캡처값은 몽타주 내내 유효.
// ============================================================
void AHellunaEnemyCharacter_Boss::CaptureDeathGroundSlope()
{
	bDeathGroundCaptured = false;
	DeathGroundNormal = FVector::UpVector;
	DeathSlopeAngleDeg = 0.f;

	UWorld* World = GetWorld();
	if (!World) return;

	const FVector Origin = GetActorLocation();
	const FVector Start  = Origin + FVector(0.f, 0.f, 50.f);
	const FVector End    = Origin - FVector(0.f, 0.f, DeathGroundTraceLength);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BossDeathGroundSlope), false, this);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);

	if (bHit)
	{
		DeathGroundNormal = Hit.ImpactNormal.GetSafeNormal();
		DeathGroundPoint  = Hit.ImpactPoint;
		const float Dot = FMath::Clamp(FVector::DotProduct(DeathGroundNormal, FVector::UpVector), -1.f, 1.f);
		DeathSlopeAngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
		bDeathGroundCaptured = true;
	}

	const TCHAR* NM = TEXT("?");
	switch (GetNetMode())
	{
	case NM_Standalone:      NM = TEXT("Standalone"); break;
	case NM_Client:          NM = TEXT("Client");     break;
	case NM_ListenServer:    NM = TEXT("Listen");     break;
	case NM_DedicatedServer: NM = TEXT("DedSrv");     break;
	default: break;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossDeathSlopeAlignV1][%s] 지형 캡처 bHit=%d 경사각=%.1f도 법선=%s 지면Z=%.1f (보스Z=%.1f)"),
		NM, bHit ? 1 : 0, DeathSlopeAngleDeg, *DeathGroundNormal.ToString(),
		bHit ? DeathGroundPoint.Z : 0.f, Origin.Z);

	if (bDeathSlopeDebugDraw && bHit)
	{
		DrawDebugLine(World, Start, DeathGroundPoint, FColor::Yellow, false, 10.f, 0, 2.f);
		DrawDebugDirectionalArrow(World, DeathGroundPoint, DeathGroundPoint + DeathGroundNormal * 150.f,
			40.f, FColor::Green, false, 10.f, 0, 4.f);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Cyan,
				FString::Printf(TEXT("[BossDeath %s] 경사각 %.1f도  법선 %s"),
					NM, DeathSlopeAngleDeg, *DeathGroundNormal.ToString()));
		}
	}
}

// ============================================================
// [HitStopV1] 히트 스톱
// ============================================================
void AHellunaEnemyCharacter_Boss::TriggerHitStop()
{
	if (!HasAuthority()) return;
	if (HitStopDuration <= KINDA_SMALL_NUMBER) return;

	// [Fix:null-guard 2026-05-02] 엔진 종료/Actor 파괴 단계 GetWorld() nullptr 가능
	UWorld* World = GetWorld();
	if (!World) return;

	const double Now = World->GetTimeSeconds();
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
	// [HitStopDeadGuardV1] 사망 상태에서는 reset skip — ActorTimeDilation 노티 (사망 몽타주 슬로우)
	//   가 set 한 CustomTimeDilation 을 1.0 으로 덮어써 슬로우가 0.08s 만에 끊기는 conflict 방지.
	//   NotifyBegin/End multiply chain 이 정상 복원하므로 dead 상태에서는 손대지 않는 게 안전.
	if (HealthComponent && HealthComponent->IsDead())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[HitStopDeadGuardV1] %s is dead — skipping CustomTimeDilation reset"),
			*GetNameSafe(this));
		return;
	}
	CustomTimeDilation = 1.f;
}

// ============================================================
// [BossDissolveSpawnV1] Multicast_ActivateDissolveStageTwo
//   DissolveActor 의 VFX_A08 NiagaraComponent 활성화 — 모든 머신.
//   NiagaraComponent active state 는 자동 replicate 안 되므로 RPC 필요.
// ============================================================
void AHellunaEnemyCharacter_Boss::Multicast_ActivateDissolveStageTwo_Implementation(AActor* DissolveActor)
{
	if (!DissolveActor) return;
	TArray<UNiagaraComponent*> Comps;
	DissolveActor->GetComponents<UNiagaraComponent>(Comps);
	for (UNiagaraComponent* C : Comps)
	{
		if (C && C->GetName().Contains(TEXT("VFX_A08")))
		{
			C->Activate();
			UE_LOG(LogTemp, Warning,
				TEXT("[BossDissolveSpawnV1] VFX_A08 activated (multicast, Auth=%d)"),
				HasAuthority() ? 1 : 0);
			break;
		}
	}
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

	// [SummonMontageMeshSinkV1] AM_Boss_Walk 의 발 높이가 캡슐 바닥과 정확히 맞지 않아 살짝 떠 보이는
	//   현상 보정 — SkelMesh 의 RelativeLocation.Z 를 SummonMontageMeshZOffset (cm) 만큼 임시로 내림.
	//   콜리전 캡슐/CMC 는 변경 없음 (mesh 만 시각적으로 sink). 복원은 OnSummonMontageEnded 의 cleanup 분기.
	//   bSummonMontageMeshOffsetApplied 가드 — Multicast 가 한 번만 호출돼도 안전하게 idempotent.
	if (!bSummonMontageMeshOffsetApplied && !FMath::IsNearlyZero(SummonMontageMeshZOffset))
	{
		// [DebugV2] 적용 전 — relative + world + 캡슐 바닥 대비 mesh world Z gap 까지 모두 로깅
		const FVector PreRel = SkelMesh->GetRelativeLocation();
		const FVector PreWorld = SkelMesh->GetComponentLocation();
		float CapBottomZ = 0.f;
		float CapHalf = 0.f;
		float CapCenterZ = 0.f;
		if (UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			CapHalf = Cap->GetScaledCapsuleHalfHeight();
			CapCenterZ = Cap->GetComponentLocation().Z;
			CapBottomZ = CapCenterZ - CapHalf;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummon_LiveCodeCheck][SinkPre] Auth=%d | Rel=(%.2f, %.2f, %.2f) | World.Z=%.2f | CapCenterZ=%.2f Half=%.2f Bottom=%.2f | MeshWorldZ-CapBottom=%.2f"),
			HasAuthority() ? 1 : 0,
			PreRel.X, PreRel.Y, PreRel.Z, PreWorld.Z, CapCenterZ, CapHalf, CapBottomZ, PreWorld.Z - CapBottomZ);

		SavedMeshRelativeZ = PreRel.Z;
		SkelMesh->SetRelativeLocation(FVector(PreRel.X, PreRel.Y, PreRel.Z + SummonMontageMeshZOffset));
		bSummonMontageMeshOffsetApplied = true;
		bSinkTickGuardLoggedDeviation = false; // 새 시네마틱마다 첫 deviation 다시 로그

		// [DebugV2] 적용 직후 readback — Set 이 실제로 반영됐는지 확인
		const FVector PostRel = SkelMesh->GetRelativeLocation();
		const FVector PostWorld = SkelMesh->GetComponentLocation();
		const float ExpectedRelZ = PreRel.Z + SummonMontageMeshZOffset;
		const float ActualDelta = PostRel.Z - PreRel.Z;
		UE_LOG(LogTemp, Warning,
			TEXT("[BossSummon_LiveCodeCheck][SinkPost] Auth=%d | RelZ %.2f -> %.2f (expected=%.2f, actualDelta=%.2f) | WorldZ %.2f -> %.2f | MeshWorldZ-CapBottom=%.2f | OffsetCfg=%.2f"),
			HasAuthority() ? 1 : 0,
			PreRel.Z, PostRel.Z, ExpectedRelZ, ActualDelta,
			PreWorld.Z, PostWorld.Z, PostWorld.Z - CapBottomZ, SummonMontageMeshZOffset);

		if (!FMath::IsNearlyEqual(PostRel.Z, ExpectedRelZ, 0.05f))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BossSummon_LiveCodeCheck][SinkMismatch] SetRelativeLocation didn't stick! got=%.4f expected=%.4f — 다른 코드/물리가 즉시 덮어씀"),
				PostRel.Z, ExpectedRelZ);
		}

		// [DebugV2] 0.5초 후 한 번 더 readback — 시네마틱 진행 중에 다른 코드가 mesh Z 를 되돌리지 않는지 검증
		FTimerHandle DebugVerifyHandle;
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelf(this);
		const float ExpectedZSnap = ExpectedRelZ;
		GetWorldTimerManager().SetTimer(DebugVerifyHandle, [WeakSelf, ExpectedZSnap]()
		{
			AHellunaEnemyCharacter_Boss* Self = WeakSelf.Get();
			if (!Self) return;
			USkeletalMeshComponent* M = Self->GetMesh();
			if (!M) return;
			const FVector NowRel = M->GetRelativeLocation();
			const FVector NowWorld = M->GetComponentLocation();
			float CapBot = 0.f;
			if (UCapsuleComponent* Cap2 = Self->GetCapsuleComponent())
			{
				CapBot = Cap2->GetComponentLocation().Z - Cap2->GetScaledCapsuleHalfHeight();
			}
			const bool bStillApplied = FMath::IsNearlyEqual(NowRel.Z, ExpectedZSnap, 0.05f);
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummon_LiveCodeCheck][SinkVerify+0.5s] Auth=%d | RelZ=%.2f (expected=%.2f, stillApplied=%d) | WorldZ=%.2f | MeshWorldZ-CapBottom=%.2f"),
				Self->HasAuthority() ? 1 : 0,
				NowRel.Z, ExpectedZSnap, bStillApplied ? 1 : 0,
				NowWorld.Z, NowWorld.Z - CapBot);
		}, 0.5f, false);
	}

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

	UE_LOG(LogTemp, Warning, TEXT("[BossSummon_LiveCodeCheck] OnSummonMontageEnded — %s, Interrupted=%d, ShouldLoop=%d"),
		*GetName(), bInterrupted ? 1 : 0, bShouldLoopSummonMontage ? 1 : 0);

	// [SummonMontageLoopV1] 시네마틱 동안 walk 가 끝나지 않게 다시 재생.
	//   Interrupted (=true) 면 외부에서 명시적으로 정지한 거라 (HandleCinematicCompletedServer)
	//   루프하지 말고 cleanup 진행.
	if (bShouldLoopSummonMontage && !bInterrupted)
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				constexpr float BossSummonExtraPlayRate = 0.5f;
				AnimInst->Montage_Play(SummonMontage, BossSummonExtraPlayRate);
				UE_LOG(LogTemp, Warning,
					TEXT("[BossSummon_LiveCodeCheck] SummonMontage looped (cinematic still active)"));
				return;
			}
		}
	}

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

		// [SummonMontageMeshSinkV1] 적용했던 Z 오프셋 복원 — 캐릭터 BP 기본값으로 되돌림.
		if (bSummonMontageMeshOffsetApplied)
		{
			const FVector PreRel = SkelMesh->GetRelativeLocation();
			SkelMesh->SetRelativeLocation(FVector(PreRel.X, PreRel.Y, SavedMeshRelativeZ));
			const FVector PostRel = SkelMesh->GetRelativeLocation();
			bSummonMontageMeshOffsetApplied = false;
			UE_LOG(LogTemp, Warning,
				TEXT("[BossSummon_LiveCodeCheck][SinkRestore] Auth=%d | RelZ %.2f -> %.2f (target=%.2f) | match=%d"),
				HasAuthority() ? 1 : 0,
				PreRel.Z, PostRel.Z, SavedMeshRelativeZ,
				FMath::IsNearlyEqual(PostRel.Z, SavedMeshRelativeZ, 0.05f) ? 1 : 0);
		}
	}

	if (HasAuthority())
	{
		OnSummonMontageFinished.ExecuteIfBound();
	}
}

// ============================================================
// SetCinematicGateUnlocked — [BossCinematicGateV1]
//   소환 시네마틱 종료 게이트. 태그 부여/제거로 STEvaluator_BossTarget 의 idle 게이트 제어.
// ============================================================
void AHellunaEnemyCharacter_Boss::SetCinematicGateUnlocked(bool bUnlocked)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC) return;

	if (bUnlocked)
	{
		ASC->AddLooseGameplayTag(HellunaGameplayTags::State_Boss_CinematicReady);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(HellunaGameplayTags::State_Boss_CinematicReady);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossCinematicGateV1] %s cinematic gate %s"),
		*GetName(), bUnlocked ? TEXT("UNLOCKED — boss may act") : TEXT("LOCKED — boss idle"));
}

// ============================================================
// [BossArmorBreakV1] 페이즈2 — 본체 메쉬 swap + 갑옷 폭발 분리
// ============================================================
void AHellunaEnemyCharacter_Boss::Phase2_BreakArmor()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// [BossArmorBreak_Diag] 진입 시점 데이터 진단 — CDO 가 정상 반영됐는지 확인용.
	UE_LOG(LogTemp, Warning,
		TEXT("[BossArmorBreak_Diag] ENTER %s NetMode=%d ArmorMeshes.Num=%d BodyNull=%d KeepArmors=%d"),
		*GetNameSafe(this),
		(int32)GetNetMode(),
		Phase2ArmorMeshes.Num(),
		Phase2BodyMesh.IsNull() ? 1 : 0,
		Phase2KeepArmorMeshes.Num());

	// Dedicated 서버는 비주얼 의미 없음 — 완전 skip (스폰 비용 0).
	if (GetNetMode() == NM_DedicatedServer) return;

	USkeletalMeshComponent* BossMesh = GetMesh();

	// 1) 본체 메쉬를 갑옷 벗겨진 SK 로 swap. 같은 스켈레톤이라 진행 중 애니/몽타주 그대로 유지됨.
	if (BossMesh && !Phase2BodyMesh.IsNull())
	{
		USkeletalMesh* Body = Phase2BodyMesh.LoadSynchronous();
		if (Body)
		{
			BossMesh->SetSkeletalMeshAsset(Body);
			UE_LOG(LogTemp, Warning,
				TEXT("[BossArmorBreakV1] Body mesh swapped to %s on %s (NetMode=%d)"),
				*GetNameSafe(Body), *GetNameSafe(this), (int32)GetNetMode());

			// [BossDeathCullFixV1] SK_body 는 physics asset 이 없어 (1페이즈 SK_FuturisticWarrior 와
			//   달리) 렌더 bounds 가 RefreshBoneTransforms 시에만 갱신된다. 메시 컴포넌트의
			//   VisibilityBasedAnimTickOption 기본값이 OnlyTickMontagesWhenNotRendered 라서,
			//   사망 클로즈업처럼 좁은 frustum 에서 한 프레임만 컬링돼도 → 본 갱신 중단 → bounds 가
			//   옛 위치에 stale → frustum 밖 → 계속 컬링되는 악순환에 빠진다 (보스가 통째로 안 보임,
			//   넓은 카메라가 잡아줘야 풀림 = "카메라 거리에 따라 사라짐"). AlwaysTickPoseAndRefreshBones
			//   로 항상 bounds 를 갱신해 악순환을 끊는다. 보스는 1개 액터라 비용 무시 가능.
			BossMesh->VisibilityBasedAnimTickOption =
				EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

			// [BossDeathBoundsFixedV1] 사망 애니 빠른 모션 중 1-프레임 bounds stale 로 메인 뷰
			//   per-view 컬링이 발생하는 문제 차단:
			//   (1) bComponentUseFixedSkelBounds=true → 본 포즈 기반 매 프레임 재계산을 끄고
			//       에셋의 ref-pose bounds 를 고정 사용. 게임/렌더 스레드 sync lag 영향 사라짐.
			//   (2) BoundsScale=3 → 그 고정 bounds 를 3 배 패드. 사망 자세에서 팔/다리가 ref-pose
			//       extent 를 벗어나도 안전 마진.
			//   둘 다 셋업한 뒤 MarkRenderStateDirty 로 즉시 반영.
			BossMesh->bComponentUseFixedSkelBounds = true;
			BossMesh->SetBoundsScale(3.f);
			BossMesh->MarkRenderStateDirty();

			// [BossBerserkSkinV2] 피부 머터리얼은 Phase2BerserkSkinDelay 초 후 적용.
			//   VFX 생성 후 약간 뒤에 피부 변화 → 시각적 임팩트.
			if (!Phase2BerserkSkinMaterial.IsNull())
			{
				TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelf(this);
				FTimerHandle SkinTimer;
				GetWorldTimerManager().SetTimer(SkinTimer,
					FTimerDelegate::CreateLambda([WeakSelf]()
					{
						AHellunaEnemyCharacter_Boss* Self = WeakSelf.Get();
						if (!Self) return;
						USkeletalMeshComponent* SK = Self->GetMesh();
						if (!SK) return;
						UMaterialInterface* SkinMat = Self->Phase2BerserkSkinMaterial.LoadSynchronous();
						if (SkinMat)
						{
							SK->SetMaterialByName(FName("body2"), SkinMat);
							UE_LOG(LogTemp, Warning,
								TEXT("[BossBerserkSkinV2] body2 slot → %s (delayed)"), *SkinMat->GetName());
						}
					}),
					FMath::Max(0.f, Phase2BerserkSkinDelay), false);
			}
		}
	}

	// 1.5) [BossArmorBreakV2] 본체 swap 후 keep 갑옷들을 LeaderPose 로 attach
	//      → 같은 스켈레톤이라 보스 자세 그대로 따라감 (다리/허리 갑옷 보존, 시각적 자연스러움 유지).
	if (BossMesh && Phase2KeepArmorMeshes.Num() > 0)
	{
		// 기존 attached 정리 (재진입 안전)
		for (USkeletalMeshComponent* Old : Phase2AttachedArmorComps)
		{
			if (Old) Old->DestroyComponent();
		}
		Phase2AttachedArmorComps.Reset();

		int32 KeepSpawned = 0;
		for (const TSoftObjectPtr<USkeletalMesh>& Soft : Phase2KeepArmorMeshes)
		{
			USkeletalMesh* KeepMesh = Soft.LoadSynchronous();
			if (!KeepMesh) continue;

			USkeletalMeshComponent* AttachComp = NewObject<USkeletalMeshComponent>(this);
			if (!AttachComp) continue;
			AttachComp->SetSkeletalMeshAsset(KeepMesh);
			AttachComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			AttachComp->bReceivesDecals = false;
			AttachComp->SetCanEverAffectNavigation(false);
			AttachComp->bDisableClothSimulation = true;
			AttachComp->SetupAttachment(BossMesh);
			// [BossDeathCullFixV2] keep-armor(바지/다리갑옷) 도 physics asset 이 없어 SK_body 와
			//   동일한 bounds 컬링 악순환에 빠진다 — 사망 클로즈업에서 바지만 사라짐. 부모(SK_body)
			//   의 bounds 를 그대로 쓰게 해 부모와 함께 컬링되도록 → SK_body 가 안 컬링되니 바지도
			//   안 사라진다. (keep-armor 는 LeaderPose 로 부모 자세를 따라가므로 부모 bounds 안에 들어옴)
			AttachComp->bUseAttachParentBound = true;
			AttachComp->RegisterComponent();
			AttachComp->AttachToComponent(BossMesh, FAttachmentTransformRules::KeepRelativeTransform);
			AttachComp->SetLeaderPoseComponent(BossMesh, /*bForceUpdate*/ true);
			Phase2AttachedArmorComps.Add(AttachComp);
			++KeepSpawned;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[BossArmorBreakV2] Attached %d keep-armor pieces to boss mesh"), KeepSpawned);
	}

	// [BossArmorBreakV3] 갑옷 분리는 피부색 timer 와 같은 시점 (Phase2BerserkSkinDelay 후) 에 호출.
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfArmor(this);
		FTimerHandle ArmorSpawnTimer;
		GetWorldTimerManager().SetTimer(ArmorSpawnTimer,
			FTimerDelegate::CreateLambda([WeakSelfArmor]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakSelfArmor.Get();
				if (!Self) return;
				Self->Phase2_SpawnArmorPieces();
			}),
			FMath::Max(0.f, Phase2BerserkSkinDelay), false);
	}
}

// ============================================================
// [BossArmorBreakV3] 갑옷 spawn (Phase2BerserkSkinDelay 후 호출)
// ============================================================
void AHellunaEnemyCharacter_Boss::Phase2_SpawnArmorPieces()
{
	UWorld* World = GetWorld();
	if (!World) return;
	if (GetNetMode() == NM_DedicatedServer) return;

	const int32 N = Phase2ArmorMeshes.Num();
	if (N <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossArmorBreakV3] No armor meshes configured — skipping spawn"));
		return;
	}

	const FVector BossLoc = GetActorLocation() + FVector(0.f, 0.f, Phase2ArmorSpawnZOffset);
	const FRotator BossRot = GetActorRotation();
	int32 Spawned = 0;

	for (int32 i = 0; i < N; ++i)
	{
		USkeletalMesh* ArmorMesh = Phase2ArmorMeshes[i].LoadSynchronous();
		if (!ArmorMesh) continue;

		// 갑옷 별 분산 임펄스 — 균등 분포 + 약간 랜덤 + 위쪽
		const float BaseAngle = (2.f * PI / FMath::Max(1, N)) * static_cast<float>(i);
		const float Angle = BaseAngle + FMath::FRandRange(-0.4f, 0.4f);
		const FVector LateralDir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const FVector Velocity =
			LateralDir * (Phase2ArmorBlastSpeed * FMath::FRandRange(0.7f, 1.3f))
			+ FVector(0.f, 0.f, Phase2ArmorUpwardSpeed * FMath::FRandRange(0.6f, 1.4f));

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = this;
		Params.ObjectFlags = RF_Transient;

		ASkeletalMeshActor* Piece = World->SpawnActor<ASkeletalMeshActor>(
			ASkeletalMeshActor::StaticClass(), BossLoc, BossRot, Params);
		if (!Piece) continue;

		// SkeletalMeshActor 기본 mobility 가 Stationary 라 ProjectileMovement 작동 위해 Movable 강제.
		USkeletalMeshComponent* PieceMesh = Piece->GetSkeletalMeshComponent();
		if (PieceMesh)
		{
			PieceMesh->SetMobility(EComponentMobility::Movable);
			PieceMesh->SetSkeletalMeshAsset(ArmorMesh);
			PieceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 가벼움 + 플레이어와 충돌 안 함
			PieceMesh->bReceivesDecals = false;
			PieceMesh->SetCanEverAffectNavigation(false);
			PieceMesh->bDisableClothSimulation = true;
		}
		Piece->SetActorEnableCollision(false);
		Piece->SetReplicates(false); // Multicast 안에서 모든 머신이 로컬 spawn → replicate 불필요.

		// 중력+초기속도 단순 물리 (PhysicsAsset 없이도 동작).
		UProjectileMovementComponent* PMC = NewObject<UProjectileMovementComponent>(Piece);
		if (PMC)
		{
			PMC->bRotationFollowsVelocity = false;
			PMC->ProjectileGravityScale = Phase2ArmorGravityScale;
			PMC->bShouldBounce = false;
			PMC->Friction = 0.f;
			PMC->InitialSpeed = Velocity.Size();
			PMC->MaxSpeed = 0.f; // 0 = 무제한
			PMC->Velocity = Velocity;
			PMC->SetUpdatedComponent(Piece->GetRootComponent());
			PMC->RegisterComponent();
			PMC->Velocity = Velocity; // RegisterComponent 후에도 한 번 더 (UpdatedComponent 변경 시 reset 방지)
			PMC->Activate(true);
		}

		// 자전 (각 축 ± 랜덤)
		URotatingMovementComponent* RMC = NewObject<URotatingMovementComponent>(Piece);
		if (RMC)
		{
			const float DegPerSec = Phase2ArmorMaxSpinRPS * 360.f;
			const FRotator Spin(
				FMath::FRandRange(-DegPerSec, DegPerSec),
				FMath::FRandRange(-DegPerSec, DegPerSec),
				FMath::FRandRange(-DegPerSec, DegPerSec));
			RMC->RotationRate = Spin;
			RMC->bRotationInLocalSpace = false;
			RMC->RegisterComponent();
		}

		// [BossArmorPersistV1] Phase2ArmorLifetime > 0: 시간 후 자동 destroy (기존 동작).
		//                     Phase2ArmorLifetime = 0: 영구 유지 — Phase2ArmorFallDuration 후 PMC/RMC 정지 + ground snap.
		if (Phase2ArmorLifetime > KINDA_SMALL_NUMBER)
		{
			Piece->SetLifeSpan(FMath::Max(0.3f, Phase2ArmorLifetime));
		}
		else
		{
			TWeakObjectPtr<ASkeletalMeshActor> WeakPiece(Piece);
			FTimerHandle StopTimer;
			World->GetTimerManager().SetTimer(StopTimer,
				FTimerDelegate::CreateLambda([WeakPiece]()
				{
					ASkeletalMeshActor* P = WeakPiece.Get();
					if (!IsValid(P)) return;

					// PMC/RMC 정지 — 자전/낙하 freeze.
					if (UProjectileMovementComponent* PMC = P->FindComponentByClass<UProjectileMovementComponent>())
					{
						PMC->SetActive(false);
					}
					if (URotatingMovementComponent* RMC = P->FindComponentByClass<URotatingMovementComponent>())
					{
						RMC->SetActive(false);
					}

					// Ground LineTrace — 갑옷 위치 아래로 5000cm 까지 검색해서 World static 에 충돌하면 snap.
					UWorld* W = P->GetWorld();
					if (!W) return;
					FHitResult Hit;
					const FVector Start = P->GetActorLocation();
					const FVector End = Start - FVector(0.f, 0.f, 5000.f);
					FCollisionQueryParams Qp(SCENE_QUERY_STAT(BossArmorGroundSnap), false, P);
					if (W->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Qp))
					{
						FVector NewLoc = Hit.Location;
						NewLoc.Z += 3.f; // 살짝 띄워 z-fight 방지
						P->SetActorLocation(NewLoc);
					}

					// [BossArmorPersistV1] 정지된 SK 의 anim tick / skinning 갱신 비활성 — 오픈월드 부담 최소화.
					if (USkeletalMeshComponent* SK = P->GetSkeletalMeshComponent())
					{
						SK->SetComponentTickEnabled(false);
						SK->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
					}
					// 영구 유지 — SetLifeSpan 호출 안 함.
				}),
				FMath::Max(0.3f, Phase2ArmorFallDuration), false);
		}
		++Spawned;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossArmorBreakV1] Phase2_BreakArmor — spawned %d/%d armor pieces (NetMode=%d)"),
		Spawned, N, (int32)GetNetMode());
}

// ============================================================
// [Phase2CamInterpV1] 광폭화 시네마틱 — 카메라 위→아래 lerp
// ============================================================
void AHellunaEnemyCharacter_Boss::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// [SummonMontageMeshSinkV1+TickGuard] SummonMontage 동안 mesh.RelZ 가 우리가 set 한 값과 다르면
	//   매 tick 강제 재적용 + "처음 어긋난 시점" 한 번 로그.
	//   클라에서 누군가 0.5초 내 reset 한다는 verify 로그를 잡기 위해.
	if (bSummonMontageMeshOffsetApplied)
	{
		if (USkeletalMeshComponent* SkelMesh_Guard = GetMesh())
		{
			const FVector CurRel = SkelMesh_Guard->GetRelativeLocation();
			const float ExpectedZ = SavedMeshRelativeZ + SummonMontageMeshZOffset;
			if (!FMath::IsNearlyEqual(CurRel.Z, ExpectedZ, 0.05f))
			{
				if (!bSinkTickGuardLoggedDeviation)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[BossSummon_LiveCodeCheck][SinkTickGuard] Auth=%d | DEVIATION at WorldTime=%.2f | RelZ=%.4f expected=%.4f delta=%.4f — 누군가 외부에서 reset 함, 강제 재적용"),
						HasAuthority() ? 1 : 0, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
						CurRel.Z, ExpectedZ, CurRel.Z - ExpectedZ);
					bSinkTickGuardLoggedDeviation = true;
				}
				SkelMesh_Guard->SetRelativeLocation(FVector(CurRel.X, CurRel.Y, ExpectedZ));
			}
		}
	}

	// [BossDeathMeshLiftV1] 사망 몽타주 동안 SkelMesh.RelativeLocation.Z 를 0 → 오프셋으로 lerp.
	//   Multicast_StartDeathMeshLift 가 set 하면 lerp duration 까지 lerp, 그 후 hold (보스는 dissolve 됨).
	if (bDeathMeshLiftActive)
	{
		if (USkeletalMeshComponent* SkelMesh_Lift = GetMesh())
		{
			DeathMeshLiftElapsed += DeltaTime;
			// [BossDeathMeshLiftV3] 사망 애니 수직 프로파일 대응 — 부드러운 bump 커브.
			//   사용자 측정: 몽타주 53~75% 구간에 몸이 땅에 박힘 → 그 구간 풀 오프셋 필요.
			//   75% 이후엔 +오프셋이 공중부양으로 보임 → 다시 0 으로 내림.
			//   모든 구간 SmoothStep (C1 연속) — 속도 불연속이 없어 메시가 "툭" 튀어 두 번
			//   쓰러지는 듯한 hitch 가 안 생김. (선형 꺾임 V2 는 53% 에서 급정지 → hitch 발생)
			//     0~35%  : 0 → 중간값(풀의 60%) 부드럽게 상승
			//     35~43% : 중간값 → 풀 부드럽게 상승
			//     43~58% : 풀 오프셋 유지 (침몰 구간)
			//     58~76% : 풀 → 0 부드럽게 하강 (최종 자세 공중부양 제거)
			//     76%~   : 0
			//   (DeathMontageMeshLiftDuration 은 미사용 — 진행률 기반으로 대체)
			const float MontageLen = (DeathMontage != nullptr)
				? FMath::Max(0.1f, DeathMontage->GetPlayLength()) : 2.333f;
			const float M = FMath::Clamp(DeathMeshLiftElapsed / MontageLen, 0.f, 1.f);
			const float FullOffset = DeathMontageMeshZOffset;
			const float MidOffset = FullOffset * 0.6f;
			float CurOffset;
			if (M < 0.35f)
			{
				CurOffset = MidOffset * FMath::SmoothStep(0.f, 1.f, M / 0.35f);
			}
			else if (M < 0.43f)
			{
				CurOffset = FMath::Lerp(MidOffset, FullOffset, FMath::SmoothStep(0.f, 1.f, (M - 0.35f) / 0.08f));
			}
			else if (M < 0.58f)
			{
				CurOffset = FullOffset;
			}
			else if (M < 0.76f)
			{
				CurOffset = FMath::Lerp(FullOffset, 0.f, FMath::SmoothStep(0.f, 1.f, (M - 0.58f) / 0.18f));
			}
			else
			{
				CurOffset = 0.f;
			}
			// [BossDeathSlopeAlignV1] 기존 Z bump 를 적용한 "베이스" 상대위치.
			//   시작 시점 백업값에서 매번 계산 — 회전을 매 tick 덮어써도 X,Y 오염 안 됨.
			FVector BaseRel = SavedDeathMeshRelLoc;
			BaseRel.Z = SavedDeathMeshRelZ + CurOffset;

			if (bDeathSlopeAlignEnabled && bDeathGroundCaptured && DeathSlopeAngleDeg > 0.5f)
			{
				// 누운 몸을 "지면 접촉점" 기준으로 지형 법선에 맞춰 기울인다.
				//   정렬 알파: 쓰러지는 동안 0→1 ramp 후 끝까지 hold (Z bump 와 달리 경사 시체는 계속 기울어야 함).
				//   기울기 = min(실측 경사각, 최대각) * 강도 * 알파.
				const float AlignAlpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(M / 0.58f, 0.f, 1.f));
				const float TiltDeg = FMath::Min(DeathSlopeAngleDeg, DeathSlopeMaxAlignDeg) * DeathSlopeAlignStrength * AlignAlpha;

				const FVector Axis = FVector::CrossProduct(FVector::UpVector, DeathGroundNormal).GetSafeNormal();
				if (!Axis.IsNearlyZero() && TiltDeg > KINDA_SMALL_NUMBER)
				{
					const FQuat TiltQuat(Axis, FMath::DegreesToRadians(TiltDeg));

					const FTransform ActorXf = GetActorTransform();
					const FQuat ActorQ = ActorXf.GetRotation();
					// 베이스(Z bump 적용) 메시 월드 트랜스폼.
					const FVector BaseWorldLoc = ActorXf.TransformPosition(BaseRel);
					const FQuat   BaseWorldQ   = ActorQ * SavedDeathMeshRelRot.Quaternion();
					// 지면 접촉점을 고정점으로 회전 → 몸이 경사면에 밀착.
					const FVector NewWorldLoc = DeathGroundPoint + TiltQuat.RotateVector(BaseWorldLoc - DeathGroundPoint);
					const FQuat   NewWorldQ   = TiltQuat * BaseWorldQ;
					// 다시 상대 트랜스폼으로 변환해 적용.
					const FVector NewRelLoc = ActorXf.InverseTransformPosition(NewWorldLoc);
					const FQuat   NewRelQ   = ActorQ.Inverse() * NewWorldQ;
					SkelMesh_Lift->SetRelativeLocationAndRotation(NewRelLoc, NewRelQ);
				}
				else
				{
					SkelMesh_Lift->SetRelativeLocation(BaseRel);
				}
			}
			else
			{
				SkelMesh_Lift->SetRelativeLocation(BaseRel);
			}
			// Alpha 1 도달 후에도 매 tick 같은 값으로 hold — 외부 reset 방지 (SummonSinkTickGuard 와 동일 컨셉).

			// [BossDeathDiagV1] 죽음 몬타지~dissolve 전 구간 0.5초 간격 진단 — 패키지에서만 피부가
			//   사라지는 #5 버그용. 각 머신(서버/클라). visibility/렌더링여부/슬롯 머티리얼 추적 →
			//   IsVisible=1 인데 Rendered=0 이면 컬링/클립 문제, 둘 다 1 인데 안 보이면 머티리얼 문제.
			if (FMath::FloorToInt(DeathMeshLiftElapsed * 2.f) != FMath::FloorToInt((DeathMeshLiftElapsed - DeltaTime) * 2.f))
			{
				const TCHAR* NM = TEXT("?");
				switch (GetNetMode())
				{
				case NM_Standalone:      NM = TEXT("Standalone"); break;
				case NM_Client:          NM = TEXT("Client");     break;
				case NM_ListenServer:    NM = TEXT("Listen");     break;
				case NM_DedicatedServer: NM = TEXT("DedSrv");     break;
				default: break;
				}
				FString SlotMats;
				const int32 NumMat = SkelMesh_Lift->GetNumMaterials();
				for (int32 mi = 0; mi < NumMat; ++mi)
				{
					UMaterialInterface* SlotMat = SkelMesh_Lift->GetMaterial(mi);
						// [BossDeathDiagV2] 슬롯별 dissolve "Animation" 파라미터 실측값도 기록 —
						//   피부가 투명해지는 순간 무엇이 Animation 을 0 보다 올리는지 추적.
						//   Animation 이 계속 0 인데도 투명하면 머티리얼 외 원인.
						float AnimVal = -999.f;
						if (SlotMat)
						{
							SlotMat->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Animation")), AnimVal);
						}
						SlotMats += FString::Printf(TEXT("[%d]%s(Anim=%.2f) "), mi, *GetNameSafe(SlotMat), AnimVal);
				}
				UE_LOG(LogTemp, Warning,
					TEXT("[BossDeathDiag][%s] t=%.2f ActorHidden=%d MeshVisible=%d VisFlag=%d Rendered=%d bInPhase2=%d Slots=%d Mats=%s"),
					NM, DeathMeshLiftElapsed,
					IsHidden() ? 1 : 0,
					SkelMesh_Lift->IsVisible() ? 1 : 0,
					SkelMesh_Lift->GetVisibleFlag() ? 1 : 0,
					SkelMesh_Lift->WasRecentlyRendered(0.5f) ? 1 : 0,
					bInPhase2 ? 1 : 0, NumMat, *SlotMats);
			}
		}
	}

	// [InertiaTickV1] Montage section 변화 감지 시 ABP 의 Inertialization 노드에 RequestInertialization.
	//   anim_hit ↔ stun_start ↔ stun_loop 같은 sequence 전환 시 자동 inertia blend.
	//   Section 분리 권장 — 같은 section 안 segment 전환은 position 연속이라 못 잡음.
	if (USkeletalMeshComponent* SkelMesh_Inertia = GetMesh())
	{
		if (UAnimInstance* AnimInst = SkelMesh_Inertia->GetAnimInstance())
		{
			UAnimMontage* CurMontage = AnimInst->GetCurrentActiveMontage();
			FName CurSection = NAME_None;
			if (CurMontage)
			{
				if (FAnimMontageInstance* Inst = AnimInst->GetActiveInstanceForMontage(CurMontage))
				{
					CurSection = Inst->GetCurrentSection();
				}
			}
			const bool bMontageChanged = (CurMontage != LastTrackedMontage.Get());
			const bool bSectionChanged = (CurSection != LastTrackedMontageSection);
			if ((bMontageChanged || bSectionChanged) && CurMontage && CurSection != NAME_None)
			{
				AnimInst->RequestSlotGroupInertialization(FName(TEXT("DefaultGroup")), 0.4f);
				UE_LOG(LogTemp, Verbose,
					TEXT("[InertiaTickV1] Inertia request — Montage=%s Section=%s (prev=%s)"),
					*CurMontage->GetName(), *CurSection.ToString(),
					*LastTrackedMontageSection.ToString());
			}
			LastTrackedMontage = CurMontage;
			LastTrackedMontageSection = CurSection;
		}
	}

	// [Phase2DescentNCV1] 페이즈2 전엔 'Phase2Descent'(회오리) / 'Phase2Laser'(레이저) Tag NiagaraComponent 비활성.
	//   보스 BP 컴포넌트 이름이 한글이라 GetName 매칭 회피 — ComponentTag 로 안전 식별.
	//   [Phase2LaserSplitV1] 레이저는 별도 컴포넌트('Phase2Laser')로 분리 → 둘 다 비활성.
	if (!bInPhase2)
	{
		TArray<UNiagaraComponent*> NCs;
		GetComponents<UNiagaraComponent>(NCs);
		for (UNiagaraComponent* NC : NCs)
		{
			if (!NC) continue;
			const bool bIsDescentOrLaser =
				NC->ComponentTags.Contains(FName(TEXT("Phase2Descent"))) ||
				NC->ComponentTags.Contains(FName(TEXT("Phase2Laser")));
			if (!bIsDescentOrLaser) continue;
			if (NC->IsActive())
			{
				NC->Deactivate();
			}
		}
	}

	// [Phase2HealthFillV2] HP fill state machine — 서버 권한만, SetHealth 가 자동 복제.
	//   Stage 1 (1→OldMax) → pause → Stage 3 (OldMax→NewMax) → done.
	if (HasAuthority() && Phase2HealthFillStage > 0 && Phase2HealthFillStage < 4 && HealthComponent)
	{
		Phase2HealthFillElapsed += DeltaTime;
		if (Phase2HealthFillStage == 1)
		{
			const float Dur = FMath::Max(0.1f, Phase2HealthFillStage1Duration);
			const float Alpha = FMath::Clamp(Phase2HealthFillElapsed / Dur, 0.f, 1.f);
			const float NewHP = FMath::Lerp(1.f, Phase2HealthFillOldMax, Alpha);
			HealthComponent->SetHealth(NewHP);
			if (Alpha >= 1.f)
			{
				Phase2HealthFillStage = 2;  // pause
				Phase2HealthFillElapsed = 0.f;
				UE_LOG(LogTemp, Warning,
					TEXT("[Phase2HealthFillV2] Stage1 done at %.0f → pause %.1fs"),
					Phase2HealthFillOldMax, Phase2HealthFillBreakthroughPause);
			}
		}
		else if (Phase2HealthFillStage == 2)
		{
			if (Phase2HealthFillElapsed >= Phase2HealthFillBreakthroughPause)
			{
				Phase2HealthFillStage = 3;  // Stage 2 — break-through
				Phase2HealthFillElapsed = 0.f;
				UE_LOG(LogTemp, Warning,
					TEXT("[Phase2HealthFillV2] Stage2 begin: %.0f → %.0f over %.1fs (break-through)"),
					Phase2HealthFillOldMax, Phase2HealthFillNewMax, Phase2HealthFillStage2Duration);
				// [Phase2HealthFillV3] HP 바 widget 의 회색 바 width 확장 시작 트리거.
				OnBossPhase2BreakThroughStart.Broadcast();
			}
		}
		else if (Phase2HealthFillStage == 3)
		{
			// [Phase2HealthFillV3] Stage 2 break-through — MaxHealth + Health 동시 lerp.
			//   SetMaxHealth(refill=true) 가 Internal_SetHealth(MaxHealth) 호출 → HP=MaxHealth 자동.
			//   OnRep_MaxHealth 가 회색 바 widget width 갱신 → 회색 바도 같이 커지는 효과.
			const float Dur = FMath::Max(0.1f, Phase2HealthFillStage2Duration);
			const float Alpha = FMath::Clamp(Phase2HealthFillElapsed / Dur, 0.f, 1.f);
			const float NewMaxNow = FMath::Lerp(Phase2HealthFillOldMax, Phase2HealthFillNewMax, Alpha);
			HealthComponent->SetMaxHealth(NewMaxNow, /*bRefillHealth=*/true);
			if (Alpha >= 1.f)
			{
				Phase2HealthFillStage = 4;  // done
				UE_LOG(LogTemp, Warning,
					TEXT("[Phase2HealthFillV3] Stage2 done — MaxHealth+HP filled to %.0f"), Phase2HealthFillNewMax);
			}
		}
	}

	// [Phase2DescentScaleV1] 광폭화 강하 VFX scale lerp — 시간 경과로 점점 커짐.
	if (bPhase2DescentScaling)
	{
		Phase2DescentScaleElapsed += DeltaTime;
		const float Duration = FMath::Max(static_cast<float>(KINDA_SMALL_NUMBER), Phase2DescentScaleDuration);
		const float t = FMath::Clamp(Phase2DescentScaleElapsed / Duration, 0.f, 1.f);
		const float Mult = FMath::Lerp(Phase2DescentScaleStartMult, Phase2DescentScaleEndMult, t);
		const int32 N = FMath::Min(Phase2DescentScalingNCs.Num(), Phase2DescentBaseScales.Num());
		for (int32 i = 0; i < N; ++i)
		{
			UNiagaraComponent* NC = Phase2DescentScalingNCs[i].Get();
			if (!NC) continue;
			// [Phase2DescentScaleV2] Z 는 base 그대로 — Z 가 커지면 emit origin 이 아래로 이동(VFX 자체 구조).
			//                       X/Y 만 lerp → 평면상 점점 커지는 효과.
			const FVector& Base = Phase2DescentBaseScales[i];
			NC->SetRelativeScale3D(FVector(Base.X * Mult, Base.Y * Mult, Base.Z));
		}
		if (t >= 1.f)
		{
			bPhase2DescentScaling = false;
		}
	}

	// [Phase2RefactorV1] 카메라 lerp 모두 트리거(BossPhase2CinematicTrigger)가 처리.
}

// ============================================================
// [Phase2StageV1] 단계 3 비주얼 묶음 — 카메라 위 도달 후 호출
// ============================================================
// [BossCloneGlowV1] 분신 SkeletalMesh 에 2페이즈 광폭화 발광 적용 — 보스 본인 발광(StartBerserkVisuals)과 동일.
void AHellunaEnemyCharacter_Boss::ApplyBerserkGlowToMesh(USkeletalMeshComponent* TargetMesh)
{
	// 데디케이티드 서버는 시각 효과 불필요
	if (GetNetMode() == NM_DedicatedServer) return;

	// 2페이즈가 아니면 발광 없음 (분신패턴은 2페이즈 전용이라 평상시엔 true)
	if (!bInPhase2) return;

	if (!IsValid(TargetMesh)) return;

	USkeletalMeshComponent* SelfMesh = GetMesh();
	const int32 MaterialCount = TargetMesh->GetNumMaterials();

	for (int32 i = 0; i < MaterialCount; ++i)
	{
		// 보스 본인이 현재 쓰는 머티리얼(광폭화 스킨 swap 포함)을 분신에 복사 → 외형 동기화
		if (IsValid(SelfMesh) && i < SelfMesh->GetNumMaterials())
		{
			if (UMaterialInterface* SrcMat = SelfMesh->GetMaterial(i))
			{
				TargetMesh->SetMaterial(i, SrcMat);
			}
		}

		// 복사한 머티리얼 위에 독립 MID 생성 후 광폭화 발광 파라미터 적용
		if (UMaterialInstanceDynamic* MID = TargetMesh->CreateAndSetMaterialInstanceDynamic(i))
		{
			MID->SetScalarParameterValue(TEXT("EnableBerserk"), 1.f);
			MID->SetVectorParameterValue(TEXT("BerserkColor"), BerserkGlowColor);
			MID->SetScalarParameterValue(TEXT("BerserkBoost"), FMath::Max(BerserkGlowBoost, 0.f));
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BossCloneGlowV1] 분신 발광 적용 — Mesh=%s Color=%s Boost=%.2f Slots=%d"),
		*GetNameSafe(TargetMesh), *BerserkGlowColor.ToString(), BerserkGlowBoost, MaterialCount);
}

void AHellunaEnemyCharacter_Boss::Phase2_PlayStage3Visuals(bool bImmediate)
{
	UWorld* World = GetWorld();
	if (!World) return;

	// [Phase2SkipFastForwardV1] 스턴 타이머 경로와 스킵 즉시발동 경로가 중복 실행되지 않도록 1회 가드.
	if (bPhase2Stage3Triggered) return;
	bPhase2Stage3Triggered = true;

	UE_LOG(LogTemp, Warning, TEXT("[Phase2StageV1] Stage 3 — visuals + enrage on %s (NetMode=%d, Immediate=%d)"),
		*GetNameSafe(this), (int32)GetNetMode(), bImmediate ? 1 : 0);

	// [Phase2SkipInvulnEndV1] 스킵(즉시 완료) — 무적과 카메라 쉐이크가 남지 않게 즉시 정리.
	if (bImmediate)
	{
		if (HasAuthority())
		{
			GetWorldTimerManager().ClearTimer(Phase2InvulnerabilityTimer);
			SetCanBeDamaged(true); // 변신 즉시 완료 → 바로 피격 가능
		}
		StopPhase2Shakes(); // 진행 중 쉐이크 + 반복 타이머 정리 (모든 머신). 신규 쉐이크 스케줄은 아래에서 생략.
		UE_LOG(LogTemp, Warning, TEXT("[Phase2SkipInvulnEndV1] 스킵 — 무적 즉시 해제 + 쉐이크 정리/생략"));
	}

	// [Phase2ShakeDelayV1] 카메라 쉐이크는 강하 VFX 보다 Phase2ShakeDelay 초 늦게 시작.
	//   레이저가 어느 정도 내려온 뒤(grow-in 후) 임팩트 쉐이크가 오도록 — 별도 지연 타이머.
	if (!bImmediate && Phase2TransitionShakeClass) // [Phase2SkipInvulnEndV1] 스킵 시 쉐이크 스케줄 생략
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakShake(this);
		FTimerHandle ShakeStartTimer;
		World->GetTimerManager().SetTimer(ShakeStartTimer,
			FTimerDelegate::CreateLambda([WeakShake]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakShake.Get();
				if (!Self) return;
				UWorld* W = Self->GetWorld();
				if (!W) return;

				// [Phase2ShakeRampV1] 카메라 쉐이크 — epicenter 가 하늘에서 보스로 lerp (거리 falloff ramp).
				Self->Phase2ShakeStartTimeSeconds = (float)W->GetTimeSeconds();
				Self->PlayPhase2RampingShake();

				if (Self->Phase2ShakeRepeatInterval > 0.f)
				{
					TWeakObjectPtr<AHellunaEnemyCharacter_Boss> Weak(Self);
					Self->GetWorldTimerManager().ClearTimer(Self->Phase2ShakeRepeatTimer);
					Self->GetWorldTimerManager().SetTimer(Self->Phase2ShakeRepeatTimer,
						FTimerDelegate::CreateLambda([Weak]()
						{
							if (AHellunaEnemyCharacter_Boss* S2 = Weak.Get())
							{
								S2->PlayPhase2RampingShake();
							}
						}),
						Self->Phase2ShakeRepeatInterval, true);
				}
			}),
			Phase2ShakeDelay, false);
	}

	// 발밑 충격파 VFX
	if (Phase2TransitionVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Phase2TransitionVFX,
			GetActorLocation(), GetActorRotation());
	}

	// 메테오 VFX
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
				Loc, FRotator(-90.f, 0.f, 0.f));
		}
	}

	// [Phase2EnrageDelayV1] 갑옷 벗김 + 피부(광폭화) 변화 + 오라는 레이저 강하보다 늦게 — 별도 지연 타이머.
	//   레이저(강하 VFX)는 이 함수 호출(Phase2StunDuration) 직후 바로 시작하지만,
	//   광폭화 외형 변화(StartBerserkVisuals + Phase2_BreakArmor + 오라)는 Phase2ArmorSkinDelay 초 뒤에 적용.
	//   [주의/CompV1] 이 딜레이(ArmorSkin/Shake/HealthFill)는 모두 stage3 시작(=Phase2StunDuration) 기준
	//   상대값이라, Phase2StunDuration 을 줄이면 갑옷/피부/쉐이크/체력 타이밍도 같이 당겨진다.
	//   "레이저만" 당기려면 줄인 만큼 각 딜레이를 늘려 절대 타이밍을 보정해야 한다(현재 CDO 가 그렇게 설정됨).
	{
		TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakEnrage(this);
		FTimerHandle EnrageVisualTimer;
		World->GetTimerManager().SetTimer(EnrageVisualTimer,
			FTimerDelegate::CreateLambda([WeakEnrage]()
			{
				AHellunaEnemyCharacter_Boss* Self = WeakEnrage.Get();
				if (!Self) return;

				// [Phase2EnrageDelayV2] 광폭화 진입(이동속도 + 몽타주) — 갑옷 메시 swap 직전에 호출.
				//   Phase2_BreakArmor 의 메시 swap 이 진행 중 몽타주를 재시작하므로,
				//   enrage 몽타주를 막 시작한 직후 swap 되게 같은 타이머에 묶어 재시작이 안 보이게 한다.
				if (Self->HasAuthority())
				{
					Self->EnterEnraged();
				}

				// BerserkGlow
				Self->StartBerserkVisuals(Self->BerserkGlowColor, Self->BerserkGlowBoost);

				// 본체 swap + 갑옷 분리
				Self->Phase2_BreakArmor();

				// 보스 상시 오라 VFX
				if (Self->Phase2AuraVFX && !Self->ActivePhase2AuraComp)
				{
					USkeletalMeshComponent* BossMesh = Self->GetMesh();
					UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAttached(
						Self->Phase2AuraVFX, BossMesh ? (USceneComponent*)BossMesh : (USceneComponent*)Self->GetRootComponent(),
						NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
						EAttachLocation::SnapToTarget, false);
					Self->ActivePhase2AuraComp = NC;
				}

				UE_LOG(LogTemp, Warning,
					TEXT("[Phase2EnrageDelayV1] 갑옷/피부/오라 적용 — 레이저 강하 %.1fs 후"), Self->Phase2ArmorSkinDelay);
			}),
			bImmediate ? 0.05f : Phase2ArmorSkinDelay, false); // [Phase2SkipFastForwardV1] 스킵 시 즉시(0 은 SetTimer 가 클리어 → 0.05)
	}

	// Phase2Descent tag NC 활성화 + scale lerp 시작 + lifetime timer
	{
		TArray<UNiagaraComponent*> NCs;
		GetComponents<UNiagaraComponent>(NCs);
		Phase2DescentScalingNCs.Reset();
		Phase2DescentBaseScales.Reset();
		int32 ActivatedCount = 0;
		for (UNiagaraComponent* NC : NCs)
		{
			if (!NC) continue;
			if (!NC->ComponentTags.Contains(FName(TEXT("Phase2Descent")))) continue;
			NC->Activate(true);
			// [Phase2LaserDelayV1] 레이저 이미터('Empty')는 끈 채로 시작 — 회오리만 먼저 등장.
			//   아래 타이머가 LaserEmitterDelay 초 뒤에 레이저 이미터를 따로 켠다.
			NC->SetEmitterEnable(FName(TEXT("Empty")), false);
			// [Phase2DescentSpeedV1] particle 시뮬 속도 배율 — fall speed 조정.
			NC->SetCustomTimeDilation(FMath::Clamp(Phase2DescentTimeDilation, 0.1f, 10.f));
			++ActivatedCount;
			if (NC->ComponentTags.Contains(FName(TEXT("NoScale"))))
			{
				continue;
			}
			Phase2DescentScalingNCs.Add(NC);
			const FVector BaseScale = NC->GetRelativeScale3D();
			Phase2DescentBaseScales.Add(BaseScale);
			// [Phase2SkipInstantVFXV1] 스킵 시 성장 생략 — X/Y 를 바로 EndMult(최종)로, Z 는 base 그대로(Tick 성장과 동일 규칙).
			if (bImmediate)
			{
				NC->SetRelativeScale3D(FVector(BaseScale.X * Phase2DescentScaleEndMult, BaseScale.Y * Phase2DescentScaleEndMult, BaseScale.Z));
			}
			else
			{
				NC->SetRelativeScale3D(BaseScale * Phase2DescentScaleStartMult);
			}
		}
		Phase2DescentScaleElapsed = 0.f;
		bPhase2DescentScaling = (!bImmediate && ActivatedCount > 0 && Phase2DescentScaleDuration > KINDA_SMALL_NUMBER);
		UE_LOG(LogTemp, Warning,
			TEXT("[Phase2DescentNCV1] Activated %d, scaling %.2f→%.2f over %.1fs (Immediate=%d)"),
			ActivatedCount, Phase2DescentScaleStartMult, Phase2DescentScaleEndMult, Phase2DescentScaleDuration, bImmediate ? 1 : 0);

		// [BossBGMV1] 회오리(Phase2Descent) 등장 시점 — 회오리 사운드 + 2페이즈 BGM 을 함께 시작.
		//   사용자 요청: 2페 BGM 을 시네마틱 시작이 아닌 '회오리 발생 시점'부터 재생.
		//   이 함수는 Multicast_PlayBossPhase2Transition 경로(모든 머신)에서 호출됨 — 각 머신 1회(데디 제외).
		if (ActivatedCount > 0 && !IsRunningDedicatedServer())
		{
			if (HurricaneSound)
			{
				UGameplayStatics::PlaySound2D(this, HurricaneSound);
			}
			if (BGMAudioComponent && Phase2BGM)
			{
				BGMAudioComponent->SetSound(Phase2BGM);
				BGMAudioComponent->Play();
			}
		}

		// [Phase2LaserSplitV1] 레이저 전용 컴포넌트('Phase2Laser') 셋업 — 회오리와 분리되어
		//   strike 종료 시 DeactivateImmediate 로 잔존(기존 빔)까지 즉시 제거 가능(=회오리 팝 없음).
		//   이 컴포넌트는 회오리 이미터(Empty001~006)를 끄고 레이저('Empty')만 쓴다. 레이저는 딜레이 후 ON.
		int32 LaserActivatedCount = 0;
		{
			static const FName TornadoEmitters[] = {
				FName(TEXT("Empty001")), FName(TEXT("Empty002")), FName(TEXT("Empty003")),
				FName(TEXT("Empty004")), FName(TEXT("Empty005")), FName(TEXT("Empty006")) };
			for (UNiagaraComponent* NC : NCs)
			{
				if (!NC) continue;
				if (!NC->ComponentTags.Contains(FName(TEXT("Phase2Laser")))) continue;
				NC->Activate(true);
				// 회오리 이미터는 끄고 레이저만 — 레이저도 딜레이 후 ON 이므로 'Empty' 일단 off.
				for (const FName& Em : TornadoEmitters)
				{
					NC->SetEmitterEnable(Em, false);
				}
				NC->SetEmitterEnable(FName(TEXT("Empty")), false);
				// [Phase2LaserSpeedV1] 레이저 전용 낙하 속도 — 회오리(Phase2DescentTimeDilation=0.4 슬로우)와
				//   독립. 분리된 컴포넌트라 회오리 가속버그 무관. 레이저 빔이 하늘에서 빨리 내려오도록
				//   회오리보다 빠른 시뮬 속도 사용("레이저가 천천히 떨어진다" 해결). 값은 룩 튜닝값.
				const float LaserTimeDilation = 0.8f; // 레이저 낙하 속도 (사용자 튜닝값)
				NC->SetCustomTimeDilation(FMath::Clamp(LaserTimeDilation, 0.1f, 10.f));
				++LaserActivatedCount;
			}
			UE_LOG(LogTemp, Warning,
				TEXT("[Phase2LaserSplitV1] 레이저 컴포넌트 셋업 %d (회오리 이미터 off, 레이저 딜레이 대기)"), LaserActivatedCount);
		}

		// [Phase2SkipInstantVFXV1] 스킵 — 강하 연출(성장+레이저 강하)을 생략하고 즉시 최종(aftermath) 상태로:
		//   회오리는 위에서 이미 풀스케일, 레이저는 켜지 않고 바로 off 확정. (레이저 on/lifetime 타이머 skip)
		if (bImmediate)
		{
			SwapDescentVFXToAftermath();
			UE_LOG(LogTemp, Warning, TEXT("[Phase2SkipInstantVFXV1] 스킵 — 강하 VFX 즉시 최종상태(풀스케일 + 레이저 off)"));
		}

		// [Phase2LaserDelayV1] 회오리(Phase2Descent)는 위에서 바로 등장 — 레이저 전용 컴포넌트
		//   (Phase2Laser)의 레이저 이미터('Empty')만 LaserEmitterDelay 초 뒤에 켠다.
		//   [Phase2LaserSplitV1] 대상이 회오리 컴포넌트가 아니라 레이저 컴포넌트로 변경됨.
		if (!bImmediate && LaserActivatedCount > 0)
		{
			TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakLaser(this);
			FTimerHandle LaserOnTimer;
			World->GetTimerManager().SetTimer(LaserOnTimer,
				FTimerDelegate::CreateLambda([WeakLaser]()
				{
					AHellunaEnemyCharacter_Boss* Self = WeakLaser.Get();
					if (!Self) return;
					TArray<UNiagaraComponent*> LNCs;
					Self->GetComponents<UNiagaraComponent>(LNCs);
					int32 LaserOnCount = 0;
					for (UNiagaraComponent* LNC : LNCs)
					{
						if (LNC && LNC->ComponentTags.Contains(FName(TEXT("Phase2Laser"))))
						{
							LNC->SetEmitterEnable(FName(TEXT("Empty")), true);
							++LaserOnCount;
						}
					}
					UE_LOG(LogTemp, Warning,
						TEXT("[Phase2LaserDelayV1] 레이저 컴포넌트 ON — 회오리 후 지연 (%d component)"), LaserOnCount);
				}),
				Phase2LaserEmitterDelay, false);
		}

		if (!bImmediate && ActivatedCount > 0 && Phase2DescentVFXLifetime > KINDA_SMALL_NUMBER)
		{
			TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelf(this);
			FTimerHandle DeactivateTimer;
			World->GetTimerManager().SetTimer(DeactivateTimer,
				FTimerDelegate::CreateLambda([WeakSelf]()
				{
					AHellunaEnemyCharacter_Boss* Self = WeakSelf.Get();
					if (!Self) return;
					// [Phase2DescentLifetimeV2] 수명 만료 시 강하 VFX 전체를 끄지 않고 레이저 이미터만 off.
					//   → 레이저는 시네마틱 종료보다 일찍 사라지고, 회오리는 그대로 루프 유지.
					//   (SwapDescentVFXToAftermath 가 레이저 이미터 비활성 + bPhase2DescentScaling=false 처리)
					Self->SwapDescentVFXToAftermath();
					UE_LOG(LogTemp, Warning,
						TEXT("[Phase2DescentLifetimeV2] 레이저 수명 만료 — 레이저 off, 회오리 유지"));

					// [Phase2ShakeCleanupV1] VFX 종료 시점에 카메라 쉐이크도 같이 정리.
					//   ShakeRepeatTimer 클리어 + 진행 중 instance fade out.
					if (UWorld* W2 = Self->GetWorld())
					{
						W2->GetTimerManager().ClearTimer(Self->Phase2ShakeRepeatTimer);
						if (Self->Phase2TransitionShakeClass)
						{
							for (FConstPlayerControllerIterator It = W2->GetPlayerControllerIterator(); It; ++It)
							{
								if (APlayerController* PC = It->Get())
								{
									if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
									{
										PCM->StopAllInstancesOfCameraShake(
											Self->Phase2TransitionShakeClass, /*bImmediately=*/false);
									}
								}
							}
							UE_LOG(LogTemp, Warning,
								TEXT("[Phase2ShakeCleanupV1] Shake repeat timer cleared + instances stopped"));
						}
					}
				}),
				Phase2DescentVFXLifetime, false);
		}
	}

	// [Phase2HealthFillV2] 강하 VFX 시작 후 Phase2HealthFillDelay 후 → 2-stage HP fill 시작.
	//   서버 권한만 — Tick 의 fill state machine 이 SetHealth 진행. SetHealth 가 자동 replicate.
	if (HasAuthority() && HealthComponent && Phase2HealthFillNewMax > 0.f && Phase2HealthFillStage == 0)
	{
		if (bImmediate)
		{
			// [Phase2SkipInstantFillV1] 스킵 — 점진 fill 대신 즉시 2페이즈 최대체력으로.
			//   SetMaxHealth(refill=true) → HP=MaxHealth 까지 채움. Stage=4(done): Stage 머신 비활성 +
			//   복제된 Stage>=3 으로 HP바가 ∞/∞ + 폭 확장 표시. 변신이 즉시 완료된 상태가 됨.
			HealthComponent->SetMaxHealth(Phase2HealthFillNewMax, /*bRefillHealth=*/true);
			Phase2HealthFillStage = 4;
			Phase2HealthFillElapsed = 0.f;
			UE_LOG(LogTemp, Warning,
				TEXT("[Phase2SkipInstantFillV1] 스킵 — HP/Max 즉시 %.0f 로 설정 (Stage=4, 점진 fill 생략)"),
				Phase2HealthFillNewMax);
		}
		else
		{
			TWeakObjectPtr<AHellunaEnemyCharacter_Boss> WeakSelfFill(this);
			FTimerHandle FillStartTimer;
			World->GetTimerManager().SetTimer(FillStartTimer,
				FTimerDelegate::CreateLambda([WeakSelfFill]()
				{
					AHellunaEnemyCharacter_Boss* Self = WeakSelfFill.Get();
					if (!Self || !Self->HealthComponent) return;
					Self->Phase2HealthFillStage = 1;  // Stage 1 시작 — 1 → OldMax
					Self->Phase2HealthFillElapsed = 0.f;
					UE_LOG(LogTemp, Warning,
						TEXT("[Phase2HealthFillV2] Stage1 begin: %.0f → %.0f over %.1fs"),
						Self->HealthComponent->GetHealth(), Self->Phase2HealthFillOldMax,
						Self->Phase2HealthFillStage1Duration);
				}),
				FMath::Max(0.0f, Phase2HealthFillDelay), false);
		}
	}
}

// [Phase2LaserSplitV1] strike(레이저 강하) 종료 시 — 레이저 전용 컴포넌트('Phase2Laser')를
//   DeactivateImmediate 로 즉시 정리. 레이저가 별도 컴포넌트라 회오리(Phase2Descent)와 완전히
//   분리되어, 컴포넌트 전체 reset 으로 기존 레이저 빔까지 잔존 없이 즉시 사라지고 회오리는 무영향.
//   (구버전: 같은 컴포넌트에서 SetEmitterEnable(false)+Laser_Scale=0 → spawn-only 라 기존 빔이
//    수명대로 ~3초 남던 문제를 컴포넌트 분리로 해결.)
void AHellunaEnemyCharacter_Boss::SwapDescentVFXToAftermath()
{
	TArray<UNiagaraComponent*> NCs;
	GetComponents<UNiagaraComponent>(NCs);

	int32 Count = 0;
	for (UNiagaraComponent* NC : NCs)
	{
		if (!NC) continue;
		if (!NC->ComponentTags.Contains(FName(TEXT("Phase2Laser")))) continue;

		// 레이저 컴포넌트만 즉시 종료 — 기존 빔 파티클까지 한 번에 제거. 회오리 컴포넌트는 그대로 루프.
		NC->DeactivateImmediate();
		++Count;
	}

	// 강하 VFX scale lerp 종료 — 회오리는 현재 스케일 그대로 계속 루프.
	bPhase2DescentScaling = false;

	UE_LOG(LogTemp, Warning,
		TEXT("[Phase2LaserSplitV1] 레이저 컴포넌트 DeactivateImmediate (잔존 빔 즉시 제거) — 회오리 유지, %d component(s)"),
		Count);
}

// [Phase2ShakeCleanupV2] 페이즈2 쉐이크 정리 — RepeatTimer + 진행 중 instance fade out.
void AHellunaEnemyCharacter_Boss::StopPhase2Shakes()
{
	UWorld* W = GetWorld();
	if (!W) return;

	W->GetTimerManager().ClearTimer(Phase2ShakeRepeatTimer);

	if (Phase2TransitionShakeClass)
	{
		int32 Stopped = 0;
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
				{
					PCM->StopAllInstancesOfCameraShake(Phase2TransitionShakeClass, /*bImmediately=*/false);
					++Stopped;
				}
			}
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[Phase2ShakeCleanupV2] StopPhase2Shakes — RepeatTimer cleared, instances fade-out on %d PC"),
			Stopped);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Phase2ShakeCleanupV2] StopPhase2Shakes — RepeatTimer cleared (no shake class)"));
	}
}

// [Phase2ShakeRampV1] 1회 쉐이크 발화 — epicenter 시간 lerp + 멀리서 시작 falloff.
//   [Phase2ShakeSuctionV1] bOrientShakeTowardsEpicenter=true — 흔들림 축이 epicenter 향함.
//   LocX 는 epicenter 방향 forward/back 흔들림 → 빨려들어가는 임팩트.
void AHellunaEnemyCharacter_Boss::PlayPhase2RampingShake()
{
	UWorld* W = GetWorld();
	if (!W || !Phase2TransitionShakeClass) return;

	const float Now = (float)W->GetTimeSeconds();
	const float Elapsed = Now - Phase2ShakeStartTimeSeconds;
	const float Alpha = FMath::Clamp(Elapsed / FMath::Max(Phase2ShakeDescentDuration, 0.1f), 0.f, 1.f);
	const float CurrentZ = FMath::Lerp(Phase2ShakeStartZ, Phase2ShakeEndZ, Alpha);
	const FVector Epicenter = GetActorLocation() + FVector(0.f, 0.f, CurrentZ);

	UGameplayStatics::PlayWorldCameraShake(
		W, Phase2TransitionShakeClass, Epicenter,
		Phase2ShakeInnerRadius, Phase2ShakeOuterRadius, Phase2ShakeFalloff,
		/*bOrientShakeTowardsEpicenter=*/true);

	// [Phase2ShakeRamp_Diag] 0.5s 마다 진단 로그 — spawn 시 CDO 값 + epicenter 추적.
	//   사용자 BP CDO 변경 적용 여부 검증용.
	{
		static float LastDiagLogTime = -1000.f;
		if (Now - LastDiagLogTime >= 0.5f)
		{
			LastDiagLogTime = Now;
			UCameraShakeBase* ShakeCDO = Phase2TransitionShakeClass
				? Phase2TransitionShakeClass->GetDefaultObject<UCameraShakeBase>() : nullptr;
			UE_LOG(LogTemp, Warning,
				TEXT("[Phase2ShakeRamp_Diag] T=%.2f Class=%s CDOScale=%.2f Alpha=%.2f Epicenter=%s OrientToEp=true"),
				Elapsed,
				*GetNameSafe(Phase2TransitionShakeClass),
				ShakeCDO ? ShakeCDO->ShakeScale : -1.f,
				Alpha,
				*Epicenter.ToString());
		}
	}
}
