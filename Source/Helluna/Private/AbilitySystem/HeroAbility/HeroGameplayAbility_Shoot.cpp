// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Shoot.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HeroWeapon_GunBase.h"
#include "Weapon/HellunaWeaponBase.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "HellunaGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#include "DebugHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogHellunaShootAbility, Log, All);

namespace
{
double AdvanceFireCooldown(double Remaining, double DeltaSeconds, double AnimationRate)
{
	if (!FMath::IsFinite(DeltaSeconds) || !FMath::IsFinite(AnimationRate)) return Remaining;
	return FMath::Max(0.0, Remaining - FMath::Max(0.0, DeltaSeconds) * FMath::Max(0.0, AnimationRate));
}
}

UHeroGameplayAbility_Shoot::UHeroGameplayAbility_Shoot()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// ✅ 네 ASC Release 로직이 이걸 보고 Cancel 해줌
	InputActionPolicy = EHellunaInputActionPolicy::Hold;

	// [AimGateV1] 견착(우클릭) 중에만 발사 가능 — Player_status_Aim 태그가 없으면
	//   Shoot 어빌리티 자체가 활성화되지 않아 좌클릭이 "아무 반응 없음" 이 된다.
	//   (Player_status_Aim 은 GA_Aim 이 활성 중에 ActivationOwnedTags 로 부여)
	ActivationRequiredTags.AddTag(HellunaGameplayTags::Player_status_Aim);

	// [MenuInputLockV1] 외부(메뉴 열림)에서 CancelAbilityByTag(Player_Ability_Shoot) 로 이 GA 를 찾아
	//   연사를 강제 정지할 수 있도록 어빌리티 태그를 C++ 에서도 보장.
	//   (현재 GA_Hero_Shoot BP 에도 동일 태그가 있으나, BP 회귀/신규 파생 대비 추가. AddTag 라 중복 무해.)
	AbilityTags.AddTag(HellunaGameplayTags::Player_Ability_Shoot);
}

void UHeroGameplayAbility_Shoot::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive()) return;

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!IsValid(Hero))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!IsValid(Weapon))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	

	if (!Weapon->CanFire())
	{
		// (선택) 0일 때는 자동으로 장전 유도 UI만 보이게 하고 싶다면 여기서 끝.
		Debug::Print(TEXT("No Mag"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ═══════════════ 건패링 분기 ═══════════════
	if (UHellunaAbilitySystemComponent* HellunaASC = Hero->GetHellunaAbilitySystemComponent())
	{
		if (UHeroGameplayAbility_GunParry::TryParryInstead(HellunaASC, Weapon))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			return;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const float Now = World->GetTimeSeconds();
	const float Interval = FMath::Max(Weapon->AttackSpeed, 0.01f);

	// =========================================================
	// [MOD] ✅ 발사 간격(=연사 제한) : FireMode 상관없이 무조건 적용
	// =========================================================
	if (!Weapon->CanFireByRate(Now, Interval))
	{
		// A rejected press must not leave an active ability with no firing task.
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	Weapon->ConsumeFireByRate(Now, Interval);

	if (Weapon->FireMode == EWeaponFireMode::SemiAuto) // 단발일 떄는 한번 발사하고 종료
	{
		Shoot();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	FiringWeapon = Weapon;
	Shoot();
	if (!IsActive()) return;

	// Advance the cadence in animation time so slowed montages can reach their fire notify.
	LastAutoFireTime = World->GetTimeSeconds();
	AutoFireTimeRemaining = Interval;
	World->GetTimerManager().SetTimer(AutoFireTimerHandle, this, &ThisClass::TickAutoFire, 0.01f, true);
}

void UHeroGameplayAbility_Shoot::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoFireTimerHandle);
	}
	FiringWeapon.Reset();
	AutoFireTimeRemaining = 0.0;
	LastAutoFireTime = 0.0;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_Shoot::TickAutoFire()
{
	if (!IsActive()) return;
	UWorld* World = GetWorld();
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHeroWeapon_GunBase* Weapon = FiringWeapon.Get();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!World || !IsValid(Hero) || !IsValid(Weapon) || Hero->GetCurrentWeapon() != Weapon
		|| !IsValid(Hero->GetMesh()) || !IsValid(ASC) || !Weapon->CanFire()
		|| !ASC->HasMatchingGameplayTag(HellunaGameplayTags::Player_status_Aim)
		|| ASC->HasMatchingGameplayTag(HellunaGameplayTags::Player_State_MenuOpen)
		|| ASC->HasMatchingGameplayTag(HellunaGameplayTags::Player_Status_Blocking))
	{
		K2_EndAbility();
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double AnimationRate = static_cast<double>(Hero->GetMesh()->GlobalAnimRateScale) * Hero->CustomTimeDilation;
	AutoFireTimeRemaining = AdvanceFireCooldown(AutoFireTimeRemaining, Now - LastAutoFireTime, AnimationRate);
	LastAutoFireTime = Now;
	if (AutoFireTimeRemaining <= KINDA_SMALL_NUMBER)
	{
		AutoFireTimeRemaining = FMath::Max(Weapon->AttackSpeed, 0.01f);
		Shoot();
	}
}

void UHeroGameplayAbility_Shoot::Shoot()
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!IsValid(Hero)) { K2_EndAbility(); return; }

	// [AimGateV1] 연사(FullAuto) 도중 견착(우클릭)이 풀리면 즉시 발사 중단.
	//   ActivationRequiredTags 는 "활성화 시점" 만 막으므로, 유지형 연사는 여기서 가드.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (!ASC->HasMatchingGameplayTag(HellunaGameplayTags::Player_status_Aim))
		{
			K2_EndAbility();
			return;
		}

		// [MenuInputLockV1] 발사 중 UI 메뉴가 열리면 즉시 연사 중단 (CancelAbilityByTag 누락 대비 백업).
		if (ASC->HasMatchingGameplayTag(HellunaGameplayTags::Player_State_MenuOpen))
		{
			K2_EndAbility();
			return;
		}
	}

	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!IsValid(Weapon)) { K2_EndAbility(); return; }

	if (!Weapon->CanFire())
	{
		K2_EndAbility();
		return;
	}

	// ═══════════════════════════════════════════════════════════
	// [SlowMo] 모든 무기 공용: 로컬 플레이어가 카메라 기준 AimPoint 캐싱
	// 실제 발사(Fire)와 반동(Recoil)은 AnimNotify_LauncherFire에서 처리
	// ═══════════════════════════════════════════════════════════
	if (Hero->IsLocallyControlled())
	{
		const FVector AimPoint = ComputeAimPointFromCamera(Hero);
		if (Hero->HasAuthority())
		{
			Weapon->CacheClientAimPoint(AimPoint);
		}
		else
		{
			Weapon->ServerCacheClientAimPoint(AimPoint);
		}
	}

	// 몽타주 재생 → AnimNotify에서 Fire + ApplyRecoil 호출
	if (UAnimMontage* AttackMontage = Weapon->AnimSet.Attack)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			if (ASC->PlayMontage(this, GetCurrentActivationInfo(), AttackMontage, 1.f) > 0.f) return;
		}
	}
	UE_LOG(LogHellunaShootAbility, Warning, TEXT("Cannot play fire montage for %s; ending Shoot."), *GetNameSafe(Weapon));
	K2_EndAbility();
}

