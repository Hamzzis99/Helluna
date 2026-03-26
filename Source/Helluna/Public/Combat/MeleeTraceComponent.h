// Source/Helluna/Public/Combat/MeleeTraceComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MeleeTraceComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMeleeTrace, Log, All);

/**
 * UMeleeTraceComponent
 *
 * 근접 공격 소켓 기반 히트 판정 컴포넌트.
 * 매 틱 소켓 위치를 추적하여 이전 프레임→현재 프레임 사이
 * SphereTrace를 수행, 실제로 소켓이 닿은 액터에 히트 이벤트를 발생시킨다.
 *
 * [엔진 소스] UActorComponent::TickComponent — Runtime/Engine/Classes/Components/ActorComponent.h
 * [엔진 소스] UWorld::SweepSingleByChannel — Runtime/Engine/Classes/Engine/World.h
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UMeleeTraceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMeleeTraceComponent();

	// ═══════════════════════════════════════════════════════════
	// 에디터 설정
	// ═══════════════════════════════════════════════════════════

	/** 추적할 소켓 이름 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeTrace",
		meta = (ToolTip = "SkeletalMesh에서 추적할 소켓 이름."))
	FName TraceSocketName = "kick_trace";

	/** SphereTrace 반지름(cm) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeTrace",
		meta = (ToolTip = "SphereTrace 구체 반지름(cm).", ClampMin = "1.0", ClampMax = "100.0"))
	float TraceSphereRadius = 30.f;

	/** Trace에 사용할 콜리전 채널 */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeTrace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Pawn;

	/** 디버그 시각화 (Sweep 경로 + 히트 지점) */
	UPROPERTY(EditDefaultsOnly, Category = "MeleeTrace",
		meta = (ToolTip = "디버그: Sweep 경로와 히트 지점을 시각화합니다."))
	bool bDrawDebugTrace = false;

	// ═══════════════════════════════════════════════════════════
	// 런타임 API
	// ═══════════════════════════════════════════════════════════

	/** 소켓 추적 시작 */
	void StartTrace(FName InSocketName = NAME_None);

	/** 소켓 추적 종료 */
	void StopTrace();

	/** 현재 추적 중인가? */
	bool IsTracing() const { return bTracing; }

	// ═══════════════════════════════════════════════════════════
	// 히트 이벤트
	// ═══════════════════════════════════════════════════════════

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMeleeHit,
		AActor*, HitActor, const FHitResult&, HitResult);

	/** 소켓 Sweep 히트 시 브로드캐스트 */
	UPROPERTY(BlueprintAssignable, Category = "MeleeTrace")
	FOnMeleeHit OnMeleeHit;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 이전 프레임 소켓 위치 */
	FVector PreviousSocketLocation = FVector::ZeroVector;

	/** 추적 활성 여부 */
	bool bTracing = false;

	/** 한 번의 Trace 세션 동안 이미 히트한 액터 (중복 방지) */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> AlreadyHitActors;
};
