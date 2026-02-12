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

// FFastArraySerializer 관련
#include "Net/Serialization/FastArraySerializer.h"

// ---------------------------------------------------------------------------
// FFastArraySerializer 콜백 구현
// ---------------------------------------------------------------------------
void FMDFHitData::PostReplicatedAdd(const FMDFHitDataArray& InArraySerializer)
{
    // 클라이언트에서 새 항목이 도착했을 때 (아이템마다 1회 호출)
    UMDF_DeformableComponent* Comp = InArraySerializer.OwnerComponent;
    if (!Comp) return;

    // 포인터 연산으로 정확한 인덱스 확보 (IndexOfByKey/operator== 불필요)
    int32 MyIndex = static_cast<int32>(this - InArraySerializer.Items.GetData());

    // 배치 추적: 첫 아이템이면 시작 인덱스 기록
    if (Comp->PendingAddStartIndex == INDEX_NONE)
    {
        Comp->PendingAddStartIndex = MyIndex;
    }
    Comp->PendingAddCount++;

    // 다음 틱에서 한 번만 처리 (bool 플래그로 중복 등록 방지)
    if (!Comp->bFastArrayBatchPending)
    {
        Comp->bFastArrayBatchPending = true;
        Comp->GetWorld()->GetTimerManager().SetTimerForNextTick([Comp]()
        {
            if (!IsValid(Comp)) return;
            Comp->bFastArrayBatchPending = false;  // 반드시 먼저 리셋

            if (Comp->bPendingDeformationReset)
            {
                // 삭제 + 추가가 같은 배치에서 일어남 (히스토리 캡) → 전체 재적용
                Comp->bPendingDeformationReset = false;
                Comp->PendingAddStartIndex = INDEX_NONE;
                Comp->PendingAddCount = 0;
                Comp->ResetDeformation();
            }
            else if (Comp->PendingAddCount > 0)
            {
                // 일반 추가 → 새로 추가된 범위만 적용
                Comp->ApplyDeformationForHits(Comp->PendingAddStartIndex, Comp->PendingAddCount);
                Comp->PendingAddStartIndex = INDEX_NONE;
                Comp->PendingAddCount = 0;
            }
        });
    }
}

void FMDFHitData::PostReplicatedChange(const FMDFHitDataArray& InArraySerializer)
{
    // HitData는 추가 후 변경되지 않으므로 비워둠
}

void FMDFHitData::PreReplicatedRemove(const FMDFHitDataArray& InArraySerializer)
{
    // 히스토리 캡 또는 RepairMesh로 항목 제거 시 (아이템마다 1회 호출)
    UMDF_DeformableComponent* Comp = InArraySerializer.OwnerComponent;
    if (!Comp) return;

    Comp->bPendingDeformationReset = true;

    // RepairMesh() 시에는 삭제만 있고 추가가 없으므로 PostReplicatedAdd가 안 불림.
    // bool 플래그로 중복 등록 방지
    if (!Comp->bFastArrayBatchPending)
    {
        Comp->bFastArrayBatchPending = true;
        Comp->GetWorld()->GetTimerManager().SetTimerForNextTick([Comp]()
        {
            if (!IsValid(Comp)) return;
            Comp->bFastArrayBatchPending = false;  // 반드시 먼저 리셋

            if (Comp->bPendingDeformationReset)
            {
                Comp->bPendingDeformationReset = false;
                Comp->PendingAddStartIndex = INDEX_NONE;
                Comp->PendingAddCount = 0;
                Comp->ResetDeformation();
            }
        });
    }
}

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

    // [Step 8 → FFastArraySerializer] 히스토리 배열 동기화 등록
    DOREPLIFETIME(UMDF_DeformableComponent, HitHistoryArray);
}

