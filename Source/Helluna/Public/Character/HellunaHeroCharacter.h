// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaBaseCharacter.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "AbilitySystemInterface.h"
#include "HellunaHeroCharacter.generated.h"


class USpringArmComponent;
class UCameraComponent;
class UDataAsset_InputConfig;
class UHeroCombatComponent;
class AHellunaHeroWeapon;
struct FInputActionValue;
class UHelluna_FindResourceComponent;
class UWeaponBridgeComponent;
class AHeroWeapon_GunBase;
class UHellunaHealthComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UMeleeTraceComponent;

class UWeaponHUDWidget;
class UHellunaHealthHUDWidget;

class UInv_LootContainerComponent;

class UWidgetComponent;
class UHellunaReviveWidget;
class UHellunaReviveProgressWidget;
class UPostProcessComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UInv_InteractPromptWidget;
class AHellunaEnemyCharacter;
class UImage;


/**
 * 
 */

UCLASS()
class HELLUNA_API AHellunaHeroCharacter : public AHellunaBaseCharacter
{
	GENERATED_BODY()
	

public:
	AHellunaHeroCharacter();

	/** F키 상호작용 홀드 상태 (BossEncounterCube 등 Tick 프로그레스용) */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool IsHoldingInteraction() const { return bHoldingInteraction; }

	/** 부활 수행 중인지 (BossEncounterCube에서 큐브 프로그레스 차단용) */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool IsReviving() const { return bIsRevivingLocal; }

protected:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;  // ⭐ 인벤토리 저장용

	virtual void PossessedBy(AController* NewController) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Resource", meta = (AllowPrivateAccess = "true"))
	UHelluna_FindResourceComponent* FindResourceComponent;




private:

#pragma region Components

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	UHeroCombatComponent* HeroCombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMeleeTraceComponent> MeleeTraceComponent;

	// ============================================
	// ⭐ [WeaponBridge] Inventory 연동 컴포넌트
	// ⭐ Inventory 플러그인의 EquipmentComponent와 통신
	// ⭐ 무기 꺼내기/집어넣기 처리
	// ============================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true", DisplayName = "무기 브릿지 컴포넌트"))
	UWeaponBridgeComponent* WeaponBridgeComponent;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, VisibleInstanceOnly, Category = "Weapon")
	TObjectPtr<AHellunaHeroWeapon> CurrentWeapon;



#pragma endregion

#pragma region Inputs

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData", meta = (AllowPrivateAccess = "true"))
	UDataAsset_InputConfig* InputConfigDataAsset;

	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_Look(const FInputActionValue& InputActionValue);



	void Input_AbilityInputPressed(FGameplayTag InInputTag);
	void Input_AbilityInputReleased(FGameplayTag InInputTag);


#pragma endregion

