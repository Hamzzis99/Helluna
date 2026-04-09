#include "Object/OreMiningEffectComponent.h"

#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Resource/Inv_ResourceComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOreMiningFX, Log, All);

UOreMiningEffectComponent::UOreMiningEffectComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UOreMiningEffectComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner) return;

    // OnTakePointDamage 델리게이트 바인딩
    Owner->OnTakePointDamage.AddDynamic(this, &UOreMiningEffectComponent::OnOwnerPointDamage);

    // HellunaHealthComponent의 OnDeath 델리게이트 바인딩
    if (UHellunaHealthComponent* HealthComp = Owner->FindComponentByClass<UHellunaHealthComponent>())
    {
        HealthComp->OnDeath.AddDynamic(this, &UOreMiningEffectComponent::OnOwnerDeath);
        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] OnDeath 바인딩 완료 (HellunaHealthComponent)"), *Owner->GetName());
    }
    else
    {
        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] HellunaHealthComponent 없음 — OnDestroyed 바인딩으로 전환"), *Owner->GetName());
    }

    // Inv_ResourceComponent의 파괴 델리게이트 바인딩
    if (UInv_ResourceComponent* ResourceComp = Owner->FindComponentByClass<UInv_ResourceComponent>())
    {
        ResourceComp->OnResourceDestroyed.AddDynamic(this, &UOreMiningEffectComponent::OnResourceDestroyed);
        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] OnResourceDestroyed 바인딩 완료 (Inv_ResourceComponent)"), *Owner->GetName());
    }

    UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 채굴 이펙트 컴포넌트 초기화 완료 (VFX:%s 쉐이크:%s 사운드:%s 파괴VFX:%s)"),
        *Owner->GetName(),
        MiningImpactVFX ? TEXT("O") : TEXT("X"),
        *MiningCameraShake ? TEXT("O") : TEXT("X"),
        MiningHitSound ? TEXT("O") : TEXT("X"),
        DestroyVFX ? TEXT("O") : TEXT("X"));
}

void UOreMiningEffectComponent::OnOwnerPointDamage(
    AActor* DamagedActor,
    float Damage,
    AController* InstigatedBy,
    FVector HitLocation,
    UPrimitiveComponent* FHitComponent,
    FName BoneName,
    FVector ShotFromDirection,
    const UDamageType* DamageType,
    AActor* DamageCauser)
{
    UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 데미지 수신: %.1f (가해자: %s)"),
        *GetOwner()->GetName(),
        Damage,
        InstigatedBy ? *InstigatedBy->GetName() : TEXT("None"));

    // HitLocation이 액터 원점(지면)일 수 있으므로, 메쉬 바운드 중심으로 보정
    FVector EffectLocation = HitLocation;
    AActor* Owner = GetOwner();
    if (Owner)
    {
        FVector Origin, BoxExtent;
        Owner->GetActorBounds(false, Origin, BoxExtent);
        EffectLocation = Origin; // 메쉬 바운드의 실제 중심점
    }

    UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 이펙트 위치 보정: HitLocation=%s → EffectLocation=%s"),
        *GetOwner()->GetName(), *HitLocation.ToString(), *EffectLocation.ToString());

    // 서버에서 멀티캐스트 → 모든 클라이언트에서 이펙트 재생
    Multicast_PlayMiningEffects(EffectLocation, ShotFromDirection, InstigatedBy);
}

void UOreMiningEffectComponent::Multicast_PlayMiningEffects_Implementation(
    FVector EffectLocation, FVector ShotFromDirection, AController* InstigatedBy)
{
    // 데디케이티드 서버에서는 이펙트 불필요
    if (GetOwner() && GetOwner()->GetWorld() && GetOwner()->GetWorld()->IsNetMode(NM_DedicatedServer))
    {
        return;
    }

    SpawnMiningVFX(EffectLocation, ShotFromDirection);
    PlayMiningCameraShake(InstigatedBy, EffectLocation);
    PlayMiningSound(EffectLocation);
}

void UOreMiningEffectComponent::OnOwnerDeath(AActor* DeadActor, AActor* KillerActor)
{
    if (bDestroyEffectsPlayed) return;
    bDestroyEffectsPlayed = true;

    UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 광석 파괴됨 — OnDeath (처치자: %s)"),
        *GetOwner()->GetName(),
        KillerActor ? *KillerActor->GetName() : TEXT("None"));

    FVector EffectLocation;
    AActor* Owner = GetOwner();
    if (Owner)
    {
        FVector Origin, BoxExtent;
        Owner->GetActorBounds(false, Origin, BoxExtent);
        EffectLocation = Origin;
    }

    Multicast_PlayDestroyEffects(EffectLocation);
}

void UOreMiningEffectComponent::OnResourceDestroyed(FVector DestroyLocation)
{
    if (bDestroyEffectsPlayed) return;
    bDestroyEffectsPlayed = true;

    UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 광석 파괴됨 — OnResourceDestroyed"), *GetOwner()->GetName());

    Multicast_PlayDestroyEffects(DestroyLocation);
}

