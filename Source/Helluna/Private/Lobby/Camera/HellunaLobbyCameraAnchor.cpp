// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyCameraAnchor.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Camera/HellunaLobbyCameraAnchor.h"
#include "Camera/CameraComponent.h"

AHellunaLobbyCameraAnchor::AHellunaLobbyCameraAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	bNetLoadOnClient = true;

	CameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComp"));
	RootComponent = CameraComp;
	CameraComp->SetFieldOfView(FOV);
}

#if WITH_EDITOR
void AHellunaLobbyCameraAnchor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// FOV 변경 시 CameraComp에 자동 동기화
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AHellunaLobbyCameraAnchor, FOV))
	{
		if (CameraComp)
		{
			CameraComp->SetFieldOfView(FOV);
		}
	}
}
#endif
