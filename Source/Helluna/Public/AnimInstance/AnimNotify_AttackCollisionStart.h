// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_AttackCollisionStart.generated.h"

/**
 * 몬스터 공격 충돌 박스 활성화 Notify.
 *
 * ■ 방식 (Overlap 기반)
 *   BP_Enemy_Melee 에 미리 배치된 UBoxComponent (이름: "Hitbox_*") 의
 *   Collision 을 QueryOnly 로 켜서 BeginOverlap 이벤트를 통해 판정.
 *
 * ■ 최적화
 *   - 공격 안 할 때: NoCollision 으로 broad-phase 에서 제외
 *   - 공격 구간: QueryOnly + ECC_Pawn 채널만 Overlap
 *   - Tick/Timer 없음
 *
 * @see AHellunaEnemyCharacter::SetAttackBoxActive()
 * @author 김민우
 */
UCLASS()
class HELLUNA_API UAnimNotify_AttackCollisionStart : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif

	/**
	 * 활성화할 Box 컴포넌트 이름.
	 * BP_Enemy_Melee 의 UBoxComponent 이름과 일치해야 함 (예: "Hitbox_MeleeAttack").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack",
		meta = (DisplayName = "Box 컴포넌트 이름"))
	FName BoxComponentName = NAME_None;

	/** 공격 데미지. GA 가 CachedMeleeAttackDamage 로 덮어쓰면 그 값이 우선. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack",
		meta = (DisplayName = "데미지", ClampMin = "0.0", ClampMax = "1000.0"))
	float Damage = 10.f;

	/** 디버그 Draw (스폰 시점 1회 + Hit 마크) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug",
		meta = (DisplayName = "디버그 표시"))
	bool bDrawDebug = false;
};
