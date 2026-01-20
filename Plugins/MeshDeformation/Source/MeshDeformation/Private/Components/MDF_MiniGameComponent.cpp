// Gihyeon's MeshDeformation Project
// File: Source/MeshDeformation/Components/MDF_MiniGameComponent.cpp

#include "Components/MDF_MiniGameComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h" 

// [★네트워크 필수] 서버-클라이언트 간 변수 복제를 위한 헤더
#include "Net/UnrealNetwork.h" 

// [Geometry Script 헤더]
#include "GeometryScript/MeshQueryFunctions.h" 
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshUVFunctions.h"
#include "GeometryScript/GeometryScriptTypes.h" 

UMDF_MiniGameComponent::UMDF_MiniGameComponent()
{
    PrimaryComponentTick.bCanEverTick = true; 
    SetIsReplicatedByDefault(true);
}

void UMDF_MiniGameComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UMDF_MiniGameComponent, WeakSpots);
}

void UMDF_MiniGameComponent::OnRep_WeakSpots()
{
    for (int32 i = 0; i < WeakSpots.Num(); ++i)
    {
        if (WeakSpots[i].bIsBroken && !LocallyProcessedIndices.Contains(i))
        {
            ApplyVisualMeshCut(i);
        }
    }
}

// -----------------------------------------------------------------------------
// [핵심 수정] HandlePointDamage - 부모 배칭 시스템 활용
// -----------------------------------------------------------------------------
void UMDF_MiniGameComponent::HandlePointDamage(AActor* DamagedActor, float Damage, class AController* InstigatedBy, FVector HitLocation, class UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const class UDamageType* DamageType, AActor* DamageCauser)
{
    // 1. 서버 권한 체크
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    // 2. 약점 명중 여부 확인
    FHitResult HitInfo;
    HitInfo.Location = HitLocation;
    
    bool bHitWeakSpot = TryBreach(HitInfo, Damage);

    if (bHitWeakSpot)
    {
        UE_LOG(LogTemp, Log, TEXT("[MDF Gatekeeper] 약점 명중! 찌그러짐 적용"));

        // 3. 좌표 변환
        FVector LocalHitPos = GetLocalLocationFromWorld(HitLocation);
        FVector LocalDir = FVector::ForwardVector;
        
        UDynamicMeshComponent* MeshComp = GetOwner()->FindComponentByClass<UDynamicMeshComponent>();
        if (IsValid(MeshComp))
        {
            LocalDir = MeshComp->GetComponentTransform().InverseTransformVector(ShotFromDirection);
        }

        FMDFHitData NewHit(
            LocalHitPos,
            LocalDir,
            Damage,
            DamageType ? DamageType->GetClass() : nullptr
        );

        // 4. [수정] 부모의 배칭 시스템 활용 - 헬퍼 함수 사용
        HitQueue.Add(NewHit);
        StartBatchTimer();
    }
    // 약점이 아니면 아무 일도 일어나지 않음
}

// -----------------------------------------------------------------------------
// [좌표 변환]
// -----------------------------------------------------------------------------
FVector UMDF_MiniGameComponent::GetLocalLocationFromWorld(FVector WorldLoc) const
{
    UDynamicMeshComponent* DynComp = GetOwner() ? GetOwner()->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
    if (DynComp)
    {
        return DynComp->GetComponentTransform().InverseTransformPosition(WorldLoc);
    }
    return GetOwner() ? GetOwner()->GetActorTransform().InverseTransformPosition(WorldLoc) : WorldLoc;
}

// -----------------------------------------------------------------------------
// [마킹 로직]
// -----------------------------------------------------------------------------
void UMDF_MiniGameComponent::StartMarking(FVector WorldLocation)
{
    bIsMarking = true;
    bIsValidCut = false; 
    
    // 시작점 기록
    LocalStartPoint = GetLocalLocationFromWorld(WorldLocation);

    // [디버그] 메쉬 바운드 확인
    UDynamicMeshComponent* DynComp = GetOwner() ? GetOwner()->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
    if (DynComp && DynComp->GetDynamicMesh())
    {
        FBox MeshBounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(DynComp->GetDynamicMesh());
        UE_LOG(LogTemp, Warning, TEXT("[MiniGame] 메쉬 바운드 - Min: %s, Max: %s"), *MeshBounds.Min.ToString(), *MeshBounds.Max.ToString());
        UE_LOG(LogTemp, Warning, TEXT("[MiniGame] 메쉬 크기 - X: %.1f, Y: %.1f, Z: %.1f"), 
            MeshBounds.GetSize().X, MeshBounds.GetSize().Y, MeshBounds.GetSize().Z);
    }

    UE_LOG(LogTemp, Warning, TEXT("[MiniGame] 드래그 시작 (Local: %s)"), *LocalStartPoint.ToString());
}

