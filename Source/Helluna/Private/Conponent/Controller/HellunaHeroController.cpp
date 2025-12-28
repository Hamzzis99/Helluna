#include "Controller/HellunaHeroController.h"

AHellunaHeroController::AHellunaHeroController()
{
	HeroTeamID = FGenericTeamId(0);
}

void AHellunaHeroController::BeginPlay()
{
	Super::BeginPlay();
	
	CreateFunctionalController();
}

void AHellunaHeroController::CreateFunctionalController()
{
	// 입력 조작 블루프린트 받는지 확인하기
	if (ControllerBlueprint)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = GetInstigator();
		
		ActiveController = GetWorld()->SpawnActor<APlayerController>(ControllerBlueprint, SpawnParams);

		if (ActiveController)
		{
			UE_LOG(LogTemp, Warning, TEXT("성공: 블루프린트 기능을 가진 컨트롤러가 생성되었습니다!"));
		}
	}
}

FGenericTeamId AHellunaHeroController::GetGenericTeamId() const
{
	return HeroTeamID;
}