// Gihyeon's Deformation Project (Helluna)

#include "Components/MDF_DeformableComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h" 
#include "Net/UnrealNetwork.h" 
#include "Interface/MDF_GameStateInterface.h"
#include "GameFramework/GameStateBase.h"

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
    DOREPLIFETIME(UMDF_DeformableComponent, HitHistory);
}

void UMDF_DeformableComponent::BeginPlay()
{
    Super::BeginPlay();
    InitializeDynamicMesh();
    
    // 리셋 안전 장치
    LastAppliedIndex = 0;
    
    AActor* Owner = GetOwner();

    // -------------------------------------------------------------------------
    // [Step 9: 인터페이스를 통한 데이터 복구 (Load)]
    // 월드 파티션으로 인해 재로딩되었을 때 GameState에서 데이터를 받아옵니다.
    // -------------------------------------------------------------------------
    if (IsValid(Owner) && Owner->HasAuthority())
    {
        // 1. GUID가 없으면 이름 기반으로 생성 (안전장치)
        if (!ComponentGuid.IsValid())
        {
            FString UniqueName = Owner->GetName();
            FGuid::Parse(UniqueName, ComponentGuid);
            if (!ComponentGuid.IsValid()) ComponentGuid = FGuid::NewGuid();
        }

        // 2. 현재 월드의 GameState 가져오기
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);

        // 3. 인터페이스(약속)를 지키는 GameState인지 확인
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        
        // 4. 맞다면 데이터 로드 요청
        if (MDF_GS)
        {
            TArray<FMDFHitData> SavedData;
            // "제 ID로 된 데이터 좀 주세요"
            if (MDF_GS->LoadMDFData(ComponentGuid, SavedData))
            {
                HitHistory = SavedData;
                UE_LOG(LogTemp, Log, TEXT("[MDF] GameState에서 데이터 복원 성공 (%d hit)"), HitHistory.Num());
            }
        }
    }
    // -------------------------------------------------------------------------

    if (IsValid(Owner))
    {
       if (Owner->HasAuthority() && !Owner->GetIsReplicated())
       {
          Owner->SetReplicates(true);
          Owner->SetReplicateMovement(true); 
       }

       Owner->OnTakePointDamage.RemoveDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
       Owner->OnTakePointDamage.AddDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
    }
    
    // Late Join 및 데이터 복구 후 강제 동기화
    if (HitHistory.Num() > 0)
    {
        OnRep_HitHistory(); 
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

    // [Log: 데미지 수신 로그]
    FString DmgTypeName = IsValid(DamageType) ? DamageType->GetName() : TEXT("None");
    UE_LOG(LogTemp, Warning, TEXT("[MDF] [Server 수신] 데미지 감지! 데미지: %.1f / 타입: %s / 공격자: %s"), Damage, *DmgTypeName, *GetNameSafe(Attacker));

    UDynamicMeshComponent* MeshComp = Cast<UDynamicMeshComponent>(FHitComponent);
    if (!IsValid(MeshComp))
    {
        MeshComp = GetOwner()->FindComponentByClass<UDynamicMeshComponent>();
    }

    if (IsValid(MeshComp))
    {
        HitQueue.Add(FMDFHitData(ConvertWorldToLocal(HitLocation), ConvertWorldDirectionToLocal(ShotFromDirection), Damage, DamageType ? DamageType->GetClass() : nullptr));

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

    // 1. 히스토리에 누적
    HitHistory.Append(HitQueue);

    UE_LOG(LogTemp, Log, TEXT("[MDF] [Server 전송] %d개의 변형 발생. RPC 발송."), HitQueue.Num());

    // -------------------------------------------------------------------------
    // [Step 9: 인터페이스를 통한 데이터 저장 (Save)]
    // 변형이 확정되었으므로 GameState에 백업합니다.
    // -------------------------------------------------------------------------
    if (ComponentGuid.IsValid())
    {
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        
        if (MDF_GS)
        {
            // "제 최신 상태 좀 저장해주세요"
            MDF_GS->SaveMDFData(ComponentGuid, HitHistory);
        }
    }
    // -------------------------------------------------------------------------

    // 2. 이펙트 방송
    NetMulticast_PlayEffects(HitQueue);

    // 3. 서버 변형 적용
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

    // [Step 10] 수리 감지 로직
    if (CurrentNum < LastAppliedIndex)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MDF] [Sync] 수리 명령(Reset) 감지! 메쉬를 원상복구합니다."));
        LastAppliedIndex = 0;
        InitializeDynamicMesh(); 
        return;
    }

    if (LastAppliedIndex == CurrentNum) return; 

    // [Log: 클라이언트/서버 동기화 로그]
    FString NetRole = (Owner->GetLocalRole() == ROLE_Authority) ? TEXT("Server") : TEXT("Client");
    UE_LOG(LogTemp, Log, TEXT("[MDF] [%s Sync] 변형 데이터 동기화 시작 (인덱스: %d ~ %d)"), *NetRole, LastAppliedIndex, CurrentNum - 1);

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
    // 1. 서버만 실행 가능
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    // 2. 데이터 초기화
    HitHistory.Empty();
    LastAppliedIndex = 0;

    // 3. GameState에도 초기화(삭제) 요청 [Step 10 추가]
    if (ComponentGuid.IsValid())
    {
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        if (MDF_GS)
        {
            // 빈 배열을 저장해서 초기화
            MDF_GS->SaveMDFData(ComponentGuid, TArray<FMDFHitData>());
        }
    }

    // 4. 서버 쪽 모양 즉시 복구
    InitializeDynamicMesh();

    UE_LOG(LogTemp, Warning, TEXT("[MDF] [Server] 수리 완료! (히스토리 초기화됨)"));
}