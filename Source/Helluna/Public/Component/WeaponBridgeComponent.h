// ============================================
// ⭐ WeaponBridgeComponent
// ⭐ 작성자: Gihyeon
// ⭐ 목적: Inventory 플러그인과 Helluna 무기 시스템 연동
// ⭐ 
// ⭐ [주요 역할]
// ⭐ - EquipmentComponent의 델리게이트(OnWeaponEquipRequested) 수신
// ⭐ - 무기 꺼내기: 팀원의 GA_SpawnWeapon 활성화
// ⭐ - 무기 집어넣기: CurrentWeapon Destroy
// ⭐ 
// ⭐ [위치]
// ⭐ - HellunaHeroCharacter에 부착됨 (생성자에서 CreateDefaultSubobject)
// ⭐ 
// ⭐ [의존성]
// ⭐ - Inventory 플러그인: Inv_EquipmentComponent, Inv_EquipActor
// ⭐ - Helluna 모듈: HellunaHeroCharacter, HellunaHeroWeapon
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Items/Fragments/Inv_AttachmentFragments.h" // UFUNCTION 파라미터에 FInv_AttachmentVisualInfo 필요
#include "WeaponBridgeComponent.generated.h"

class AHellunaHeroCharacter;
class AHellunaHeroWeapon; // 김기현 — 부착물 시각 전달용
class UHellunaAbilitySystemComponent;
class AInv_EquipActor;
class UInv_EquipmentComponent;
class UGameplayAbility;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UWeaponBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ⭐ 생성자
	UWeaponBridgeComponent();

protected:
	// ⭐ BeginPlay - 초기화 시작점
	virtual void BeginPlay() override;

private:
	// ============================================
	// ⭐ 장착 애니메이션 진행 중 플래그
	// ⭐ true일 때 무기 전환 입력을 차단
	// ============================================
	bool bIsEquipping = false;

public:
	// ⭐ 장착 중 여부 Getter (Inv_EquipmentComponent에서 사용)
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsEquipping() const { return bIsEquipping; }

	// ⭐ 장착 상태 Setter (GA_SpawnWeapon에서 호출)
	void SetEquipping(bool bNewEquipping);