void UMDF_DeformableComponent::BeginPlay()
{
    Super::BeginPlay();

    // 1. 다이나믹 메쉬 초기화 (스태틱 메쉬 복사)
    InitializeDynamicMesh();

    // FFastArray에 소유 컴포넌트 연결
    HitHistoryArray.OwnerComponent = this;

    AActor* Owner = GetOwner();

    // -------------------------------------------------------------------------
    // [Step 9: 인터페이스를 통한 데이터 복구 (Load)]
    // 서버가 시작될 때, GameState에 저장해둔 찌그러짐 데이터가 있다면 불러옵니다.
    // (월드 파티션이나 레벨 스트리밍 상황 대비)
    // -------------------------------------------------------------------------
    if (IsValid(Owner) && Owner->HasAuthority())
    {
        // GUID가 없으면 액터 경로 해시로 결정적 생성 (서버 재시작해도 동일)
        if (!ComponentGuid.IsValid())
        {
            // GetPathName()은 "/Game/Maps/Level1.Level1:PersistentLevel.MDF_Actor_003" 같은
            // 고유하고 안정적인 경로를 반환합니다. 레벨 내 위치가 바뀌지 않는 한 항상 동일합니다.
            FString StablePath = Owner->GetPathName();
            uint32 HashA = FCrc::StrCrc32(*StablePath);
            uint32 HashB = GetTypeHash(StablePath);

            ComponentGuid = FGuid(HashA, HashB, HashA ^ 0x9E3779B9, HashB ^ 0x517CC1B7);

            UE_LOG(LogTemp, Log, TEXT("[MDF] GUID 생성 (경로 해시): %s → %s"),
                *StablePath, *ComponentGuid.ToString());
        }

        // GameState에서 복원
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);

        if (MDF_GS)
        {
            TArray<FMDFHitData> SavedData;
            if (MDF_GS->LoadMDFData(ComponentGuid, SavedData))
            {
                // 복원된 데이터를 FFastArray에 넣기
                for (const FMDFHitData& Hit : SavedData)
                {
                    HitHistoryArray.Items.Add(Hit);
                }
                HitHistoryArray.MarkArrayDirty();
                UE_LOG(LogTemp, Log, TEXT("[MDF] GameState에서 데이터 복원 성공 (%d hit)"), HitHistoryArray.Items.Num());
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
    if (HitHistoryArray.Items.Num() > 0)
    {
        ApplyDeformationForHits(0, HitHistoryArray.Items.Num());
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

    // 새 히트 시작 인덱스 기록
    int32 StartIndex = HitHistoryArray.Items.Num();

    // 큐 → FFastArray에 추가 (MarkItemDirty로 델타 복제 트리거)
    for (const FMDFHitData& Hit : HitQueue)
    {
        HitHistoryArray.Items.Add(Hit);
        HitHistoryArray.MarkItemDirty(HitHistoryArray.Items.Last());
    }

    // [최적화] 히스토리 상한선 초과 시 오래된 데이터 제거
    if (MaxHitHistorySize > 0 && HitHistoryArray.Items.Num() > MaxHitHistorySize)
    {
        int32 RemoveCount = HitHistoryArray.Items.Num() - MaxHitHistorySize;
        HitHistoryArray.Items.RemoveAt(0, RemoveCount);
        HitHistoryArray.MarkArrayDirty(); // 대량 삭제 후 전체 재동기화

        // StartIndex 보정
        StartIndex = FMath::Max(0, StartIndex - RemoveCount);

        UE_LOG(LogTemp, Log, TEXT("[MDF] 히스토리 캡 적용: %d개 제거, 현재 %d/%d"),
            RemoveCount, HitHistoryArray.Items.Num(), MaxHitHistorySize);
    }

    // GameState에 데이터 백업 (영속성 보장)
    if (ComponentGuid.IsValid())
    {
        AGameStateBase* GS = UGameplayStatics::GetGameState(this);
        IMDF_GameStateInterface* MDF_GS = Cast<IMDF_GameStateInterface>(GS);
        if (MDF_GS)
        {
            MDF_GS->SaveMDFData(ComponentGuid, HitHistoryArray.Items);
        }
    }

    // 이펙트(소리, 파티클)는 NetMulticast로 모든 클라이언트에 전송
    NetMulticast_PlayEffects(HitQueue);

    // 서버 자신도 변형 적용
    int32 Count = HitHistoryArray.Items.Num() - StartIndex;
    if (Count > 0)
    {
        ApplyDeformationForHits(StartIndex, Count);
    }

    // 큐 비우기
    HitQueue.Empty();

    // [디버그] 개발자 모드 자동 복구
    if (bDevMode_AutoRepair)
    {
        // 기존 타이머가 있으면 리셋 (마지막 타격 기준으로 딜레이 재시작)
        GetWorld()->GetTimerManager().ClearTimer(DevMode_RepairTimerHandle);

        float Delay = FMath::Max(0.5f, DevMode_RepairDelay);
        GetWorld()->GetTimerManager().SetTimer(
            DevMode_RepairTimerHandle,
            this,
            &UMDF_DeformableComponent::RepairMesh,
            Delay,
            false // 반복 아님
        );

        UE_LOG(LogTemp, Log, TEXT("[MDF] [DevMode] %.1f초 후 자동 복구 예약됨"), Delay);
    }
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
// [Step 8 → FFastArray] 변형 적용 (기존 OnRep_HitHistory 대체)
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::ApplyDeformationForHits(int32 StartIndex, int32 Count)
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    UDynamicMeshComponent* MeshComp = Owner->FindComponentByClass<UDynamicMeshComponent>();
    if (!IsValid(MeshComp) || !IsValid(MeshComp->GetDynamicMesh())) return;

    int32 EndIndex = FMath::Min(StartIndex + Count, HitHistoryArray.Items.Num());
    if (StartIndex >= EndIndex) return;

    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] ========== 변형 시작 =========="));
    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] StartIndex: %d, EndIndex: %d"), StartIndex, EndIndex);
    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] DeformRadius: %.1f, DeformStrength: %.1f"), DeformRadius, DeformStrength);

    // 변형 계산 준비
    const double RadiusSq = FMath::Square((double)DeformRadius);
    const double InverseRadius = 1.0 / (double)DeformRadius;

    double MinDebugDistSq = DBL_MAX;
    bool bAnyModified = false;
    int32 ModifiedVertexCount = 0;

    // [디버그] 적용할 지점 표시
    for (int32 i = StartIndex; i < EndIndex; ++i)
    {
        FVector WorldPos = MeshComp->GetComponentTransform().TransformPosition(HitHistoryArray.Items[i].LocalLocation);
        UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] Hit[%d] LocalPos: %s, Damage: %.1f"),
            i, *HitHistoryArray.Items[i].LocalLocation.ToString(), HitHistoryArray.Items[i].Damage);

        if (bShowDebugPoints)
        {
            DrawDebugPoint(GetWorld(), WorldPos, 15.0f, FColor::Blue, false, 5.0f);
        }
    }

    // 메쉬 편집 시작 (Vertex 순회)
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
            for (int32 i = StartIndex; i < EndIndex; ++i)
            {
                const FMDFHitData& Hit = HitHistoryArray.Items[i];

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

    // 충돌 업데이트 (서버 + 클라 모두)
    MeshComp->UpdateCollision();

    // 렌더링 업데이트 (클라이언트 전용)
    if (!IsRunningDedicatedServer())
    {
        UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(MeshComp->GetDynamicMesh(), FGeometryScriptCalculateNormalsOptions());
        UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(MeshComp->GetDynamicMesh(), FGeometryScriptTangentsOptions());
        MeshComp->NotifyMeshUpdated();
    }

    UE_LOG(LogTemp, Warning, TEXT("[MDF Deform] ========== 변형 완료 =========="));
}

