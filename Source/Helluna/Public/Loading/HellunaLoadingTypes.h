// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingTypes.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로딩 배리어 공용 타입 정의 (Reedme/loading/*.md 설계 기반)
//   - ELoadingBarrierState: 서버 측 상태 머신
//   - FHellunaReadyInfo   : 서버→클라 파티 현황 스냅샷 엔트리
//
// 📌 작성자: Gihyeon (Loading Barrier Stage B/E)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "HellunaLoadingTypes.generated.h"

UENUM(BlueprintType)
enum class ELoadingBarrierState : uint8
{
	Idle                 UMETA(DisplayName = "Idle"),
	WaitingForArrivals   UMETA(DisplayName = "WaitingForArrivals"),
	WaitingForReady      UMETA(DisplayName = "WaitingForReady"),
	Released             UMETA(DisplayName = "Released"),
	Spawned              UMETA(DisplayName = "Spawned"),
};

USTRUCT(BlueprintType)
struct FHellunaReadyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString PlayerId;

	UPROPERTY(BlueprintReadOnly)
	bool bReady = false;
};
