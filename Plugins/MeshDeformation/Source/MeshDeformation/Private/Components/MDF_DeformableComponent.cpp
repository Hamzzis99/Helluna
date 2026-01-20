// Gihyeon's Deformation Project (Helluna)
// File: Source/MeshDeformation/Components/MDF_DeformableComponent.cpp

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

// [★필수 추가] 탄젠트 옵션 구조체 정의 헤더
// (이게 없으면 FGeometryScriptTangentsOptions 에러가 발생할 수 있습니다)
#include "GeometryScript/GeometryScriptTypes.h" 

// 나이아가라 관련 헤더
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UMDF_DeformableComponent::UMDF_DeformableComponent()
{
    // 최적화를 위해 Tick은 끕니다. (이벤트 기반 작동)
    PrimaryComponentTick.bCanEverTick = false; 
    
    // 컴포넌트 자체 리플리케이션 활성화
    SetIsReplicatedByDefault(true); 
}

void UMDF_DeformableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    // [Step 8] 히스토리 배열 동기화 등록
    // 서버의 HitHistory가 변경되면 클라이언트에게 자동으로 전송됩니다.
    DOREPLIFETIME(UMDF_DeformableComponent, HitHistory);
}

void UMDF_DeformableComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // 1. 다이나믹 메쉬 초기화 (스태틱 메쉬 복사)
    InitializeDynamicMesh();
    
    LastAppliedIndex = 0;
    AActor* Owner = GetOwner();

    // -------------------------------------------------------------------------
    // [Step 9: 인터페이스를 통한 데이터 복구 (Load)]
    // 서버가 시작될 때, GameState에 저장해둔 찌그러짐 데이터가 있다면 불러옵니다.
    // (월드 파티션이나 레벨 스트리밍 상황 대비)
    // -------------------------------------------------------------------------
    if (IsValid(Owner) && Owner->HasAuthority())
    {
        // GUID가 없으면 이름 기반으로 생성 (고유 식별자)
        if (!ComponentGuid.IsValid())
        {
            FString UniqueName = Owner->GetName();
            FGuid::Parse(UniqueName, ComponentGuid);
            if (!ComponentGuid.IsValid()) ComponentGuid = FGuid::NewGuid();
        }

        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        
        if (MDF_GS)
        {
            TArray<FMDFHitData> SavedData;
            if (MDF_GS->LoadMDFData(ComponentGuid, SavedData))
            {
                HitHistory = SavedData;
                UE_LOG(LogTemp, Log, TEXT("[MDF] GameState에서 데이터 복원 성공 (%d hit)"), HitHistory.Num());
            }
        }
    }

    // 2. 이벤트 바인딩 및 네트워크 설정 보정
    if (IsValid(Owner))
    {
       // 액터가 리플리케이션이 꺼져있다면 강제로 켭니다.
       if (Owner->HasAuthority() && !Owner->GetIsReplicated())
       {
          Owner->SetReplicates(true);
          Owner->SetReplicateMovement(true); 
       }

       // 데미지 이벤트 연결
       Owner->OnTakePointDamage.RemoveDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
       Owner->OnTakePointDamage.AddDynamic(this, &UMDF_DeformableComponent::HandlePointDamage);
    }
    
    // 3. 불러온 데이터가 있다면 즉시 적용 (모양 복구)
    if (HitHistory.Num() > 0)
    {
        OnRep_HitHistory(); 
    }
}

