/**
 * EnvQueryContext_SpaceShip.cpp
 *
 * 우주선 Actor를 EQS Context로 제공한다.
 * FocusActor가 우주선이면 바로 사용, 아니면 월드에서 태그 탐색.
 *
 * @author 김민우
 */

#include "AI/EQS/EnvQueryContext_SpaceShip.h"

#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"

void UEnvQueryContext_SpaceShip::ProvideContext(
	FEnvQueryInstance& QueryInstance,
	FEnvQueryContextData& ContextData) const
{
	AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor) return;

	APawn* QuerierPawn = Cast<APawn>(QuerierActor);
	if (!QuerierPawn) return;

	AAIController* AIC = Cast<AAIController>(QuerierPawn->GetController());
	if (!AIC) return;

	// 1순위: FocusActor가 우주선 태그를 가지고 있으면 바로 사용
	AActor* FocusActor = AIC->GetFocusActor();
	if (IsValid(FocusActor) && FocusActor->ActorHasTag(SpaceShipTag))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, FocusActor);
		return;
	}

	// 2순위: 월드에서 태그로 우주선 탐색
	const UWorld* World = QuerierPawn->GetWorld();
	if (!World) return;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(SpaceShipTag))
		{
			UEnvQueryItemType_Actor::SetContextHelper(ContextData, *It);
			return;
		}
	}
}