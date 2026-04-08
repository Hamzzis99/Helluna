// 광석 채굴 이펙트 컴포넌트
// BP_Test_Farming 등 광석 BP에 추가하면
// 데미지를 받을 때 VFX + 카메라 쉐이크 + 사운드를 자동 재생합니다.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "OreMiningEffectComponent.generated.h"

class UNiagaraSystem;
class UHellunaHealthComponent;
class UInv_ResourceComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName = "광석 채굴 이펙트"))
class HELLUNA_API UOreMiningEffectComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOreMiningEffectComponent();

protected:
    virtual void BeginPlay() override;

    // ==================================================================================
    // VFX/쉐이크 — 채굴
    // ==================================================================================

    /** 채굴 시 스폰할 Niagara 이펙트 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|채굴",
        meta = (DisplayName = "채굴 VFX"))
    UNiagaraSystem* MiningImpactVFX = nullptr;

    /** VFX 스케일 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|채굴",
        meta = (DisplayName = "VFX 스케일"))
    FVector MiningVFXScale = FVector(1.0f);

    /** 채굴 시 적용할 카메라 쉐이크 클래스 (BP에서 설정) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|채굴",
        meta = (DisplayName = "카메라 쉐이크"))
    TSubclassOf<UCameraShakeBase> MiningCameraShake;

    /** 카메라 쉐이크 강도 배율 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|채굴",
        meta = (DisplayName = "쉐이크 강도", ClampMin = "0.0", ClampMax = "20.0"))
    float CameraShakeScale = 1.0f;

    /** 카메라 쉐이크 적용 범위 (0 = 데미지를 가한 플레이어만) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|채굴",
        meta = (DisplayName = "쉐이크 반경(cm, 0=가해자만)", ClampMin = "0.0"))
    float CameraShakeRadius = 0.f;

    /** 채굴 시 광석에서 재생할 사운드 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|채굴",
        meta = (DisplayName = "채굴 타격 사운드"))
    USoundBase* MiningHitSound = nullptr;

    // ==================================================================================
    // VFX/쉐이크 — 파괴
    // ==================================================================================

    /** 광석 파괴 시 스폰할 Niagara 이펙트 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|파괴",
        meta = (DisplayName = "파괴 VFX"))
    UNiagaraSystem* DestroyVFX = nullptr;

    /** 파괴 VFX 스케일 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|파괴",
        meta = (DisplayName = "파괴 VFX 스케일"))
    FVector DestroyVFXScale = FVector(1.0f);

    /** 파괴 시 재생할 사운드 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|파괴",
        meta = (DisplayName = "파괴 사운드"))
    USoundBase* DestroySound = nullptr;

    /** 파괴 시 적용할 카메라 쉐이크 클래스 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|파괴",
        meta = (DisplayName = "파괴 카메라 쉐이크"))
    TSubclassOf<UCameraShakeBase> DestroyCameraShake;

    /** 파괴 카메라 쉐이크 강도 배율 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|파괴",
        meta = (DisplayName = "파괴 쉐이크 강도", ClampMin = "0.0", ClampMax = "20.0"))
    float DestroyCameraShakeScale = 2.0f;

    /** 파괴 카메라 쉐이크 적용 범위 (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX/쉐이크|파괴",
        meta = (DisplayName = "파괴 쉐이크 반경(cm)", ClampMin = "0.0"))
    float DestroyCameraShakeRadius = 500.f;

private:
    /** OnTakePointDamage 콜백 — 서버에서 데미지 수신 시 멀티캐스트 호출 */
    UFUNCTION()
    void OnOwnerPointDamage(
        AActor* DamagedActor,
        float Damage,
        class AController* InstigatedBy,
        FVector HitLocation,
        class UPrimitiveComponent* FHitComponent,
        FName BoneName,
        FVector ShotFromDirection,
        const class UDamageType* DamageType,
        AActor* DamageCauser);

    /** [Unreliable Multicast] 모든 클라이언트에서 이펙트 재생 */
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayMiningEffects(FVector EffectLocation, FVector ShotFromDirection, AController* InstigatedBy);

    /** [Reliable Multicast] 파괴 이펙트 재생 (1회성이므로 Reliable) */
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PlayDestroyEffects(FVector EffectLocation);

    /** OnDeath 콜백 — HellunaHealthComponent 사망 시 호출 */
    UFUNCTION()
    void OnOwnerDeath(AActor* DeadActor, AActor* KillerActor);

    /** Inv_ResourceComponent 파괴 델리게이트 콜백 */
    UFUNCTION()
    void OnResourceDestroyed(FVector DestroyLocation);

    /** 파괴 이펙트 중복 재생 방지 */
    bool bDestroyEffectsPlayed = false;

    /** VFX 스폰 */
    void SpawnMiningVFX(const FVector& HitLocation, const FVector& ShotFromDirection);

    /** 카메라 쉐이크 재생 */
    void PlayMiningCameraShake(AController* InstigatedBy, const FVector& EpicenterLocation);

    /** 사운드 재생 */
    void PlayMiningSound(const FVector& HitLocation);
};