// -----------------------------------------------------------------------------
// [Step 5] 데미지 처리 및 Gatekeeper 로직
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::HandlePointDamage(AActor* DamagedActor, float Damage, AController* InstigatedBy, FVector HitLocation, UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const UDamageType* DamageType, AActor* DamageCauser)
{
    // 1. 서버 권한 체크 (서버만 로직 수행)
    if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority()) return;
    
    // 2. 기능 활성화 여부 및 유효 데미지 체크
    if (!bIsDeformationEnabled || Damage <= 0.0f) return;

    // 3. 공격자 식별
    AActor* Attacker = DamageCauser;
    if (InstigatedBy && InstigatedBy->GetPawn())
    {
        Attacker = InstigatedBy->GetPawn();
    }

    // -------------------------------------------------------------------------
    // [보안 Check] 태그 검사 로직 (Gatekeeper)
    // -------------------------------------------------------------------------
    if (IsValid(Attacker))
    {
        // (1) 자해 방지: 내가 쏜 총에 내가 찌그러지면 안 됨
        if (Attacker == GetOwner()) return;

        // (2) 권한 검사: 특정 태그(Enemy, MDF_Test)가 있는 대상만 찌그러뜨릴 수 있음
        bool bIsEnemy = Attacker->ActorHasTag(TEXT("Enemy"));
        bool bIsTester = Attacker->ActorHasTag(TEXT("MDF_Test"));

        // 자격이 없으면 무시 (변형 거부)
        if (!bIsEnemy && !bIsTester) return; 
    }

    // 4. 컴포넌트 찾기
    UDynamicMeshComponent* MeshComp = Cast<UDynamicMeshComponent>(FHitComponent);
    if (!IsValid(MeshComp))
    {
        MeshComp = GetOwner()->FindComponentByClass<UDynamicMeshComponent>();
    }

    if (IsValid(MeshComp))
    {
        // 5. 좌표 변환 (월드 좌표 -> 메쉬 로컬 좌표)
        FVector LocalPos = ConvertWorldToLocal(HitLocation);
        
        // 6. 대기열(Queue)에 추가
        // 즉시 처리하지 않고 큐에 넣었다가 타이머로 한 번에 처리합니다 (최적화)
        HitQueue.Add(FMDFHitData(LocalPos, ConvertWorldDirectionToLocal(ShotFromDirection), Damage, DamageType ? DamageType->GetClass() : nullptr));

        // [디버그] 타격 위치 표시
        if (bShowDebugPoints)
        {
            DrawDebugPoint(GetWorld(), HitLocation, 10.0f, FColor::Red, false, 3.0f);
        }

        // 7. 배칭 타이머 시작 (아직 안 돌고 있다면)
        if (!BatchTimerHandle.IsValid())
        {
            float Delay = FMath::Max(0.001f, BatchProcessDelay);
            GetWorld()->GetTimerManager().SetTimer(BatchTimerHandle, this, &UMDF_DeformableComponent::ProcessDeformationBatch, Delay, false);
        }
    }
}

// -----------------------------------------------------------------------------
// [Step 6] 배칭 처리 (최적화)
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::ProcessDeformationBatch()
{
    BatchTimerHandle.Invalidate();

    if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority()) return;
    if (HitQueue.IsEmpty()) return;

    // 1. 큐에 있던 데이터를 실제 히스토리(Replicated 변수)에 병합
    HitHistory.Append(HitQueue);

    // 2. GameState에 데이터 백업 (영속성 보장)
    if (ComponentGuid.IsValid())
    {
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        if (MDF_GS)
        {
            MDF_GS->SaveMDFData(ComponentGuid, HitHistory);
        }
    }

    // 3. 이펙트(소리, 파티클)는 NetMulticast로 모든 클라이언트에 전송
    NetMulticast_PlayEffects(HitQueue);
    
    // 4. 서버도 자기 자신의 모양을 바꿔야 하므로 호출
    OnRep_HitHistory(); 

    // 5. 큐 비우기
    HitQueue.Empty();
}

// -----------------------------------------------------------------------------
// [자식 클래스용] 배칭 타이머 시작 헬퍼
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::StartBatchTimer()
{
    if (!BatchTimerHandle.IsValid())
    {
        float Delay = FMath::Max(0.001f, BatchProcessDelay);
        GetWorld()->GetTimerManager().SetTimer(
            BatchTimerHandle, 
            this, 
            &UMDF_DeformableComponent::ProcessDeformationBatch, 
            Delay, 
            false
        );
    }
}

