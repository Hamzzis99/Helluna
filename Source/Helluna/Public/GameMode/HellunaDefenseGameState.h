// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameState.h"

// [MDF 추가] 플러그인 인터페이스 및 컴포넌트 헤더
#include "Interface/MDF_GameStateInterface.h"
#include "Components/MDF_DeformableComponent.h"

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

    UFUNCTION(NetMulticast, Reliable)
    void MulticastPrintNight(int32 Current, int32 Need);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastPrintDay();

    // ✅ UI에서 "남은 몬스터 수" 읽어오기 용도
    UFUNCTION(BlueprintPure, Category = "Defense|Monster")
    int32 GetAliveMonsterCount() const { return AliveMonsterCount; }

    // ✅ 서버(GameMode)에서만 값을 갱신하도록 하는 Setter
    void SetAliveMonsterCount(int32 NewCount);

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

    // 디버그용
    FTimerHandle TimerHandle_NightDebug;

    // ✅ 출력 간격(원인 파악 끝나면 지우기 쉬움)
    float NightDebugInterval = 5.f;

    // ✅ 2.5초마다 호출될 함수(몹 수 출력)
    void PrintNightDebug();
};