public:
	FORCEINLINE UHeroCombatComponent* GetHeroCombatComponent() const { return HeroCombatComponent; }
	FORCEINLINE UMeleeTraceComponent* GetMeleeTraceComponent() const { return MeleeTraceComponent; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera;}
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }


	AHellunaHeroWeapon* GetCurrentWeapon() const { return CurrentWeapon; }
	void SetCurrentWeapon(AHellunaHeroWeapon* NewWeapon);

	// ── 무기 HUD ────────────────────────────────────────────────────

	/** BP에서 사용할 WeaponHUD 위젯 클래스 (에디터에서 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Weapon",
		meta = (DisplayName = "무기 HUD 위젯 클래스"))
	TSubclassOf<UWeaponHUDWidget> WeaponHUDWidgetClass;

	/** 생성된 WeaponHUD 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UWeaponHUDWidget> WeaponHUDWidget;

	/** 낮/밤 HUD 위젯 클래스 (에디터에서 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "UI|DayNight",
		meta = (DisplayName = "낮밤 HUD 위젯 클래스"))
	TSubclassOf<UUserWidget> DayNightHUDWidgetClass;

	/** 생성된 낮/밤 HUD 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UUserWidget> DayNightHUDWidget;

	// ── 체력 HUD (270도 Arc) ──────────────────────────────────────

	/** 체력 HUD 위젯 클래스 (에디터에서 WBP_HellunaHealthHUD 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Health",
		meta = (DisplayName = "체력 HUD 위젯 클래스"))
	TSubclassOf<UHellunaHealthHUDWidget> HealthHUDWidgetClass;

	/** 생성된 체력 HUD 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaHealthHUDWidget> HealthHUDWidget;

	/** 로컬 플레이어 전용 HUD 생성 및 뷰포트 추가 */
	void InitWeaponHUD();

	/** GameState 복제 지연 대비 — InitWeaponHUD 재시도 타이머 */
	FTimerHandle InitWeaponHUDRetryTimer;

	// ⭐ SpaceShip 수리 RPC (PlayerController가 소유하므로 작동!)
	// @param Material1Tag - 재료 1 태그
	// @param Material1Amount - 재료 1 개수
	// @param Material2Tag - 재료 2 태그
	// @param Material2Amount - 재료 2 개수
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Repair")
	void Server_RepairSpaceShip(FGameplayTag Material1Tag, int32 Material1Amount, FGameplayTag Material2Tag, int32 Material2Amount);

	// 무기 스폰 RPC
	UFUNCTION(Server, Reliable)  
	void Server_RequestSpawnWeapon(TSubclassOf<class AHellunaHeroWeapon> InWeaponClass, FName InAttachSocket, UAnimMontage* EquipMontage);

	// ============================================
	// ⭐ [WeaponBridge] 무기 제거 RPC
	// ⭐ 클라이언트에서 호출 → 서버에서 CurrentWeapon Destroy
	// ============================================
	UFUNCTION(Server, Reliable)
	void Server_RequestDestroyWeapon();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayEquipMontageExceptOwner(UAnimMontage* Montage);
	
	// 서버에 애니 재생 요청
	// Unreliable: 코스메틱 애니메이션 동기화. 유실돼도 다음 애니메이션에서 보정
	UFUNCTION(Server, Unreliable)
	void Server_RequestPlayMontageExceptOwner(UAnimMontage* Montage);


	// 이동,카메라 입력 잠금/해제에 관한 함수들 ====================
	void LockMoveInput();
	void UnlockMoveInput();
	void LockLookInput();
	void UnlockLookInput();

private:
	UPROPERTY(VisibleAnywhere, Category = "Input")
	bool bMoveInputLocked = false;

	UPROPERTY(VisibleAnywhere, Category = "Input")
	bool bLookInputLocked = false;

	FRotator CachedLockedControlRotation = FRotator::ZeroRotator;

	// ============================================
	// ✅ GAS: Ability System Interface 구현 -> 웨폰태그 적용

public:

	// ✅ 현재 무기 태그(서버가 결정 → 클라로 복제)
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponTag)
	FGameplayTag CurrentWeaponTag;


private:
	// ✅ ASC를 캐릭터가 소유하도록 
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHellunaAbilitySystemComponent> AbilitySystemComponent; // ✅ 추가

protected:
	// ✅ OnRep: 클라에서 태그를 ASC에 적용
	UFUNCTION()
	void OnRep_CurrentWeaponTag();

	UFUNCTION()
	void OnRep_CurrentWeapon();

	// ✅ 서버/클라 공통으로 태그 적용 유틸
	void ApplyTagToASC(const FGameplayTag& OldTag, const FGameplayTag& NewTag);

	// ✅ 클라에서 “이전에 적용했던 태그” 저장용 (RepNotify에서 Old 값을 못 받는 경우 대비)
	FGameplayTag LastAppliedWeaponTag;

