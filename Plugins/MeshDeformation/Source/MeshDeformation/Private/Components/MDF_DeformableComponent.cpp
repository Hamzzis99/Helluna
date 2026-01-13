// Gihyeon's Deformation Project (Helluna)
// MDF_DeformableComponent.cpp

#include "Components/MDF_DeformableComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h" 
#include "Net/UnrealNetwork.h" 
#include "GameFramework/GameStateBase.h"

// 인터페이스 헤더
#include "Interface/MDF_GameStateInterface.h"

// 다이나믹 메시 관련 헤더
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"

// 나이아가라 관련 헤더
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UMDF_DeformableComponent::UMDF_DeformableComponent()
{
    PrimaryComponentTick.bCanEverTick = false; 
    SetIsReplicatedByDefault(true); 
}

void UMDF_DeformableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    // 변형 히스토리만 복제 (HP 제거됨)
    DOREPLIFETIME(UMDF_DeformableComponent, HitHistory);
}

void UMDF_DeformableComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // 1. 초기 메쉬 상태 설정
    InitializeDynamicMesh();
    
    LastAppliedIndex = 0;
    LoadRetryCount = 0;
    
    AActor* Owner = GetOwner();

    // -------------------------------------------------------------------------
    // [GUID 생성 및 데이터 로드]
    // -------------------------------------------------------------------------
    if (IsValid(Owner) && Owner->HasAuthority())
    {
        // 1. GUID가 없다면, '액터 이름'을 해싱하여 고정 ID 생성
        if (!ComponentGuid.IsValid())
        {
            FString MyName = Owner->GetName();
            uint32 NameHash = GetTypeHash(MyName);
            ComponentGuid = FGuid(NameHash, NameHash, NameHash, NameHash);
        }

        // [DEBUG] 식별자 확인 로그
        UE_LOG(LogTemp, Log, TEXT("[MDF] [BeginPlay] Actor: %s -> Auto-Generated GUID: %s"), 
            *Owner->GetName(), *ComponentGuid.ToString());

        // 2. 데이터 로드 요청 (HP 관련 인자 없음)
        TryLoadDataFromGameState();
    }
    // -------------------------------------------------------------------------

    if (IsValid(Owner))
    {
       if (Owner->HasAuthority() && !Owner->GetIsReplicated())
       {
          Owner->SetReplicates(true);
          Owner->SetReplicateMovement(true); 
       }

       // 델리게이트 등록
       Owner->OnTakePointDamage.RemoveDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
       Owner->OnTakePointDamage.AddDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
    }
    
    // Late Join 시각적 동기화
    if (HitHistory.Num() > 0)
    {
        OnRep_HitHistory(); 
    }
}

void UMDF_DeformableComponent::TryLoadDataFromGameState()
{
    AGameStateBase* GS = UGameplayStatics::GetGameState(this);
    IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);

    // 1. GameState 준비 확인
    if (!MDF_GS)
    {
        if (LoadRetryCount < 10) 
        {
            LoadRetryCount++;
            GetWorld()->GetTimerManager().SetTimer(LoadRetryTimerHandle, this, &UMDF_DeformableComponent::TryLoadDataFromGameState, 0.2f, false);
        }
        return;
    }

    TArray<FMDFHitData> SavedData;
    
    // 2. 데이터 로드 시도 (HP 인자 제거됨)
    // [중요] Interface의 LoadMDFData 함수도 인자를 맞춰주세요.
    if (MDF_GS->LoadMDFData(ComponentGuid, SavedData))
    {
        HitHistory = SavedData;
        
        UE_LOG(LogTemp, Warning, TEXT("[MDF] [Load Success] %s 데이터 복원 완료! (Hit: %d)"), 
            *GetOwner()->GetName(), HitHistory.Num());
            
        OnRep_HitHistory();
    }
    else
    {
        if (LoadRetryCount < 5) 
        {
            LoadRetryCount++;
            GetWorld()->GetTimerManager().SetTimer(LoadRetryTimerHandle, this, &UMDF_DeformableComponent::TryLoadDataFromGameState, 0.5f, false);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[MDF] [Load Final] 저장된 데이터 없음."));
        }
    }
}

