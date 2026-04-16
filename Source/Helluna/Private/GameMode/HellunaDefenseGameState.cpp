// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/HellunaDefenseGameState.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Helluna.h"
#include "Net/UnrealNetwork.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "DebugHelper.h"
#include "TimerManager.h"
// [김기현 추가] 저장 시스템 및 게임 인스턴스 헤더
#include "Kismet/GameplayStatics.h"
#include "PCGComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"  // TActorIterator
#include "Save/MDF_SaveActor.h"                    // 저장용 액터 클래스 (SaveGame)
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h" // 이사 확인증 확인용
#include "Chat/HellunaChatTypes.h"
#include "Components/VolumetricCloudComponent.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

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
    DOREPLIFETIME_CONDITION_NOTIFY(AHellunaDefenseGameState, AliveMonsterCount, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME(AHellunaDefenseGameState, DayTimeRemaining);
    DOREPLIFETIME(AHellunaDefenseGameState, TotalMonstersThisNight);
    DOREPLIFETIME(AHellunaDefenseGameState, CurrentDayForUI);
    DOREPLIFETIME(AHellunaDefenseGameState, bIsBossNight);
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
#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[GameState] OnRep_Phase 호출됨! Phase=%d, HasAuthority=%d"),
        (int32)Phase, HasAuthority());
#endif

    switch (Phase)
    {
    case EDefensePhase::Day:
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[GameState] OnDayStarted 호출 시도"));
#endif
        CleanupInitialNightPCGClientArtifacts();
        bIsCurrentlyRaining = false;    // 낮 → 비 종료
        OnDayStarted();
        if (bHasUDS) SetVolumetricCloudVisible(true);   // 낮: 구름 ON
        if (bHasUDW) ApplyRandomWeather(true);
        break;
    case EDefensePhase::Night:
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[GameState] OnNightStarted 호출 시도"));
#endif
        OnNightStarted();
        bHasBeenNight = true;  // ★ 밤 경험 기록
        // [Patch 1] UDW 레플리케이션 타이밍과 무관하게 밤 고정 날씨 기준으로 비 상태 즉시 확정
        //   bHasUDW가 false(클라이언트 BeginPlay 시점에 UDW 액터 미로드)여도
        //   NightForcedWeather 이름으로 bIsCurrentlyRaining을 먼저 설정하여 MPC 축적 타이머가 동작하도록 보장.
        if (NightForcedWeather)
        {
            const FString ForcedName = NightForcedWeather->GetName();
            bIsCurrentlyRaining = ForcedName.Contains(TEXT("Rain"), ESearchCase::IgnoreCase);
        }
        // 밤: NightWeatherTypes에서 랜덤 선택 (비가 오도록 배열에 Rain 프리셋 배치)
        // 구름 토글은 UDW Change Weather 이후에 적용해야 UDW가 켠 구름이 다시 꺼짐.
        if (bHasUDW) ApplyRandomWeather(false);
        if (bHasUDS) SetVolumetricCloudVisible(false);  // 밤: 구름 OFF (오로라/분위기 가시성)
        // bIsCurrentlyRaining 추가 보정은 ApplyRandomWeather 내부 (에셋 이름 기반)에서 수행
        // ★ Animate OFF — 현재 시간에서 멈춤
        if (bHasUDS)
        {
            AActor* UDS = GetUDSActor();
            if (UDS)
            {
                // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용
                if (FBoolProperty* AnimProp = CastField<FBoolProperty>(CachedProp_Animate))
                    AnimProp->SetPropertyValue_InContainer(UDS, false);
            }
        }
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
#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[GameState] OnDawnPassed! RoundDuration=%.1f초, Authority=%d"),
        RoundDuration, HasAuthority());
