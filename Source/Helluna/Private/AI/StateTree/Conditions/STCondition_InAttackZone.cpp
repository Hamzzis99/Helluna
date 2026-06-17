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

	// [AttackGraceAfterSwitchV1] 플레이어로 타겟 전환 직후 잠깐은 공격존 미충족 취급 → 즉시 "그 자리에서
	//   한 대 때리는" 버그 방지하고 추격(Run) 먼저. PlayerTargetingTime 은 TargetSelector 가 전환 시 0 으로
	//   리셋 후 매 틱 누적하므로, 전환 후 경과시간으로 사용. (bCheckInside=True 인 공격 진입 조건에만 적용.)
	if (TargetData.TargetType == EHellunaTargetType::Player)
	{
		static constexpr float PlayerAttackGraceAfterSwitch = 0.5f;
		if (TargetData.PlayerTargetingTime < PlayerAttackGraceAfterSwitch)
		{
			// 전환 직후 grace: 플레이어를 "공격존 밖" 으로 취급.
			//   - 공격 진입 조건(bCheckInside=true) → false 반환: 새 공격 안 들어감.
			//   - 공격 이탈 조건(bCheckInside=false) → true 반환: 이미 Attack 상태였으면 즉시 이탈 → Run(추격).
			//   → "전환되자마자 그 자리에서 플레이어를 한 대 때리는" 버그 제거. (이미 Attack 상태라
			//      진입조건 재평가가 아니라 '이탈조건'이 관건이라 !bCheckInside 가 핵심.)
			return !bCheckInside;
		}
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
