// Gihyeon's Deformation Project (Helluna)

#include "Components/MDF_DeformableComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h" 
#include "Net/UnrealNetwork.h" // [Step 8] 리플리케이션 필수 헤더

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
    SetIsReplicatedByDefault(true); // [Step 7] 컴포넌트 복제 활성화
}

// [Step 8] 동기화 변수 등록
void UMDF_DeformableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // HitHistory는 서버 -> 클라이언트로 복제됨 (Late Join 지원)
    DOREPLIFETIME(UMDF_DeformableComponent, HitHistory);
}

void UMDF_DeformableComponent::BeginPlay()
{
    Super::BeginPlay();
    InitializeDynamicMesh();
    
    AActor* Owner = GetOwner();
    if (IsValid(Owner))
    {
       if (Owner->HasAuthority() && !Owner->GetIsReplicated())
       {
          Owner->SetReplicates(true);
          Owner->SetReplicateMovement(true); 
       }

       // 데미지 델리게이트는 서버에서만 반응하면 되지만, 
       // 안전을 위해 등록해두고 HandlePointDamage 내부에서 Authority 체크를 합니다.
       Owner->OnTakePointDamage.RemoveDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
       Owner->OnTakePointDamage.AddDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
    }
}

void UMDF_DeformableComponent::HandlePointDamage(AActor* DamagedActor, float Damage, AController* InstigatedBy, FVector HitLocation, UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const UDamageType* DamageType, AActor* DamageCauser)
{
    // 1. [Server Only] 데미지 처리는 오직 서버 권한이 있는 경우에만 수행
    if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority()) return;

    // 2. 기본 유효성 검사
    if (!bIsDeformationEnabled || Damage <= 0.0f) return;

    // --- [안전장치 & 디버깅 권한 검사] ---
    AActor* Attacker = DamageCauser;
    if (InstigatedBy && InstigatedBy->GetPawn())
    {
        Attacker = InstigatedBy->GetPawn();
    }

    if (IsValid(Attacker))
    {
        // 1. 적군인가? (Enemy 태그 확인 - AI용)
        bool bIsEnemy = Attacker->ActorHasTag(TEXT("Enemy"));
        
        // 2. 테스트 권한이 있는 플레이어인가? (MDF_Test 태그 확인 - 디버깅용)
        bool bIsTester = Attacker->ActorHasTag(TEXT("MDF_Test"));

        // 둘 다 아니라면 찌그러트리지 않고 무시함
        if (!bIsEnemy && !bIsTester) return; 
    }
    // ----------------------------------------

    // [Log: 데미지 수신 로그]
    FString DmgTypeName = IsValid(DamageType) ? DamageType->GetName() : TEXT("None");
    UE_LOG(LogTemp, Warning, TEXT("[MDF] [Server 수신] 데미지 감지! 데미지: %.1f / 타입: %s"), Damage, *DmgTypeName);

    // 3. 큐에 데이터 적재 (서버 메모리)
    UDynamicMeshComponent* MeshComp = Cast<UDynamicMeshComponent>(FHitComponent);
    if (!IsValid(MeshComp))
    {
        MeshComp = GetOwner()->FindComponentByClass<UDynamicMeshComponent>();
    }

    if (IsValid(MeshComp))
    {
        HitQueue.Add(FMDFHitData(ConvertWorldToLocal(HitLocation), ConvertWorldDirectionToLocal(ShotFromDirection), Damage, DamageType ? DamageType->GetClass() : nullptr));

        // 디버그 포인트 표시 (서버)
        if (bShowDebugPoints)
        {
            DrawDebugPoint(GetWorld(), HitLocation, 10.0f, FColor::Red, false, 3.0f);
        }

        // 타이머가 없으면 작동 (배칭 시작)
        if (!BatchTimerHandle.IsValid())
        {
            float Delay = FMath::Max(0.001f, BatchProcessDelay);
            GetWorld()->GetTimerManager().SetTimer(BatchTimerHandle, this, &UMDF_DeformableComponent::ProcessDeformationBatch, Delay, false);
        }
    }
}

