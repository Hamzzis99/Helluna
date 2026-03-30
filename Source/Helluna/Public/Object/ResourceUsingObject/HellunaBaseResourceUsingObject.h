// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaBaseResourceUsingObject.generated.h"

// [김기현 - MDF 시스템] 전방 선언
class UDynamicMeshComponent;
class UMDF_DeformableComponent;
class UStaticMesh;

// [기존 로직] UI 상호작용
class UBoxComponent;

UCLASS()
class HELLUNA_API AHellunaBaseResourceUsingObject : public AActor
{
    GENERATED_BODY()
    
public: 
    AHellunaBaseResourceUsingObject();

    // 에디터에서 값 변경 시 프리뷰 업데이트
    virtual void OnConstruction(const FTransform& Transform) override;

protected:
    virtual void BeginPlay() override;

protected:
    // ==================================================================================
    // [김기현 - MDF 시스템 영역] 
    // ==================================================================================

    // [김기현] 기존 StaticMeshComponent를 대체하는 변형 가능한 메쉬 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF | Components", meta = (AllowPrivateAccess = "true"))
    UDynamicMeshComponent* DynamicMeshComponent;

    // [김기현] 변형 로직 및 스태틱 메쉬 정보를 담고 있는 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF | Components", meta = (AllowPrivateAccess = "true"))
    UMDF_DeformableComponent* DeformableComponent;


    // ==================================================================================
    // [기존 로직 - UI 상호작용 영역]
    // ==================================================================================

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ResouceUsing")
    UBoxComponent* ResouceUsingCollisionBox;

    UFUNCTION()
    virtual void CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    virtual void CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};