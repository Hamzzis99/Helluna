// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingHUDWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로딩 배리어 HUD 위젯 C++ 베이스. 2-Phase (Reedme/loading/08-final-design-decisions §2).
// BP에서 WBP_SelfLoadingHUD로 상속해 UMG 애니메이션/서브타이틀을 바인딩한다.
//
// 📌 작성자: Gihyeon (Loading Barrier Stage F/G)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLoadingHUDWidget.generated.h"

class UHellunaPartySlotWidget;
class UTextBlock;
class UProgressBar;
class UPanelWidget;
class UTextBlock;
class UProgressBar;
class UPanelWidget;
class UWidgetAnimation;

UCLASS(Abstract)
class HELLUNA_API UHellunaLoadingHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 초기 세팅: 기대 파티원 + 로컬 PlayerId. Phase 1에서 슬롯을 눈속임으로 채운다. */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void InitializeHUD(const TArray<FString>& ExpectedIds, const FString& InLocalPlayerId, int32 PartyId);

	/** 내 로딩 진행률(0~1) 갱신. */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void UpdateMyProgress(float Normalized01);

	/** Phase 1 → Phase 2 전환 (내가 Ready 된 직후). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void TransitionToPhase2();

	/** 특정 파티원 슬롯 상태 갱신 (서버 브로드캐스트에서 호출). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void UpdateSlot(const FString& PlayerId, bool bReady);

	/** 페이드 아웃 애니메이션 재생 — BP에서 WidgetAnimation을 돌려라. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loading Barrier|HUD")
	void PlayFadeOutAnimation();

	/** Phase 전환 애니메이션 — BP 구현. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loading Barrier|HUD")
	void PlayTransitionAnimation();

	/** 슬롯 생성 — BP에서 override 가능, 미구현 시 cpp가 PartySlotWidgetClass로 자동 생성. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Loading Barrier|HUD")
	UHellunaPartySlotWidget* SpawnPartySlot(const FString& PlayerId, bool bIsMe);
	virtual UHellunaPartySlotWidget* SpawnPartySlot_Implementation(const FString& PlayerId, bool bIsMe);

	// ─────────────────────────────────────────────────────────────────────
	// §13 v2.1 — A구간(L_Lobby) HUD 모드 + 가짜 진행률
	// ─────────────────────────────────────────────────────────────────────

	/** §13 §3.1.4 — true=A구간(로비, 가짜 progress) / false=C구간(MainMap, 실제 polling). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void SetIsLobbyStage(bool bInLobby);

	/** §13 §3.1.3 (Q4) — 가짜 진행률 0→TargetValue 선형 증가 시작 (A구간). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void StartFakeProgress(float Duration, float TargetValue);

	/** §13 §3.1.3 (Q4) — 캡처 시점 가짜 진행률 값 반환 (CaptureAndTravel에서 GI에 저장). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	float GetCurrentFakeProgress() const { return MyProgress; }

	/** §13 §3.3.4 (Q4 / X-2) — C구간 진입 시 A의 마지막 값 이어받기 (≈0.9). */
	UFUNCTION(BlueprintCallable, Category = "Loading Barrier|HUD")
	void SetInitialFakeProgress(float InitialValue);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	TArray<FString> ExpectedPlayerIds;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	FString LocalPlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	int32 PartyId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	bool bInPhase2 = false;

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	float MyProgress = 0.f;

	/** §13 v2.1 — true면 가짜 진행률 모드 (Tick 갱신, polling 무시). */
	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	bool bIsLobbyStage = false;

	// 가짜 진행률 타이머 상태
	float FakeProgressStart = 0.f;
	float FakeProgressTarget = 0.f;
	float FakeProgressDuration = 0.f;
	float FakeProgressElapsed = 0.f;
	bool bFakeProgressActive = false;
	FTimerHandle FakeProgressTickHandle;

	/** Tick 콜백 — 0.05s 간격으로 호출 (StartFakeProgress가 SetTimer로 구동). */
	void TickFakeProgress();

	UPROPERTY(BlueprintReadOnly, Category = "Loading Barrier|HUD")
	TMap<FString, TObjectPtr<UHellunaPartySlotWidget>> PlayerIdToSlot;

	// ─────────────────────────────────────────────────────────────────────
	// BindWidget — WBP에서 동일 이름의 위젯이 있으면 자동 바인딩됨.
	// 없어도 에디터/런타임 에러 없음 (Optional).
	// ─────────────────────────────────────────────────────────────────────

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_MyProgress;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_MyProgressText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Subtitle;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> VBox_PartyList;

	/** WBP에서 지정할 PartySlot 위젯 클래스. SpawnPartySlot의 cpp 폴백 구현이 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Barrier|HUD",
		meta = (DisplayName = "Party Slot Widget Class"))
	TSubclassOf<UHellunaPartySlotWidget> PartySlotWidgetClass;
};