// 총알 개수 저장
// UPROPERTY()
private:
	UPROPERTY()
	TMap<TObjectPtr<UClass>, int32> SavedMagByWeaponClass;

	void SaveCurrentMagByClass(AHellunaHeroWeapon* Weapon);
	void ApplySavedCurrentMagByClass(AHellunaHeroWeapon* Weapon);
	
	
	// 애니메이션을 전체 재생할 것인가
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Animation")
	bool PlayFullBody = false;

	/** 패링 카메라 복귀 진행 중 — 다른 GA 차단용 */
	UPROPERTY(BlueprintReadOnly, Category = "GunParry")
	bool bParryCameraReturning = false;

	// =========================================================
	// ★ 추가: 피격 / 사망 애니메이션
	// =========================================================

	/** 피격 시 재생할 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "피격 몽타주",
			ToolTip = "데미지를 받았을 때 재생할 Hit React 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> HitReactMontage = nullptr;

	/** 사망 시 재생할 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "사망 몽타주",
			ToolTip = "HP가 0이 되어 사망할 때 재생할 Death 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> DeathMontage = nullptr;

	// =========================================================
	// ★ Downed/Revive System (다운/부활)
	// =========================================================

	/** 다운 시 쓰러지는 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Downed",
		meta = (DisplayName = "Downed 몽타주 (Downed Montage)"))
	TObjectPtr<UAnimMontage> DownedMontage = nullptr;

	/** 부활 시 일어나는 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Downed",
		meta = (DisplayName = "Revive 몽타주 (Revive Montage)"))
	TObjectPtr<UAnimMontage> ReviveMontage = nullptr;

	/** 부활 시 HP 비율 (MaxHP * 비율) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Revive",
		meta = (DisplayName = "Revive HP Percent (부활 시 HP 비율)", ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveHealthPercent = 0.2f;

	/** 부활 소요 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Revive",
		meta = (DisplayName = "Revive Duration (부활 소요 시간, 초)", ClampMin = "0.5"))
	float ReviveDuration = 5.f;

	/** 부활 가능 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Revive",
		meta = (DisplayName = "Revive Range (부활 거리, cm)", ClampMin = "50.0"))
	float ReviveRange = 250.f;

	/** 현재 Revive 진행률 (0~1, 서버→클라 Replicated, UI용) */
	UPROPERTY(ReplicatedUsing = OnRep_ReviveProgress, BlueprintReadOnly, Category = "Downed|Revive")
	float ReviveProgress = 0.f;

	/** 나를 부활시키고 있는 사람 (서버 전용) */
	UPROPERTY()
	TObjectPtr<AHellunaHeroCharacter> CurrentReviver = nullptr;

	/** 내가 부활시키고 있는 대상 (서버 전용) */
	UPROPERTY()
	TObjectPtr<AHellunaHeroCharacter> ReviveTarget = nullptr;

	/** 다운 카메라 거리 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Downed",
		meta = (DisplayName = "Downed Camera Arm Length (다운 시 카메라 거리)"))
	float DownedCameraArmLength = 150.f;

	/** 다운 카메라 오프셋 */
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Downed",
		meta = (DisplayName = "Downed Camera Socket Offset (다운 시 카메라 오프셋)"))
	FVector DownedCameraSocketOffset = FVector(0.f, 60.f, 20.f);

	// =========================================================
	// 3D 부활 위젯 (다운된 캐릭터 머리 위 표시)
	// =========================================================

	/** 3D 부활 위젯 컴포넌트 (Screen Space, 모든 클라에게 표시) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Downed|UI")
	TObjectPtr<UWidgetComponent> ReviveWidgetComp;

	/** 위젯 BP 클래스 (에디터에서 할당) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Downed|UI",
		meta = (DisplayName = "Revive Widget Class (부활 위젯 클래스)"))
	TSubclassOf<UHellunaReviveWidget> ReviveWidgetClass;

	/** 위젯 인스턴스 캐시 */
	UPROPERTY()
	TObjectPtr<UHellunaReviveWidget> ReviveWidgetInstance;

	/** 3D 위젯 표시 (클라이언트에서 호출) */
	void ShowReviveWidget();

	/** 3D 위젯 숨김 (클라이언트에서 호출) */
	void HideReviveWidget();

	/** 출혈 잔여시간을 위젯에 업데이트 (클라이언트 Tick) */
	void UpdateReviveWidgetBleedout();

	// =========================================================
	// 부활 진행 HUD (부활 수행자 화면에 표시)
	// =========================================================

	/** 부활 진행 HUD 위젯 클래스 (에디터에서 할당) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|UI",
		meta = (DisplayName = "Revive Progress Widget Class (부활 진행 HUD)"))
	TSubclassOf<class UHellunaReviveProgressWidget> ReviveProgressWidgetClass;

	/** 부활 진행 HUD 인스턴스 */
	UPROPERTY()
	TObjectPtr<class UHellunaReviveProgressWidget> ReviveProgressWidget;

	/** ReviveProgressHUD가 Show된 시점 (Grace Period용) */
	float ReviveProgressShowTime = 0.f;

	/** ReviveProgressHUD Grace Period (초) — 이 시간 동안 Progress=0이어도 숨기지 않음 */
	static constexpr float REVIVE_HUD_GRACE_PERIOD = 0.5f;

	/** 부활 HUD 표시 (부활 수행자 로컬) */
	void ShowReviveProgressHUD(const FString& TargetName);

	/** 부활 HUD 숨김 */
	void HideReviveProgressHUD();

	/** 부활 HUD 업데이트 (Tick에서 호출) */
	void UpdateReviveProgressHUD();

	// =========================================================
	// ★ Downed Screen Effect (다운 선혈 화면 효과) [Phase21-C]
	// =========================================================

	/** 다운 선혈 오버레이 위젯 클래스 (WBP_DownedOverlay) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Effect",
		meta = (DisplayName = "Downed Overlay Widget Class (다운 오버레이 위젯)"))
	TSubclassOf<UUserWidget> DownedOverlayWidgetClass;

	/** 다운 선혈 오버레이 위젯 인스턴스 (로컬 전용) */
	UPROPERTY()
	TObjectPtr<UUserWidget> DownedOverlayWidget;

	/** 다운 전용 PostProcessComponent (HackMode PP와 별개) */
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> DownedPostProcess;

	/** 다운 PP Material Instance Dynamic */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DownedPPMID;

	/** 다운 PP 기본 머티리얼 (M_DownedVignette) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Effect",
		meta = (DisplayName = "Downed PP Material (다운 PP 머티리얼)"))
	TObjectPtr<UMaterialInterface> DownedPPMaterial;

	/** 다운 화면 효과 활성 여부 */
	bool bDownedEffectActive = false;

	/** 다운 효과 Tick 로그 제한용 타이머 */
	float DownedEffectLogTimer = 0.f;

	// ── Downed Screen Effect v2: 3단계 + 심장박동 펄스 ──

	float DownedIR = 0.55f;           // 현재 InnerRadius
	float DownedIRTarget = 0.55f;     // 목표 InnerRadius
	float DownedOpacity = 0.f;        // 현재 Opacity
	float DownedOpacityTarget = 0.f;  // 목표 Opacity
	float DownedBrightness = 1.f;     // 화면 밝기 (40% 이하에서 어두워짐)
	float DownedBrightnessTarget = 1.f;
	float DownedBlackout = 0.f;       // 완전 암전 (5% 이하)
	float DownedBlackoutTarget = 0.f;

	// 심장박동 펄스
	float PulseTimer = 0.f;
	float PulsePeriod = 2.0f;
	float PulseIRBoost = 0.f;
	float PulseOpacityBoost = 0.f;

	// =========================================================
	// ★ [Phase21] 다운 화면 효과 — BP 조절 가능 파라미터
	// =========================================================

	// --- 비네트 InnerRadius ---
	/** 다운 시작 시 비네트 내부 반경 (클수록 중앙이 넓게 보임) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Vignette",
		meta = (DisplayName = "비네트 시작 반경", ClampMin = "0.0", ClampMax = "1.0"))
	float IR_START = 0.55f;

	/** 사망 직전 비네트 내부 반경 (작을수록 화면이 좁아짐) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Vignette",
		meta = (DisplayName = "비네트 종료 반경", ClampMin = "0.0", ClampMax = "1.0"))
	float IR_END = 0.10f;

	// --- 불투명도 ---
	/** 다운 시작 시 효과 불투명도 */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Vignette",
		meta = (DisplayName = "시작 불투명도", ClampMin = "0.0", ClampMax = "1.0"))
	float OP_START = 0.5f;

	/** 사망 직전 효과 불투명도 */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Vignette",
		meta = (DisplayName = "종료 불투명도", ClampMin = "0.0", ClampMax = "1.0"))
	float OP_END = 1.0f;

	// --- 밝기/암전 ---
	/** 어두워지기 시작하는 체력 비율 (0.4 = 40% 이하부터) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Brightness",
		meta = (DisplayName = "어두워짐 시작 비율", ClampMin = "0.0", ClampMax = "1.0"))
	float DARKEN_START = 0.40f;

	/** 완전 암전 시작 체력 비율 (0.05 = 5% 이하부터) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Brightness",
		meta = (DisplayName = "암전 시작 비율", ClampMin = "0.0", ClampMax = "0.5"))
	float BLACKOUT_START = 0.05f;

	// --- 심장박동 펄스 ---
	/** 심장박동 주기 — 다운 직후 (초, 느린 두근) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Heartbeat",
		meta = (DisplayName = "심장박동 최대 주기(초)", ClampMin = "0.2", ClampMax = "5.0"))
	float PULSE_PERIOD_MAX = 2.0f;

	/** 심장박동 주기 — 사망 직전 (초, 빠른 두근) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Heartbeat",
		meta = (DisplayName = "심장박동 최소 주기(초)", ClampMin = "0.1", ClampMax = "2.0"))
	float PULSE_PERIOD_MIN = 0.4f;

	/** 심장박동 IR 부스트량 (펄스마다 비네트가 좁아지는 정도) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Heartbeat",
		meta = (DisplayName = "펄스 IR 부스트", ClampMin = "0.0", ClampMax = "0.2"))
	float PULSE_IR_AMOUNT = 0.05f;

	/** 심장박동 Opacity 부스트량 (펄스마다 효과가 진해지는 정도) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Heartbeat",
		meta = (DisplayName = "펄스 Opacity 부스트", ClampMin = "0.0", ClampMax = "0.3"))
	float PULSE_OP_AMOUNT = 0.08f;

	/** 심장박동 펄스 감쇠 속도 (클수록 빠르게 원래로 돌아옴) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Heartbeat",
		meta = (DisplayName = "펄스 감쇠 속도", ClampMin = "1.0", ClampMax = "15.0"))
	float PULSE_DECAY_SPEED = 5.0f;

	// --- PP 머티리얼 (Vefects Hurt) ---
	/** PP 머티리얼 Weight 최대값 (1.0이면 화면 100% 대체 → 0.35 권장) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|PP Material",
		meta = (DisplayName = "PP Weight 최대값", ClampMin = "0.05", ClampMax = "1.0"))
	float PP_WEIGHT_MAX = 0.35f;

	/** Hurt PP 혈관 흔들림 (0=정적, 0.18=기본값, 낮을수록 부드러움) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|PP Material",
		meta = (DisplayName = "혈관 흔들림 (VeinsOffsetX)", ClampMin = "0.0", ClampMax = "0.5"))
	float DownedVeinsOffsetX = 0.05f;

	/** Hurt PP 물방울 굴절 강도 (0=없음, 0.1=기본값) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|PP Material",
		meta = (DisplayName = "물방울 굴절 (DropsNormalStrength)", ClampMin = "0.0", ClampMax = "0.3"))
	float DownedDropsNormalStrength = 0.03f;

	/** 전체 효과 보간 속도 (클수록 즉각 반응) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|General",
		meta = (DisplayName = "효과 보간 속도", ClampMin = "1.0", ClampMax = "10.0"))
	float EFFECT_INTERP_SPEED = 3.0f;

	/** 다운 효과 시작 (로컬 전용, Multicast_PlayHeroDowned에서 호출) */
	void StartDownedScreenEffect();

	/** 다운 효과 종료 (로컬 전용, Revive/Death에서 호출) */
	void StopDownedScreenEffect();

	/** 다운 효과 Tick 업데이트 (로컬 전용) */
	void TickDownedScreenEffect(float DeltaTime);

