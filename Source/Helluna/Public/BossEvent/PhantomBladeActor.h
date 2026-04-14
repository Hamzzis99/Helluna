// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhantomBladeActor.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * APhantomBladeActor
 *
 * 환영 검무 패턴에서 리사주 궤적을 따라 비행하는 팬텀 검 액터.
 * bReplicateMovement = true 로 서버에서 SetActorLocation() 호출 시
 * 클라이언트에 자동 동기화되며, 루트에 붙은 NiagaraComponent가 따라감.
 *
 * VFX 에셋과 스케일은 BP에서 직접 설정한다.
 * Zone에서는 이 액터의 BP 클래스를 지정하여 스폰만 담당.
 */
UCLASS(Blueprintable)
class HELLUNA_API APhantomBladeActor : public AActor
{
	GENERATED_BODY()

public:
	APhantomBladeActor();

	/** VFX 비활성화 (패턴 종료 시) */
	void DeactivateVFX();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> BladeVFXComp;

	/** 검 VFX 에셋 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "VFX",
		meta = (DisplayName = "검 VFX"))
	TObjectPtr<UNiagaraSystem> BladeVFX = nullptr;

	/** 검 VFX 크기 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "VFX",
		meta = (DisplayName = "검 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float BladeVFXScale = 1.f;
};
