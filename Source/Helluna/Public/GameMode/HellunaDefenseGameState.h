// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Helluna.h"  // 디버그 매크로 정의 (HELLUNA_DEBUG_DEFENSE - Phase/DayNight/몬스터/날씨)
#include "GameMode/HellunaBaseGameState.h"

// [MDF 추가] 플러그인 인터페이스 및 컴포넌트 헤더
#include "Interface/MDF_GameStateInterface.h"
#include "Components/MDF_DeformableComponent.h"

#include "Chat/HellunaChatTypes.h"

#include "HellunaDefenseGameState.generated.h"

// =========================================================================================
// [김기현 작업 영역 시작] 구조체 정의
// 언리얼 엔진의 TMap은 TArray를 값(Value)으로 직접 가질 수 없습니다.
// 따라서 TArray를 감싸주는 구조체(Wrapper)를 정의하여 사용합니다.
// =========================================================================================
USTRUCT(BlueprintType)
struct FMDFHitHistoryWrapper
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TArray<FMDFHitData> History;
};
// =========================================================================================

UENUM(BlueprintType)
enum class EDefensePhase : uint8
{
    Day,
    Night
};

class AResourceUsingObject_SpaceShip;

UCLASS()
class HELLUNA_API AHellunaDefenseGameState : public AHellunaBaseGameState, public IMDF_GameStateInterface
{
    GENERATED_BODY()

public:
    /** 생성자 */
    AHellunaDefenseGameState();

    // =========================================================================================
    // [민우님 작업 영역] 기존 팀원 코드 (우주선 및 페이즈 관리)
    // =========================================================================================
    UFUNCTION(BlueprintPure, Category = "Defense")
    AResourceUsingObject_SpaceShip* GetSpaceShip() const { return SpaceShip; }

    void RegisterSpaceShip(AResourceUsingObject_SpaceShip* InShip);

    // ═══════════════════════════════════════════════════════════════════════════
    // [Phase 10] 채팅 시스템
    // ═══════════════════════════════════════════════════════════════════════════

    /** 채팅 메시지 수신 델리게이트 — 위젯에서 바인딩 */
    UPROPERTY(BlueprintAssignable, Category = "Chat (채팅)")
    FOnChatMessageReceived OnChatMessageReceived;

    /**
     * 채팅 메시지 브로드캐스트 (서버 전용 헬퍼)
     * @param SenderName  발신자 이름 (시스템 메시지: 빈 문자열)
     * @param Message     메시지 본문
     * @param Type        메시지 타입 (Player / System)
     */
    void BroadcastChatMessage(const FString& SenderName, const FString& Message, EChatMessageType Type);

    /** [Multicast RPC] 채팅 메시지를 모든 클라이언트에 전달 */
    UFUNCTION(NetMulticast, Reliable)
    void NetMulticast_ReceiveChatMessage(const FChatMessage& ChatMessage);

    UFUNCTION(BlueprintPure, Category = "Defense")
    EDefensePhase GetPhase() const { return Phase; }

    void SetPhase(EDefensePhase NewPhase);

    // ═══════════════════════════════════════════════════════════════════════════
    // 낮/밤 전환 이벤트 (BP에서 구현)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * Phase 복제 콜백 - 클라이언트에서 Phase가 변경될 때 자동 호출
     * 서버에서는 SetPhase() 내부에서 직접 호출
     */
    UFUNCTION()
    void OnRep_Phase();