protected:
	/** HealthComponent (피격/사망 처리) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component",
		meta = (DisplayName = "체력 컴포넌트"))
	TObjectPtr<UHellunaHealthComponent> HeroHealthComponent = nullptr;

	/** Phase 9: 사체 루팅용 컨테이너 (사망 시 아이템 이전) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component",
		meta = (DisplayName = "루트 컨테이너 컴포넌트"))
	TObjectPtr<UInv_LootContainerComponent> LootContainerComponent = nullptr;

	UFUNCTION()
	void OnHeroHealthChanged(UActorComponent* HealthComp, float OldHealth, float NewHealth, AActor* InstigatorActor);

	UFUNCTION()
	void OnHeroDeath(AActor* DeadActor, AActor* KillerActor);

	/** 다운 델리게이트 바인딩 */
	UFUNCTION()
	void OnHeroDowned(AActor* DownedActor, AActor* InstigatorActor);

	/** 피격 몽타주 멀티캐스트 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHeroHitReact();

	/** 사망 몽타주 멀티캐스트 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHeroDeath();

	/** 다운 몽타주 + 카메라 멀티캐스트 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHeroDowned();

	/** 부활 몽타주 + 카메라 복구 멀티캐스트 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayHeroRevived();

	// ── Revive 입력 (F키 홀드) ──
	void Input_ReviveStarted(const FInputActionValue& Value);
	void Input_ReviveCompleted(const FInputActionValue& Value);

	// ── Interaction 입력 (F키 홀드 — BossEncounterCube 등) ──
	void Input_InteractionStarted(const FInputActionValue& Value);
	void Input_InteractionCompleted(const FInputActionValue& Value);

	/** F키 홀드 상태 (BossEncounterCube 등에서 Tick 프로그레스 갱신용) */
	bool bHoldingInteraction = false;

	/** 로컬: 현재 부활 수행 중인지 (클라이언트용 플래그, ReviveTarget은 서버 전용이라 사용 불가) */
	bool bIsRevivingLocal = false;

	/** 서버 RPC: 부활 시작 */
	UFUNCTION(Server, Reliable)
	void Server_StartRevive(AHellunaHeroCharacter* TargetHero);

	/** 서버 RPC: 부활 중단 */
	UFUNCTION(Server, Reliable)
	void Server_StopRevive();

	UFUNCTION()
	void OnRep_ReviveProgress();

	FTimerHandle ReviveTickTimerHandle;
	void TickRevive();

