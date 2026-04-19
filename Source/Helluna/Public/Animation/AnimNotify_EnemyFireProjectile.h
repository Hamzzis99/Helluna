/**
 * AnimNotify_EnemyFireProjectile.h
 *
 * 에너미 원거리 공격 몬타지의 "발사 프레임" 노티.
 *
 * ─── 사용법 ──────────────────────────────────────────────────────
 *  1. 원거리 공격 몬타지를 연다 (예: AM_Enemy_Attack).
 *  2. 발사 타이밍 프레임에 이 노티를 추가한다.
 *  3. 해당 GA (UEnemyGameplayAbility_RangedAttack) 의
 *     `bFireViaAnimNotify = true` 로 설정하면 자동 발사 타이머는 비활성화되고
 *     이 노티만 투사체를 발사한다.
 *
 * 멀티플레이어 주의 ─────────────────────────────────────────────
 *  - 몬타지는 Multicast_PlayAttackMontage 로 모든 클라이언트에서 재생됨.
 *  - 이 노티도 모든 클라에서 발화하지만, FireActiveRangedProjectile 내부에서
 *    HasAuthority 검사를 통해 서버만 실제 스폰 → 투사체 자체가 replicated 이므로
 *    클라는 서버에서 복제받는 정상 동작.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnemyFireProjectile.generated.h"

UCLASS(meta = (DisplayName = "Enemy: Fire Projectile"))
class HELLUNA_API UAnimNotify_EnemyFireProjectile : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
