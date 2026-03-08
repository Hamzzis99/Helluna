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
#include "Engine/World.h"
#include "EngineUtils.h"  // TActorIterator
#include "Save/MDF_SaveActor.h"                    // 저장용 액터 클래스 (SaveGame)
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h" // 이사 확인증 확인용
#include "Chat/HellunaChatTypes.h"

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
        OnDayStarted();
        if (bHasUDW) ApplyRandomWeather(true);
        break;
    case EDefensePhase::Night:
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[GameState] OnNightStarted 호출 시도"));
#endif
        OnNightStarted();
        bHasBeenNight = true;  // ★ 밤 경험 기록
        if (bHasUDW) ApplyRandomWeather(false);
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

    // DawnTransitionDuration이 0 이하이거나, 첫 시작(밤 미경험)이면 즉시 전환
    if (DawnTransitionDuration <= 0.f || !bHasBeenNight)
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

void AHellunaDefenseGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(TimerHandle_UDSDebug);
#if HELLUNA_DEBUG_DEFENSE
    GetWorldTimerManager().ClearTimer(TimerHandle_NightDebug);
#endif
    GetWorldTimerManager().ClearTimer(TimerHandle_DawnTransition);
    
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
// 🌤️ 랜덤 날씨 시스템
// ═══════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameState::ApplyRandomWeather(bool bIsDay)
{
    const TArray<UObject*>& WeatherArray = bIsDay ? DayWeatherTypes : NightWeatherTypes;
    
    if (WeatherArray.Num() == 0) return;
    
    int32 RandomIdx = FMath::RandRange(0, WeatherArray.Num() - 1);
    UObject* SelectedWeather = WeatherArray[RandomIdx];
    if (!SelectedWeather) return;
    
    if (bIsDay)
        CurrentDayWeather = SelectedWeather;
    else
        CurrentNightWeather = SelectedWeather;
    
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
        UE_LOG(LogTemp, Log, TEXT("[Weather] %s → %s (%d/%d)"),
            bIsDay ? TEXT("낮") : TEXT("밤"),
            *SelectedWeather->GetName(),
            RandomIdx, WeatherArray.Num());
#endif
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🎭 캐릭터 선택 시스템은 Base(AHellunaBaseGameState)로 이동됨
// ═══════════════════════════════════════════════════════════════════════════════
