/**
 * EnemyMassTrait.cpp
 *
 * BuildTemplate에서 모든 Trait UPROPERTY 값을 FEnemyDataFragment에 복사한다.
 * CurrentHP/MaxHP는 런타임 상태이므로 기본값(-1/100) 유지.
 * 
 * Entity 상태에서는 AI 없이 단순 이동만 수행한다.
 * Actor 전환 후에는 Actor 기반 StateTree가 AI를 담당한다.
 * 
 * @author 김민우
 */

#include "ECS/Traits/EnemyMassTrait.h"
#include "ECS/Fragments/EnemyMassFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "Character/HellunaEnemyCharacter.h"

// 이동/조향 관련
#include "MassMovementFragments.h"        // Velocity, Force
#include "MassNavigationFragments.h"      // MoveTarget

void UEnemyMassTrait::BuildTemplate(
	FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World) const
{
	// Transform Fragment
	BuildContext.AddFragment<FTransformFragment>();

	// 스폰 상태 추적용
	BuildContext.AddFragment<FEnemySpawnStateFragment>();

	// 설정 데이터 Fragment
	FEnemyDataFragment& Data = BuildContext.AddFragment_GetRef<FEnemyDataFragment>();

	// Trait UPROPERTY 값 복사
	Data.EnemyClass = EnemyClass;
	Data.SpawnThreshold = SpawnThreshold;
	Data.DespawnThreshold = DespawnThreshold;
	Data.MaxConcurrentActors = MaxConcurrentActors;
	Data.PoolSize = PoolSize;
	Data.NearDistance = NearDistance;
	Data.MidDistance = MidDistance;
	Data.NearTickInterval = NearTickInterval;
	Data.MidTickInterval = MidTickInterval;
	Data.FarTickInterval = FarTickInterval;
	// Entity 시각화 설정 복사
	Data.EntityVisualizationMesh = EntityVisualizationMesh;
	Data.EntityMeshScale = EntityMeshScale;
	Data.EntityMeshZOffset = EntityMeshZOffset;
	Data.bShowEntityVisualization = bShowEntityVisualization;
	
	//기본 이동
	Data.GoalActorTag = GoalActorTag;
	Data.EntityMoveSpeed = EntityMoveSpeed;
	Data.EntitySeparationRadius = EntitySeparationRadius;
	Data.bMove2DOnly = bMove2DOnly;
	
	UE_LOG(LogTemp, Log,
		TEXT("[EnemyMassTrait] BuildTemplate 완료 - Class: %s, Spawn: %.0f, Despawn: %.0f"),
		EnemyClass ? *EnemyClass->GetName() : TEXT("None"),
		SpawnThreshold, DespawnThreshold);
	
	// ========================================================================
	// 이동/조향 Fragment 추가
	// ========================================================================
	
	// 속도 정보
	BuildContext.AddFragment<FMassVelocityFragment>();
	
	// 이동 힘
	BuildContext.AddFragment<FMassForceFragment>();
	
	// 이동 목표 지점
	BuildContext.AddFragment<FMassMoveTargetFragment>();

	// ========================================================================
	// 충돌/회피 Fragment 추가
	// ========================================================================
	
	// 충돌 반경 설정
	FAgentRadiusFragment& AgentRadius = BuildContext.AddFragment_GetRef<FAgentRadiusFragment>();
	AgentRadius.Radius = EntitySeparationRadius;  // Trait에서 설정한 반경 사용

	UE_LOG(LogTemp, Log,
		TEXT("[EnemyMassTrait] Fragment 추가 완료 - 이동 속도: %.0f cm/s, 충돌 반경: %.0f cm"),
		EntityMoveSpeed, AgentRadius.Radius);
}
