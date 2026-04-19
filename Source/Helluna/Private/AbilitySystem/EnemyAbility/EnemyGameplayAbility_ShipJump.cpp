/**
 * EnemyGameplayAbility_ShipJump.cpp
 *
 * [ShipJumpV1] 우주선 상단 점프 테스트 GA.
 */

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_ShipJump.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"

UEnemyGameplayAbility_ShipJump::UEnemyGameplayAbility_ShipJump()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UEnemyGameplayAbility_ShipJump::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// [ShipJump.Diag] 첫 활성화 여부 추적 (프로세스 단위 전역).
	//   - 첫 점프에 의존성 로드/컴파일/셰이더 준비 등이 걸리면 여기만 렉
	static int32 GGlobalJumpActivationCount = 0;
	const int32 ThisActivationIndex = ++GGlobalJumpActivationCount;

	const double tActivateStart = FPlatformTime::Seconds();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	const double tSuperDone = FPlatformTime::Seconds();

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	const double tGotEnemy = FPlatformTime::Seconds();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const double tGotASC = FPlatformTime::Seconds();

	FGameplayTag AttackingTagValue;
	if (ASC)
	{
		// [JumpLagFix] ErrorIfNotFound=false — 네이티브/INI 등록이 어떤 이유로 유효하지
		// 않더라도 ensure+StackWalkAndDump(약 4s) 를 발생시키지 않게 방어.
		AttackingTagValue = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
	}
	const double tRequestTag = FPlatformTime::Seconds();

	if (ASC && AttackingTagValue.IsValid())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(AttackingTagValue);
		ASC->AddLooseGameplayTags(AttackingTag);
	}

	// [ShipJumpV7.StaleTagClear] 점프 시작 시점에 이전에 붙은 OnShip 태그 제거.
	// Why: 우주선에서 내려오는 로직이 명세되지 않아, 한번 OnShip 이 된 적이 지상으로 복귀해도
	//      태그가 남아있어 OnShip 분기로 오진하는 사례 방지.
	// How: ActivateAbility 시점 = 지상에서 점프를 시작하는 시점 → 태그가 있다면 확실히 stale.
	if (ASC)
	{
		const FGameplayTag OnShipTagForClear = FGameplayTag::RequestGameplayTag(FName("State.Enemy.OnShip"), false);
		if (OnShipTagForClear.IsValid() && ASC->HasMatchingGameplayTag(OnShipTagForClear))
		{
			FGameplayTagContainer OnShipContainer;
			OnShipContainer.AddTag(OnShipTagForClear);
			ASC->RemoveLooseGameplayTags(OnShipContainer);
			UE_LOG(LogTemp, Warning, TEXT("[ShipJumpV7.StaleTagClear] Enemy=%s — removed stale OnShip tag before jump"),
				*Enemy->GetName());
		}
	}

	const double tAddTag = FPlatformTime::Seconds();

	bHasLanded = false;

	const double tBeforeJump = FPlatformTime::Seconds();
	PerformJump();
	const double tAfterJump = FPlatformTime::Seconds();

	// [ShipJump.Diag] 중간 구간을 세분화 — 1.17s 렉이 어디서 발생하는지 범인 특정.
	UE_LOG(LogTemp, Warning,
		TEXT("[ShipJump.Diag][Activate] Idx=%d Enemy=%s  Super=%.2f  GetEnemy=%.2f  GetASC=%.2f  ReqTag=%.2f  AddTag=%.2f  Jump=%.2f  [Total=%.2f ms]"),
		ThisActivationIndex,
		*Enemy->GetName(),
		(tSuperDone  - tActivateStart) * 1000.0,
		(tGotEnemy   - tSuperDone     ) * 1000.0,
		(tGotASC     - tGotEnemy      ) * 1000.0,
		(tRequestTag - tGotASC        ) * 1000.0,
		(tAddTag     - tRequestTag    ) * 1000.0,
		(tAfterJump  - tBeforeJump    ) * 1000.0,
		(tAfterJump  - tActivateStart ) * 1000.0);
}