void UMDF_MiniGameComponent::UpdateMarking(FVector WorldLocation)
{
    if (!bIsMarking) return;

    FVector CurrentLocalPos = GetLocalLocationFromWorld(WorldLocation);
    UDynamicMeshComponent* DynComp = GetOwner() ? GetOwner()->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
    if (!DynComp || !DynComp->GetDynamicMesh()) return;
    
    FBox MeshBounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(DynComp->GetDynamicMesh());

    // -------------------------------------------------------------------------
    // [사각형 드래그 방식] 시작점 ~ 현재점으로 Box 생성
    // X축 = 너비 (좌우, 드래그)
    // Y축 = 깊이 (메쉬 관통)
    // Z축 = 높이 (상하, 드래그)
    // -------------------------------------------------------------------------
    FVector PointA = LocalStartPoint;  // 시작점
    FVector PointB = CurrentLocalPos;  // 끝점

    // X축 (너비): 사용자가 드래그한 범위
    float MinX = FMath::Min(PointA.X, PointB.X);
    float MaxX = FMath::Max(PointA.X, PointB.X);

    // Y축 (깊이): 메쉬 전체 관통
    float DepthExtension = MeshBounds.GetSize().GetMax() * 0.5f + 100.0f;
    float MinY = MeshBounds.Min.Y - DepthExtension;
    float MaxY = MeshBounds.Max.Y + DepthExtension;

    // Z축 (높이): 사용자가 드래그한 범위 + 바닥 보정
    float TopZ = FMath::Max(PointA.Z, PointB.Z);
    float BottomZ = FMath::Min(PointA.Z, PointB.Z);
    
    // [Z축 보정 로직] 끝점이 바닥에서 YAxisSnapThreshold 이내면 바닥까지 확장
    float DistanceFromBottom = BottomZ - MeshBounds.Min.Z;
    if (DistanceFromBottom <= YAxisSnapThreshold && DistanceFromBottom >= 0.0f)
    {
        BottomZ = MeshBounds.Min.Z;  // 바닥까지 단두대!
        UE_LOG(LogTemp, Log, TEXT("[MiniGame] Z축 보정 적용! 바닥까지 확장"));
    }

    CurrentPreviewBox = FBox(
        FVector(MinX, MinY, BottomZ),
        FVector(MaxX, MaxY, TopZ)
    );

    // [디버그] 좌표 확인
    UE_LOG(LogTemp, Log, TEXT("[MiniGame] Start: %s → Current: %s"), *PointA.ToString(), *PointB.ToString());
    UE_LOG(LogTemp, Log, TEXT("[MiniGame] Box X(너비): %.1f ~ %.1f, Z(높이): %.1f ~ %.1f"), MinX, MaxX, BottomZ, TopZ);

    // 유효성 검사: 최소 크기 체크 (X와 Z 기준)
    FVector BoxSize = CurrentPreviewBox.GetSize();
    bool bSizeOK = (BoxSize.X > 5.0f) && (BoxSize.Z > 5.0f);
    
    bIsValidCut = bSizeOK;
}