void UMDF_DeformableComponent::HandlePointDamage(AActor* DamagedActor, float Damage, AController* InstigatedBy, FVector HitLocation, UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const UDamageType* DamageType, AActor* DamageCauser)
{
    if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority()) return;
    if (!bIsDeformationEnabled || Damage <= 0.0f) return;

    AActor* Attacker = DamageCauser;
    if (InstigatedBy && InstigatedBy->GetPawn())
    {
        Attacker = InstigatedBy->GetPawn();
    }

    if (IsValid(Attacker))
    {
        bool bIsEnemy = Attacker->ActorHasTag(TEXT("Enemy"));
        bool bIsTester = Attacker->ActorHasTag(TEXT("MDF_Test"));
        if (!bIsEnemy && !bIsTester) return; 
    }

    // [Visual Only] 체력 감소 및 사망 로직은 제거됨
    // 오직 변형을 위한 데이터 수집만 수행

    UDynamicMeshComponent* MeshComp = Cast<UDynamicMeshComponent>(FHitComponent);
    if (!IsValid(MeshComp))
    {
        MeshComp = GetOwner()->FindComponentByClass<UDynamicMeshComponent>();
    }

    if (IsValid(MeshComp))
    {
        // 큐에 데이터 저장 (Damage 값은 변형 강도 계산용)
        HitQueue.Add(FMDFHitData(
            ConvertWorldToLocal(HitLocation), 
            ConvertWorldDirectionToLocal(ShotFromDirection), 
            Damage, 
            DamageType ? DamageType->GetClass() : nullptr
        ));

        if (bShowDebugPoints)
        {
            DrawDebugPoint(GetWorld(), HitLocation, 10.0f, FColor::Red, false, 3.0f);
        }

        if (!BatchTimerHandle.IsValid())
        {
            float Delay = FMath::Max(0.001f, BatchProcessDelay);
            GetWorld()->GetTimerManager().SetTimer(BatchTimerHandle, this, &UMDF_DeformableComponent::ProcessDeformationBatch, Delay, false);
        }
    }
}

void UMDF_DeformableComponent::ProcessDeformationBatch()
{
    BatchTimerHandle.Invalidate();

    if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority()) return;
    if (HitQueue.IsEmpty()) return;

    HitHistory.Append(HitQueue);

    UE_LOG(LogTemp, Log, TEXT("[MDF] [Batch Process] %d개의 히트 데이터 처리."), HitQueue.Num());

    // -------------------------------------------------------------------------
    // [Save Request] 변형 확정 시 저장 요청
    // -------------------------------------------------------------------------
    if (ComponentGuid.IsValid())
    {
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        
        if (MDF_GS)
        {
            // [수정] HP 인자 없이 히스토리만 저장
            MDF_GS->SaveMDFData(ComponentGuid, HitHistory);
            
            UE_LOG(LogTemp, Log, TEXT("[MDF] [Save Request] GameState 저장 완료. (Hit: %d)"), HitHistory.Num());
        }
    }

    NetMulticast_PlayEffects(HitQueue);
    OnRep_HitHistory(); 
    HitQueue.Empty();
}

void UMDF_DeformableComponent::OnRep_HitHistory()
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    UDynamicMeshComponent* MeshComp = Owner->FindComponentByClass<UDynamicMeshComponent>();
    if (!IsValid(MeshComp) || !IsValid(MeshComp->GetDynamicMesh())) return;

    int32 CurrentNum = HitHistory.Num();

    // [수리 감지]
    if (CurrentNum < LastAppliedIndex)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MDF] [Sync-Repair] 수리 명령(Reset) 감지! 메쉬를 원상복구합니다."));
        LastAppliedIndex = 0;
        InitializeDynamicMesh(); 
    }

    if (LastAppliedIndex == CurrentNum) return; 

    // [변형 적용]
    const double RadiusSq = FMath::Square((double)DeformRadius);
    const double InverseRadius = 1.0 / (double)DeformRadius;
        
    MeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& EditMesh) 
    {
        for (int32 VertexID : EditMesh.VertexIndicesItr())
        {
            FVector3d VertexPos = EditMesh.GetVertex(VertexID);
            FVector3d TotalOffset(0.0, 0.0, 0.0);
            bool bModified = false;

            for (int32 i = LastAppliedIndex; i < CurrentNum; ++i)
            {
                const FMDFHitData& Hit = HitHistory[i];

                double DistSq = FVector3d::DistSquared(VertexPos, (FVector3d)Hit.LocalLocation);
                
                if (DistSq < RadiusSq)
                {
                    double Distance = FMath::Sqrt(DistSq);
                    double Falloff = 1.0 - (Distance * InverseRadius);
                    
                    // 데미지는 오직 '강도' 계산에만 쓰임
                    float DamageFactor = Hit.Damage * 0.05f; 
                    float CurrentStrength = DeformStrength * DamageFactor;

                    if (Hit.DamageTypeClass && MeleeDamageType && Hit.DamageTypeClass->IsChildOf(MeleeDamageType)) 
                    {
                        CurrentStrength *= 1.5f; 
                    }
                    else if (Hit.DamageTypeClass && RangedDamageType && Hit.DamageTypeClass->IsChildOf(RangedDamageType))
                    {
                        CurrentStrength *= 0.5f; 
                    }

                    TotalOffset += (FVector3d)Hit.LocalDirection * (double)(CurrentStrength * Falloff);
                    bModified = true;
                }
            }
            
            if (bModified)
            {
                EditMesh.SetVertex(VertexID, VertexPos + TotalOffset);
            }
        }
    }, EDynamicMeshChangeType::GeneralEdit);

    LastAppliedIndex = CurrentNum;

    UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(MeshComp->GetDynamicMesh(), FGeometryScriptCalculateNormalsOptions());
    MeshComp->UpdateCollision();
    MeshComp->NotifyMeshUpdated();
}

