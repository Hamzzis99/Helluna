// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"

// [MDF 추가] 플러그인 인터페이스 헤더 포함
#include "Interface/MDF_GameStateInterface.h"

#include "HellunaDefenseGameState.generated.h"

/**
 * */

UENUM(BlueprintType)
enum class EDefensePhase : uint8
{
	Day,
	Night
};

class AResourceUsingObject_SpaceShip;

// [MDF 수정] public IMDF_GameStateInterface 추가 찌그러진 함수가
UCLASS()
class HELLUNA_API AHellunaDefenseGameState : public AGameStateBase, public IMDF_GameStateInterface
{
	GENERATED_BODY()
    
public:
	UFUNCTION(BlueprintPure, Category = "Defense")
	AResourceUsingObject_SpaceShip* GetSpaceShip() const { return SpaceShip; }

	void RegisterSpaceShip(AResourceUsingObject_SpaceShip* InShip);

	UFUNCTION(BlueprintPure, Category = "Defense")
	EDefensePhase GetPhase() const { return Phase; }

	void SetPhase(EDefensePhase NewPhase);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPrintNight(int32 Current, int32 Need);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPrintDay();
	
	// =============================================================================================================================
	// [기현 추가] MDF Interface 구현 (SaveMDFData / LoadMDFData) (아직 프로토타입 단계 문제 생길 시 김기현에게 문의 및 주석 처리 하세요!!!)
	// =============================================================================================================================
	virtual void SaveMDFData(const FGuid& ID, const TArray<FMDFHitData>& Data) override; // 상속 받는 개념이라 주석 처리 해도 코드 잘 작동 합니다!
	virtual bool LoadMDFData(const FGuid& ID, TArray<FMDFHitData>& OutData) override; // 상속 받는 개념이라 주석 처리 해도 코드 잘 작동 합니다!

protected:
	/** [MDF 컴포넌트 서버 전용 저장소] */
	TMap<FGuid, TArray<FMDFHitData>> SavedDeformationMap;
    
protected:
	// [팀원 코드 유지]
	UPROPERTY(Replicated)
	TObjectPtr<AResourceUsingObject_SpaceShip> SpaceShip;

	UPROPERTY(Replicated)
	EDefensePhase Phase = EDefensePhase::Day;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};