#endif

    // BP 이벤트 호출
    OnDawnPassed(RoundDuration);

    // ★ UDS가 없으면 스킵 (데디서버)
    if (!bHasUDS) return;

    AActor* UDS = GetUDSActor();
    if (!UDS) return;

    // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용 — Animate OFF (전환 중 자체 애니메이션 방지)
    if (FBoolProperty* AnimProp = CastField<FBoolProperty>(CachedProp_Animate))
        AnimProp->SetPropertyValue_InContainer(UDS, false);

    // [Step3 O-02] 캐싱된 프로퍼티로 현재 UDS Time of Day 읽기
    float CurrentTime = 0.f;
    if (FFloatProperty* Prop = CastField<FFloatProperty>(CachedProp_TimeOfDay))
        CurrentTime = Prop->GetPropertyValue_InContainer(UDS);
    else if (FDoubleProperty* DProp = CastField<FDoubleProperty>(CachedProp_TimeOfDay))
        CurrentTime = (float)DProp->GetPropertyValue_InContainer(UDS);

    // DawnTransitionDuration이 0 이하이면 즉시 전환
    if (DawnTransitionDuration <= 0.f)
    {
        SetUDSTimeOfDay(DayStartTime);

        float TimeRange = DayEndTime - DayStartTime;
        if (TimeRange <= 0.f) TimeRange = 1000.f;
        float DayLength = 20.f * RoundDuration / TimeRange;

        // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용 — Day Length 설정
        if (FFloatProperty* DLProp = CastField<FFloatProperty>(CachedProp_DayLength))
            DLProp->SetPropertyValue_InContainer(UDS, DayLength);
        else if (FDoubleProperty* DLDProp = CastField<FDoubleProperty>(CachedProp_DayLength))
            DLDProp->SetPropertyValue_InContainer(UDS, (double)DayLength);

        // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용 — Animate ON
        if (FBoolProperty* AnimProp2 = CastField<FBoolProperty>(CachedProp_Animate))
            AnimProp2->SetPropertyValue_InContainer(UDS, true);

#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[GameState] %s: 즉시 전환 DayLength=%.3f"),
            HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"), DayLength);
#endif
        return;
    }

    // 새벽 Lerp 시작 준비
    DawnLerpStart = CurrentTime;
    DawnLerpElapsed = 0.f;
    DawnTotalDistance = (2400.f - CurrentTime) + DayStartTime;
    PendingRoundDuration = RoundDuration;

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[GameState] %s: 새벽 전환 시작! 현재=%.0f → 목표=%.0f (이동량=%.0f, %.1f초)"),
        HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
        CurrentTime, DayStartTime, DawnTotalDistance, DawnTransitionDuration);
#endif

    // ~60fps 루핑 타이머 시작
    GetWorldTimerManager().ClearTimer(TimerHandle_DawnTransition);
    GetWorldTimerManager().SetTimer(
        TimerHandle_DawnTransition,
        this,
        &ThisClass::TickDawnTransition,
        0.016f,
        true
    );
}

// ═══════════════════════════════════════════════════════════════════════════════
// 새벽 전환 Tick — 밤→새벽→아침 UDS 시간 보간
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::TickDawnTransition()
{
    // Phase가 Day가 아니면 전환 중단 (안전장치)
    if (Phase != EDefensePhase::Day)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_DawnTransition);
        return;
    }

    // [Step3 O-03] 고정 delta 제거 - 실제 경과 시간 사용
    // 타이머는 0.016f 간격이지만 실제 호출 간격은 프레임에 따라 다를 수 있음
    UWorld* World = GetWorld();
    const float DeltaTime = World ? World->GetDeltaSeconds() : 0.016f;
    DawnLerpElapsed += DeltaTime;
    float Alpha = FMath::Clamp(DawnLerpElapsed / DawnTransitionDuration, 0.f, 1.f);

    float NewTime = DawnLerpStart + (DawnTotalDistance * Alpha);
    if (NewTime >= 2400.f) NewTime -= 2400.f;

    SetUDSTimeOfDay(NewTime);

    // 전환 완료
    if (Alpha >= 1.0f)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_DawnTransition);

        // 정확히 DayStartTime에 맞추기
        SetUDSTimeOfDay(DayStartTime);

        // 기존 Day Length 계산 + Animate ON 로직
        AActor* UDS = GetUDSActor();
        if (UDS)
        {
            float TimeRange = DayEndTime - DayStartTime;
            if (TimeRange <= 0.f) TimeRange = 1000.f;
            float DayLength = 20.f * PendingRoundDuration / TimeRange;

            // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용 — Day Length 설정
            if (FFloatProperty* DLProp = CastField<FFloatProperty>(CachedProp_DayLength))
                DLProp->SetPropertyValue_InContainer(UDS, DayLength);
            else if (FDoubleProperty* DLDProp = CastField<FDoubleProperty>(CachedProp_DayLength))
                DLDProp->SetPropertyValue_InContainer(UDS, (double)DayLength);

            // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용 — Animate ON
            if (FBoolProperty* AnimProp = CastField<FBoolProperty>(CachedProp_Animate))
                AnimProp->SetPropertyValue_InContainer(UDS, true);

#if HELLUNA_DEBUG_DEFENSE
            UE_LOG(LogTemp, Warning, TEXT("[GameState] %s: 새벽 전환 완료! DayLength=%.3f, TimeRange=%.0f"),
                HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"), DayLength, TimeRange);
#endif
        }
    }
}

