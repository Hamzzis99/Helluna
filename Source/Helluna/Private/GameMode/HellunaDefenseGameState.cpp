// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/HellunaDefenseGameState.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "DebugHelper.h"
#include "TimerManager.h"
// [김기현 추가] 저장 시스템 및 게임 인스턴스 헤더
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Save/MDF_SaveActor.h"                    // 저장용 액터 클래스 (SaveGame)
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h" // 이사 확인증 확인용

// =========================================================================================
// 생성자 (김기현)
// =========================================================================================

AHellunaDefenseGameState::AHellunaDefenseGameState()
{
    // VoteManagerComponent는 Base(AHellunaBaseGameState)에서 생성됨
}

// =========================================================================================
// [민우님 작업 영역] 기존 팀원 코드
// =========================================================================================

void AHellunaDefenseGameState::RegisterSpaceShip(AResourceUsingObject_SpaceShip* InShip)
{
    if (!HasAuthority())
       return;

    SpaceShip = InShip;
}

void AHellunaDefenseGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // UsedCharacters는 Base(AHellunaBaseGameState)에서 복제됨
    DOREPLIFETIME(AHellunaDefenseGameState, SpaceShip);
    DOREPLIFETIME_CONDITION_NOTIFY(AHellunaDefenseGameState, Phase, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME(AHellunaDefenseGameState, AliveMonsterCount);
}

void AHellunaDefenseGameState::SetPhase(EDefensePhase NewPhase)
{
    if (!HasAuthority())
       return;

    Phase = NewPhase;

    // 서버에서는 OnRep이 자동 호출되지 않으므로 직접 호출
    OnRep_Phase();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase RepNotify - 클라이언트에서 Phase 복제 시 자동 호출
// 서버에서는 SetPhase() 내부에서 직접 호출
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::OnRep_Phase()
{
    UE_LOG(LogTemp, Warning, TEXT("[GameState] OnRep_Phase 호출됨! Phase=%d, HasAuthority=%d"),
        (int32)Phase, HasAuthority());

    switch (Phase)
    {
    case EDefensePhase::Day:
        UE_LOG(LogTemp, Warning, TEXT("[GameState] OnDayStarted 호출 시도"));
        OnDayStarted();
        break;
    case EDefensePhase::Night:
        UE_LOG(LogTemp, Warning, TEXT("[GameState] OnNightStarted 호출 시도"));
        OnNightStarted();
        break;
    default:
        break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 새벽 완료 Multicast RPC — 모든 클라이언트에서 OnDawnPassed(BP) 호출
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::NetMulticast_OnDawnPassed_Implementation(float RoundDuration)
{
    UE_LOG(LogTemp, Warning, TEXT("[GameState] OnDawnPassed 호출! RoundDuration=%.1f초"), RoundDuration);
    OnDawnPassed(RoundDuration);
}

void AHellunaDefenseGameState::MulticastPrintNight_Implementation(int32 Current, int32 Need)
{
    Debug::Print(FString::Printf(TEXT("Night! SpaceShip Repair: %d / %d"), Current, Need));


    // 디버그용 

    GetWorldTimerManager().ClearTimer(TimerHandle_NightDebug);

    PrintNightDebug();

    GetWorldTimerManager().SetTimer(
        TimerHandle_NightDebug,
        this,
        &ThisClass::PrintNightDebug,
        NightDebugInterval,
        true
    );
}

void AHellunaDefenseGameState::PrintNightDebug()
{
    // ✅ 여기서 "현재 복제된 몬스터 수"를 출력
    // AliveMonsterCount가 멤버면 그대로 쓰면 되고,
    // Getter가 있으면 GetAliveMonsterCount()로 바꿔도 됨.

    Debug::Print(FString::Printf(
        TEXT("Night Debug | AliveMonsters: %d"),
        AliveMonsterCount
    ));
}


void AHellunaDefenseGameState::MulticastPrintDay_Implementation()
{
    Debug::Print(TEXT("Day!"));
}

void AHellunaDefenseGameState::SetAliveMonsterCount(int32 NewCount)
{
    if (!HasAuthority()) return;

    // ✅ 음수 방지(안전)
    AliveMonsterCount = FMath::Max(0, NewCount);
}

// =============================================================================================================================
// [김기현 작업 영역 시작] MDF 구현 및 저장 시스템
// 내용: 맵 이동 시 메쉬 변형 데이터 및 게임 진행 상황을 저장/로드합니다.
// =============================================================================================================================

void AHellunaDefenseGameState::BeginPlay()
{
    Super::BeginPlay();

    // 데이터 관리는 오직 서버(Authority)에서만 수행합니다.
    if (HasAuthority())
    {
        bool bShouldLoad = false;
        UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
        
        if (GI && GI->bIsMapTransitioning)
        {
            // Case A: 정상적인 맵 이동(이사)으로 넘어온 경우 -> 데이터 로드
            UE_LOG(LogTemp, Warning, TEXT("[HellunaGameState] '맵 이동' 확인증 발견! 데이터를 유지합니다."));
            bShouldLoad = true;
            GI->bIsMapTransitioning = false; // 확인증 회수
        }
        else
        {
            // Case B: 새 게임이거나, 에디터 시작 -> 데이터 초기화
            UE_LOG(LogTemp, Warning, TEXT("[HellunaGameState] 확인증 없음 (새 게임). 기존 데이터를 파기합니다."));
            if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
            {
                UGameplayStatics::DeleteGameInSlot(SaveSlotName, 0);
            }
            bShouldLoad = false;
        }

        // 로드 실행
        if (bShouldLoad)
        {
            if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
            {
                UMDF_SaveActor* LoadedGame = Cast<UMDF_SaveActor>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));

                if (LoadedGame)
                {
                    // 1. MDF(변형) 데이터 복구
                    SavedDeformationMap.Empty();
                    for (const auto& Pair : LoadedGame->SavedDeformationMap)
                    {
                        FMDFHitHistoryWrapper NewWrapper;
                        NewWrapper.History = Pair.Value.History; 
                        SavedDeformationMap.Add(Pair.Key, NewWrapper);
                    }

                    // -----------------------------------------------------------------------------------------
                    // [팀원 가이드: 우주선 수리 데이터 로드하려면?]
                    // -----------------------------------------------------------------------------------------
                    // 만약 우주선 수리 진행도(int)나 현재 페이즈 정보를 다음 맵으로 가져오고 싶다면
                    // 여기에 로직을 추가하시면 됩니다. (MDF_SaveActor에 변수 추가 필요)
                    // 예시:
                    // if (SpaceShip) 
                    // {
                    //    SpaceShip->SetRepairProgress(LoadedGame->SavedSpaceShipRepairProgress);
                    // }
                    // -----------------------------------------------------------------------------------------

                    UE_LOG(LogTemp, Warning, TEXT("[HellunaGameState] 데이터 로드 완료! (MDF 객체 수: %d)"), SavedDeformationMap.Num());
                }
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[HellunaGameState] 신규 시작이므로 데이터를 로드하지 않았습니다."));
        }
    }
}

void AHellunaDefenseGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        WriteDataToDisk();
    }
    Super::EndPlay(EndPlayReason);
}