void UOreMiningEffectComponent::Multicast_PlayDestroyEffects_Implementation(FVector EffectLocation)
{
    // 데디케이티드 서버에서는 이펙트 불필요
    if (GetOwner() && GetOwner()->GetWorld() && GetOwner()->GetWorld()->IsNetMode(NM_DedicatedServer))
    {
        return;
    }

    // 파괴 VFX 스폰
    if (DestroyVFX)
    {
        UNiagaraComponent* SpawnedComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            this,
            DestroyVFX,
            EffectLocation,
            FRotator::ZeroRotator,
            DestroyVFXScale,
            true,   // bAutoDestroy
            true,   // bAutoActivate
            ENCPoolMethod::AutoRelease
        );

        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 파괴 VFX 스폰 %s — 위치: %s"),
            *GetOwner()->GetName(),
            SpawnedComp ? TEXT("성공") : TEXT("실패"),
            *EffectLocation.ToString());
    }

    // 파괴 사운드 재생
    if (DestroySound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, DestroySound, EffectLocation);
        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 파괴 사운드 재생 완료"), *GetOwner()->GetName());
    }

    // 파괴 카메라 쉐이크
    if (DestroyCameraShake)
    {
        UGameplayStatics::PlayWorldCameraShake(
            this,
            DestroyCameraShake,
            EffectLocation,
            0.f,
            DestroyCameraShakeRadius,
            DestroyCameraShakeScale
        );
        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 파괴 카메라 쉐이크 재생 (반경: %.0fcm, 강도: %.1f)"),
            *GetOwner()->GetName(), DestroyCameraShakeRadius, DestroyCameraShakeScale);
    }
}

void UOreMiningEffectComponent::SpawnMiningVFX(const FVector& HitLocation, const FVector& ShotFromDirection)
{
    if (!MiningImpactVFX)
    {
        UE_LOG(LogOreMiningFX, Warning, TEXT("[%s] VFX 스폰 실패 — MiningImpactVFX가 설정되지 않음!"), *GetOwner()->GetName());
        return;
    }

    // 타격 방향의 반대 방향으로 이펙트 회전 (파편이 플레이어 쪽으로 튀는 느낌)
    const FRotator VFXRotation = (-ShotFromDirection).Rotation();

    UNiagaraComponent* SpawnedComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        this,
        MiningImpactVFX,
        HitLocation,
        VFXRotation,
        MiningVFXScale,
        true,   // bAutoDestroy
        true,   // bAutoActivate
        ENCPoolMethod::AutoRelease
    );

    if (SpawnedComp)
    {
        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] VFX 스폰 성공 — 위치: %s, IsActive: %d, IsVisible: %d, Asset: %s"),
            *GetOwner()->GetName(),
            *HitLocation.ToString(),
            SpawnedComp->IsActive(),
            SpawnedComp->IsVisible(),
            *MiningImpactVFX->GetName());
    }
    else
    {
        UE_LOG(LogOreMiningFX, Error, TEXT("[%s] VFX 스폰 실패 — SpawnSystemAtLocation이 nullptr 반환! Asset: %s"),
            *GetOwner()->GetName(),
            *MiningImpactVFX->GetName());
    }
}

void UOreMiningEffectComponent::PlayMiningCameraShake(AController* InstigatedBy, const FVector& EpicenterLocation)
{
    if (!MiningCameraShake)
    {
        UE_LOG(LogOreMiningFX, Warning, TEXT("[%s] 카메라 쉐이크 실패 — MiningCameraShake가 설정되지 않음!"), *GetOwner()->GetName());
        return;
    }

    if (CameraShakeRadius > 0.f)
    {
        UGameplayStatics::PlayWorldCameraShake(
            this,
            MiningCameraShake,
            EpicenterLocation,
            0.f,
            CameraShakeRadius,
            CameraShakeScale
        );
        UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 카메라 쉐이크 재생 — 월드 범위 (반경: %.0fcm, 강도: %.1f)"),
            *GetOwner()->GetName(), CameraShakeRadius, CameraShakeScale);
    }
    else
    {
        APlayerController* PC = Cast<APlayerController>(InstigatedBy);
        if (PC)
        {
            PC->ClientStartCameraShake(MiningCameraShake, CameraShakeScale);
            UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 카메라 쉐이크 재생 — 가해자 전용 (플레이어: %s, 강도: %.1f)"),
                *GetOwner()->GetName(), *PC->GetName(), CameraShakeScale);
        }
        else
        {
            UE_LOG(LogOreMiningFX, Warning, TEXT("[%s] 카메라 쉐이크 실패 — InstigatedBy가 PlayerController가 아님"),
                *GetOwner()->GetName());
        }
    }
}

void UOreMiningEffectComponent::PlayMiningSound(const FVector& HitLocation)
{
    if (!MiningHitSound)
    {
        UE_LOG(LogOreMiningFX, Warning, TEXT("[%s] 사운드 재생 실패 — MiningHitSound가 설정되지 않음!"), *GetOwner()->GetName());
        return;
    }

    UGameplayStatics::PlaySoundAtLocation(this, MiningHitSound, HitLocation);

    UE_LOG(LogOreMiningFX, Log, TEXT("[%s] 사운드 재생 완료 — 위치: %s"), *GetOwner()->GetName(), *HitLocation.ToString());
}