void AHellunaDefenseGameState::MulticastPrintNight_Implementation(int32 Current, int32 Need)
{
#if HELLUNA_DEBUG_DEFENSE
    // 디버그 전용: 화면 출력 및 몹 수 모니터링 타이머
    Debug::Print(FString::Printf(TEXT("Night! SpaceShip Repair: %d / %d"), Current, Need));

    GetWorldTimerManager().ClearTimer(TimerHandle_NightDebug);

    PrintNightDebug();

    GetWorldTimerManager().SetTimer(
        TimerHandle_NightDebug,
        this,
        &ThisClass::PrintNightDebug,
        NightDebugInterval,
        true
    );
#endif
}

#if HELLUNA_DEBUG_DEFENSE
void AHellunaDefenseGameState::PrintNightDebug()
{
    // 현재 복제된 몬스터 수를 화면에 디버그 출력
    Debug::Print(FString::Printf(
        TEXT("Night Debug | AliveMonsters: %d"),
        AliveMonsterCount
    ));
}
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// 🔍 UDS 디버그: 1초마다 Time of Day, Animate 상태, Phase 출력
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::PrintUDSDebug()
{
    AActor* UDS = GetUDSActor();  // 캐시 사용
    if (!UDS) return;

    float TimeOfDay = -1.f;
    bool bAnimate = false;
    float DayLength = -1.f;
    
    // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용 — 매 호출마다 FindFProperty 제거
    if (FFloatProperty* Prop = CastField<FFloatProperty>(CachedProp_TimeOfDay))
        TimeOfDay = Prop->GetPropertyValue_InContainer(UDS);
    else if (FDoubleProperty* DProp = CastField<FDoubleProperty>(CachedProp_TimeOfDay))
        TimeOfDay = (float)DProp->GetPropertyValue_InContainer(UDS);

    if (FBoolProperty* AnimProp = CastField<FBoolProperty>(CachedProp_Animate))
        bAnimate = AnimProp->GetPropertyValue_InContainer(UDS);

    if (FFloatProperty* DLProp = CastField<FFloatProperty>(CachedProp_DayLength))
        DayLength = DLProp->GetPropertyValue_InContainer(UDS);
    else if (FDoubleProperty* DLDProp = CastField<FDoubleProperty>(CachedProp_DayLength))
        DayLength = (float)DLDProp->GetPropertyValue_InContainer(UDS);

#if HELLUNA_DEBUG_UDS
    UE_LOG(LogTemp, Warning, TEXT("[UDS Debug] %s | Phase=%s | TimeOfDay=%.2f | Animate=%s | DayLength=%.2f"),
        HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"),
        Phase == EDefensePhase::Day ? TEXT("Day") : TEXT("Night"),
        TimeOfDay,
        bAnimate ? TEXT("ON") : TEXT("OFF"),
        DayLength);
#endif
}


void AHellunaDefenseGameState::MulticastPrintDay_Implementation()
{
#if HELLUNA_DEBUG_DEFENSE
    Debug::Print(TEXT("Day!"));
#endif
}

void AHellunaDefenseGameState::SetAliveMonsterCount(int32 NewCount)
{
    if (!HasAuthority()) return;

    // ✅ 음수 방지(안전)
    AliveMonsterCount = FMath::Max(0, NewCount);
    ForceNetUpdate(); // 즉시 복제 강제
}
void AHellunaDefenseGameState::SetDayTimeRemaining(float InTime)
{
    if (!HasAuthority()) return;
    DayTimeRemaining = FMath::Max(0.f, InTime);
}

void AHellunaDefenseGameState::SetTotalMonstersThisNight(int32 InTotal)
{
    if (!HasAuthority()) return;
    TotalMonstersThisNight = FMath::Max(0, InTotal);
}

