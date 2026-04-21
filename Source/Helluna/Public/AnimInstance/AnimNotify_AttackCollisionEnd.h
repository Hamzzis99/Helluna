// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_AttackCollisionEnd.generated.h"

/**
 * 몬스터 공격 충돌 박스 비활성화 Notify (Overlap 기반).
 *
 * BoxComponentName 에 해당하는 UBoxComponent 의 Collision 을 NoCollision 으로 되돌림.
 * 공격 애니메이션 끝 프레임에 배치.
 *
 * @see AHellunaEnemyCharacter::SetAttackBoxActive()
 * @author 김민우
 */
UCLASS()
class HELLUNA_API UAnimNotify_AttackCollisionEnd : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif

	/** 비활성화할 Box 컴포넌트 이름 (Start Notify 와 동일 값으로 설정) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack",
		meta = (DisplayName = "Box 컴포넌트 이름"))
	FName BoxComponentName = NAME_None;
};