void AHellunaDefenseGameState::SaveMDFData(const FGuid& ID, const TArray<FMDFHitData>& Data)
{
    // 서버만 저장 가능
    if (!HasAuthority()) return;
    
    if (ID.IsValid())
    {
       // Wrapper 구조체를 통해 TMap에 저장 (TArray 직접 중첩 방지)
       FMDFHitHistoryWrapper& Wrapper = SavedDeformationMap.FindOrAdd(ID);
       Wrapper.History = Data;
       
       // UE_LOG(LogTemp, Log, TEXT("[GameState] ID: %s 메모리 갱신 완료"), *ID.ToString());
    }
}

bool AHellunaDefenseGameState::LoadMDFData(const FGuid& ID, TArray<FMDFHitData>& OutData)
{
    // 서버만 로드 가능
    if (!HasAuthority()) return false;

    if (ID.IsValid() && SavedDeformationMap.Contains(ID))
    {
       // Wrapper 안의 History를 꺼내서 반환
       OutData = SavedDeformationMap[ID].History;
       return true;
    }

    return false;
}

// Server_SaveAndMoveLevel()은 Base(AHellunaBaseGameState)로 이동됨

void AHellunaDefenseGameState::OnPreMapTransition()
{
    WriteDataToDisk();
}

void AHellunaDefenseGameState::WriteDataToDisk()
{
    if (!HasAuthority()) return;

    UMDF_SaveActor* SaveInst = Cast<UMDF_SaveActor>(UGameplayStatics::CreateSaveGameObject(UMDF_SaveActor::StaticClass()));

    if (SaveInst)
    {
        // 1. MDF 데이터 저장 (메모리 -> 파일 객체)
        for (const auto& Pair : SavedDeformationMap)
        {
            FGuid CurrentGUID = Pair.Key;
            
            // SaveActor에도 FMDFHistoryWrapper 타입으로 정의되어 있어야 함
            FMDFHistoryWrapper SaveWrapper; 
            SaveWrapper.History = Pair.Value.History; 

            SaveInst->SavedDeformationMap.Add(CurrentGUID, SaveWrapper);
        }

        // -----------------------------------------------------------------------------------------
        // [팀원 가이드: 우주선 데이터 저장하려면?]
        // -----------------------------------------------------------------------------------------
        // 맵 이동 시 우주선 수리 상태도 저장해야 한다면 여기서 SaveInst에 값을 넣어주세요.
        // (먼저 MDF_SaveActor.h 파일에 변수(예: int32 SavedSpaceShipRepairProgress)를 추가해야 합니다)
        // 예시:
        // if (SpaceShip)
        // {
        //     SaveInst->SavedSpaceShipRepairProgress = SpaceShip->GetRepairProgress();
        //     SaveInst->SavedPhase = Phase;
        // }
        // -----------------------------------------------------------------------------------------

        // 파일 쓰기 수행
        if (UGameplayStatics::SaveGameToSlot(SaveInst, SaveSlotName, 0))
        {
            // UE_LOG(LogTemp, Log, TEXT("[HellunaGameState] 디스크 저장 성공."));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[HellunaGameState] 디스크 저장 실패!"));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🎭 캐릭터 선택 시스템은 Base(AHellunaBaseGameState)로 이동됨
// ═══════════════════════════════════════════════════════════════════════════════