void AHellunaDefenseGameState::SetCurrentDayForUI(int32 InDay)
{
    if (!HasAuthority()) return;
    CurrentDayForUI = InDay;
}

void AHellunaDefenseGameState::SetIsBossNight(bool bInVal)
{
    if (!HasAuthority()) return;
    bIsBossNight = bInVal;
}


// =============================================================================================================================
// [김기현 작업 영역 시작] MDF 구현 및 저장 시스템
// 내용: 맵 이동 시 메쉬 변형 데이터 및 게임 진행 상황을 저장/로드합니다.
// =============================================================================================================================

void AHellunaDefenseGameState::BeginPlay()
{
    Super::BeginPlay();

    // ★ UDS/UDW 존재 여부 1회 체크 (데디서버에서 없을 수 있음)
    bHasUDS = (GetUDSActor() != nullptr);
    bHasUDW = (GetUDWActor() != nullptr);

    // [Step3 O-02] UDS 프로퍼티 캐싱 (FindFProperty를 매 프레임 호출하지 않도록)
    if (bHasUDS)
    {
        CacheUDSProperties();
    }

#if HELLUNA_DEBUG_DEFENSE
    if (!bHasUDS)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameState] UDS 액터 없음 (데디서버 또는 미배치)"));
    }
    if (!bHasUDW)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameState] UDW 액터 없음 (데디서버 또는 미배치)"));
    }
#endif

    // ── 비 MPC 점진 Push 타이머 (렌더링 가능한 곳에서만) ──────────────────
    if (GetNetMode() != NM_DedicatedServer)
    {
        AccumulatedRainSeconds = 0.f;
        bIsCurrentlyRaining = false;
        const float Interval = FMath::Max(0.05f, PuddleTickInterval);
        GetWorldTimerManager().SetTimer(
            TimerHandle_PuddleAccumulation,
            this,
            &ThisClass::TickRainMPC,
            Interval,
            true,
            Interval
        );
    }

#if !UE_BUILD_SHIPPING && HELLUNA_DEBUG_UDS
    // 디버그 빌드에서만 UDS 로깅 (1초 간격)
    if (bHasUDS)
    {
        GetWorldTimerManager().SetTimer(
            TimerHandle_UDSDebug,
            this,
            &ThisClass::PrintUDSDebug,
            1.0f,
            true,
            2.0f
        );
    }
#endif

    // 데이터 관리는 오직 서버(Authority)에서만 수행합니다.
    if (HasAuthority())
    {
        bool bShouldLoad = false;
        UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
        
        if (GI && GI->bIsMapTransitioning)
        {
            // Case A: 정상적인 맵 이동(이사)으로 넘어온 경우 -> 데이터 로드
#if HELLUNA_DEBUG_DEFENSE
            UE_LOG(LogTemp, Warning, TEXT("[HellunaGameState] '맵 이동' 확인증 발견! 데이터를 유지합니다."));
#endif
            bShouldLoad = true;
            GI->bIsMapTransitioning = false; // 확인증 회수
        }
        else
        {
            // Case B: 새 게임이거나, 에디터 시작 -> 데이터 초기화
#if HELLUNA_DEBUG_DEFENSE
            UE_LOG(LogTemp, Warning, TEXT("[HellunaGameState] 확인증 없음 (새 게임). 기존 데이터를 파기합니다."));
#endif
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

#if HELLUNA_DEBUG_DEFENSE
                    UE_LOG(LogTemp, Warning, TEXT("[HellunaGameState] 데이터 로드 완료! (MDF 객체 수: %d)"), SavedDeformationMap.Num());
#endif
                }
            }
        }
        else
        {
#if HELLUNA_DEBUG_DEFENSE
            UE_LOG(LogTemp, Log, TEXT("[HellunaGameState] 신규 시작이므로 데이터를 로드하지 않았습니다."));
#endif
        }
    }
}

