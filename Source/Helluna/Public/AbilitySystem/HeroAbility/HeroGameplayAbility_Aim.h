// File: Source/Helluna/Public/AbilitySystem/HeroAbility/HeroGameplayAbility_Aim.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Aim.generated.h"

class UAT_AimCameraInterp;
class UUserWidget;

// ═══════════════════════════════════════════════════════════
// [Aim System] 조준 GA — 3페이즈 AbilityTask 방식
//
// Phase 1: 줌인 + 이동감속 (AbilityTask가 카메라 보간)
// Phase 2: 유지 + 사격가능 (Hold 대기)
// Phase 3: 줌아웃 + 속도복원 (AbilityTask가 카메라 복귀)
// ═══════════════════════════════════════════════════════════

UCLASS()
class HELLUNA_API UHeroGameplayAbility_Aim : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroGameplayAbility_Aim();

	/**
	 * [ScopeBreakV1] 외부(피격·보스 특수패턴)에서 스나이퍼 스코프(탭 토글 강줌)를 강제 해제.
	 *   스코프가 켜져 있을 때만 동작 → 줌아웃 후 GA 종료. 견착(ADS 홀드)에는 영향 없음.
	 *   로컬 카메라/UI 상태만 건드리므로 멀티 안전(소유 클라에서 호출).
	 */
	void ForceExitScope();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	// [Sniper Scope] 스코프 토글 중 재입력(2번째 탭) → 스코프 해제용.
	//   ASC::OnAbilityInputPressed 가 활성 Aim GA 인스턴스에 라우팅해 호출.
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

private:
	/** Phase 1 완료 → Phase 2 진입 */
	UFUNCTION()
	void OnZoomInCompleted();

	/** Phase 3 완료 → EndAbility */
	UFUNCTION()
	void OnZoomOutCompleted();

	/** Phase 3 시작 (줌아웃) */
	void StartZoomOut();

	// ── [Sniper Scope] 스코프 헬퍼 ──
	/** 카메라 보간 태스크 시작 (이전 태스크 자동 종료 후 새로 생성). 반환 = 새 태스크 */
	UAT_AimCameraInterp* StartCameraInterp(float TargetFOV, float TargetArmLength, float InterpSpeed);

	/** 짧은 탭 → 스코프 강줌 토글 진입 */
	void EnterScope();

	/** 스코프 오버레이 위젯 표시/제거 (로컬 전용) */
	void ShowScopeOverlay();
	void HideScopeOverlay();

	/** [Scope HUD] 스코프 오버레이의 라이브 readout(줌배율·탄약·거리) 실시간 갱신 (로컬 전용, 타이머 구동) */
	void UpdateScopeHUD();

	// ── 설정값 ──
	UPROPERTY(EditDefaultsOnly, Category = "Aim",
		meta = (DisplayName = "Aim Walk Speed (조준 시 이동 속도)"))
	float AimMaxWalkSpeed = 100.f;

	/** [Sniper Scope] 탭/홀드 구분 임계 시간(초). 이보다 짧게 떼면 탭(스코프 토글), 길면 홀드(견착). */
	UPROPERTY(EditDefaultsOnly, Category = "Aim",
		meta = (DisplayName = "탭/홀드 임계 시간(초)", ClampMin = "0.05", ClampMax = "1.0",
			ToolTip = "우클릭을 이 시간보다 짧게 떼면 탭=스코프 토글, 길면 홀드=견착. 스나이퍼(스코프 토글 지원 무기)만 적용."))
	float TapHoldThreshold = 0.2f;

	// ── 캐싱 ──
	float CachedDefaultMaxWalkSpeed = 0.f;
	float CachedDefaultFOV = 0.f;
	float CachedDefaultArmLength = 0.f;
	FVector CachedDefaultSocketOffset = FVector::ZeroVector;

	/** 캐싱: 조준 전 회전 방식 */
	bool CachedOrientRotationToMovement = true;
	bool CachedUseControllerDesiredRotation = false;
	FRotator CachedRotationRate = FRotator::ZeroRotator;

	/** 현재 페이즈 (디버그용) */
	int32 CurrentPhase = 0;

	/** 입력 해제 플래그 (Phase 1 중 해제 시 바로 Phase 3 전환용) */
	bool bInputReleased = false;

	/** 줌아웃 태스크 참조 (GA 취소 시 정리용) */
	UPROPERTY()
	TObjectPtr<UAT_AimCameraInterp> ZoomOutTask = nullptr;

	// ── [Sniper Scope] 상태 ──
	/** 현재 무기가 스코프 토글을 지원하는지 (ActivateAbility 시 캐싱) */
	bool bWeaponSupportsScope = false;

	/** 스코프(탭 토글) 강줌 모드가 켜져 있는지 */
	bool bScopeActive = false;

	/** 줌아웃/종료 진행 중 — InputReleased/InputPressed 중복 처리 방지 */
	bool bExiting = false;

	/** ActivateAbility 시각 (탭/홀드 경과시간 판정용) */
	float AimActivationTime = 0.f;

	/** 현재 활성 카메라 보간 태스크 (모드 전환 시 교체) */
	UPROPERTY()
	TObjectPtr<UAT_AimCameraInterp> ActiveInterpTask = nullptr;

	/** 스코프 오버레이 위젯 인스턴스 (로컬 전용) */
	UPROPERTY()
	TObjectPtr<UUserWidget> ScopeOverlayWidget = nullptr;

	/** [Scope HUD] readout 갱신 타이머 (스코프 활성 동안만, 로컬 전용) */
	FTimerHandle ScopeHUDUpdateTimer;
};
