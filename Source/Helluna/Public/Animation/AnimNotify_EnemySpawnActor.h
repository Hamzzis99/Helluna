/**
 * AnimNotify_EnemySpawnActor.h
 *
 * 몬타지의 임팩트 프레임 등에서 임의의 AActor BP 를 스폰하는 범용 노티.
 *
 * 사용 예: 보스의 내려찍기 몬타지 임팩트 프레임에 추가하면
 *           해당 시점에 파동/충격파/이펙트 액터가 보스 발 밑에 스폰됨.
 *
 * ─── 멀티플레이어 ──────────────────────────────────────────────
 *  노티는 모든 클라이언트에서 발화함. 중복 스폰을 막기 위해 서버만 SpawnActor 수행 (HasAuthority).
 *  스폰된 액터가 replicated 면 클라이언트는 자동으로 복제받음.
 *  bServerOnly = false 로 두면 서버/클라 모두 로컬 스폰 (시각용 일회성 이펙트에만 권장).
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnemySpawnActor.generated.h"

UCLASS(meta = (DisplayName = "Enemy: Spawn Actor"))
class HELLUNA_API UAnimNotify_EnemySpawnActor : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** 스폰할 BP/AActor 클래스. */
	UPROPERTY(EditAnywhere, Category = "스폰",
		meta = (DisplayName = "스폰할 액터 클래스"))
	TSubclassOf<AActor> SpawnedActorClass;

	/**
	 * 기준 소켓/본 이름. 비어있으면 액터 루트 위치에 스폰.
	 * 주로 발 소켓 (예: foot_l, foot_r) 이나 무기 소켓 사용.
	 */
	UPROPERTY(EditAnywhere, Category = "스폰",
		meta = (DisplayName = "기준 소켓/본 이름"))
	FName SocketName = NAME_None;

	/** 소켓 위치에서의 추가 오프셋 (월드 또는 본 로컬). */
	UPROPERTY(EditAnywhere, Category = "스폰",
		meta = (DisplayName = "위치 오프셋 (cm)"))
	FVector LocationOffset = FVector::ZeroVector;

	/** 오프셋을 본 로컬 스페이스로 해석할지. false 면 월드 스페이스. */
	UPROPERTY(EditAnywhere, Category = "스폰",
		meta = (DisplayName = "오프셋이 본 로컬 스페이스"))
	bool bOffsetInBoneSpace = false;

	/** 스폰 시 추가로 적용할 회전 (Yaw 포함). */
	UPROPERTY(EditAnywhere, Category = "스폰",
		meta = (DisplayName = "회전 오프셋 (도)"))
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** 스폰 시 적용할 균등 스케일. */
	UPROPERTY(EditAnywhere, Category = "스폰",
		meta = (DisplayName = "균등 스케일", ClampMin = "0.01", ClampMax = "20.0"))
	float UniformScale = 1.f;

	/** 스폰된 액터를 캐릭터에 Attach 할지. true 면 캐릭터 따라 이동. */
	UPROPERTY(EditAnywhere, Category = "스폰",
		meta = (DisplayName = "캐릭터에 Attach"))
	bool bAttachToOwner = false;

	/**
	 * true: 서버에서만 스폰 (중복 방지, replicated 액터 권장).
	 * false: 모든 머신에서 로컬 스폰 (일회성 시각 이펙트용).
	 */
	UPROPERTY(EditAnywhere, Category = "스폰|네트워크",
		meta = (DisplayName = "서버에서만 스폰"))
	bool bServerOnly = true;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
