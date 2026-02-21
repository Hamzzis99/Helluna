/**
 * EnvQueryContext_HellunaTarget.cpp
 */

#include "AI/EQS/EnvQueryContext_HellunaTarget.h"

#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

#include "AI/StateTree/Evaluators/STEvaluator_TargetSelector.h"
#include "Components/StateTreeComponent.h"
#include "StateTreeExecutionContext.h"

void UEnvQueryContext_HellunaTarget::ProvideContext(
	FEnvQueryInstance& QueryInstance,
	FEnvQueryContextData& ContextData) const
{
	// Querier는 EQS를 실행한 주체 (= AI 폰)
	AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor) return;

	APawn* QuerierPawn = Cast<APawn>(QuerierActor);
	if (!QuerierPawn) return;

	AAIController* AIC = Cast<AAIController>(QuerierPawn->GetController());
	if (!AIC) return;

	// AIController의 Blackboard 또는 StateTreeComponent에서 타겟을 찾는다.
	// 현재 프로젝트는 StateTree 기반이므로 StateTreeComponent에서
	// STEvaluator_TargetSelector가 채운 타겟 데이터를 가져온다.
	// 단, StateTree InstanceData는 외부에서 직접 접근이 불가하므로
	// EnemyCharacter에 캐싱된 CurrentTarget을 사용한다.
	UStateTreeComponent* STComp = AIC->FindComponentByClass<UStateTreeComponent>();
	if (!STComp) return;

	// STEvaluator_TargetSelector가 매 틱 갱신하는 타겟을
	// EnemyCharacter에서 직접 접근할 수 없으므로,
	// AIController의 Pawn이 HellunaEnemyCharacter인 경우
	// GetCurrentTarget() 또는 캐싱 변수를 통해 가져온다.
	// 여기서는 Blackboard 없이 동작하도록 AIC에 저장된 FocusActor를 사용한다.
	AActor* FocusActor = AIC->GetFocusActor();
	if (!IsValid(FocusActor)) return;

	// EQS Context에 타겟 Actor를 등록
	UEnvQueryItemType_Actor::SetContextHelper(ContextData, FocusActor);
}