// ════════════════════════════════════════════════════════════════
// [AimFix] ComputeAimPointFromCamera
// ════════════════════════════════════════════════════════════════
// 클라이언트의 카메라 위치에서 화면 중앙 방향으로 LineTrace,
// 히트한 월드 위치(AimPoint)를 반환.
// 서버에서는 카메라가 없으므로 반드시 로컬 플레이어에서만 호출.
// ════════════════════════════════════════════════════════════════
FVector UHeroGameplayAbility_Shoot::ComputeAimPointFromCamera(const AHellunaHeroCharacter* Hero)
{
	if (!Hero)
		return FVector::ZeroVector;

	APlayerController* PC = Cast<APlayerController>(Hero->GetController());
	if (!PC)
		return Hero->GetActorLocation() + Hero->GetActorForwardVector() * 10000.f;

	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);

	const FVector CamForward = CamRot.Vector();
	const FVector TraceStart = CamLoc;
	// 충분히 먼 거리 (무기 Range 이상)
	const FVector TraceEnd = TraceStart + CamForward * 50000.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CameraAimTrace), false);
	Params.AddIgnoredActor(Hero);
	if (const AHellunaWeaponBase* Weapon = Hero->GetCurrentWeapon())
	{
		Params.AddIgnoredActor(Weapon);
	}

	FHitResult Hit;
	UWorld* World = Hero->GetWorld();
	if (World && World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
	{
		return Hit.ImpactPoint;
	}

	return TraceEnd;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHellunaFireCadenceTest, "Helluna.Combat.Fire.AnimationCadence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHellunaFireCadenceTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Normal animation consumes normal fire interval"),
		FMath::IsNearlyZero(AdvanceFireCooldown(0.2, 0.2, 1.0)));
	TestTrue(TEXT("Slow animation must not restart before its notify"),
		FMath::IsNearlyEqual(AdvanceFireCooldown(0.2, 0.2, 0.1), 0.18));
	TestTrue(TEXT("Slow animation eventually reaches the next shot"),
		FMath::IsNearlyZero(AdvanceFireCooldown(0.2, 2.0, 0.1)));
	const double AfterSlow = AdvanceFireCooldown(0.2, 0.5, 0.1);
	TestTrue(TEXT("Restoring speed immediately advances the remaining cadence"),
		FMath::IsNearlyZero(AdvanceFireCooldown(AfterSlow, 0.15, 1.0)));
	TestEqual(TEXT("Paused animation cannot consume its next shot"), AdvanceFireCooldown(0.2, 2.0, 0.0), 0.2);
	TestEqual(TEXT("Repeated timer callbacks in the same world frame do not advance"), AdvanceFireCooldown(0.2, 0.0, 1.0), 0.2);
	TestEqual(TEXT("Clock correction cannot increase the delay"), AdvanceFireCooldown(0.2, -0.1, 1.0), 0.2);
	TestEqual(TEXT("A hitch does not accumulate a catch-up burst"), AdvanceFireCooldown(0.2, 10.0, 1.0), 0.0);
	return true;
}
#endif