void AHellunaDefenseGameState::CleanupInitialNightPCGClientArtifacts()
{
    if (HasAuthority() || bHasBeenNight || bInitialNightPCGArtifactsCleaned)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    TArray<AActor*> PCGActors;
    UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("NightPCG")), PCGActors);
    for (AActor* PCGActor : PCGActors)
    {
        if (!IsValid(PCGActor))
        {
            continue;
        }

        if (UPCGComponent* PCGComp = PCGActor->FindComponentByClass<UPCGComponent>())
        {
            PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
            PCGComp->CleanupLocal(true);
            PCGComp->Deactivate();
        }
    }

    TArray<AActor*> ExistingOres;
    UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("Ore")), ExistingOres);
    for (AActor* OreActor : ExistingOres)
    {
        if (!IsValid(OreActor) || OreActor->GetIsReplicated())
        {
            continue;
        }

        OreActor->SetActorHiddenInGame(true);
        OreActor->SetActorEnableCollision(false);
        OreActor->SetActorTickEnabled(false);
    }

    bInitialNightPCGArtifactsCleaned = true;
}

void AHellunaDefenseGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(TimerHandle_UDSDebug);
#if HELLUNA_DEBUG_DEFENSE
    GetWorldTimerManager().ClearTimer(TimerHandle_NightDebug);
#endif
    GetWorldTimerManager().ClearTimer(TimerHandle_DawnTransition);
    GetWorldTimerManager().ClearTimer(TimerHandle_PuddleAccumulation);

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
#if HELLUNA_DEBUG_DEFENSE
            UE_LOG(LogTemp, Error, TEXT("[HellunaGameState] 디스크 저장 실패!"));
#endif
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// [Phase 10] 채팅 시스템
// ═══════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameState::BroadcastChatMessage(const FString& SenderName, const FString& Message, EChatMessageType Type)
{
	if (!HasAuthority()) return;

	FChatMessage ChatMsg;
	ChatMsg.SenderName = SenderName;
	ChatMsg.Message = Message;
	ChatMsg.MessageType = Type;
	ChatMsg.ServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// 서버 히스토리에 보관 (최대 100개)
	if (ChatHistory.Num() >= MaxChatHistory)
	{
		ChatHistory.RemoveAt(0);
	}
	ChatHistory.Add(ChatMsg);

	// 모든 클라이언트에 전달
	NetMulticast_ReceiveChatMessage(ChatMsg);
}

void AHellunaDefenseGameState::NetMulticast_ReceiveChatMessage_Implementation(const FChatMessage& ChatMessage)
{
	// 로컬 델리게이트 브로드캐스트 → ChatWidget에서 수신
	OnChatMessageReceived.Broadcast(ChatMessage);

	UE_LOG(LogHellunaChat, Verbose, TEXT("[Chat] %s: %s"),
		ChatMessage.MessageType == EChatMessageType::System ? TEXT("[시스템]") : *ChatMessage.SenderName,
		*ChatMessage.Message);
}

// ===============================================================
// [Step3 O-02] UDS 프로퍼티 캐싱 - BeginPlay에서 1회 호출
// FindFProperty는 리플렉션 기반이라 매 프레임 호출하면 성능 저하
// 캐싱 후에는 포인터만 사용하여 O(1) 접근
// ===============================================================
void AHellunaDefenseGameState::CacheUDSProperties()
{
    AActor* UDS = GetUDSActor();
    if (!UDS) return;

    UClass* UDSClass = UDS->GetClass();
    CachedProp_TimeOfDay = FindFProperty<FProperty>(UDSClass, TEXT("Time of Day"));
    CachedProp_Animate = FindFProperty<FProperty>(UDSClass, TEXT("Animate Time of Day"));
    CachedProp_DayLength = FindFProperty<FProperty>(UDSClass, TEXT("Day Length"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// ☀️ UDS/UDW 헬퍼 함수 - 다이나믹 스카이 날씨변경 부분
// ═══════════════════════════════════════════════════════════════════════════════
AActor* AHellunaDefenseGameState::GetUDSActor()
{
    if (CachedUDS.IsValid())
        return CachedUDS.Get();

    // [Fix26] GetWorld() null 체크 (EndPlay teardown 시 TActorIterator 크래시 방지)
    UWorld* World = GetWorld();
    if (!World) return nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetName().Contains(TEXT("Ultra_Dynamic_Sky")))
        {
            CachedUDS = *It;
            return *It;
        }
    }
    return nullptr;
}

AActor* AHellunaDefenseGameState::GetUDWActor()
{
    if (CachedUDW.IsValid())
        return CachedUDW.Get();

    // [Fix26] GetWorld() null 체크
    UWorld* World = GetWorld();
    if (!World) return nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetName().Contains(TEXT("Ultra_Dynamic_Weather")))
        {
            CachedUDW = *It;
            return *It;
        }
    }
    return nullptr;
}