    /**
     * 낮 시작 시 호출 (BP에서 UDS/UDW 날씨 변경 구현)
     * - 서버: SetPhase(Day) 시 직접 호출
     * - 클라이언트: OnRep_Phase()에서 자동 호출
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Defense|DayNight")
    void OnDayStarted();

    /**
     * 밤 시작 시 호출 (BP에서 UDS/UDW 날씨 변경 구현)
     * - 서버: SetPhase(Night) 시 직접 호출
     * - 클라이언트: OnRep_Phase()에서 자동 호출
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Defense|DayNight")
    void OnNightStarted();

    // ═══════════════════════════════════════════════════════════════════════════
    // 새벽 완료 → 라운드 시작 (UDS 비례 구동 트리거)
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // [호출 흐름]
    //   GameMode::EnterDay()
    //     → SetPhase(Day) → OnDayStarted() (밤→아침 빠른 전환 연출, ~5초)
    //     → NetMulticast_OnDawnPassed(TestDayDuration) (새벽 끝, 라운드 시작)
    //       → OnDawnPassed(RoundDuration) (BP에서 UDS 비례 구동 시작)
    //     → TimerHandle_ToNight 시작 (RoundDuration 후 EnterNight)
    //
    // [BP 구현 가이드]
    //   OnDawnPassed에서 받은 RoundDuration을 이용하여:
    //     UDS Time of Day 속도 = (2100 - 800) / RoundDuration
    //   이 속도로 UDS를 800(아침)에서 2100(밤)까지 자연스럽게 진행
    //
    // ─────────────────────────────────────────────────────────────────────────
    // [향후 개선안: A방식 — Dawn Phase 도입]
    //
    // 현재(B방식): Phase는 Day/Night 2단계. 새벽 전환은 Multicast RPC로 처리.
    //   - 장점: GameMode 수정 최소화, 빠른 테스트 가능
    //   - 단점: 새벽 중 Phase가 이미 Day라서, 새벽 전환 중인지 구분 불가
    //
    // A방식: EDefensePhase에 Dawn을 추가하여 3단계로 운용
    //   enum class EDefensePhase : uint8 { Night, Dawn, Day };
    //
    //   GameMode 흐름:
    //     EnterDay()
    //       → SetPhase(Dawn)           ← 새벽 전환 시작
    //       → 5초 타이머
    //       → SetPhase(Day)            ← 새벽 완료, 라운드 시작
    //       → TimerHandle_ToNight 시작
    //       → EnterNight()
    //
    //   GameState OnRep_Phase 분기:
    //     case Dawn:  OnDawnStarted()   → BP: 밤→아침 빠른 전환 연출
    //     case Day:   OnDayStarted()    → BP: UDS 비례 구동 시작
    //     case Night: OnNightStarted()  → BP: UDS 밤 고정
    //
    //   장점: Phase 리플리케이션으로 모든 상태가 자동 동기화.
    //         중간 접속 클라이언트도 현재 Phase만 보면 올바른 UDS 상태 적용 가능.
    //         게임플레이 규칙(새벽엔 몬스터 안 나옴 등) 분기 용이.
    //
    //   구현 시 필요한 변경:
    //     1. EDefensePhase에 Dawn 추가
    //     2. OnRep_Phase()에 Dawn case 추가
    //     3. OnDawnStarted() BlueprintImplementableEvent 추가
    //     4. GameMode::EnterDay()에서 SetPhase(Dawn) → 타이머 → SetPhase(Day) 순서로 변경
    //     5. NetMulticast_OnDawnPassed 제거 가능 (Phase 리플리케이션이 대체)
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * [Multicast RPC] 새벽 전환 완료 → 라운드 시작 신호
     *
     * GameMode::EnterDay()에서 호출.
     * 모든 클라이언트에서 OnDawnPassed()를 발동시킨다.
     *
     * @param RoundDuration  라운드 지속 시간(초). UDS 비례 구동 속도 계산에 사용.
     */
    UFUNCTION(NetMulticast, Reliable)
    void NetMulticast_OnDawnPassed(float RoundDuration);

    /**
     * 새벽 완료 시 호출 (BP에서 UDS 비례 구동 구현)
     *
     * NetMulticast_OnDawnPassed → 이 함수 호출.
     * BP에서 RoundDuration을 이용해 UDS Time of Day를
     * 800(아침) → 2100(밤)으로 비례 구동한다.
     *
     * @param RoundDuration  라운드 지속 시간(초)
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Defense|DayNight")
    void OnDawnPassed(float RoundDuration);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastPrintNight(int32 Current, int32 Need);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastPrintDay();

    // ✅ UI에서 "남은 몬스터 수" 읽어오기 용도
    UFUNCTION(BlueprintPure, Category = "Defense|Monster")
    int32 GetAliveMonsterCount() const { return AliveMonsterCount; }

    // ✅ 서버(GameMode)에서만 값을 갱신하도록 하는 Setter
    void SetAliveMonsterCount(int32 NewCount);

    // ── 낮/밤 UI용 복제 변수 ────────────────────────────────────────────────

    /** 낮에 밤까지 남은 시간(초) — GameMode에서 매 틱 갱신 */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Defense|UI")
    float DayTimeRemaining = 0.f;

