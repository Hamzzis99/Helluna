// Capstone Project Helluna

#include "AnimInstance/AnimNotify_AttackCollisionStart.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Helluna.h"
void UAnimNotify_AttackCollisionStart::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// Null 체크
	if (!MeshComp || !MeshComp->GetOwner())
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Error, TEXT("AnimNotify_AttackCollisionStart: Invalid MeshComp or Owner"));
#endif
		return;
	}

	// EnemyCharacter 캐스팅
	AHellunaEnemyCharacter* EnemyCharacter = Cast<AHellunaEnemyCharacter>(MeshComp->GetOwner());
	if (!EnemyCharacter)
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Error, TEXT("AnimNotify_AttackCollisionStart: Owner is not AHellunaEnemyCharacter"));
#endif
		return;
	}

	// GA 시작 시 EnemyCharacter에 캐시한 값을 우선 사용, 없으면 AnimNotify 설정값 폴백
	float FinalDamage = Damage;
	if (EnemyCharacter->GetCachedMeleeAttackDamage() > 0.f)
	{
		FinalDamage = EnemyCharacter->GetCachedMeleeAttackDamage();
	}

	EnemyCharacter->StartAttackTrace(SocketName, TraceRadius, TraceInterval, FinalDamage, bDrawDebug);

#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Log, TEXT("AnimNotify_AttackCollisionStart: %s started attack trace (Socket: %s, Radius: %.1f, Interval: %.3f, Damage: %.1f)"),
		*EnemyCharacter->GetName(), *SocketName.ToString(), TraceRadius, TraceInterval, Damage);
#endif
}
