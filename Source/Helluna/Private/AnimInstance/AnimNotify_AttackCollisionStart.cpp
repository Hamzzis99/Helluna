// Capstone Project Helluna

#include "AnimInstance/AnimNotify_AttackCollisionStart.h"
#include "Character/HellunaEnemyCharacter.h"

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

	// Character에게 트레이스 시작 요청
	EnemyCharacter->StartAttackTrace(SocketName, TraceRadius, TraceInterval, Damage, bDrawDebug);
	
	UE_LOG(LogTemp, Log, TEXT("AnimNotify_AttackCollisionStart: %s started attack trace (Socket: %s, Radius: %.1f, Interval: %.3f, Damage: %.1f)"),
		*EnemyCharacter->GetName(), *SocketName.ToString(), TraceRadius, TraceInterval, Damage);
}