// -----------------------------------------------------------------------------
// [Step 8] 클라이언트 동기화 및 변형 적용 (핵심 로직)
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::OnRep_HitHistory()
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    UDynamicMeshComponent* MeshComp = Owner->FindComponentByClass<UDynamicMeshComponent>();
    if (!IsValid(MeshComp) || !IsValid(MeshComp->GetDynamicMesh())) return;

    int32 CurrentNum = HitHistory.Num();

    // 1. 수리 명령 감지 (히스토리가 줄어들었으면 리셋된 것임)
    if (CurrentNum < LastAppliedIndex)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MDF] [Sync] 수리 명령(Reset) 감지!"));
        LastAppliedIndex = 0;
        InitializeDynamicMesh(); // 메쉬 원상복구
        return;
    }

    // 2. 변경사항 없으면 스킵
    if (LastAppliedIndex == CurrentNum) return; 

    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] ========== 변형 시작 =========="));
    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] LastAppliedIndex: %d, CurrentNum: %d"), LastAppliedIndex, CurrentNum);
    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] DeformRadius: %.1f, DeformStrength: %.1f"), DeformRadius, DeformStrength);

    // 3. 변형 계산 준비
    const double RadiusSq = FMath::Square((double)DeformRadius);
    const double InverseRadius = 1.0 / (double)DeformRadius;
    
    double MinDebugDistSq = DBL_MAX;
    bool bAnyModified = false;
    int32 ModifiedVertexCount = 0;

    // [디버그] 적용할 지점 표시
    for (int32 i = LastAppliedIndex; i < CurrentNum; ++i)
    {
        FVector WorldPos = MeshComp->GetComponentTransform().TransformPosition(HitHistory[i].LocalLocation);
        UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] Hit[%d] LocalPos: %s, Damage: %.1f"), 
            i, *HitHistory[i].LocalLocation.ToString(), HitHistory[i].Damage);
        
        if (bShowDebugPoints)
        {
            DrawDebugPoint(GetWorld(), WorldPos, 15.0f, FColor::Blue, false, 5.0f);
        }
    }

    // 4. 메쉬 편집 시작 (Vertex 순회)
    int32 TotalVertexCount = 0;
    MeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& EditMesh) 
    {
        for (int32 VertexID : EditMesh.VertexIndicesItr())
        {
            TotalVertexCount++;
            FVector3d VertexPos = EditMesh.GetVertex(VertexID);
            FVector3d TotalOffset(0.0, 0.0, 0.0);
            bool bModified = false;

            // 새로 추가된 히트 데이터들만 순회하며 오프셋 누적
            for (int32 i = LastAppliedIndex; i < CurrentNum; ++i)
            {
                const FMDFHitData& Hit = HitHistory[i];

                double DistSq = FVector3d::DistSquared(VertexPos, (FVector3d)Hit.LocalLocation);
                
                if (DistSq < MinDebugDistSq) MinDebugDistSq = DistSq;

                // 반경 내에 있는 버텍스라면?
                if (DistSq < RadiusSq)
                {
                    double Distance = FMath::Sqrt(DistSq);
                    double Falloff = 1.0 - (Distance * InverseRadius); // 중심일수록 1.0, 멀어지면 0.0
                    
                    // [수정] 데미지에 따른 강도 조절 - 계수를 0.05 → 0.15로 상향
                    float DamageFactor = Hit.Damage * 0.15f; 
                    float CurrentStrength = DeformStrength * DamageFactor;

                    // 데미지 타입별 가중치 (근접은 더 세게, 원거리는 약하게)
                    if (Hit.DamageTypeClass && MeleeDamageType && Hit.DamageTypeClass->IsChildOf(MeleeDamageType)) 
                        CurrentStrength *= 1.5f; 
                    else if (Hit.DamageTypeClass && RangedDamageType && Hit.DamageTypeClass->IsChildOf(RangedDamageType))
                        CurrentStrength *= 0.5f; 

                    // 총알 방향으로 밀어넣기
                    TotalOffset += (FVector3d)Hit.LocalDirection * (double)(CurrentStrength * Falloff);
                    bModified = true;
                }
            }
            
            // 실제 버텍스 위치 이동
            if (bModified)
            {
                EditMesh.SetVertex(VertexID, VertexPos + TotalOffset);
                bAnyModified = true;
                ModifiedVertexCount++;
            }
        }
    }, EDynamicMeshChangeType::GeneralEdit);

    double MinDist = FMath::Sqrt(MinDebugDistSq);
    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] 총 버텍스: %d, 수정된 버텍스: %d"), TotalVertexCount, ModifiedVertexCount);
    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] 최소 거리: %.2f, 반경: %.1f"), (float)MinDist, DeformRadius);
    
    if (!bAnyModified)
    {
        UE_LOG(LogTemp, Error, TEXT("[MDF Deform] >>> 변형 실패! 반경 내 버텍스 없음!"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] >>> 변형 성공! %d개 버텍스 이동"), ModifiedVertexCount);
    }

    // 인덱스 업데이트 (다음엔 여기부터 처리)
    LastAppliedIndex = CurrentNum;

    // -------------------------------------------------------------------------
    // [★핵심 렌더링 업데이트]
    // -------------------------------------------------------------------------
    
    // 1. 법선(Normal) 재계산: 표면이 바라보는 방향 갱신
    UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(MeshComp->GetDynamicMesh(), FGeometryScriptCalculateNormalsOptions());

    // 2. [★필수 추가] 탄젠트(Tangent) 재계산
    // 움직이는 물체(Movable Actor)가 빛을 받을 때 투명해지거나 검게 나오는 것을 방지합니다.
    UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(MeshComp->GetDynamicMesh(), FGeometryScriptTangentsOptions());

    // 3. 충돌 및 렌더링 알림
    MeshComp->UpdateCollision();
    MeshComp->NotifyMeshUpdated();
    
    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] ========== 변형 완료 =========="));
}