private:
	// ============================================
	// ⭐ 참조 변수들
	// ============================================
	
	// 소유 캐릭터 (HellunaHeroCharacter)
	// CurrentWeapon 접근 및 GA 활성화에 사용
	TWeakObjectPtr<AHellunaHeroCharacter> OwningCharacter;
	
	// AbilitySystemComponent
	// GA 활성화에 사용 (TryActivateAbilityByClass)
	TWeakObjectPtr<UHellunaAbilitySystemComponent> AbilitySystemComponent;
	
	// EquipmentComponent (PlayerController에 부착됨)
	// 델리게이트 바인딩 대상
	TWeakObjectPtr<UInv_EquipmentComponent> EquipmentComponent;
	
	// ============================================
	// ⭐ 초기화 함수
	// ============================================
	
	// EquipmentComponent 찾아서 델리게이트 바인딩
	// BeginPlay에서 호출됨
	void InitializeWeaponBridge();
	
	// ============================================
	// ⭐ 델리게이트 콜백 함수
	// ============================================
	
	// Inventory에서 무기 꺼내기/집어넣기 요청 시 호출
	// @param WeaponTag: 무기 종류 태그 (예: GameItems.Equipment.Weapons.Axe)
	// @param BackWeaponActor: 등에 붙은 무기 Actor
	// @param SpawnWeaponAbility: 활성화할 GA 클래스 (팀원의 GA_SpawnWeapon)
	// @param bEquip: true=꺼내기, false=집어넣기
	// @param WeaponSlotIndex: 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
	// ════════════════════════════════════════════════════════════════
	// TODO: [독립화] 졸작 후 변경 사항
	//
	// 1) 콜백 시그니처에서 SpawnWeaponAbility 파라미터 삭제:
	//    void OnWeaponEquipRequested(
	//        const FGameplayTag& WeaponTag,
	//        AInv_EquipActor* BackWeaponActor,
	//        bool bEquip,
	//        int32 WeaponSlotIndex);
	//
	// 2) WeaponGAMap 프로퍼티 추가 (private):
	//    UPROPERTY(EditAnywhere, Category = "Weapon",
	//        meta = (DisplayName = "무기 태그 -> GA 매핑"))
	//    TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> WeaponGAMap;
	//
	// 3) OnWeaponEquipRequested 내부에서:
	//    기존: SpawnHandWeapon(SpawnWeaponAbility) — 델리게이트에서 GA를 직접 받음
	//    변경: SpawnHandWeapon(WeaponGAMap.FindRef(WeaponTag)) — 자체 매핑에서 GA 획득
	//
	// 4) EquipmentComponent의 bUseBuiltInHandWeapon = false 설정 필수
	//    (Helluna는 커스텀 모드 사용)
	// ════════════════════════════════════════════════════════════════
	UFUNCTION()
	void OnWeaponEquipRequested(
		const FGameplayTag& WeaponTag,
		AInv_EquipActor* BackWeaponActor,
		TSubclassOf<UGameplayAbility> SpawnWeaponAbility,
		bool bEquip,
		int32 WeaponSlotIndex
	);
	
	// ============================================
	// ⭐ 손 무기 관리 함수
	// ============================================
	
	// 손 무기 스폰 (GA 활성화)
	// 팀원의 GA_SpawnWeapon을 TryActivateAbilityByClass로 호출
	// @param SpawnWeaponAbility: 활성화할 GA 클래스
	void SpawnHandWeapon(TSubclassOf<UGameplayAbility> SpawnWeaponAbility);
	
	// 손 무기 제거
	// CurrentWeapon을 Destroy하고 nullptr로 설정
	void DestroyHandWeapon();

	// ============================================
	// ⭐ 부착물 시각 전달 (EquipActor → HandWeapon)
	// 작성: 김기현 (인벤토리 부착물 시스템 연동)
	// ============================================
	// EquipActor(등 무기)의 부착물 시각 정보를 읽어서
	// HandWeapon(손 무기)에 동일하게 복제한다.
	// GA 수정 없이 WeaponBridge에서 처리.

	// EquipActor → HandWeapon으로 부착물 메시 전달
	void TransferAttachmentVisuals(AInv_EquipActor* EquipActor, AHellunaHeroWeapon* HandWeapon);

	// 부착물 시각 데이터 배열을 HandWeapon에 직접 적용
	void ApplyVisualsToHandWeapon(const TArray<FInv_AttachmentVisualInfo>& Visuals);

	// GA 비동기 스폰 대기용 타이머 콜백 (서버에서 실행)
	// GetCurrentWeapon()이 유효해지면 Multicast RPC로 부착물 데이터 전송
	void WaitAndTransferAttachments();

	// 부착물 전달 대기 중인 EquipActor (서버 타이머에서 참조)
	TWeakObjectPtr<AInv_EquipActor> PendingAttachmentSource;

	// 서버 타이머 핸들 (대기 중 취소용)
	FTimerHandle AttachmentTransferTimerHandle;

	// 서버 타이머 재시도 횟수 (무한 루프 방지)
	int32 AttachmentTransferRetryCount = 0;

	// 최대 재시도 횟수 (0.05초 * 100 = 5초)
	static constexpr int32 MaxAttachmentTransferRetries = 100;

	// ============================================
	// ⭐ 멀티플레이 부착물 시각 리플리케이션
	// ============================================
	// 소유 클라이언트 → 서버: 부착물 전달 요청
	UFUNCTION(Server, Reliable)
	void Server_RequestAttachmentTransfer(AInv_EquipActor* EquipActor);

	// 서버 → 모든 클라이언트: 부착물 시각 데이터 전송
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyAttachmentVisuals(const TArray<FInv_AttachmentVisualInfo>& Visuals);

	// Multicast 수신 후 HandWeapon 대기용 (클라이언트 로컬)
	TArray<FInv_AttachmentVisualInfo> PendingMulticastVisuals;
	FTimerHandle MulticastVisualTimerHandle;
	int32 MulticastVisualRetryCount = 0;
	void OnMulticastVisualTimerTick();
};