public:
	/** 로컬: 근처 다운 팀원 탐색 (GA_Repair 등 외부에서도 호출) */
	AHellunaHeroCharacter* FindNearestDownedHero() const;

public:
	// =========================================================
	// ★ 건패링 워프 VFX 멀티캐스트 (Step 2b)
	// =========================================================

	/** 워프 잔상 나이아가라 이펙트 — 서버에서 호출, 모든 클라에서 스폰 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayParryWarpVFX(UNiagaraSystem* Effect, FVector Location, FRotator Rotation, float Scale, FLinearColor Color, bool bGhostMesh, float GhostOpacity);

	/** 워프 잔상 VFX 중단 — AN_ParryExecutionFire 타이밍에 서버에서 호출 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_StopParryWarpVFX();

	/** 패링 잔상(Ghost Trail) 스폰 — 서버에서 호출, 모든 클라에서 PoseableMesh 스폰 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SpawnParryGhostTrail(int32 Count, float FadeDuration,
		FVector StartLocation, FVector EndLocation, FRotator TrailRotation,
		FLinearColor GhostColor, UMaterialInterface* TrailMaterial);

private:
	/** 현재 활성 상태인 패링 워프 VFX 컴포넌트 (Deactivate용 추적) */
	TArray<TWeakObjectPtr<UNiagaraComponent>> ActiveParryVFX;

	// ═══════════════════════════════════════════════════════════
	// OTS 카메라 — 조준(Aim) 줌인 보간
	// ═══════════════════════════════════════════════════════════

	// ── 조준 카메라 기본값 (BeginPlay에서 캐싱) ──
	float DefaultTargetArmLength = 250.f;
	float DefaultFOV = 90.f;
	FVector DefaultSocketOffset = FVector(0.f, 60.f, 55.f);

	// ── 조준 시 카메라 목표값 (에디터에서 조정 가능) ──
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Aim",
		meta = (DisplayName = "Aim Target Arm Length (조준 시 카메라 거리)", ClampMin = "50.0", ClampMax = "400.0"))
	float AimTargetArmLength = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Aim",
		meta = (DisplayName = "Aim FOV (조준 시 FOV)", ClampMin = "40.0", ClampMax = "120.0"))
	float AimFOV = 65.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Aim",
		meta = (DisplayName = "Aim Socket Offset (조준 시 SocketOffset)"))
	FVector AimSocketOffset = FVector(0.f, 70.f, 45.f);

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Aim",
		meta = (DisplayName = "Aim Interp Speed (카메라 보간 속도)", ClampMin = "1.0", ClampMax = "30.0"))
	float AimInterpSpeed = 10.f;

	/** 현재 조준 상태 (ASC 태그 기반) */
	bool bIsCurrentlyAiming = false;
	bool bWasAimingLastFrame = false;

	// =========================================================
	// ★ [Phase18] 킥 3D 프롬프트 위젯
	// =========================================================
