// File: Source/Helluna/Private/AbilitySystem/HeroAbility/HeroGameplayAbility_Block.cpp
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Block.h"

#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HellunaFunctionLibrary.h"
#include "HellunaGameplayTags.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHellunaBlockAbility, Log, All);

UHeroGameplayAbility_Block::UHeroGameplayAbility_Block()
{
	InputActionPolicy = EHellunaInputActionPolicy::Hold;
	SetAssetTags(FGameplayTagContainer(HellunaGameplayTags::Player_Ability_Block));
	ActivationRequiredTags.AddTag(HellunaGameplayTags::Player_status_Aim);
	ActivationOwnedTags.AddTag(HellunaGameplayTags::Player_Status_Blocking);
	BlockAbilitiesWithTag.AddTag(HellunaGameplayTags::Player_Ability_Jump);
	// [Block 중 사격 금지] Aim+Block 동안 Shoot/Shoot_Auto 활성화 차단
	BlockAbilitiesWithTag.AddTag(HellunaGameplayTags::Player_Ability_Shoot);
	CancelAbilitiesWithTag.AddTag(HellunaGameplayTags::Player_Ability_Run);

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	BlockMontage = TSoftObjectPtr<UAnimMontage>(
		FSoftObjectPath(TEXT("/Game/Hero/Hero_Animation/AM_Hero_StandingBlock_Idle_Default.AM_Hero_StandingBlock_Idle_Default")));

	BlockShieldVFX = TSoftObjectPtr<UNiagaraSystem>(
		FSoftObjectPath(TEXT("/Game/Assets/Niagara/Shield/NS_Shield_1.NS_Shield_1")));
}

bool UHeroGameplayAbility_Block::IsBlocking(const AActor* Blocker)
{
	if (!IsValid(Blocker))
	{
		return false;
	}

	return UHellunaFunctionLibrary::NativeDoesActorHaveTag(
		const_cast<AActor*>(Blocker),
		HellunaGameplayTags::Player_Status_Blocking);
}

bool UHeroGameplayAbility_Block::IsPerfectBlocking(const AActor* Blocker)
{
	if (!IsValid(Blocker))
	{
		return false;
	}

	return UHellunaFunctionLibrary::NativeDoesActorHaveTag(
		const_cast<AActor*>(Blocker),
		HellunaGameplayTags::Player_Status_PerfectBlocking);
}

bool UHeroGameplayAbility_Block::EvaluateBlock(AActor* Blocker, AActor* SourceActor, bool& bOutPerfectBlock)
{
	bOutPerfectBlock = false;

	if (!IsValid(Blocker) || !IsValid(SourceActor))
	{
		return false;
	}

	if (!IsBlocking(Blocker))
	{
		return false;
	}

	if (!UHellunaFunctionLibrary::IsValidBlock(SourceActor, Blocker))
	{
		return false;
	}

	bOutPerfectBlock = IsPerfectBlocking(Blocker);
	return true;
}

void UHeroGameplayAbility_Block::ExecuteBlockCue(AActor* Blocker, AActor* SourceActor, bool bPerfectBlock)
{
	if (!IsValid(Blocker))
	{
		return;
	}

	UHellunaAbilitySystemComponent* ASC = UHellunaFunctionLibrary::NativeGetHellunaASCFromActor(Blocker);
	if (!ASC)
	{
		return;
	}

	FGameplayCueParameters CueParameters;
	CueParameters.Instigator = SourceActor;
	CueParameters.EffectCauser = SourceActor;
	CueParameters.SourceObject = SourceActor;
	CueParameters.Location = Blocker->GetActorLocation();
	if (const ACharacter* Character = Cast<ACharacter>(Blocker))
	{
		CueParameters.TargetAttachComponent = Character->GetMesh();
	}
	else
	{
		CueParameters.TargetAttachComponent = Blocker->GetRootComponent();
	}

	const FGameplayTag CueTag = bPerfectBlock
		? HellunaGameplayTags::GameplayCue_FX_MagicShield_SuceessfulBlock_PerfectBlock
		: HellunaGameplayTags::GameplayCue_FX_MagicShield_SuceessfulBlock;

	ASC->ExecuteGameplayCue(CueTag, CueParameters);
}

UAnimMontage* UHeroGameplayAbility_Block::ResolveBlockMontage()
{
	if (LoadedBlockMontage)
	{
		return LoadedBlockMontage;
	}

	if (BlockMontage.IsNull())
	{
		return nullptr;
	}

	LoadedBlockMontage = BlockMontage.LoadSynchronous();
	return LoadedBlockMontage;
}

UNiagaraSystem* UHeroGameplayAbility_Block::ResolveBlockShieldVFX()
{
	if (LoadedBlockShieldVFX)
	{
		return LoadedBlockShieldVFX;
	}

	if (BlockShieldVFX.IsNull())
	{
		UE_LOG(LogHellunaBlockAbility, Warning, TEXT("BlockShieldVFX is not assigned."));
		return nullptr;
	}

	LoadedBlockShieldVFX = BlockShieldVFX.LoadSynchronous();
	if (!LoadedBlockShieldVFX)
	{
		UE_LOG(LogHellunaBlockAbility, Warning, TEXT("Failed to load BlockShieldVFX: %s"), *BlockShieldVFX.ToSoftObjectPath().ToString());
	}
	return LoadedBlockShieldVFX;
}

