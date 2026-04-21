/**
 * STCondition_HasGameplayTag.cpp
 */

#include "AI/StateTree/Conditions/STCondition_HasGameplayTag.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"

bool FSTCondition_HasGameplayTag::TestCondition(FStateTreeExecutionContext& Context) const
{
	// [RageBranchV1] 태그가 비어 있으면 검사 무효 → 폴백 (기본: 미보유로 간주).
	if (!Tag.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RageBranchV1] HasGameplayTag 조건에 태그가 비어 있음 — 기본값 반환 (!bMustHaveTag)"));
		return !bMustHaveTag;
	}

	const AAIController* AIC = Cast<AAIController>(Context.GetOwner());
	const APawn* Pawn = AIC ? AIC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return !bMustHaveTag;
	}

	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn);
	UAbilitySystemComponent* ASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return !bMustHaveTag;
	}

	const bool bHas = ASC->HasMatchingGameplayTag(Tag);

	// Verbose 레벨 — 매 Tick Condition 평가마다 찍혀서 렉 유발. 필요 시 LogTemp Verbose 로 활성.
	UE_LOG(LogTemp, Verbose,
		TEXT("[RageBranchV1] %s HasTag(%s)=%s  MustHave=%s  → Result=%s"),
		*Pawn->GetName(),
		*Tag.ToString(),
		bHas ? TEXT("TRUE") : TEXT("FALSE"),
		bMustHaveTag ? TEXT("TRUE") : TEXT("FALSE"),
		(bMustHaveTag == bHas) ? TEXT("PASS") : TEXT("FAIL"));

	return bMustHaveTag ? bHas : !bHas;
}
