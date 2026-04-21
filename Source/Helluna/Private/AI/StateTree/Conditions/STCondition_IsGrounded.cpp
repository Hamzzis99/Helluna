/**
 * STCondition_IsGrounded.cpp
 */

#include "AI/StateTree/Conditions/STCondition_IsGrounded.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"

bool FSTCondition_IsGrounded::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AAIController* AIC = Cast<AAIController>(Context.GetOwner());
	if (!AIC) return !bRequireGrounded;

	const APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return !bRequireGrounded;

	const UCharacterMovementComponent* CMC =
		Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent());
	if (!CMC)
	{
		// 이동 컴포넌트 없으면 "지면"으로 간주(기본 폴백).
		return bRequireGrounded;
	}

	const bool bGrounded = !CMC->IsFalling();
	return bRequireGrounded ? bGrounded : !bGrounded;
}
