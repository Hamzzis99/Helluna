// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaBaseCharacter.h"
#include "HellunaEnemyCharacter.generated.h"

class UEnemyCombatComponent;
class UHellunaHealthComponent;
class UMassAgentComponent;	
/**
 * 
 */
UCLASS()
class HELLUNA_API AHellunaEnemyCharacter : public AHellunaBaseCharacter
{
	GENERATED_BODY()
	
public:
	AHellunaEnemyCharacter();

protected:
	virtual void BeginPlay() override;

	virtual void PossessedBy(AController* NewController) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	UEnemyCombatComponent* EnemyCombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	UHellunaHealthComponent* HealthComponent;

	UFUNCTION()
	void OnMonsterHealthChanged(UActorComponent* MonsterHealthComponent, float OldHealth, float NewHealth, AActor* InstigatorActor);

	UFUNCTION()
	void OnMonsterDeath(AActor* DeadActor, AActor* KillerActor);

private:
	void InitEnemyStartUpData();

public:
	FORCEINLINE UEnemyCombatComponent* GetEnemyCombatComponent() const { return EnemyCombatComponent; }


// ECS 관련 함수
public:
	// ✅ 사망 시 서버에서 호출: Mass 엔티티 자체를 제거해서 “재생성” 방지
	void DespawnMassEntityOnServer(const TCHAR* Where);

protected:
	// MassAgent가 이미 달려있다고 했으니 캐싱(없으면 FindComponentByClass로 찾아도 됨)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass")
	TObjectPtr<UMassAgentComponent> MassAgentComp = nullptr;

	/**
	 * 거리 기반 애니메이션/그림자 품질 조절.
	 * 카메라 거리 기준으로 근/중/원거리 LOD를 적용한다.
	 * Processor의 UpdateActorTickRate에서 매 틱 호출.
	 *
	 * @param DistanceToCamera  카메라(또는 플레이어)와의 거리 (cm 단위)
	 * @author 김기현
	 */
	void UpdateAnimationLOD(float DistanceToCamera);
};
	