    /** 이번 밤 총 소환 몬스터 수 */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Defense|UI")
    int32 TotalMonstersThisNight = 0;

    /** 현재 진행 중인 Day 번호 (1-based) */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Defense|UI")
    int32 CurrentDayForUI = 0;

    /** 이번 밤이 보스 출현 밤인지 */
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Defense|UI")
    bool bIsBossNight = false;

    void SetDayTimeRemaining(float InTime);
    void SetTotalMonstersThisNight(int32 InTotal);
    void SetCurrentDayForUI(int32 InDay);
    void SetIsBossNight(bool bInVal);

    // =========================================================================================
    // [김기현 작업 영역 시작] MDF Interface 구현 및 시스템 함수
    // (MDF: Mesh Deformation System - 구조물 변형 상태 저장 관리)
    // =========================================================================================

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // [MDF Interface] 데이터 저장 (메모리 갱신)
    virtual void SaveMDFData(const FGuid& ID, const TArray<FMDFHitData>& Data) override;

    // [MDF Interface] 데이터 로드 (메모리 조회)
    virtual bool LoadMDFData(const FGuid& ID, TArray<FMDFHitData>& OutData) override;

protected:
    /** MDF 데이터 디스크 저장 (맵 이동 전) */
    virtual void OnPreMapTransition() override;

    // 현재 데이터를 실제 디스크 파일(.sav)로 저장하는 함수
    void WriteDataToDisk();

    // 세이브 파일 슬롯 이름
    FString SaveSlotName = TEXT("MDF_SaveSlot");

    // [MDF 컴포넌트 서버 전용 저장소]
    // TArray 직접 중첩 불가 이슈 해결을 위해 Wrapper 구조체 사용
    UPROPERTY()
    TMap<FGuid, FMDFHitHistoryWrapper> SavedDeformationMap;

    // =========================================================================================
    // [김기현 작업 영역 끝]
    // =========================================================================================


protected:
    // [팀원 코드 유지]
    UPROPERTY(Replicated)
    TObjectPtr<AResourceUsingObject_SpaceShip> SpaceShip;

    UPROPERTY(ReplicatedUsing = OnRep_Phase)
    EDefensePhase Phase = EDefensePhase::Day;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//몬스터 생존 개수 관리, GameMode는 서버에만 있으니, UI/디버그를 위해 GameState에서 복제(Replicate)로 공유
    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Defense|Monster")
    int32 AliveMonsterCount = 0;

#if HELLUNA_DEBUG_DEFENSE
    // 디버그용 (HELLUNA_DEBUG_DEFENSE가 1일 때만 컴파일)
    FTimerHandle TimerHandle_NightDebug;
    float NightDebugInterval = 5.f;
    void PrintNightDebug();
#endif

    // ═══════════════════════════════════════════════════════════════════════════
    // 🔍 UDS 디버그 타이머 (1초마다 Time of Day 로깅)
    // ═══════════════════════════════════════════════════════════════════════════
    FTimerHandle TimerHandle_UDSDebug;
    void PrintUDSDebug();

    // ═══════════════════════════════════════════════════════════════════════════
    // ☀️ UDS 시간 제어
    // ═══════════════════════════════════════════════════════════════════════════
    