FVector UEnemyGameplayAbility_ShipJump::ComputeLaunchVelocityToShipTop(const AHellunaEnemyCharacter* Enemy, AActor* Ship) const
{
	// [ShipJumpV3.CenterAim] 우주선 중심 조준 — 기울어진 우주선의 날개 회피 목적.
	// Why: V2 는 고정 Vxy 라 어디서 점프하든 같은 거리만 이동 → 기울어진 우주선의 낮은
	//      날개 아래에서 점프 시 포물선 정점이 날개에 그대로 박힘.
	// How: Vxy = D_toShipCenter / T_toApex 로 계산 → 포물선 정점에서 XY 가 우주선
	//      중심 바로 위에 오도록. 자연스럽게 안쪽 위로 휘어 날개를 피함.
	//      Vxy 는 MaxHorizontalSpeed 로 상한 clamp (너무 멀면 초고속 방지).
	//      Ship 이 null 이거나 거리 0 이면 V2 폴백 (FallbackHorizontalSpeed + 전방).

	// 중력 (CMC 값, 폴백 980).
	float GravityZ = 980.f;
	if (const UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement())
	{
		GravityZ = FMath::Abs(CMC->GetGravityZ());
		if (GravityZ < KINDA_SMALL_NUMBER) GravityZ = 980.f;
	}

	// [ShipJumpV5.PerSideAdaptive] 접근 방향의 우주선 상단 고도 기반 per-side 정점 계산.
	// Why: V4 는 Bounds 전체 최상단(ShipOrigin.Z + Extent.Z) 을 사용 → 기울어진 우주선의
	//      LOW-wing 쪽에서 점프해도 HIGH-wing 높이까지 올라가 불필요하게 높음.
	//      유저 요구: 어느 방향에서 점프하냐에 따라 정점 높이가 달라야 함.
	// How: 착지 XY(= 엔티티와 우주선 중심 사이 중간점)에서 아래로 LineTrace → 해당
	//      지점의 실제 상단 Z 를 얻음. 기울어진 우주선의 LOW 쪽은 낮고 HIGH 쪽은 높음.
	//      트레이스 실패 시 V4 Bounds 방식으로 폴백.
	const float EnemyZ = Enemy->GetActorLocation().Z;
	float ShipTopZ = EnemyZ; // Ship 없으면 상대높이 0
	float BoundsTopZ = EnemyZ;
	if (Ship)
	{
		FVector ShipOrigin, ShipExtent;
		Ship->GetActorBounds(false, ShipOrigin, ShipExtent, true);
		BoundsTopZ = ShipOrigin.Z + ShipExtent.Z;
		ShipTopZ = BoundsTopZ; // 폴백값

		// 착지 예상 XY (Enemy → Ship 중심 경로의 중간점)
		FVector EnemyLoc = Enemy->GetActorLocation();
		FVector ShipLoc = Ship->GetActorLocation();
		FVector LandingXY = FVector(
			(EnemyLoc.X + ShipLoc.X) * 0.5f,
			(EnemyLoc.Y + ShipLoc.Y) * 0.5f,
			BoundsTopZ + 1500.f);

		if (UWorld* World = GetWorld())
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(ShipJumpApexProbe), true);
			Params.AddIgnoredActor(Enemy);
			const FVector TraceEnd = LandingXY - FVector(0.f, 0.f, 5000.f);
			if (World->LineTraceSingleByChannel(Hit, LandingXY, TraceEnd, ECC_WorldStatic, Params)
				&& Hit.GetActor() == Ship)
			{
				ShipTopZ = Hit.ImpactPoint.Z;
			}
		}
	}
	const float RequiredClimb = FMath::Max(ShipTopZ - EnemyZ, 0.f);
	// [ShipJumpV8.Apex] ApexClearance 를 UPROPERTY 로 노출 (과거 V7 하드 300 → 기본 500 으로 상향).
	// 상향 이유: 측면/날개 걸림 사고 감소. BP 에서 미세 튜닝 가능.
	const float EffectiveApexClearance = FMath::Max(ApexClearance, 50.f);
	// [ShipJumpV5.NoFloor] OvershootHeight 를 최저 보장치로 쓰지 않음 → per-side 트레이스 결과가 실제 Apex 에 반영됨.
	// 단 극단적으로 얕은 경우만 20cm 로 바닥 보호.
	const float ApexHeight = FMath::Max(RequiredClimb + EffectiveApexClearance, 20.f);
	const float LaunchVelocityZ = FMath::Sqrt(2.f * GravityZ * ApexHeight);
	const float TimeToApex = LaunchVelocityZ / GravityZ;

	// 우주선 중심까지 XY 거리/방향.
	FVector HorizDir = Enemy->GetActorForwardVector();
	float HorizDistance = 0.f;
	if (Ship)
	{
		FVector ToShip = Ship->GetActorLocation() - Enemy->GetActorLocation();
		ToShip.Z = 0.f;
		HorizDistance = ToShip.Size();
		if (HorizDistance > KINDA_SMALL_NUMBER)
		{
			HorizDir = ToShip / HorizDistance;
		}
	}

	// Vxy: **착지 시점** 에서 우주선 중심 XY 에 도달하도록.
	// 평지 기준 총 체공시간 = 2 * TimeToApex → Vxy = D / (2*T).
	// Why: D / T 는 정점 도달 공식 → 정점 이후에도 같은 Vxy 로 이동해 중심을 훨씬 지나침.
	//      D / (2T) 는 평지 착지 공식 — 기울어진 우주선 위에 착지하면 D 보다 짧게 내려
	//      중심 근처에 안착 (오버런 방지).
	// Ship 없거나 거의 붙어있으면 폴백 속도.
	float Vxy = FallbackHorizontalSpeed;
	if (HorizDistance > KINDA_SMALL_NUMBER && TimeToApex > KINDA_SMALL_NUMBER)
	{
		Vxy = FMath::Clamp(HorizDistance / (2.f * TimeToApex), 100.f, MaxHorizontalSpeed);
	}

	// [ShipJumpSpreadV2] 슬롯 인덱스 → 좌우 부채꼴 yaw 오프셋 + Vxy cos 보정.
	// Why V2: V1 은 Vxy 그대로 두고 yaw 회전만 → forward 도달거리 = D*cos(yaw) 로 짧아짐.
	//         반대로 측면 거리 = D*sin(yaw) 로 우주선 옆을 빗나가는 사례 발생 (광폭화 등 D 가 클 때 특히 심각).
	// How V2: yaw 만 회전한 뒤 Vxy 를 1/cos(yaw) 로 보정 → forward 거리는 항상 D 로 보존,
	//         측면 거리만 D*tan(yaw) 만큼 분산 → 우주선 중심 위 호에 안정 착지.
	//         보정 후 다시 MaxHorizontalSpeed 로 clamp 해 광폭화에도 폭주 방지.
	float YawOffsetDeg = 0.f;
	if (AssignedSlotIndex >= 0 && SpreadSlotCount > 1 && SpreadFanHalfAngleDeg > 0.f)
	{
		const int32 ClampedIdx = FMath::Clamp(AssignedSlotIndex, 0, SpreadSlotCount - 1);
		const float Step = (2.f * SpreadFanHalfAngleDeg) / static_cast<float>(SpreadSlotCount - 1);
		YawOffsetDeg = -SpreadFanHalfAngleDeg + Step * static_cast<float>(ClampedIdx);
		HorizDir = FRotator(0.f, YawOffsetDeg, 0.f).RotateVector(HorizDir);

		const float YawCos = FMath::Cos(FMath::DegreesToRadians(YawOffsetDeg));
		if (YawCos > KINDA_SMALL_NUMBER)
		{
			Vxy = FMath::Clamp(Vxy / YawCos, 100.f, MaxHorizontalSpeed);
		}
	}

	const FVector Launch = HorizDir * Vxy + FVector(0.f, 0.f, LaunchVelocityZ);

	UE_LOG(LogTemp, Warning,
		TEXT("[ShipJumpSpreadV2] Enemy=%s Ship=%s SlotIdx=%d YawOff=%.1f D=%.0f ShipTopZ=%.0f BoundsTop=%.0f Climb=%.0f Apex=%.0f Vz=%.0f T=%.2f Vxy=%.0f Dir=(%.2f,%.2f)"),
		*Enemy->GetName(), Ship ? *Ship->GetName() : TEXT("null"),
		AssignedSlotIndex, YawOffsetDeg,
		HorizDistance, ShipTopZ, BoundsTopZ, RequiredClimb, ApexHeight, LaunchVelocityZ, TimeToApex, Vxy, HorizDir.X, HorizDir.Y);

	return Launch;
}

