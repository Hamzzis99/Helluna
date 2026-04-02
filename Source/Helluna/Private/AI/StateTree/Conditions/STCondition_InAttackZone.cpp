#include "AI/StateTree/Conditions/STCondition_InAttackZone.h"

#include "AIController.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"
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

	const FCollisionShape BoxShape = FCollisionShape::MakeBox(AttackZoneHalfExtent);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Pawn);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		BoxCenter,
		PawnRot,
		FCollisionObjectQueryParams::AllObjects,
		BoxShape,
		QueryParams);

	bool bOverlapping = false;
	bool bFoundTargetButWrongChannel = false;
	for (const FOverlapResult& Result : Overlaps)
	{
		if (Result.GetActor() != TargetActor)
		{
			continue;
		}

		const UPrimitiveComponent* Component = Result.GetComponent();
		if (!Component)
		{
			continue;
		}

		const bool bBlocksPawn = (Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
		const bool bBlocksStatic = (Component->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
		const bool bBlocksDynamic = (Component->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
		if (bBlocksPawn && bBlocksStatic && bBlocksDynamic)
		{
			bOverlapping = true;
			break;
		}
		else
		{
			bFoundTargetButWrongChannel = true;
		}
	}

	// 주기적 디버그 로그 (1초마다, 대표 몬스터 1마리만)
	{
		static double LastLogTime = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastLogTime >= 1.0)
		{
			LastLogTime = Now;
			const float DistToTarget = FVector::Dist(PawnLoc, TargetActor->GetActorLocation());
			const float DistToTarget2D = FVector::Dist2D(PawnLoc, TargetActor->GetActorLocation());
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][AttackZoneCheck] Monster=%s Target=%s Overlap=%d WrongChannel=%d TotalOverlaps=%d BoxCenter=%s HalfExt=%s FwdOffset=%.1f Dist=%.1f Dist2D=%.1f FwdDir=%s"),
				*Pawn->GetName(), *TargetActor->GetName(),
				(int32)bOverlapping, (int32)bFoundTargetButWrongChannel,
				Overlaps.Num(), *BoxCenter.ToString(),
				*AttackZoneHalfExtent.ToString(), ForwardOffset,
				DistToTarget, DistToTarget2D,
				*Forward.ToString());
		}
	}

#if ENABLE_DRAW_DEBUG
	{
		static IConsoleVariable* CVarDebug = IConsoleManager::Get().RegisterConsoleVariable(
			TEXT("ai.debug.attackzone"),
			0,
			TEXT("1 = Draw AttackZone debug boxes"),
			ECVF_Cheat);
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
		}
	}
#endif

	return bCheckInside ? bOverlapping : !bOverlapping;
}