void UMDF_MiniGameComponent::EndMarking(FVector WorldLocation)
{
    if (!bIsMarking) return;

    // 최종 위치로 박스 업데이트
    UpdateMarking(WorldLocation);

    // -------------------------------------------------------------------------
    // [사각형 드래그] 유효하면 약점 생성
    // -------------------------------------------------------------------------
    if (bIsValidCut)
    {
        if (GetOwner()->HasAuthority())
        {
            // 서버: 직접 생성
            Internal_CreateWeakSpot(CurrentPreviewBox);
        }
        else
        {
            // 클라이언트: 서버에 요청 (RPC)
            Server_RequestCreateWeakSpot(CurrentPreviewBox.Min, CurrentPreviewBox.Max);
        }
        
        UE_LOG(LogTemp, Display, TEXT("[MiniGame] 영역 확정! Box: %s ~ %s"), 
            *CurrentPreviewBox.Min.ToString(), *CurrentPreviewBox.Max.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[MiniGame] 취소: 영역이 너무 작음"));
    }

    bIsMarking = false;
}

// -----------------------------------------------------------------------------
// [NEW] 서버 RPC 구현
// -----------------------------------------------------------------------------
void UMDF_MiniGameComponent::Server_RequestCreateWeakSpot_Implementation(FVector BoxMin, FVector BoxMax)
{
    // 서버에서 실행됨 - 클라이언트가 보낸 박스 데이터로 약점 생성
    FBox ReceivedBox(BoxMin, BoxMax);
    
    // 간단한 검증: 박스 크기가 너무 크거나 작으면 거부
    FVector BoxSize = ReceivedBox.GetSize();
    if (BoxSize.GetMin() < 1.0f || BoxSize.GetMax() > 10000.0f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MiniGame] 서버: 비정상적인 박스 크기 거부"));
        return;
    }

    Internal_CreateWeakSpot(ReceivedBox);
    UE_LOG(LogTemp, Log, TEXT("[MiniGame] 서버: 클라이언트 요청으로 약점 생성"));
}

void UMDF_MiniGameComponent::Internal_CreateWeakSpot(const FBox& LocalBox)
{
    FWeakSpotData NewSpot;
    NewSpot.ID = FGuid::NewGuid();
    NewSpot.LocalBox = LocalBox;
    NewSpot.MaxHP = CalculateHPFromBox(LocalBox);
    NewSpot.CurrentHP = NewSpot.MaxHP;
    NewSpot.bIsBroken = false;
    
    WeakSpots.Add(NewSpot);
    UE_LOG(LogTemp, Display, TEXT("[MiniGame] >> 영역 확정! HP: %.1f"), NewSpot.MaxHP);
}

// -----------------------------------------------------------------------------
// [파괴 로직]
// -----------------------------------------------------------------------------
bool UMDF_MiniGameComponent::TryBreach(const FHitResult& HitInfo, float DamageAmount)
{
    if (GetOwner() && !GetOwner()->HasAuthority()) return false;
    FVector LocalHit = GetLocalLocationFromWorld(HitInfo.Location);

    for (int32 i = 0; i < WeakSpots.Num(); ++i)
    {
        if (WeakSpots[i].bIsBroken) continue;

        if (WeakSpots[i].LocalBox.ExpandBy(5.0f).IsInside(LocalHit))
        {
            WeakSpots[i].CurrentHP -= DamageAmount;
            UE_LOG(LogTemp, Display, TEXT("   >>> [HIT!] 약점 명중! (Index: %d, 남은HP: %.1f)"), i, WeakSpots[i].CurrentHP);

            if (WeakSpots[i].CurrentHP <= 0.0f)
            {
                UE_LOG(LogTemp, Error, TEXT("   >>> [DESTROY] 파괴 조건 달성! 절단 실행!"));
                ExecuteDestruction(i);
            }
            return true;
        }
    }
    return false;
}

void UMDF_MiniGameComponent::ExecuteDestruction(int32 WeakSpotIndex)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    if (!WeakSpots.IsValidIndex(WeakSpotIndex) || WeakSpots[WeakSpotIndex].bIsBroken) return; 

    WeakSpots[WeakSpotIndex].bIsBroken = true;
    ApplyVisualMeshCut(WeakSpotIndex);
}