void UEnemyGameplayAbility_ShipJump::PerformJump()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	// [ShipJump.Diag] 각 단계 타이밍 — 이상치 단계 식별
	const double t0 = FPlatformTime::Seconds();

	if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Falling);
	}

	// [ShipJumpAirCollisionV1] 공중 진입 직전 Pawn↔Pawn 캡슐 충돌 해제.
	EnableAirCollisionPassthrough();

	const double t1 = FPlatformTime::Seconds();

	AActor* Ship = CurrentTarget.Get();
	const FVector LaunchVelocity = ComputeLaunchVelocityToShipTop(Enemy, Ship);

	const double t2 = FPlatformTime::Seconds();

	// bXYOverride=true, bZOverride=true 로 기존 속도 덮어써서 포물선 정확도 확보.
	Enemy->LaunchCharacter(LaunchVelocity, true, true);

	const double t3 = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Warning,
		TEXT("[ShipJump.Diag][Phase] Enemy=%s  MoveMode=%.2f ms  ComputeLaunch=%.2f ms  LaunchChar=%.2f ms"),
		*Enemy->GetName(),
		(t1 - t0) * 1000.0,
		(t2 - t1) * 1000.0,
		(t3 - t2) * 1000.0);

	if (UWorld* World = Enemy->GetWorld())
	{
		// 착지 체크 타이머 (0.1초 반복, 도약 후 0.3초 대기 후 시작).
		World->GetTimerManager().SetTimer(
			LandingCheckTimerHandle,
			[this]()
			{
				AHellunaEnemyCharacter* E = GetEnemyCharacterFromActorInfo();
				if (!E) { OnLanded(); return; }

				UCharacterMovementComponent* MoveComp = E->GetCharacterMovement();
				if (MoveComp && MoveComp->IsMovingOnGround() && !bHasLanded)
				{
					OnLanded();
				}
			},
			0.1f, true, 0.3f);

		// 페일세이프: 너무 오래 공중에 있으면 강제 종료.
		World->GetTimerManager().SetTimer(
			AirborneFailsafeHandle,
			[this]()
			{
				if (!bHasLanded)
				{
					UE_LOG(LogTemp, Warning, TEXT("[ShipJumpV1] Airborne failsafe fired — forcing land"));
					OnLanded();
				}
			},
			MaxAirborneTime, false);
	}
}

