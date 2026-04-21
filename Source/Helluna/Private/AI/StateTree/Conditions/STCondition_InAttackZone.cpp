#include "AI/StateTree/Conditions/STCondition_InAttackZone.h"

#include "AIController.h"
#include "AI/HellunaAIAttackZone.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "StateTreeExecutionContext.h"

bool FSTCondition_InAttackZone::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	if (!TargetData.HasValidTarget())
	{
		return !bCheckInside;
	}

	AActor* TargetActor = TargetData.TargetActor.Get();
	if (!TargetActor)
	{
		return !bCheckInside;
	}

	AAIController* AIController = InstanceData.AIController;
	if (!AIController)
	{
		return !bCheckInside;
	}

	const APawn* Pawn = AIController->GetPawn();
	if (!Pawn)
	{
		return !bCheckInside;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		return !bCheckInside;
	}

	const FVector PawnLoc = Pawn->GetActorLocation();
	const FQuat PawnRot = Pawn->GetActorQuat();
	const FVector Forward = PawnRot.GetForwardVector();
	const FVector BoxCenter = PawnLoc + Forward * ForwardOffset;

	// [ZoneCheckV1][LCv12] 공용 헬퍼로 통일 — Chase/Evaluator와 동일 로직 공유.
	const bool bOverlapping = HellunaAI::IsTargetInAttackZone(Pawn, TargetActor, AttackZoneHalfExtent, ForwardOffset);

#if ENABLE_DRAW_DEBUG
	{
		// CVar는 UHellunaAIDebugSettings(Project Settings > Helluna > AI Debug)에서 등록·관리.
		static IConsoleVariable* CVarDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("ai.debug.attackzone"));
		if (!CVarDebug)
		{
			CVarDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("ai.debug.attackzone"));
		}
		if (CVarDebug && CVarDebug->GetInt() > 0)
		{
			const FColor ZoneColor = bOverlapping ? FColor::Green : FColor::Red;
			DrawDebugBox(World, BoxCenter, AttackZoneHalfExtent, PawnRot, ZoneColor, false, 0.f, 0, 2.f);
			DrawDebugDirectionalArrow(
				World,
				PawnLoc,
				PawnLoc + Forward * (ForwardOffset + AttackZoneHalfExtent.X),
				30.f,
				FColor::Yellow,
				false,
				0.f,
				0,
				1.5f);

			// 타겟 주위에도 박스 표시 — 어떤 컴포넌트가 차단/비차단인지 시각화
			if (const UPrimitiveComponent* TargetRoot = Cast<UPrimitiveComponent>(TargetActor->GetRootComponent()))
			{
				const FBox TargetBounds = TargetRoot->Bounds.GetBox();
				DrawDebugBox(World, TargetBounds.GetCenter(), TargetBounds.GetExtent(),
					FColor::Cyan, false, 0.f, 0, 1.f);
			}
		}
	}
#endif

	return bCheckInside ? bOverlapping : !bOverlapping;
}
