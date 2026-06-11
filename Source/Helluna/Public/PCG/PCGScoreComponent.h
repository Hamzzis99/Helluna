#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCGScoreComponent.generated.h"

/**
 * 광석 종류 + 비중(가중치) 한 항목.
 * PCG가 생성한 각 포인트마다 OreTypes 목록에서 Weight 비례 확률로 한 종류가 뽑힌다.
 */
USTRUCT(BlueprintType)
struct FOreSpawnEntry
{
	GENERATED_BODY()

	/** 스폰할 광석 액터 클래스 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "광석 클래스"))
	TSubclassOf<AActor> OreClass;

	/**
	 * 비중(가중치). 다른 항목 대비 상대 확률.
	 * 예) A=50, B=30, C=20 → A 50%, B 30%, C 20%. 0 이하면 안 나옴.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "비중(가중치)", ClampMin = "0.0"))
	float Weight = 1.f;
};

/**
 * PCG 액터에 부착하여 광석 배치 설정을 관리하는 컴포넌트.
 *
 * 두 축으로 분리된 컨트롤:
 *   - ClusterAmount (뭉침비): 한 뭉침에 몇 개의 광석이 모이는가
 *   - PlacementDensity (촘촘함): 전체 영역에 광석이 얼마나 빼곡히 깔리는가
 *
 * 두 값은 독립적으로 동작 — 둘 중 하나만 올려도 총 광석 수는 증가한다.
 *
 * 광석 종류:
 *   - OreTypes (광석 종류 + 비중): 여러 광석을 비중과 함께 등록하면 포인트마다 가중치 추첨으로 섞여 나온다.
 *   - OreTypes가 비어 있으면 SpawnActorClassOverride(단일) 폴백 — 기존 동작 보존.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UPCGScoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * 뭉침비 — 한 뭉침에 들어가는 광석 수를 조절.
	 * 1.0 = 기본(GameMode의 MinClusterSize~MaxClusterSize), 2.0 = 두 배 큰 뭉침.
	 * 값이 높아도 단독 광석은 여전히 존재한다(촘촘함이 결정).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "뭉침비", ClampMin = "0.1", ClampMax = "5.0"))
	float ClusterAmount = 1.f;

	/**
	 * 촘촘함 — 전체 광석 배치 밀도를 조절.
	 * 1.0 = 기본. 런타임에 PCG 그래프의 float 유저 파라미터 `PlacementDensity`로 전달되어
	 * 그래프가 생성하는 후보 포인트 풀 자체의 크기를 바꾼다.
	 * (그래프에 해당 파라미터가 노출되어 있어야 하며, 없으면 로그에 전달 실패가 찍힌다)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "촘촘함", ClampMin = "0.1", ClampMax = "10.0"))
	float PlacementDensity = 1.f;

	/**
	 * [광석 종류 + 비중] 이 PCG 영역에서 나올 광석들과 각각의 비중.
	 * 여러 종류를 등록하면 포인트마다 Weight 비례 확률로 섞여 배치된다.
	 * 비어 있으면 아래 SpawnActorClassOverride(단일)로 폴백.
	 *   예) 황무지: Stone_A=50, Stone_B=30, Stone_C=15, Amethyst=3, Ruby=2
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "광석 종류 + 비중", TitleProperty = "OreClass"))
	TArray<FOreSpawnEntry> OreTypes;

	/**
	 * 스폰할 액터 클래스를 직접 지정 (단일).
	 * OreTypes가 비어 있을 때만 사용되는 폴백. nullptr이면 PCG 원래 클래스 사용.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCG",
		meta = (DisplayName = "스폰 액터 클래스 오버라이드 (폴백)"))
	TSubclassOf<AActor> SpawnActorClassOverride;

	/**
	 * 포인트 하나에 대해 스폰할 광석 클래스를 가중치 추첨으로 반환.
	 *   - OreTypes 중 유효(클래스 있고 Weight>0)한 항목들에서 비중 비례로 1개 선택.
	 *   - 유효 항목이 없으면 SpawnActorClassOverride 폴백(없으면 nullptr).
	 * 서버(GameMode)에서 포인트마다 호출. 스폰된 액터는 복제되므로 멀티 안전.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCG")
	TSubclassOf<AActor> PickWeightedOreClass() const;
};