void UMDF_MiniGameComponent::ApplyVisualMeshCut(int32 Index)
{
    if (!WeakSpots.IsValidIndex(Index)) return;
    if (LocallyProcessedIndices.Contains(Index)) return;
    LocallyProcessedIndices.Add(Index);

    UDynamicMeshComponent* DynComp = GetOwner() ? GetOwner()->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
    if (!DynComp || !DynComp->GetDynamicMesh()) return;

    UDynamicMesh* TargetMesh = DynComp->GetDynamicMesh();

    UDynamicMesh* ToolMesh = NewObject<UDynamicMesh>(this); 
    
    // [핵심] X축 좌/우, Z축 위/아래 방향으로 확장
    FBox OriginalBox = WeakSpots[Index].LocalBox;
    FBox CutBox = FBox(
        FVector(OriginalBox.Min.X - CutXExpansionLeft, OriginalBox.Min.Y, OriginalBox.Min.Z - CutZExpansionDown),   // X 좌측, Z 아래로 확장
        FVector(OriginalBox.Max.X + CutXExpansionRight, OriginalBox.Max.Y, OriginalBox.Max.Z + CutZExpansionUp)     // X 우측, Z 위로 확장
    );
    
    UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
        ToolMesh, 
        FGeometryScriptPrimitiveOptions(), 
        FTransform(CutBox.GetCenter()), 
        CutBox.GetExtent().X * 2.0f, 
        CutBox.GetExtent().Y * 2.0f, 
        CutBox.GetExtent().Z * 2.0f
    );

    FGeometryScriptMeshBooleanOptions BoolOptions;
    BoolOptions.bFillHoles = true;       
    BoolOptions.bSimplifyOutput = false; 

    UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
        TargetMesh, FTransform::Identity, ToolMesh, FTransform::Identity, 
        EGeometryScriptBooleanOperation::Subtract, BoolOptions
    );
    
    UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(TargetMesh, FGeometryScriptCalculateNormalsOptions());
    
    FBox MeshBounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(TargetMesh);
    FTransform BoxTransform = FTransform::Identity;
    BoxTransform.SetTranslation(MeshBounds.GetCenter());
    BoxTransform.SetScale3D(MeshBounds.GetSize());

    UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromBoxProjection(TargetMesh, 0, BoxTransform, FGeometryScriptMeshSelection());
    UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(TargetMesh, FGeometryScriptTangentsOptions());
    
    DynComp->MarkRenderTransformDirty(); 
    DynComp->NotifyMeshUpdated();        
    DynComp->UpdateCollision(true);      
    DynComp->MarkRenderStateDirty();

    if (ToolMesh)
    {
        ToolMesh->MarkAsGarbage();
    }

    UE_LOG(LogTemp, Log, TEXT("[MDF] 절단 완료! X확장(좌/우): %.1f/%.1f, Z확장(아래/위): %.1f/%.1f (Index: %d)"), 
        CutXExpansionLeft, CutXExpansionRight, CutZExpansionDown, CutZExpansionUp, Index);
}

// -----------------------------------------------------------------------------
// [유틸리티]
// -----------------------------------------------------------------------------
float UMDF_MiniGameComponent::CalculateHPFromBox(const FBox& Box) const
{
    FVector Size = Box.GetSize();
    float LocalVolume = Size.X * Size.Y * Size.Z;
    
    // [수정] 스케일 이중 적용 제거 - LocalBox는 이미 로컬 좌표
    return 100.0f + (LocalVolume * HPDensityMultiplier * 0.005f);
}

void UMDF_MiniGameComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UDynamicMeshComponent* DynComp = GetOwner() ? GetOwner()->FindComponentByClass<UDynamicMeshComponent>() : nullptr;
    if (!DynComp) return;

    FTransform CompTrans = DynComp->GetComponentTransform();
    FVector WorldScale = CompTrans.GetScale3D();
    
    FBox WallBounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(DynComp->GetDynamicMesh());

    for (const FWeakSpotData& Spot : WeakSpots)
    {
        if (Spot.bIsBroken) continue;
        FBox VisualBox = Spot.LocalBox.Overlap(WallBounds).ExpandBy(0.5f);
        FVector ScaledExtent = VisualBox.GetExtent() * WorldScale;
        DrawDebugBox(GetWorld(), CompTrans.TransformPosition(VisualBox.GetCenter()), ScaledExtent, CompTrans.GetRotation(), FColor::Cyan, false, -1.0f, 0, 1.5f);
    }

    if (bIsMarking)
    {
        FColor DrawColor = bIsValidCut ? FColor::Green : FColor::Red;
        FBox VisualPreview = CurrentPreviewBox.Overlap(WallBounds).ExpandBy(0.5f);
        FVector ScaledExtent = VisualPreview.GetExtent() * WorldScale;
        DrawDebugBox(GetWorld(), CompTrans.TransformPosition(VisualPreview.GetCenter()), ScaledExtent, CompTrans.GetRotation(), DrawColor, false, -1.0f, 0, 3.0f);
    }
}