// Capstone Project Helluna

#include "AnimInstance/AnimNotify_AttackCollisionStart.h"
#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"

void UAnimNotify_AttackCollisionStart::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// Null 체크
	if (!MeshComp || !MeshComp->GetOwner())
	{
		UE_LOG(LogTemp, Error, TEXT("AnimNotify_AttackCollisionStart: Invalid MeshComp or Owner"));
		return;
	}

	// EnemyCharacter 캐스팅
	AHellunaEnemyCharacter* EnemyCharacter = Cast<AHellunaEnemyCharacter>(MeshComp->GetOwner());
	if (!EnemyCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("AnimNotify_AttackCollisionStart: Owner is not AHellunaEnemyCharacter"));
		return;
	}

	// GA의 AttackDamage 값을 우선 사용, AnimNotify에 설정된 값은 GA가 없을 때 폴백
	float FinalDamage = Damage;

	// 현재 활성화된 GA_Attack에서 AttackDamage 읽기
	if (UAbilitySystemComponent* ASC = EnemyCharacter->GetAbilitySystemComponent())
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (UEnemyGameplayAbility_Attack* AttackGA = Cast<UEnemyGameplayAbility_Attack>(Spec.Ability))
			{
				if (Spec.IsActive())
				{
					FinalDamage = AttackGA->AttackDamage;
					break;
				}
			}
		}
	}

	EnemyCharacter->StartAttackTrace(SocketName, TraceRadius, TraceInterval, FinalDamage, bDrawDebug);
	
	UE_LOG(LogTemp, Log, TEXT("AnimNotify_AttackCollisionStart: %s started attack trace (Socket: %s, Radius: %.1f, Interval: %.3f, Damage: %.1f)"),
		*EnemyCharacter->GetName(), *SocketName.ToString(), TraceRadius, TraceInterval, Damage);
}
