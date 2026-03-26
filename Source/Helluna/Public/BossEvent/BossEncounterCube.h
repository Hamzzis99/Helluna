// Source/Helluna/Public/BossEvent/BossEncounterCube.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossEncounterCube.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UHoldInteractWidget;
class AHellunaHeroController;

/**
 * 중간보스 조우 큐브 — F키 홀드로 보스 소환 이벤트 시작
 *
 * PuzzleCubeActor와 유사한 3D 홀드 상호작용 패턴:
 *   1. 플레이어가 InteractionSphere 범위 안에 진입
 *   2. HoldInteractWidget(3D Screen Space) 표시
 *   3. F키 홀드 → 프로그레스 → 완료 시 Server RPC
 *
 * 퍼즐 시스템과 완전 분리 (별도 InputTag_Interaction 사용)
 */
UCLASS()
class HELLUNA_API ABossEncounterCube : public AActor
{
	GENERATED_BODY()

public:
	ABossEncounterCube();

	// =========================================================================================
	// 컴포넌트
	// =========================================================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BossEncounter|Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BossEncounter|Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BossEncounter|UI")
	TObjectPtr<UWidgetComponent> InteractWidgetComp;

	// =========================================================================================
	// 에디터 설정
	// =========================================================================================

	/** 3D 위젯에 사용할 위젯 클래스 (BP에서 WBP_BossEncounter_InteractWidget 할당) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BossEncounter|UI",
		meta = (DisplayName = "Interact Widget Class (상호작용 위젯 클래스)"))
	TSubclassOf<UHoldInteractWidget> InteractWidgetClass;

	/** F키 홀드 완료까지 걸리는 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter",
		meta = (DisplayName = "Hold Duration (홀드 시간)", ClampMin = "0.5", ClampMax = "5.0"))
	float HoldDuration = 1.5f;

	/** 상호작용 반경 조회 */
	float GetInteractionRadius() const;

	// =========================================================================================
	// 리플리케이션
	// =========================================================================================

	/** 이미 활성화됨 (중복 방지) */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "BossEncounter")
	bool bActivated = false;

	// =========================================================================================
	// 서버 함수 (HellunaHeroController에서 호출)
	// =========================================================================================

	/**
	 * 보스 조우 활성화 (서버 권한)
	 * @return 활성화 성공 여부
	 */
	bool TryActivate(AController* Activator);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// =========================================================================================
	// 3D 위젯 표시/숨김
	// =========================================================================================

	/** 위젯 표시 여부 업데이트 (클라이언트 Tick) */
	void UpdateInteractWidgetVisibility();

	/** 위젯 인스턴스 캐시 */
	UPROPERTY()
	TObjectPtr<UHoldInteractWidget> InteractWidgetInstance;

	/** 위젯 현재 표시 중인지 */
	bool bInteractWidgetVisible = false;

	/** F키 홀드 프로그레스 (로컬 클라이언트) */
	float LocalHoldProgress = 0.f;

	/** 홀드 완료 플래그 (중복 RPC 방지) */
	bool bHoldCompleted = false;
};
