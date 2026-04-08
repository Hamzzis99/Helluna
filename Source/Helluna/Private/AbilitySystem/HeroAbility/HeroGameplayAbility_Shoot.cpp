// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Shoot.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HeroWeapon_GunBase.h"
#include "Weapon/HeroWeapon_Launcher.h"
#include "Weapon/HellunaWeaponBase.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#include "DebugHelper.h"


UHeroGameplayAbility_Shoot::UHeroGameplayAbility_Shoot()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// ✅ 네 ASC Release 로직이 이걸 보고 Cancel 해줌
	InputActionPolicy = EHellunaInputActionPolicy::Hold;
}

void UHeroGameplayAbility_Shoot::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon)
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
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	const float Interval = FMath::Max(Weapon->AttackSpeed, 0.01f);

	// =========================================================
	// [MOD] ✅ 발사 간격(=연사 제한) : FireMode 상관없이 무조건 적용
	// =========================================================
	if (!Weapon->CanFireByRate(Now, Interval))
	{
		// 너무 자주 클릭했거나(단발/연발 공통), 타이머가 아주 미세하게 빨리 불린 경우
		return;
	}
	Weapon->ConsumeFireByRate(Now, Interval);

	if (Weapon->FireMode == EWeaponFireMode::SemiAuto) // 단발일 떄는 한번 발사하고 종료
	{
		Shoot();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	// 연발일 때는 타이머로 자동 발사 시작
	Shoot();

	//const float Interval = Weapon->AttackSpeed;
	if (World)
	{
		World->GetTimerManager().SetTimer(
			AutoFireTimerHandle,
			this,
			&ThisClass::Shoot,
			Interval,
			true
		);
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

}

void UHeroGameplayAbility_Shoot::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoFireTimerHandle);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_Shoot::Shoot()
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero) return;

	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon) { Debug::Print(TEXT("Shoot Failed: No Weapon"), FColor::Red); return; }

	if (!Weapon->CanFire())
	{
		return;
	}

	const bool bIsLauncher = Weapon->IsA<AHeroWeapon_Launcher>();

	// ═══════════════════════════════════════════════════════════
	// [AimFix] 런처: 로컬 플레이어가 카메라 기준 AimPoint로 캐싱
	// ═══════════════════════════════════════════════════════════
	if (bIsLauncher)
	{
		if (AHeroWeapon_Launcher* Launcher = Cast<AHeroWeapon_Launcher>(Weapon))
		{
			if (AController* Controller = Hero->GetController())
			{
				if (Hero->IsLocallyControlled())
				{
					// 클라이언트: 카메라 기준 AimPoint → 서버에 캐싱
					const FVector AimPoint = ComputeAimPointFromCamera(Hero);
					if (Hero->HasAuthority())
					{
						Launcher->CacheAimWithAimPoint(Controller, AimPoint);
					}
					else
					{
						Launcher->ServerCacheAimWithPoint(AimPoint);
					}
				}
				else
				{
					// 서버 비로컬: 폴백 (클라이언트 RPC가 더 정확하지만 안전장치)
					Launcher->CacheAimFromController(Controller);
				}
			}
		}
	}

	// 몽타주 재생
	if (UAnimMontage* AttackMontage = Weapon->AnimSet.Attack)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->PlayMontage(this, GetCurrentActivationInfo(), AttackMontage, 1.f);
		}
	}

	// 로컬 코스메틱 (반동)
	if (Hero->IsLocallyControlled())
	{
		Weapon->ApplyRecoil(Hero);
	}

	// ═══════════════════════════════════════════════════════════
	// [AimFix] Fire: 로컬 플레이어가 카메라 기준 AimPoint 계산 → 서버에 전달
	// 런처는 AnimNotify_LauncherFire에서 Fire하므로 여기서 스킵
	// ═══════════════════════════════════════════════════════════
	if (!bIsLauncher)
	{
		if (Hero->IsLocallyControlled())
		{
			const FVector AimPoint = ComputeAimPointFromCamera(Hero);

			if (Hero->HasAuthority())
			{
				// 리슨서버 호스트: 직접 실행
				if (AController* Controller = Hero->GetController())
				{
					Weapon->FireWithAimPoint(Controller, AimPoint);
				}
			}
			else
			{
				// 데디서버 클라이언트: Server RPC
				Weapon->ServerFireWithAimPoint(AimPoint);
			}
		}
		// 서버 비로컬: 아무것도 안 함
		// 클라이언트의 Reliable RPC(ServerFireWithAimPoint)가 보장하므로 폴백 불필요
		// (폴백 Fire를 호출하면 RPC와 이중 발사 버그 발생)
	}
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