void AHellunaDefenseGameState::SetUDSTimeOfDay(float Time)
{
    AActor* UDS = GetUDSActor();
    if (!UDS) return;

    // [Step3 O-02] 캐싱된 프로퍼티 포인터 사용 (매 호출마다 FindFProperty 제거)
    if (!CachedProp_TimeOfDay) return;
    if (FFloatProperty* FP = CastField<FFloatProperty>(CachedProp_TimeOfDay))
        FP->SetPropertyValue_InContainer(UDS, Time);
    else if (FDoubleProperty* DP = CastField<FDoubleProperty>(CachedProp_TimeOfDay))
        DP->SetPropertyValue_InContainer(UDS, (double)Time);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🧪 치트 — 시간 정지 토글
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::Cheat_ToggleTimeFrozen()
{
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] GS::Cheat_ToggleTimeFrozen ENTER HasAuthority=%d Current=%d"),
        (int32)HasAuthority(), (int32)bCheatTimeFrozen);
    if (!HasAuthority())
    {
        UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] GS::Cheat_ToggleTimeFrozen ABORT: no authority"));
        return;
    }
    bCheatTimeFrozen = !bCheatTimeFrozen;
    NetMulticast_ApplyCheatTimeFreeze(bCheatTimeFrozen);

    // 낮/밤 전환 타이머(ToNight/ToDay/DayCountdown) pause/unpause — UI 카운트다운 및 밤 전환 차단
    if (UWorld* W = GetWorld())
    {
        if (AHellunaDefenseGameMode* GM = W->GetAuthGameMode<AHellunaDefenseGameMode>())
        {
            GM->Cheat_SetPhaseTimersPaused(bCheatTimeFrozen);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] GS::Cheat_ToggleTimeFrozen DONE -> %s"),
        bCheatTimeFrozen ? TEXT("FROZEN") : TEXT("RUNNING"));
}

void AHellunaDefenseGameState::NetMulticast_ApplyCheatTimeFreeze_Implementation(bool bFreeze)
{
    bCheatTimeFrozen = bFreeze;
    UWorld* W = GetWorld();
    const ENetMode NM = W ? W->GetNetMode() : NM_Standalone;
    UE_LOG(LogTemp, Warning,
        TEXT("[cheatdebug] GS::NetMulticast_ApplyCheatTimeFreeze bFreeze=%d NetMode=%d HasAuth=%d bHasUDS=%d"),
        (int32)bFreeze, (int32)NM, (int32)HasAuthority(), (int32)bHasUDS);

    AActor* UDS = GetUDSActor();
    UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] GS::TimeFreeze UDS=%s CachedProp_Animate=%s"),
        *GetNameSafe(UDS), CachedProp_Animate ? *CachedProp_Animate->GetName() : TEXT("<null>"));
    if (!UDS)
    {
        UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] GS::TimeFreeze ABORT: UDS actor not found"));
        return;
    }

    // 1) Animate Time of Day UPROPERTY OFF
    if (FBoolProperty* AnimProp = CastField<FBoolProperty>(CachedProp_Animate))
    {
        AnimProp->SetPropertyValue_InContainer(UDS, !bFreeze);
    }

    // 2) 정지 시 낮 시간으로 고정
    if (bFreeze)
    {
        SetUDSTimeOfDay(DayStartTime);
    }

    // 3) UDS 액터/컴포넌트 Tick 비활성
    UDS->SetActorTickEnabled(!bFreeze);
    TArray<UActorComponent*> Components;
    UDS->GetComponents(Components);
    for (UActorComponent* Comp : Components)
    {
        if (Comp) Comp->SetComponentTickEnabled(!bFreeze);
    }

    // 4) CustomTimeDilation 해머 — BP Tick이든 Timer든 이 액터에서 발생하는 모든 시간 진행을 0으로 만듦
    UDS->CustomTimeDilation = bFreeze ? 0.f : 1.f;

    UE_LOG(LogTemp, Warning,
        TEXT("[cheatdebug] GS::TimeFreeze APPLIED TickEnabled=%d TimeDilation=%.2f"),
        (int32)UDS->IsActorTickEnabled(), UDS->CustomTimeDilation);

    // 5) 정지 상태 유지 타이머 — Phase 전환 등 외부 로직이 Animate/Tick을 다시 켜더라도 0.2s마다 되돌림
    if (bFreeze)
    {
        GetWorldTimerManager().SetTimer(TimerHandle_CheatTimeFreezeHold, this,
            &AHellunaDefenseGameState::CheatTimeFreeze_HoldTick, 0.2f, true);
    }
    else
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_CheatTimeFreezeHold);
    }
}