    /** 낮 시작 시간 (UDS 기준, 800 = 오전 8시) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "디펜스|낮밤", meta = (DisplayName = "낮 시작 시간"))
    float DayStartTime = 800.f;

    /** 밤→낮 새벽 전환 시간 (초) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "디펜스|낮밤", meta = (DisplayName = "새벽 전환 시간(초)"))
    float DawnTransitionDuration = 5.f;

    FTimerHandle TimerHandle_DawnTransition;
    float DawnLerpStart = 0.f;          // 전환 시작 시 UDS 시간
    float DawnLerpElapsed = 0.f;        // 경과 시간
    float DawnTotalDistance = 0.f;       // 총 이동량 (순환 고려)
    float PendingRoundDuration = 0.f;   // 새벽 완료 후 사용할 RoundDuration
    void TickDawnTransition();           // 타이머 콜백 (매 프레임)
    
    /** 낮 종료 시간 (UDS 기준, 1800 = 오후 6시 일몰) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "디펜스|낮밤", meta = (DisplayName = "낮 종료 시간"))
    float DayEndTime = 1800.f;

    // ═══════════════════════════════════════════════════════════════════════════
    // 🌤️ 랜덤 날씨 시스템
    // ═══════════════════════════════════════════════════════════════════════════
    
    /** 낮에 사용할 날씨 목록 (UDS Weather Type Data Asset) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "디펜스|날씨", meta = (DisplayName = "낮 날씨 배열"))
    TArray<UObject*> DayWeatherTypes;
    
    /** 밤에 사용할 날씨 목록 (UDS Weather Type Data Asset) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "디펜스|날씨", meta = (DisplayName = "밤 날씨 배열"))
    TArray<UObject*> NightWeatherTypes;
    
    /** 날씨 전환 시간 (초) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "디펜스|날씨", meta = (DisplayName = "날씨 전환 시간(초)"))
    float WeatherTransitionTime = 10.f;
    
    /** 현재 선택된 낮 날씨 (디버그/읽기용) */
    UPROPERTY(BlueprintReadOnly, Category = "디펜스|날씨", meta = (DisplayName = "현재 낮 날씨"))
    UObject* CurrentDayWeather = nullptr;
    
    /** 현재 선택된 밤 날씨 (디버그/읽기용) */
    UPROPERTY(BlueprintReadOnly, Category = "디펜스|날씨", meta = (DisplayName = "현재 밤 날씨"))
    UObject* CurrentNightWeather = nullptr;
    
    /** 배열에서 랜덤 날씨 선택 후 Change Weather 호출 */
    void ApplyRandomWeather(bool bIsDay);

    // ═══════════════════════════════════════════════════════════════════════════
    // 캐싱 (UDS/UDW 액터 + 리플렉션 프로퍼티)
    // ═══════════════════════════════════════════════════════════════════════════
    UPROPERTY()
    TWeakObjectPtr<AActor> CachedUDS;
    
    UPROPERTY()
    TWeakObjectPtr<AActor> CachedUDW;
    
    AActor* GetUDSActor();
    AActor* GetUDWActor();
    void SetUDSTimeOfDay(float Time);

    // ---------------------------------------------------------------
    // [Step3 O-02] UDS 리플렉션 프로퍼티 캐시
    // FindFProperty는 비용이 높으므로 BeginPlay에서 1회만 조회하여 캐싱
    // ---------------------------------------------------------------
    FProperty* CachedProp_TimeOfDay = nullptr;    // "Time of Day" (Float or Double)
    FProperty* CachedProp_Animate = nullptr;      // "Animate Time of Day" (Bool)
    FProperty* CachedProp_DayLength = nullptr;    // "Day Length" (Float or Double)

    /** BeginPlay에서 UDS 프로퍼티를 캐싱하는 헬퍼 */
    void CacheUDSProperties();
    
    /** 데디서버에서 UDS/UDW가 존재하는지 (BeginPlay에서 1회 체크) */
    bool bHasUDS = false;
    bool bHasUDW = false;
    
    /** 밤을 한 번이라도 거쳤는지 (첫 시작 시 새벽 전환 방지) */
    bool bHasBeenNight = false;

    // ═══════════════════════════════════════════════════════════════════════════
    // [Phase 10] 채팅 히스토리 (서버 메모리 전용, 리플리케이션 안 함)
    // ═══════════════════════════════════════════════════════════════════════════
    TArray<FChatMessage> ChatHistory;
    static constexpr int32 MaxChatHistory = 100;
};