protected:
	/** 킥 프롬프트 WidgetComponent (플로팅 — Staggered 적 위치로 이동) */
	UPROPERTY()
	TObjectPtr<UWidgetComponent> KickPromptWidgetComp;

	/** 킥 프롬프트 위젯 클래스 (BP에서 할당 — WBP_Inv_InteractPrompt) */
	UPROPERTY(EditDefaultsOnly, Category = "Kick|Widget",
		meta = (DisplayName = "Kick Prompt Widget Class (킥 프롬프트 위젯 클래스)"))
	TSubclassOf<UUserWidget> KickPromptWidgetClass;

	/** 킥 프롬프트 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UUserWidget> KickPromptWidgetInstance;

	/** 현재 킥 프롬프트가 표시 중인지 */
	bool bKickPromptVisible = false;

	/** 킥 감지 범위 (GA_MeleeKick과 동일하게 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Kick|Widget",
		meta = (DisplayName = "Kick Prompt Range (킥 감지 범위)"))
	float KickPromptRange = 200.f;

	/** 킥 감지 전방각 (도) */
	UPROPERTY(EditDefaultsOnly, Category = "Kick|Widget",
		meta = (DisplayName = "Kick Prompt Half Angle (킥 감지 전방각)"))
	float KickPromptHalfAngle = 60.f;

	/** 킥 프롬프트 업데이트 (Tick에서 호출) */
	void UpdateKickPrompt(float DeltaTime);

