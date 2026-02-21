// Gihyeon's MeshDeformation Project

#pragma once

#include "CoreMinimal.h"
#include "Weapons/MDF_BaseWeapon.h"
#include "MDF_LaserWeapon.generated.h"

// [전방 선언] 미니게임 컴포넌트를 알기 위해
class UMDF_MiniGameComponent;

/**
 * [Step 2] 리더용 레이저 무기
 * - Tick을 돌며 레이저를 쏘고, 벽(MiniGameComponent)을 발견하면 마킹 명령을 내립니다.
 */
UCLASS()
class MESHDEFORMATION_API AMDF_LaserWeapon : public AMDF_BaseWeapon
{
	GENERATED_BODY()
	
public:
	AMDF_LaserWeapon();

protected:
	virtual void BeginPlay() override;

public:
	// 매 프레임 레이저 추적
	virtual void Tick(float DeltaTime) override;

	// 발사 시작/중지 오버라이드
	virtual void StartFire() override;
	virtual void StopFire() override;

protected:
	// 레이저 로직
	void ProcessLaserTrace();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Stats", meta = (DisplayName = "초당 배터리 소모량"))
	float BatteryDrainRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Effects", meta = (DisplayName = "레이저 색상 (Debug)"))
	FColor LaserColor;

	/** true: 화면 중앙(크로스헤어) 기준, false: 총구(Muzzle) 기준 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Stats", meta = (DisplayName = "화면 중앙 기준 사용"))
	bool bUseScreenCenter;

private:
	// 현재 마킹 중인 벽 컴포넌트 (기억용)
	UPROPERTY()
	TObjectPtr<UMDF_MiniGameComponent> CurrentTargetComp;
	
	// 마지막 히트 위치 (EndMarking에서 사용)
	FVector LastHitLocation;
};