void UEnemyGameplayAbility_ShipJump::OnLanded()
{
	if (bHasLanded) return;
	bHasLanded = true;

	// [ShipJumpAirCollisionV1] 착지 즉시 캡슐 Pawn 응답 복구.
	RestorePawnCollision();

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	UWorld* World = Enemy ? Enemy->GetWorld() : nullptr;
	if (World)
	{
		World->GetTimerManager().ClearTimer(LandingCheckTimerHandle);
		World->GetTimerManager().ClearTimer(AirborneFailsafeHandle);
	}

	// [ShipJumpV6.OnShipGate] 착지한 바닥이 실제 우주선이고 + 높이가 우주선 상단 근방일 때만 OnShip.
	// Why: V6.1 까지는 CurrentFloor=Ship 만 봤는데 앞 몬스터에 막혀 우주선 옆면/하단 콜리전에
	//      걸쳐 멈춘 적도 TRUE 로 판정 → 땅에서 내려찍기(Slam). 실제로는 우주선 "상단"에
	//      올라간 경우만 OnShip 이어야 함.
	// How: 1) Floor 가 Ship 이어야 하고, 2) Enemy.Z 가 Ship 바운딩박스 중앙보다 위 (상반부)
	//      일 때만 통과. 우주선이 지면 근처에 있어도 상단/하단 분리됨.
	bool bLandedOnShip = false;
	AActor* Ship = CurrentTarget.Get();
	float EnemyZ = 0.f, ShipMidZ = 0.f, ShipTopZ = 0.f;
	FString FloorActorName = TEXT("none");
	if (Enemy && Ship)
	{
		EnemyZ = Enemy->GetActorLocation().Z;
		FVector SO, SE;
		Ship->GetActorBounds(false, SO, SE, true);
		ShipMidZ = SO.Z;
		ShipTopZ = SO.Z + SE.Z;

		if (UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement())
		{
			AActor* FloorActor = CMC->CurrentFloor.HitResult.GetActor();
			bool bFloorIsShip = (FloorActor == Ship);
			if (!bFloorIsShip)
			{
				if (UPrimitiveComponent* Base = CMC->GetMovementBase())
				{
					if (Base->GetOwner() == Ship)
					{
						bFloorIsShip = true;
						FloorActor = Ship;
					}
				}
			}
			FloorActorName = FloorActor ? FloorActor->GetName() : TEXT("none");

			// 상반부(중앙보다 위) + Floor=Ship 일 때만 OnShip 인정.
			if (bFloorIsShip && EnemyZ >= ShipMidZ)
			{
				bLandedOnShip = true;
			}
		}
	}

	if (bLandedOnShip)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayTag OnShipTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.OnShip"), false);
			if (OnShipTag.IsValid())
			{
				FGameplayTagContainer OnShipContainer;
				OnShipContainer.AddTag(OnShipTag);
				ASC->AddLooseGameplayTags(OnShipContainer);
			}
		}
	}
	else
	{
		// [ShipJumpFailV1] 상단 착지 실패 → State.Enemy.ShipJumpFailed 태그 영구 부여.
		// StateTree 의 spaceshipJump EnterCondition 이 이 태그 보유 시 진입 차단 → 재시도 영구 중지.
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayTag FailTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.ShipJumpFailed"), false);
			if (FailTag.IsValid() && !ASC->HasMatchingGameplayTag(FailTag))
			{
				FGameplayTagContainer FailContainer;
				FailContainer.AddTag(FailTag);
				ASC->AddLooseGameplayTags(FailContainer);
				UE_LOG(LogTemp, Warning,
					TEXT("[ShipJumpFailV1] Enemy=%s — ShipJumpFailed 태그 부여 (재시도 차단)"),
					Enemy ? *Enemy->GetName() : TEXT("none"));
			}
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ShipJumpV6.OnShipGate] Landed Enemy=%s OnShip=%s Ship=%s Floor=%s EnemyZ=%.0f ShipMidZ=%.0f ShipTopZ=%.0f"),
		Enemy ? *Enemy->GetName() : TEXT("none"),
		bLandedOnShip ? TEXT("TRUE") : TEXT("FALSE"),
		Ship ? *Ship->GetName() : TEXT("null"),
		*FloorActorName,
		EnemyZ, ShipMidZ, ShipTopZ);

	if (Enemy && World)
	{
		World->GetTimerManager().SetTimer(
			DelayedReleaseTimerHandle,
			[this]()
			{
				if (AHellunaEnemyCharacter* E = GetEnemyCharacterFromActorInfo())
				{
					E->UnlockMovement();
				}
				HandleAttackFinished();
			},
			AttackRecoveryDelay, false);
	}
	else
	{
		HandleAttackFinished();
	}
}