// -----------------------------------------------------------------------------
// [Step 7] 이펙트 재생 (Multicast)
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::NetMulticast_PlayEffects_Implementation(const TArray<FMDFHitData>& NewHits)
{
    if (IsRunningDedicatedServer()) return; // 데디 서버는 이펙트 재생 안 함

    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    UDynamicMeshComponent* MeshComp = Owner->FindComponentByClass<UDynamicMeshComponent>();
    if (!IsValid(MeshComp)) return;
    
    const FTransform& ComponentTransform = MeshComp->GetComponentTransform();

    for (const FMDFHitData& Hit : NewHits)
    {
        FVector WorldHitLoc = ComponentTransform.TransformPosition(Hit.LocalLocation);
        FVector WorldHitDir = ComponentTransform.TransformVector(Hit.LocalDirection);

        // 나이아가라 파티클 (파편)
        if (IsValid(DebrisSystem))
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), DebrisSystem, WorldHitLoc, WorldHitDir.Rotation());
        }

        // 타격 사운드
        if (IsValid(ImpactSound))
        {
            UGameplayStatics::PlaySoundAtLocation(GetWorld(), ImpactSound, WorldHitLoc, FRotator::ZeroRotator, 1.0f, 1.0f, 0.0f, ImpactAttenuation);
        }
    }
}

// -----------------------------------------------------------------------------
// [초기화] 스태틱 메쉬 -> 다이나믹 메쉬 복사
// -----------------------------------------------------------------------------
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

        // 복사 실행
        UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(
            SourceStaticMesh, MeshComp->GetDynamicMesh(), AssetOptions, FGeometryScriptMeshReadLOD(), Outcome
        );

        if (Outcome == EGeometryScriptOutcomePins::Success)
        {
            // 1. 법선 재계산
            UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(MeshComp->GetDynamicMesh(), FGeometryScriptCalculateNormalsOptions());
            
            // -------------------------------------------------------------------------
            // [★핵심 추가] 초기화 시 탄젠트 재계산
            // 처음 생성될 때부터 메쉬가 투명하게 보이지 않도록 합니다.
            // -------------------------------------------------------------------------
            UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(MeshComp->GetDynamicMesh(), FGeometryScriptTangentsOptions());
            
            MeshComp->UpdateCollision(); 
            MeshComp->NotifyMeshUpdated();
        }
    }
}

// -----------------------------------------------------------------------------
// [유틸리티] 좌표 변환 함수
// -----------------------------------------------------------------------------
FVector UMDF_DeformableComponent::ConvertWorldToLocal(FVector WorldLocation)
{
    UDynamicMeshComponent* MeshComp = nullptr;
    if (GetOwner()) MeshComp = GetOwner()->FindComponentByClass<UDynamicMeshComponent>();

    if (IsValid(MeshComp)) return MeshComp->GetComponentTransform().InverseTransformPosition(WorldLocation);
    return IsValid(GetOwner()) ? GetOwner()->GetActorTransform().InverseTransformPosition(WorldLocation) : WorldLocation;
}

FVector UMDF_DeformableComponent::ConvertWorldDirectionToLocal(FVector WorldDirection)
{
    UDynamicMeshComponent* MeshComp = nullptr;
    if (GetOwner()) MeshComp = GetOwner()->FindComponentByClass<UDynamicMeshComponent>();

    if (IsValid(MeshComp)) return MeshComp->GetComponentTransform().InverseTransformVector(WorldDirection);
    return IsValid(GetOwner()) ? GetOwner()->GetActorTransform().InverseTransformVector(WorldDirection) : WorldDirection;
}

// -----------------------------------------------------------------------------
// [Step 10] 수리(Repair) 함수
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::RepairMesh()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    // 히스토리 초기화
    HitHistory.Empty();
    LastAppliedIndex = 0;

    // 저장된 데이터도 비움
    if (ComponentGuid.IsValid())
    {
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        if (MDF_GS) MDF_GS->SaveMDFData(ComponentGuid, TArray<FMDFHitData>());
    }

    // 메쉬 리셋
    InitializeDynamicMesh();
    UE_LOG(LogTemp, Warning, TEXT("[MDF] [Server] 수리 완료! (히스토리 초기화됨)"));
}