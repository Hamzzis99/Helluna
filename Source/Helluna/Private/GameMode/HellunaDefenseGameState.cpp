// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/HellunaDefenseGameState.h"
#include "Net/UnrealNetwork.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

#include "DebugHelper.h"

void AHellunaDefenseGameState::RegisterSpaceShip(AResourceUsingObject_SpaceShip* InShip)
{
    if (!HasAuthority())
       return;

    SpaceShip = InShip;
}


void AHellunaDefenseGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AHellunaDefenseGameState, SpaceShip);
    DOREPLIFETIME(AHellunaDefenseGameState, Phase);
}

void AHellunaDefenseGameState::SetPhase(EDefensePhase NewPhase)
{
    if (!HasAuthority())
       return;

    Phase = NewPhase;
}

void AHellunaDefenseGameState::MulticastPrintNight_Implementation(int32 Current, int32 Need)
{
    Debug::Print(FString::Printf(TEXT("Night! SpaceShip Repair: %d / %d"), Current, Need));
}

void AHellunaDefenseGameState::MulticastPrintDay_Implementation()
{
    Debug::Print(TEXT("Day!"));
}

// =============================================================================================================================
// [기현 추가] MDF Interface 구현 (SaveMDFData / LoadMDFData) (아직 프로토타입 단계 작동 안 할시 김기현에게 문의 및 주석 처리 하세요!!!)
// 상속 받는 개념이라 주석 처리 해도 코드 잘 작동 합니다!
// =============================================================================================================================

void AHellunaDefenseGameState::SaveMDFData(const FGuid& ComponentID, const TArray<FMDFHitData>& History)
{
    // 서버만 저장 가능
    if (!HasAuthority()) return;
    
    if (ComponentID.IsValid())
    {
       SavedDeformationMap.Add(ComponentID, History);
       UE_LOG(LogTemp, Log, TEXT("[GameState] ID: %s 데이터 저장 완료 (개수: %d)"), *ComponentID.ToString(), History.Num());
    }
}

bool AHellunaDefenseGameState::LoadMDFData(const FGuid& ComponentID, TArray<FMDFHitData>& OutHistory)
{
    // 서버만 로드 가능
    if (!HasAuthority()) return false;

    if (ComponentID.IsValid() && SavedDeformationMap.Contains(ComponentID))
    {
       OutHistory = SavedDeformationMap[ComponentID];
       return true;
    }

    return false;
}