// -----------------------------------------------------------------------------
// [FFastArray] 메쉬 리셋 후 전체 재적용
// -----------------------------------------------------------------------------
void UMDF_DeformableComponent::ResetDeformation()
{
    // 메쉬를 원본으로 리셋하고, 현재 남아있는 히스토리 전체를 다시 적용
    InitializeDynamicMesh();

    if (HitHistoryArray.Items.Num() > 0)
    {
        ApplyDeformationForHits(0, HitHistoryArray.Items.Num());
    }
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
            // 충돌 업데이트 (서버 + 클라 모두)
            MeshComp->UpdateCollision();

            // 렌더링 업데이트 (클라이언트 전용)
            if (!IsRunningDedicatedServer())
            {
                // 1. 법선 재계산
                UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(MeshComp->GetDynamicMesh(), FGeometryScriptCalculateNormalsOptions());

                // -------------------------------------------------------------------------
                // [★핵심 추가] 초기화 시 탄젠트 재계산
                // 처음 생성될 때부터 메쉬가 투명하게 보이지 않도록 합니다.
                // -------------------------------------------------------------------------
                UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(MeshComp->GetDynamicMesh(), FGeometryScriptTangentsOptions());

                MeshComp->NotifyMeshUpdated();
            }
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

    // [디버그] 자동 복구 타이머 정리
    if (DevMode_RepairTimerHandle.IsValid())
    {
        GetWorld()->GetTimerManager().ClearTimer(DevMode_RepairTimerHandle);
    }

    // FFastArray 초기화
    HitHistoryArray.Items.Empty();
    HitHistoryArray.MarkArrayDirty();

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