void UHeroGameplayAbility_Block::StartBlockShieldVFX(AHellunaHeroCharacter* Hero)
{
	if (!IsValid(Hero) || !Hero->GetMesh())
	{
		UE_LOG(LogHellunaBlockAbility, Warning, TEXT("Cannot start Block shield VFX because Hero or Mesh is invalid."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (IsValid(ActiveBlockShieldVFX))
	{
		ActiveBlockShieldVFX->Activate(true);
		return;
	}
	ActiveBlockShieldVFX = nullptr;

	UNiagaraSystem* ShieldVFX = ResolveBlockShieldVFX();
	if (!ShieldVFX)
	{
		return;
	}

	const FName AttachSocket = Hero->GetMesh()->DoesSocketExist(BlockShieldAttachSocket)
		? BlockShieldAttachSocket
		: NAME_None;

	ActiveBlockShieldVFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
		ShieldVFX,
		Hero->GetMesh(),
		AttachSocket,
		BlockShieldRelativeLocation,
		BlockShieldRelativeRotation,
		BlockShieldRelativeScale,
		EAttachLocation::SnapToTarget,
		false,
		ENCPoolMethod::None,
		true,
		false);

	if (ActiveBlockShieldVFX)
	{
		ActiveBlockShieldVFX->SetRelativeLocation(BlockShieldRelativeLocation);
		ActiveBlockShieldVFX->SetRelativeRotation(BlockShieldRelativeRotation);
		ActiveBlockShieldVFX->SetRelativeScale3D(BlockShieldRelativeScale);
	}
	else
	{
		UE_LOG(
			LogHellunaBlockAbility,
			Warning,
			TEXT("Failed to spawn Block shield VFX. Effect=%s Mesh=%s Socket=%s"),
			*GetNameSafe(ShieldVFX),
			*GetNameSafe(Hero->GetMesh()),
			*AttachSocket.ToString());
	}
}

void UHeroGameplayAbility_Block::StopBlockShieldVFX()
{
	if (!IsValid(ActiveBlockShieldVFX))
	{
		ActiveBlockShieldVFX = nullptr;
		return;
	}

	ActiveBlockShieldVFX->DeactivateImmediate();
	ActiveBlockShieldVFX->DestroyComponent();
	ActiveBlockShieldVFX = nullptr;
}

void UHeroGameplayAbility_Block::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	if (!Hero || !ASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ASC->AddStateTag(HellunaGameplayTags::Player_Status_PerfectBlocking);
	StartBlockShieldVFX(Hero);

	UAnimMontage* MontageToPlay = ResolveBlockMontage();
	if (MontageToPlay && Hero->GetMesh())
	{
		if (UAnimInstance* AnimInstance = Hero->GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(MontageToPlay, BlockMontagePlayRate);

			// [Block 떨림 방지] Block 몽타주는 nextSectionName이 자기 자신을 가리켜 1.4s마다 self-loop
			// → 시작/끝 키 미스매치로 미세 점프 발생. 런타임에서 다음 섹션을 끊어 마지막 프레임 hold 처리.
			// Block 한정 동작 — 다른 몽타주/애니메이션 자산은 영향 없음.
			if (MontageToPlay->CompositeSections.Num() > 0)
			{
				const FName FirstSectionName = MontageToPlay->CompositeSections[0].SectionName;
				if (FirstSectionName != NAME_None)
				{
					AnimInstance->Montage_SetNextSection(FirstSectionName, NAME_None, MontageToPlay);
				}
			}
		}
	}

	// [As-A-Client 멀티 동기화] 서버 권위에서만 Multicast로 다른 클라(시뮬레이션 프록시)에 VFX/몽타주 전파.
	// Owning client는 LocalPredicted로 이미 위에서 자체 spawn 완료 → Multicast _Implementation에서 skip.
	// 데디서버는 _Implementation에서 렌더 스킵.
	if (HasAuthority(&ActivationInfo))
	{
		UNiagaraSystem* ResolvedVFX = ResolveBlockShieldVFX();
		Hero->Multicast_StartBlockShield(
			ResolvedVFX,
			BlockShieldAttachSocket,
			BlockShieldRelativeLocation,
			BlockShieldRelativeRotation,
			BlockShieldRelativeScale,
			MontageToPlay,
			BlockMontagePlayRate);
	}

	if (PerfectBlockWindowSeconds <= 0.f)
	{
		ClearPerfectBlockWindow();
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PerfectBlockTimerHandle,
			this,
			&ThisClass::ClearPerfectBlockWindow,
			PerfectBlockWindowSeconds,
			false);
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UHeroGameplayAbility_Block::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PerfectBlockTimerHandle);
	}

	if (UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->RemoveStateTag(HellunaGameplayTags::Player_Status_PerfectBlocking);
	}

	StopBlockShieldVFX();

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	UAnimMontage* MontageToStop = LoadedBlockMontage.Get();
	if (Hero)
	{
		if (MontageToStop && Hero->GetMesh())
		{
			if (UAnimInstance* AnimInstance = Hero->GetMesh()->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.15f, MontageToStop);
			}
		}

		// [As-A-Client 멀티 동기화] 서버 권위에서 Multicast로 다른 클라 VFX/몽타주 종료.
		if (HasAuthority(&ActivationInfo))
		{
			Hero->Multicast_StopBlockShield(MontageToStop);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_Block::ClearPerfectBlockWindow()
{
	if (UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->RemoveStateTag(HellunaGameplayTags::Player_Status_PerfectBlocking);
	}
}