void UEnemyGameplayAbility_ShipJump::HandleAttackFinished()
{
	const FGameplayAbilitySpecHandle H = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* AI = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo AV = GetCurrentActivationInfo();
	EndAbility(H, AI, AV, true, false);
}

void UEnemyGameplayAbility_ShipJump::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// [ShipJumpAirCollisionV1] OnLanded 미경유 종료 경로 (Cancel/페일세이프) 안전 복구.
	RestorePawnCollision();

	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		Enemy->UnlockMovement();
		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(LandingCheckTimerHandle);
			World->GetTimerManager().ClearTimer(DelayedReleaseTimerHandle);
			World->GetTimerManager().ClearTimer(AirborneFailsafeHandle);
		}
	}

	// [JumpLagFix] ErrorIfNotFound=false — EndAbility 경로 ensure 비용 회피.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
		if (AttackTag.IsValid())
		{
			FGameplayTagContainer AttackingTag;
			AttackingTag.AddTag(AttackTag);
			ASC->RemoveLooseGameplayTags(AttackingTag);
		}
	}

	CurrentTarget = nullptr;
	bHasLanded = false;
	AssignedSlotIndex = INDEX_NONE;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ============================================================
// [ShipJumpAirCollisionV1] 공중 동안 Pawn 충돌 해제 / 복구
// ============================================================
void UEnemyGameplayAbility_ShipJump::EnableAirCollisionPassthrough()
{
	if (!bIgnorePawnCollisionInAir || bPawnCollisionOverridden) return;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	if (!Capsule) return;

	SavedPawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	bPawnCollisionOverridden = true;

	UE_LOG(LogTemp, Warning,
		TEXT("[ShipJumpAirCollisionV1] Enemy=%s Pawn 충돌 해제 (Saved=%d)"),
		*Enemy->GetName(), static_cast<int32>(SavedPawnResponse.GetValue()));
}

void UEnemyGameplayAbility_ShipJump::RestorePawnCollision()
{
	if (!bPawnCollisionOverridden) return;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, SavedPawnResponse.GetValue());
			UE_LOG(LogTemp, Warning,
				TEXT("[ShipJumpAirCollisionV1] Enemy=%s Pawn 충돌 복구 (Restored=%d)"),
				*Enemy->GetName(), static_cast<int32>(SavedPawnResponse.GetValue()));
		}
	}

	bPawnCollisionOverridden = false;
}
