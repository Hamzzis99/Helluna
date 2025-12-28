#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "HellunaHeroController.generated.h"

UCLASS()
class HELLUNA_API AHellunaHeroController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AHellunaHeroController();
	
	virtual FGenericTeamId GetGenericTeamId() const override;

protected:
	virtual void BeginPlay() override;

	// 컨트롤러 블루프린트 
	UPROPERTY(EditAnywhere, Category = "Helluna | Controller")
	TSubclassOf<APlayerController> ControllerBlueprint;
	
	void CreateFunctionalController();

private:
	UPROPERTY()
	TObjectPtr<APlayerController> ActiveController;

	FGenericTeamId HeroTeamID;
};