void UMDF_DeformableComponent::NetMulticast_PlayEffects_Implementation(const TArray<FMDFHitData>& NewHits)
{
    if (IsRunningDedicatedServer()) return;

    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    UDynamicMeshComponent* MeshComp = Owner->FindComponentByClass<UDynamicMeshComponent>();
    if (!IsValid(MeshComp)) return;
    
    const FTransform& ComponentTransform = MeshComp->GetComponentTransform();

    for (const FMDFHitData& Hit : NewHits)
    {
        FVector WorldHitLoc = ComponentTransform.TransformPosition(Hit.LocalLocation);
        FVector WorldHitDir = ComponentTransform.TransformVector(Hit.LocalDirection);

        if (IsValid(DebrisSystem))
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), DebrisSystem, WorldHitLoc, WorldHitDir.Rotation());
        }

        if (IsValid(ImpactSound))
        {
            UGameplayStatics::PlaySoundAtLocation(GetWorld(), ImpactSound, WorldHitLoc, FRotator::ZeroRotator, 1.0f, 1.0f, 0.0f, ImpactAttenuation);
        }
    }
}

void UMDF_DeformableComponent::InitializeDynamicMesh()
{
    if (!IsValid(SourceStaticMesh)) return;

    AActor* Owner = GetOwner();
    UDynamicMeshComponent* MeshComp = IsValid(Owner) ? Owner->FindComponentByClass<UDynamicMeshComponent>() : nullptr;

    if (IsValid(MeshComp) && IsValid(MeshComp->GetDynamicMesh()))
    {
        FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
        AssetOptions.bApplyBuildSettings = true;
        EGeometryScriptOutcomePins Outcome;

        UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(
            SourceStaticMesh, MeshComp->GetDynamicMesh(), AssetOptions, FGeometryScriptMeshReadLOD(), Outcome
        );

        if (Outcome == EGeometryScriptOutcomePins::Success)
        {
            UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(MeshComp->GetDynamicMesh(), FGeometryScriptCalculateNormalsOptions());
            MeshComp->UpdateCollision(); 
            MeshComp->NotifyMeshUpdated();
        }
    }
}

FVector UMDF_DeformableComponent::ConvertWorldToLocal(FVector WorldLocation)
{
    return IsValid(GetOwner()) ? GetOwner()->GetActorTransform().InverseTransformPosition(WorldLocation) : WorldLocation;
}

FVector UMDF_DeformableComponent::ConvertWorldDirectionToLocal(FVector WorldDirection)
{
    return IsValid(GetOwner()) ? GetOwner()->GetActorTransform().InverseTransformVector(WorldDirection) : WorldDirection;
}

void UMDF_DeformableComponent::RepairMesh()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    HitHistory.Empty();
    LastAppliedIndex = 0;

    // [수리 요청] 빈 데이터로 덮어쓰기 (HP 인자 없음)
    if (ComponentGuid.IsValid())
    {
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        if (MDF_GS)
        {
            MDF_GS->SaveMDFData(ComponentGuid, TArray<FMDFHitData>());
            UE_LOG(LogTemp, Warning, TEXT("[MDF] [Repair] GameState 데이터 초기화 요청 완료 (GUID: %s)."), *ComponentGuid.ToString());
        }
    }

    InitializeDynamicMesh();
    UE_LOG(LogTemp, Warning, TEXT("[MDF] [Server] 수리 완료! (모양 복구됨)"));
}