void AHellunaDefenseGameState::CheatTimeFreeze_HoldTick()
{
    if (!bCheatTimeFrozen) return;
    AActor* UDS = GetUDSActor();
    if (!UDS) return;

    if (FBoolProperty* AnimProp = CastField<FBoolProperty>(CachedProp_Animate))
    {
        AnimProp->SetPropertyValue_InContainer(UDS, false);
    }
    SetUDSTimeOfDay(DayStartTime);
    UDS->SetActorTickEnabled(false);
    UDS->CustomTimeDilation = 0.f;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🌤️ 랜덤 날씨 시스템
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::ApplyRandomWeather(bool bIsDay)
{
    // 밤은 고정 날씨(비)가 지정되어 있으면 랜덤 선택을 건너뛴다.
    UObject* SelectedWeather = nullptr;
    int32 RandomIdx = -1;
    int32 ArrayNum = 0;

    if (!bIsDay && NightForcedWeather)
    {
        SelectedWeather = NightForcedWeather;
    }
    else
    {
        const TArray<UObject*>& WeatherArray = bIsDay ? DayWeatherTypes : NightWeatherTypes;
        if (WeatherArray.Num() == 0) return;

        RandomIdx = FMath::RandRange(0, WeatherArray.Num() - 1);
        SelectedWeather = WeatherArray[RandomIdx];
        ArrayNum = WeatherArray.Num();
    }
    if (!SelectedWeather) return;

    // 비 판별: 에셋 이름에 "Rain" 포함 시 비로 간주 (Rain, Rain_Light, Rain_Thunderstorm 등)
    const FString WeatherName = SelectedWeather->GetName();
    const bool bIsRainWeather = WeatherName.Contains(TEXT("Rain"), ESearchCase::IgnoreCase);

    if (bIsDay)
    {
        CurrentDayWeather = SelectedWeather;
        // 낮 비 날씨도 지원 (낮에 비가 오면 웅덩이 차오름)
        bIsCurrentlyRaining = bIsRainWeather;
    }
    else
    {
        CurrentNightWeather = SelectedWeather;
        bIsCurrentlyRaining = bIsRainWeather;
    }
    
    AActor* UDW = GetUDWActor();  // 캐시 사용
    if (!UDW) return;
    
    UFunction* Func = UDW->FindFunction(TEXT("Change Weather"));
    if (!Func)
        Func = UDW->FindFunction(TEXT("ChangeWeather"));
    
    if (Func)
    {
        struct { UObject* NewWeatherType; float TransitionTime; } Params;
        Params.NewWeatherType = SelectedWeather;
        Params.TransitionTime = WeatherTransitionTime;
        UDW->ProcessEvent(Func, &Params);
        
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Log, TEXT("[Weather] %s → %s (%d/%d)%s"),
            bIsDay ? TEXT("낮") : TEXT("밤"),
            *SelectedWeather->GetName(),
            RandomIdx, ArrayNum,
            (!bIsDay && NightForcedWeather) ? TEXT(" [Forced]") : TEXT(""));
#endif
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ☁️ 볼류메트릭 클라우드 토글 (밤: OFF, 낮: ON)
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::SetVolumetricCloudVisible(bool bVisible)
{
    AActor* UDS = GetUDSActor();
    if (!UDS) return;

    UVolumetricCloudComponent* CloudComp = UDS->FindComponentByClass<UVolumetricCloudComponent>();
    if (CloudComp)
    {
        CloudComp->SetVisibility(bVisible);
    }

    // Cloud Shadows도 연동 토글
    FProperty* ShadowProp = FindFProperty<FProperty>(UDS->GetClass(), TEXT("Use Cloud Shadows"));
    if (FBoolProperty* BoolProp = CastField<FBoolProperty>(ShadowProp))
    {
        BoolProp->SetPropertyValue_InContainer(UDS, bVisible);
    }

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Log, TEXT("[DayNight] VolumetricCloud %s"), bVisible ? TEXT("ON (낮)") : TEXT("OFF (밤)"));
#endif
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🌙 밤 맑은 날씨 강제 (오로라 가시성 확보)
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::ForceClearWeather()
{
    AActor* UDW = GetUDWActor();
    if (!UDW) return;

    UFunction* Func = UDW->FindFunction(TEXT("Change Weather"));
    if (!Func)
        Func = UDW->FindFunction(TEXT("ChangeWeather"));

    if (Func)
    {
        // nullptr = "None" → 모든 날씨 효과 해제 (구름/비/눈 제거)
        struct { UObject* NewWeatherType; float TransitionTime; } Params;
        Params.NewWeatherType = nullptr;
        Params.TransitionTime = WeatherTransitionTime;
        UDW->ProcessEvent(Func, &Params);

#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Log, TEXT("[Weather] 밤 → 맑은 날씨 강제 (전환 %.0f초)"), WeatherTransitionTime);
#endif
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 💧 비 MPC 점진 Push
//   UDW Material State Sim이 MPC를 push하지 않을 수 있으므로,
//   Phase==Night(비 강제)일 때 SkyPreviewActor와 동일한 MPC 파라미터를
//   시간에 따라 단계적으로 올리고, 낮이면 내린다.
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::TickRainMPC()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    static const TCHAR* MPCPath = TEXT("/Game/UltraDynamicSky/Materials/Weather/UltraDynamicWeather_Parameters");
    UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, MPCPath);
    if (!MPC)
        return;

    const float DeltaSec = FMath::Max(0.05f, PuddleTickInterval);
    const float SafeFill = FMath::Max(1.f, PuddleFillSeconds);
    const float SafeDry = FMath::Max(1.f, PuddleDrySeconds);

    // ── 누적 시간 갱신 ─────────────────────────────────────────────
    if (bIsCurrentlyRaining)
    {
        AccumulatedRainSeconds += DeltaSec;
    }
    else
    {
        const float DryRate = SafeFill / SafeDry;
        AccumulatedRainSeconds -= DeltaSec * DryRate;
    }
    AccumulatedRainSeconds = FMath::Clamp(AccumulatedRainSeconds, 0.f, SafeFill);

    // [Patch 3] 단계 양자화 제거 — 선형 Alpha로 매 0.25초 부드럽게 상승/하강.
    //   기존 10초 Step 양자화는 경계에서 값이 갑자기 튀어 시각적 pop을 유발.
    const float Alpha = FMath::Clamp(AccumulatedRainSeconds / SafeFill, 0.f, 1.f);

    // [Patch 2] UDW가 자체 푸시하는 파라미터(Wet, Raining, Fog)는 push 제외.
    //   UDW의 Material State Sim이 Rain 프리셋 값(Material Wetness=1, Rain=7, Fog 등)으로
    //   해당 MPC를 매 프레임 write할 수 있어, 우리 0.25초 push와 경합해 틱 단위 깜빡임 발생.
    //   DLWE 전용 3종 파라미터만 push하여 경합 제거.
    UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("DLWE_Base Wetness"),      Alpha * RainTargetBaseWetness);
    UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("DLWE_Puddle Coverage"),   Alpha * RainTargetPuddleCoverage);
    UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("DLWE Puddle Sharpness"),  PuddleSharpness);

    // [Patch 4] 웅덩이 반짝임(서브픽셀 specular aliasing) 억제.
    //   기본 Water Roughness=0.05는 거울 같은 반사로 TAA가 픽셀보다 작은 하이라이트에 수렴 실패 → 프레임마다 다른 픽셀이 튐.
    //   Raining>0 구간에서는 Tiling Ripples Framerate까지 높아 노멀이 매 프레임 변함 → 반짝임 가중.
    //   두 파라미터 모두 GameState가 권위적으로 push하여 UDW 프리셋이 덮어쓰는 것을 상쇄.
    UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("Water Roughness"),            PuddleWaterRoughness);
    UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, TEXT("Tiling Ripples Framerate"),   RippleFramerate);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🎭 캐릭터 선택 시스템은 Base(AHellunaBaseGameState)로 이동됨
// ═══════════════════════════════════════════════════════════════════════════════