private:
	/** 킥 프롬프트 로그 타이머 (1초 1회 제한) */
	float KickPromptLogTimer = 0.f;

	// =========================================================
	// ★ [Phase21] 8방향 피격 혈흔 (별도 위젯: WBP_BloodHitOverlay)
	// =========================================================
public:
	/** 피격 방향 혈흔 표시 (Multicast — 로컬 플레이어만 표시) */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ShowBloodHitDirection(uint8 DirIndex);

protected:
	/** 피격 혈흔 위젯 클래스 (BP에서 WBP_BloodHitOverlay 할당) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Blood Hit",
		meta = (DisplayName = "Blood Hit Widget Class (피격 혈흔 위젯 클래스)"))
	TSubclassOf<UUserWidget> BloodHitWidgetClass;

	/** 피격 혈흔 위젯 인스턴스 (런타임 생성) */
	UPROPERTY()
	TObjectPtr<UUserWidget> BloodHitWidget;

	/** 피격 혈흔 이미지 배열 (WBP_BloodHitOverlay에서 바인딩) — BeginPlay에서 초기화 */
	UPROPERTY()
	TArray<TObjectPtr<UImage>> BloodHitImages;

	/** 피격 혈흔 페이드아웃 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Blood Hit",
		meta = (DisplayName = "Blood Hit Fade Duration (피격 혈흔 페이드 시간, 초)", ClampMin = "0.1", ClampMax = "2.0"))
	float BloodHitFadeDuration = 0.4f;

	/** 피격 혈흔 텍스처 (8개 Image의 Brush에 할당, 미설정 시 WBP 기본 Brush 사용) */
	UPROPERTY(EditDefaultsOnly, Category = "Downed|Blood Hit",
		meta = (DisplayName = "Blood Hit Texture (피격 혈흔 텍스처)"))
	TObjectPtr<UTexture2D> BloodHitTexture;

	/** 현재 진행 중인 페이드 타이머 핸들 (방향별) */
	FTimerHandle BloodHitTimers[8];

private:
	/** 방향 인덱스로 해당 Image 페이드 시작 */
	void PlayBloodHitFade(uint8 DirIndex);

	/** 피격 방향 계산 (서버) — 0~7 반환 */
	uint8 CalcHitDirection(AActor* InstigatorActor) const;
};
