// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inv_ResourceComponent.generated.h"

// 자원 채집 컴포넌트 - Static Mesh Actor에 붙여서 사용
// 오픈월드/PCG 최적화된 컴포넌트 기반 시스템
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable)
class INVENTORY_API UInv_ResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInv_ResourceComponent();

	virtual void BeginPlay() override;
	
	// 리플리케이션 속성 등록 (UE 5.7.1)
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Owner 액터가 데미지를 받을 때 호출
	UFUNCTION()
	void OnOwnerTakeDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

private:
	// ========== 자원 체력 설정 ==========
	
	// 자원 기본 체력 (블루프린트에서 설정 가능)
	UPROPERTY(EditAnywhere, Category = "자원|체력",
		meta = (ClampMin = "1.0", DisplayName = "기본 체력", Tooltip = "자원의 최대 체력값. 이 값이 0이 되면 자원이 파괴됩니다."))
	float MaxHealth = 100.f;

	// 현재 체력 (내부 사용, BeginPlay에서 자동으로 기본 체력으로 설정됨)
	// 서버에서 클라이언트로 복제됨 (네트워크 동기화)
	UPROPERTY(Replicated)
	float CurrentHealth = 0.f;

	// HP가 이 값만큼 감소할 때마다 아이템 드롭 (0 = 파괴 시에만 드롭)
	UPROPERTY(EditAnywhere, Category = "자원|체력",
		meta = (ClampMin = "0.0", DisplayName = "드롭 HP 간격 (0=파괴시만)", Tooltip = "HP가 이 값만큼 감소할 때마다 아이템을 드롭합니다. 0이면 파괴 시에만 드롭합니다."))
	float DropHealthInterval = 0.f;

	// 마지막으로 드롭한 HP 지점 (서버 전용, 드롭 간격 추적용)
	UPROPERTY(Replicated)
	float LastDropHealth;

	// 파괴 진행 중 플래그 (중복 데미지/파괴 방지)
	bool bIsDestroyed = false;

	// ========== 드롭 아이템 설정 ==========
	
	// 드롭할 아이템 Blueprint 클래스 배열 (여러 개 등록 가능, 랜덤 선택)
	UPROPERTY(EditAnywhere, Category = "자원|드롭 아이템",
		meta = (DisplayName = "드롭 아이템 목록 (랜덤)", Tooltip = "드롭할 아이템 블루프린트 클래스 배열. 여러 개 등록하면 랜덤으로 선택됩니다."))
	TArray<TSubclassOf<AActor>> DropItemClasses;

	// 한 번에 드롭되는 아이템 개수 (최소)
	UPROPERTY(EditAnywhere, Category = "자원|드롭 아이템",
		meta = (ClampMin = "1", DisplayName = "드롭 개수 (최소)", Tooltip = "한 번에 드롭되는 아이템의 최소 개수입니다."))
	int32 DropCountMin = 1;

	// 한 번에 드롭되는 아이템 개수 (최대)
	UPROPERTY(EditAnywhere, Category = "자원|드롭 아이템",
		meta = (ClampMin = "1", DisplayName = "드롭 개수 (최대)", Tooltip = "한 번에 드롭되는 아이템의 최대 개수입니다."))
	int32 DropCountMax = 3;

	// ========== 드롭 위치 설정 (수학 공식) ==========
	
	// 드롭 각도 범위 (최소)
	UPROPERTY(EditAnywhere, Category = "자원|드롭 위치",
		meta = (DisplayName = "드롭 각도 (최소)", Tooltip = "아이템이 드롭되는 각도 범위의 최솟값입니다."))
	float DropSpawnAngleMin = -85.f;

	// 드롭 각도 범위 (최대)
	UPROPERTY(EditAnywhere, Category = "자원|드롭 위치",
		meta = (DisplayName = "드롭 각도 (최대)", Tooltip = "아이템이 드롭되는 각도 범위의 최댓값입니다."))
	float DropSpawnAngleMax = 85.f;

	// 드롭 거리 (최소)
	UPROPERTY(EditAnywhere, Category = "자원|드롭 위치",
		meta = (DisplayName = "드롭 거리 (최소)", Tooltip = "아이템이 드롭되는 거리 범위의 최솟값입니다."))
	float DropSpawnDistanceMin = 10.f;

	// 드롭 거리 (최대)
	UPROPERTY(EditAnywhere, Category = "자원|드롭 위치",
		meta = (DisplayName = "드롭 거리 (최대)", Tooltip = "아이템이 드롭되는 거리 범위의 최댓값입니다."))
	float DropSpawnDistanceMax = 50.f;

	// 드롭 높이 보정값
	UPROPERTY(EditAnywhere, Category = "자원|드롭 위치",
		meta = (DisplayName = "드롭 높이 보정", Tooltip = "드롭 시 Z축 높이 보정값입니다."))
	float RelativeSpawnElevation = 70.f;

	// ========== 포물선 드롭 설정 ==========
	
	// 아이템 스폰 시작 높이 (자원 위치에서 이만큼 위로 올려서 시작)
	UPROPERTY(EditAnywhere, Category = "자원|곡선 드롭",
		meta = (DisplayName = "시작 높이 (Z축)", ClampMin = "0.0", Tooltip = "아이템 스폰 시작 높이. 자원 위치에서 이만큼 위로 올려서 시작합니다."))
	float SpawnStartHeight = 50.f;

	// 포물선 발사 각도 (0도 = 수평, 90도 = 수직)
	UPROPERTY(EditAnywhere, Category = "자원|곡선 드롭",
		meta = (DisplayName = "발사각 (도)", ClampMin = "0.0", ClampMax = "90.0", Tooltip = "포물선 발사 각도입니다. 0도는 수평, 90도는 수직입니다."))
	float LaunchAngleDegrees = 30.f;

	// 발사 속도 배율 (1.0 = 기본, 0.5 = 절반, 2.0 = 2배) - 포물선을 더 선명하게!
	UPROPERTY(EditAnywhere, Category = "자원|곡선 드롭",
		meta = (DisplayName = "발사 속도 배율", ClampMin = "0.1", ClampMax = "5.0", Tooltip = "발사 속도 배율입니다. 1.0이 기본이며, 높을수록 더 멀리 날아갑니다."))
	float LaunchSpeedMultiplier = 0.5f;

	// 중력 배율 (1.0 = 기본, 0.5 = 느리게 떨어짐, 2.0 = 빠르게 떨어짐)
	UPROPERTY(EditAnywhere, Category = "자원|곡선 드롭",
		meta = (DisplayName = "중력 배율", ClampMin = "0.1", ClampMax = "5.0", Tooltip = "중력 배율입니다. 1.0이 기본이며, 낮을수록 느리게 떨어집니다."))
	float GravityScale = 0.6f;

	// ========== 사운드 설정 ==========
	
	// 데미지 받을 때 재생되는 사운드 (타격음)
	UPROPERTY(EditAnywhere, Category = "자원|사운드",
		meta = (DisplayName = "데미지 사운드 (타격음)", Tooltip = "자원이 데미지를 받을 때 재생되는 타격음입니다."))
	USoundBase* DamageSound = nullptr;

	// 자원이 파괴될 때 재생되는 사운드 (파괴음)
	UPROPERTY(EditAnywhere, Category = "자원|사운드",
		meta = (DisplayName = "파괴 사운드 (파괴음)", Tooltip = "자원이 파괴될 때 재생되는 파괴음입니다."))
	USoundBase* DestroySound = nullptr;

	// 3D 사운드 거리 감쇠 설정 (블루프린트에서 선택 가능)
	// Content Browser에서 우클릭 → Sounds → Sound Attenuation으로 생성
	UPROPERTY(EditAnywhere, Category = "자원|사운드",
		meta = (DisplayName = "사운드 감쇠 설정 (거리 조절)", Tooltip = "3D 사운드 거리 감쇠 설정입니다. Content Browser에서 Sound Attenuation 에셋을 생성하여 지정하세요."))
	class USoundAttenuation* SoundAttenuation = nullptr;

	// 사운드 볼륨 배율
	UPROPERTY(EditAnywhere, Category = "자원|사운드",
		meta = (DisplayName = "사운드 볼륨", ClampMin = "0.0", ClampMax = "2.0", Tooltip = "사운드 볼륨 배율입니다. 1.0이 기본 볼륨입니다."))
	float SoundVolumeMultiplier = 1.0f;

	// 자원 드롭 처리
	void SpawnDroppedResources();

	// Owner 액터 파괴
	void DestroyOwnerActor();
	
	// 사운드 재생 (자원 위치에서 3D 사운드)
	void PlaySoundAtResource(USoundBase* Sound);
	
	// Multicast RPC: 모든 클라이언트에 사운드 전파
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlaySoundAtLocation(USoundBase* Sound, FVector Location);
};

