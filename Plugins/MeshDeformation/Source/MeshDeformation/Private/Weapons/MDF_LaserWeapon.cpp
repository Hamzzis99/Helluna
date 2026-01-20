// Gihyeon's MeshDeformation Project
// File: Source/MeshDeformation/Weapon/MDF_LaserWeapon.cpp

#include "Weapons/MDF_LaserWeapon.h"
#include "Components/MDF_MiniGameComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

AMDF_LaserWeapon::AMDF_LaserWeapon()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false; 
    BatteryDrainRate = 20.0f;
    LaserColor = FColor::Red;
    FireRange = 3000.0f;
    bUseScreenCenter = true;  // 기본값: 화면 중앙 기준
}

void AMDF_LaserWeapon::BeginPlay() { Super::BeginPlay(); }

void AMDF_LaserWeapon::StartFire()
{
    if (CurrentAmmo <= 0.0f) return;
    SetActorTickEnabled(true);
    UE_LOG(LogTemp, Log, TEXT("[레이저] 가동 시작"));
}

void AMDF_LaserWeapon::StopFire()
{
    SetActorTickEnabled(false);
    
    if (CurrentTargetComp)
    {
        // 마지막 히트 위치로 EndMarking
        CurrentTargetComp->EndMarking(LastHitLocation);
        CurrentTargetComp = nullptr;
    }
    UE_LOG(LogTemp, Log, TEXT("[레이저] 가동 중지"));
}

void AMDF_LaserWeapon::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    CurrentAmmo -= BatteryDrainRate * DeltaTime;
    if (CurrentAmmo <= 0.0f) { CurrentAmmo = 0.0f; StopFire(); return; }
    
    ProcessLaserTrace();
}

void AMDF_LaserWeapon::ProcessLaserTrace()
{
    FVector Start;
    FVector End;
    
    // -------------------------------------------------------------------------
    // [화면 중앙 기준 vs 총구 기준] 선택
    // -------------------------------------------------------------------------
    if (bUseScreenCenter)
    {
        // 화면 중앙(크로스헤어) 기준 레이캐스트
        APawn* OwnerPawn = Cast<APawn>(GetOwner());
        APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
        
        if (PC && PC->PlayerCameraManager)
        {
            Start = PC->PlayerCameraManager->GetCameraLocation();
            FVector CameraForward = PC->PlayerCameraManager->GetCameraRotation().Vector();
            End = Start + (CameraForward * FireRange);
        }
        else
        {
            // 폴백: 총구 기준
            Start = MuzzleLocation ? MuzzleLocation->GetComponentLocation() : GetActorLocation();
            FVector Forward = MuzzleLocation ? MuzzleLocation->GetForwardVector() : GetActorForwardVector();
            End = Start + (Forward * FireRange);
        }
    }
    else
    {
        // 기존 방식: 총구 기준
        Start = MuzzleLocation ? MuzzleLocation->GetComponentLocation() : GetActorLocation();
        FVector Forward = MuzzleLocation ? MuzzleLocation->GetForwardVector() : GetActorForwardVector();
        End = Start + (Forward * FireRange);
    }

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);
    Params.AddIgnoredActor(GetOwner());

    bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);

    // [디버깅] 레이저 궤적 그리기
    if (bHit)
    {
        // 화면 중앙 모드일 때는 총구에서 히트 지점까지 레이저 그리기 (시각적)
        FVector LaserStart = MuzzleLocation ? MuzzleLocation->GetComponentLocation() : GetActorLocation();
        DrawDebugLine(GetWorld(), LaserStart, HitResult.Location, LaserColor, false, -1.0f, 0, 2.0f);
        DrawDebugPoint(GetWorld(), HitResult.Location, 10.0f, FColor::Blue, false, -1.0f);
        
        LastHitLocation = HitResult.Location;  // 마지막 히트 위치 저장
    }
    else
    {
        FVector LaserStart = MuzzleLocation ? MuzzleLocation->GetComponentLocation() : GetActorLocation();
        FVector LaserEnd = LaserStart + ((End - Start).GetSafeNormal() * FireRange);
        DrawDebugLine(GetWorld(), LaserStart, LaserEnd, LaserColor, false, -1.0f, 0, 1.0f);
        
        LastHitLocation = LaserEnd;
    }

    // -------------------------------------------------------------------------
    // 타겟 연동 로직
    // -------------------------------------------------------------------------
    UMDF_MiniGameComponent* NewWallComp = nullptr;
    if (bHit && HitResult.GetActor())
    {
        NewWallComp = HitResult.GetActor()->FindComponentByClass<UMDF_MiniGameComponent>();
    }

    if (NewWallComp)
    {
        if (CurrentTargetComp != NewWallComp)
        {
            if (CurrentTargetComp) CurrentTargetComp->EndMarking(HitResult.Location); 
            NewWallComp->StartMarking(HitResult.Location);
            CurrentTargetComp = NewWallComp;
        }
        else
        {
            CurrentTargetComp->UpdateMarking(HitResult.Location);
        }
    }
    else
    {
        // 벽에서 벗어나면 마킹 종료
        if (CurrentTargetComp)
        {
            CurrentTargetComp->EndMarking(LastHitLocation);
            CurrentTargetComp = nullptr;
        }
    }
}