/** * [Step 6 최적화 -> Step 8 업데이트] 
 * 서버에서만 호출됨. 큐에 모인 데이터를 히스토리로 옮기고 이펙트 명령을 내림.
 */
void UMDF_DeformableComponent::ProcessDeformationBatch()
{
    // 타이머 핸들 초기화
    BatchTimerHandle.Invalidate();

    // 서버인지 다시 한 번 확인 (안전장치)
    if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority()) return;

    if (HitQueue.IsEmpty()) return;

    // [Step 8 변경점]
    // 1. [상태 저장 - Track B] 서버 히스토리에 누적 -> 클라 자동 복제
    HitHistory.Append(HitQueue);

    // 2. [이펙트 방송 - Track A] "지금 당장 소리와 파티클을 재생해라" (모양변형 X)
    NetMulticast_PlayEffects(HitQueue);

    // 3. [서버 변형 적용] 
    // 서버는 OnRep이 자동 호출되지 않으므로, 수동으로 호출하여 물리/충돌 상태를 업데이트합니다.
    OnRep_HitHistory(); 

    // 전송 완료했으므로 서버의 대기열 비움
    HitQueue.Empty();
}

/** * [Step 8 핵심: 상태 동기화] 
 * - 히스토리 배열이 변할 때마다 호출됨 (서버는 수동호출, 클라는 자동호출)
 * - "모양"만 바꿈 (소리 X)
 * - 늦게 들어온 유저는 LastAppliedIndex가 0이므로 처음부터 끝까지 루프를 돌아 모양을 완성함.
 */
void UMDF_DeformableComponent::OnRep_HitHistory()
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    UDynamicMeshComponent* MeshComp = Owner->FindComponentByClass<UDynamicMeshComponent>();
    if (!IsValid(MeshComp) || !IsValid(MeshComp->GetDynamicMesh())) return;

    // [최적화] 새로 추가된 데이터만 순회 (LastAppliedIndex 활용)
    int32 CurrentNum = HitHistory.Num();
    if (LastAppliedIndex >= CurrentNum) return; 

    // 디버그 로그
    UE_LOG(LogTemp, Log, TEXT("[MDF] [Sync] 변형 데이터 적용 시작. (인덱스: %d ~ %d)"), LastAppliedIndex, CurrentNum - 1);

    const double RadiusSq = FMath::Square((double)DeformRadius);
    const double InverseRadius = 1.0 / (double)DeformRadius;
        
    // --- [변형 알고리즘] ---
    MeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& EditMesh) 
    {
        for (int32 VertexID : EditMesh.VertexIndicesItr())
        {
            FVector3d VertexPos = EditMesh.GetVertex(VertexID);
            FVector3d TotalOffset(0.0, 0.0, 0.0);
            bool bModified = false;

            // [최적화 루프] 아직 적용 안 한 것부터 끝까지
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

    // 인덱스 갱신 (처리 완료)
    LastAppliedIndex = CurrentNum;

    // --- [물리 및 렌더링 갱신] ---
    UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(MeshComp->GetDynamicMesh(), FGeometryScriptCalculateNormalsOptions());
    MeshComp->UpdateCollision();
    MeshComp->NotifyMeshUpdated();
}

/** * [Step 8] 이펙트 전용 (소리/파티클)
 * - 모양 변형 로직은 없음 (위 OnRep에서 처리됨).
 */
void UMDF_DeformableComponent::NetMulticast_PlayEffects_Implementation(const TArray<FMDFHitData>& NewHits)
{
    // Dedicated Server는 이펙트 재생 불필요
    if (IsRunningDedicatedServer()) return;

    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    UDynamicMeshComponent* MeshComp = Owner->FindComponentByClass<UDynamicMeshComponent>();
    if (!IsValid(MeshComp)) return;
    
    const FTransform& ComponentTransform = MeshComp->GetComponentTransform();

    // --- [시각(Niagara) 및 청각(Sound) 효과 재생] ---
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
            MeshComp->UpdateCollision(); // 초기화 시 충돌 업데이트
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