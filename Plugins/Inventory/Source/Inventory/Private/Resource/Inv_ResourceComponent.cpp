// Fill out your copyright notice in the Description page of Project Settings.

#include "Resource/Inv_ResourceComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

UInv_ResourceComponent::UInv_ResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInv_ResourceComponent::BeginPlay()
{
	Super::BeginPlay();

	// 현재 체력이 0이면 최대 체력으로 자동 설정
	if (CurrentHealth <= 0.f)
	{
		CurrentHealth = MaxHealth;
	}
	LastDropHealth = CurrentHealth;

	// Owner 액터의 OnTakeAnyDamage 델리게이트에 바인딩
	AActor* Owner = GetOwner();
	if (IsValid(Owner))
	{
		// Owner 액터의 리플리케이션 강제 활성화 (네트워크 동기화 필수)
		if (!Owner->GetIsReplicated())
		{
			Owner->SetReplicates(true);
			UE_LOG(LogTemp, Warning, TEXT("[자원 컴포넌트] Owner 액터의 Replicate를 자동으로 활성화했습니다: %s"), *Owner->GetName());
		}
		
		Owner->OnTakeAnyDamage.AddDynamic(this, &UInv_ResourceComponent::OnOwnerTakeDamage);
		
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("[자원 컴포넌트] 초기화 완료!"));
		UE_LOG(LogTemp, Warning, TEXT("  - Owner 액터: %s"), *Owner->GetName());
		UE_LOG(LogTemp, Warning, TEXT("  - 기본 체력: %.1f"), MaxHealth);
		UE_LOG(LogTemp, Warning, TEXT("  - 시작 HP: %.1f"), CurrentHealth);
		UE_LOG(LogTemp, Warning, TEXT("  - 드롭 HP 간격: %.1f %s"), DropHealthInterval, DropHealthInterval > 0.f ? TEXT("(HP마다 드롭)") : TEXT("(파괴시만 드롭)"));
		
		// 드롭 아이템 목록 출력
		if (DropItemClasses.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  - 드롭 아이템 개수: %d (랜덤 선택)"), DropItemClasses.Num());
			for (int32 i = 0; i < DropItemClasses.Num(); i++)
			{
				if (DropItemClasses[i])
				{
					UE_LOG(LogTemp, Warning, TEXT("    [%d] %s"), i, *DropItemClasses[i]->GetName());
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("  - 드롭 아이템: ❌ 없음! (배열이 비어있음)"));
		}
		
		UE_LOG(LogTemp, Warning, TEXT("  - 드롭 개수: %d ~ %d"), DropCountMin, DropCountMax);
		UE_LOG(LogTemp, Warning, TEXT("  - Can Damage? %s"), Owner->CanBeDamaged() ? TEXT("✅ YES") : TEXT("❌ NO ← 문제!"));
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		
		// 데미지를 받을 수 있도록 강제 설정
		if (!Owner->CanBeDamaged())
		{
			UE_LOG(LogTemp, Error, TEXT("[자원] Owner 액터가 데미지를 받을 수 없음! bCanBeDamaged를 true로 설정합니다."));
			Owner->SetCanBeDamaged(true);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[자원 컴포넌트] Owner가 없습니다! 컴포넌트가 제대로 붙지 않았을 수 있습니다."));
	}
}

void UInv_ResourceComponent::OnOwnerTakeDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	const bool bIsServer = GetOwner()->HasAuthority();
	const FString RoleStr = bIsServer ? TEXT("🔴 서버") : TEXT("🔵 클라이언트");
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[자원 %s] OnOwnerTakeDamage 호출됨!"), *RoleStr);
	UE_LOG(LogTemp, Warning, TEXT("  - 데미지 받은 액터: %s"), DamagedActor ? *DamagedActor->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("  - 데미지 양: %.1f"), Damage);
	UE_LOG(LogTemp, Warning, TEXT("  - 데미지 원인: %s"), DamageCauser ? *DamageCauser->GetName() : TEXT("없음"));
	
	if (!bIsServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("[자원 %s] 클라이언트이므로 데미지 처리 건너뜀 (서버에서만 처리)"), *RoleStr);
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		return;
	}

	CurrentHealth -= Damage;

	UE_LOG(LogTemp, Warning, TEXT("  - 이전 HP: %.1f → 현재 HP: %.1f (기본: %.1f)"), 
		CurrentHealth + Damage, CurrentHealth, MaxHealth);

	// 데미지 받을 때 사운드 재생 (타격음)
	PlaySoundAtResource(DamageSound);

	// HP 간격마다 드롭 기능
	if (DropHealthInterval > 0.f)
	{
		UE_LOG(LogTemp, Log, TEXT("[자원] HP 간격 체크: LastDrop=%.1f, Current=%.1f, Interval=%.1f"), 
			LastDropHealth, CurrentHealth, DropHealthInterval);
			
		// 이전 드롭 지점에서 DropHealthInterval만큼 떨어졌는지 확인
		while (LastDropHealth - CurrentHealth >= DropHealthInterval && CurrentHealth > 0.f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[자원] ✅ HP 간격 도달! 아이템 드롭 (간격: %.1f, 현재 HP: %.1f)"), 
				DropHealthInterval, CurrentHealth);
			SpawnDroppedResources();
			LastDropHealth -= DropHealthInterval;
		}
	}

	// 파괴 시 최종 드롭
	if (CurrentHealth <= 0.f)
	{
		// HP를 0으로 고정 (음수 방지)
		CurrentHealth = 0.f;
		
		UE_LOG(LogTemp, Warning, TEXT("[자원] ❌ HP 0 도달! %s 파괴 및 최종 자원 드롭..."), *GetOwner()->GetName());
		
		// 파괴 사운드 재생 (파괴음)
		PlaySoundAtResource(DestroySound);
		
		// HP 간격 드롭을 사용하지 않는 경우에만 파괴 시 드롭
		if (DropHealthInterval <= 0.f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[자원] 파괴 시 드롭 실행 (HP 간격 드롭 미사용)"));
			SpawnDroppedResources();
		}
		else
		{
			// HP 간격 드롭 사용 중이지만, 마지막 남은 HP에 대해서도 드롭
			const float RemainingHP = LastDropHealth - CurrentHealth;
			if (RemainingHP > 0.f)
			{
				UE_LOG(LogTemp, Warning, TEXT("[자원] 파괴 시 남은 HP %.1f에 대한 최종 드롭 실행"), RemainingHP);
				SpawnDroppedResources();
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[자원] 파괴 시 드롭 생략 (이미 모든 HP 간격 드롭 완료)"));
			}
		}
		
		// 즉시 파괴 (더 이상 데미지를 받지 않도록)
		DestroyOwnerActor();
	}
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void UInv_ResourceComponent::SpawnDroppedResources()
{
	if (!GetOwner()->HasAuthority()) return;
	
	// 드롭 아이템 배열 체크
	if (DropItemClasses.Num() == 0) 
	{
		UE_LOG(LogTemp, Error, TEXT("[자원] 드롭할 아이템 목록이 비어있습니다!"));
		return;
	}

	const int32 DropCount = FMath::RandRange(DropCountMin, DropCountMax);
	UE_LOG(LogTemp, Warning, TEXT("[자원] %d개의 아이템 소환 중 (랜덤 선택)"), DropCount);

	AActor* Owner = GetOwner();
	const FVector ActorLocation = Owner->GetActorLocation();

	for (int32 i = 0; i < DropCount; i++)
	{
		// 배열에서 랜덤으로 아이템 선택
		const int32 RandomIndex = FMath::RandRange(0, DropItemClasses.Num() - 1);
		TSubclassOf<AActor> SelectedItemClass = DropItemClasses[RandomIndex];
		
		if (!SelectedItemClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[자원] 드롭 아이템 [%d]가 nullptr입니다! 건너뜀"), RandomIndex);
			continue;
		}
		
		UE_LOG(LogTemp, Log, TEXT("[자원] 아이템 %d/%d: 배열[%d] 선택 → %s"), 
			i + 1, DropCount, RandomIndex, *SelectedItemClass->GetName());
		
		// 완전 랜덤 360도 방향
		const float RandomYaw = FMath::FRandRange(0.f, 360.f);
		FVector RandomDirection = FVector::ForwardVector.RotateAngleAxis(RandomYaw, FVector::UpVector);
		
		// 스폰 위치: 자원 위치 + 시작 높이 (블루프린트에서 설정 가능)
		FVector SpawnLocation = ActorLocation;
		SpawnLocation.Z += SpawnStartHeight; // Z축으로 위로 올림


		const FRotator SpawnRotation = FRotator::ZeroRotator;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AActor* SpawnedItem = GetWorld()->SpawnActor<AActor>(
			SelectedItemClass, 
			SpawnLocation, 
			SpawnRotation, 
			SpawnParams
		);

		if (IsValid(SpawnedItem))
		{
			// Static Mesh에 Physics 활성화 + 초기 속도 부여 (포물선 효과)
			TArray<UStaticMeshComponent*> MeshComponents;
			SpawnedItem->GetComponents<UStaticMeshComponent>(MeshComponents);
			
			UE_LOG(LogTemp, Warning, TEXT("[자원 드롭] 아이템 %d/%d 스폰됨: %s"), i + 1, DropCount, *SpawnedItem->GetName());
			UE_LOG(LogTemp, Warning, TEXT("  - StaticMesh 컴포넌트 개수: %d"), MeshComponents.Num());
			
			if (MeshComponents.Num() == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("  ❌ StaticMesh 컴포넌트가 없습니다! BP에 Static Mesh가 있는지 확인하세요!"));
			}
			
			for (UStaticMeshComponent* MeshComp : MeshComponents)
			{
				if (IsValid(MeshComp))
				{
					UE_LOG(LogTemp, Warning, TEXT("  - Mesh 이름: %s"), *MeshComp->GetName());
					UE_LOG(LogTemp, Warning, TEXT("  - Physics 활성화 전: Simulate=%d, Gravity=%d"), 
						MeshComp->IsSimulatingPhysics(), MeshComp->IsGravityEnabled());
					
					// Physics 활성화
					MeshComp->SetSimulatePhysics(true);
					MeshComp->SetEnableGravity(true);
					
					UE_LOG(LogTemp, Warning, TEXT("  - Physics 활성화 후: Simulate=%d, Gravity=%d"), 
						MeshComp->IsSimulatingPhysics(), MeshComp->IsGravityEnabled());
					
					// Collision 체크
					UE_LOG(LogTemp, Warning, TEXT("  - Collision Enabled: %d"), (int32)MeshComp->GetCollisionEnabled());
					UE_LOG(LogTemp, Warning, TEXT("  - Has Physics State: %d"), MeshComp->HasValidPhysicsState());
					
					// 포물선 발사각 계산 (블루프린트에서 설정 가능, 기본값 30도)
					const float LaunchAngleRadians = FMath::DegreesToRadians(LaunchAngleDegrees);
					
					// 랜덤 거리 (얼마나 멀리 날아갈지)
					const float LaunchDistance = FMath::FRandRange(DropSpawnDistanceMin, DropSpawnDistanceMax);
					
					// 포물선 공식: V = sqrt(g * d / sin(2θ))
					const float Gravity = FMath::Abs(GetWorld()->GetGravityZ()); // 중력 (양수)
					const float BaseSpeed = FMath::Sqrt(
						(Gravity * LaunchDistance) / FMath::Sin(2.f * LaunchAngleRadians)
					);
					
					// 속도 배율 적용 (포물선을 더 선명하게!)
					const float LaunchSpeed = BaseSpeed * LaunchSpeedMultiplier;
					
					// 속도 벡터 계산 (설정한 발사각 사용)
					// 수평 성분 = V * cos(θ)
					// 수직 성분 = V * sin(θ)
					const float HorizontalSpeed = LaunchSpeed * FMath::Cos(LaunchAngleRadians);
					const float VerticalSpeed = LaunchSpeed * FMath::Sin(LaunchAngleRadians);
					
					// 최종 초기 속도 = 랜덤 수평 방향 + 수직
					FVector InitialVelocity = RandomDirection * HorizontalSpeed; // 수평 (랜덤 360도)
					InitialVelocity.Z = VerticalSpeed; // 수직 (설정한 발사각)
					
					// 초기 속도 적용 (포물선으로 날아감!)
					MeshComp->SetPhysicsLinearVelocity(InitialVelocity);
					
					// 중력 스케일 적용 (천천히 떨어지도록!)
					MeshComp->SetEnableGravity(true);
					MeshComp->BodyInstance.SetMassScale(1.0f); // 질량은 기본값
					
					// 언리얼 엔진 중력 조정 (올바른 API 사용)
					if (FBodyInstance* BodyInst = MeshComp->GetBodyInstance())
					{
						BodyInst->bEnableGravity = true;
						BodyInst->SetMassOverride(1.0f); // 질량 1kg
					}
					
					// 중력 배율 적용 (SetLinearDamping으로 떨어지는 속도 조절)
					MeshComp->SetLinearDamping(GravityScale); // 공기 저항 (0 = 없음, 1 = 많음)
					
					// 속도 재적용 (중력 설정 후)
					MeshComp->SetPhysicsLinearVelocity(InitialVelocity);
					
					// 회전도 랜덤하게
					FVector RandomAngularVelocity = FVector(
						FMath::FRandRange(-360.f, 360.f),
						FMath::FRandRange(-360.f, 360.f),
						FMath::FRandRange(-360.f, 360.f)
					);
					MeshComp->SetPhysicsAngularVelocityInDegrees(RandomAngularVelocity);
					
					UE_LOG(LogTemp, Warning, TEXT("  ✅ 포물선 설정: 각도=%.1f°, 속도배율=%.2fx, 중력=%.2fx"), 
						LaunchAngleDegrees, LaunchSpeedMultiplier, GravityScale);
					UE_LOG(LogTemp, Warning, TEXT("  - 발사 속도: %.1f (기본: %.1f), 거리: %.1f"), 
						LaunchSpeed, BaseSpeed, LaunchDistance);
				}
			}
			
			UE_LOG(LogTemp, Log, TEXT("[자원] 아이템 소환 성공 %d/%d: %s (위치: %s)"), 
				i + 1, DropCount, *SpawnedItem->GetName(), *SpawnLocation.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[자원] 아이템 소환 실패 %d/%d"), i + 1, DropCount);
		}
	}
}

void UInv_ResourceComponent::DestroyOwnerActor()
{
	AActor* Owner = GetOwner();
	if (IsValid(Owner))
	{
		const bool bIsServer = Owner->HasAuthority();
		const FString RoleStr = bIsServer ? TEXT("🔴 서버") : TEXT("🔵 클라이언트");
		
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("[자원 %s] Owner 액터 파괴 시작: %s"), *RoleStr, *Owner->GetName());
		
		// 1. 즉시 데미지 받기 비활성화 (중복 데미지 방지)
		Owner->SetCanBeDamaged(false);
		UE_LOG(LogTemp, Warning, TEXT("[자원 %s] 1. 데미지 받기 비활성화 완료"), *RoleStr);
		
		// 2. 모든 컴포넌트 처리 (Collision 끄기 + 메시 숨기기)
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		int32 HiddenCount = 0;
		for (UActorComponent* Component : Components)
		{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
			{
				// Collision 비활성화
				PrimComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				
				// 메시 즉시 숨기기 (시각적 제거)
				PrimComp->SetVisibility(false, true);
				PrimComp->SetHiddenInGame(true, true);
				HiddenCount++;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[자원 %s] 2. %d개 컴포넌트 숨김 처리 완료"), *RoleStr, HiddenCount);
		
		// 3. 액터 전체 Collision 비활성화
		Owner->SetActorEnableCollision(false);
		UE_LOG(LogTemp, Warning, TEXT("[자원 %s] 3. 액터 Collision 비활성화 완료"), *RoleStr);
		
		// 4. 액터 전체 숨김 처리 (추가 안전장치)
		Owner->SetActorHiddenInGame(true);
		UE_LOG(LogTemp, Warning, TEXT("[자원 %s] 4. 액터 숨김 처리 완료"), *RoleStr);
		
		// 5. 네트워크 동기화 파괴 설정 (서버만)
		if (bIsServer)
		{
			// SetReplicates(false) 대신 SetLifeSpan 사용 (네트워크 동기화 보장)
			Owner->SetLifeSpan(0.01f); // 0.01초 후 자동 파괴 (클라이언트까지 동기화)
			UE_LOG(LogTemp, Warning, TEXT("[자원 %s] 5. LifeSpan 0.01초 설정 (네트워크 동기화 파괴)"), *RoleStr);
		}
		else
		{
			// 클라이언트는 즉시 파괴
			Owner->Destroy();
			UE_LOG(LogTemp, Warning, TEXT("[자원 %s] 5. 클라이언트 즉시 파괴"), *RoleStr);
		}
		
		UE_LOG(LogTemp, Warning, TEXT("[자원 %s] ✅ 메시 숨김 + 충돌 비활성화 완료. 파괴 대기중..."), *RoleStr);
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
	}
}

void UInv_ResourceComponent::PlaySoundAtResource(USoundBase* Sound)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		UE_LOG(LogTemp, Error, TEXT("[자원 사운드] ❌ Owner가 유효하지 않음!"));
		return;
	}

	const bool bIsServer = Owner->HasAuthority();
	const FString RoleStr = bIsServer ? TEXT("🔴 서버") : TEXT("🔵 클라이언트");

	// 사운드가 설정되지 않았으면 재생 안 함
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[자원 사운드 %s] ⚠️ 사운드가 설정되지 않음 (nullptr)"), *RoleStr);
		return;
	}

	// 자원 위치에서 3D 사운드 재생
	const FVector SoundLocation = Owner->GetActorLocation();
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[자원 사운드 %s] 🔊 사운드 재생 시도!"), *RoleStr);
	UE_LOG(LogTemp, Warning, TEXT("  - 사운드: %s"), *Sound->GetName());
	UE_LOG(LogTemp, Warning, TEXT("  - 위치: %s"), *SoundLocation.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  - 볼륨: %.2f"), SoundVolumeMultiplier);
	UE_LOG(LogTemp, Warning, TEXT("  - 거리: %.1f"), SoundAttenuationDistance);
	UE_LOG(LogTemp, Warning, TEXT("  - World 유효: %s"), GetWorld() ? TEXT("✅ YES") : TEXT("❌ NO"));
	
	// 서버/클라이언트 모두에서 사운드 재생 (로컬 사운드)
	// 멀티플레이 환경에서는 각자의 클라이언트에서 들어야 함!
	UGameplayStatics::PlaySoundAtLocation(
		GetWorld(),                    // World Context
		Sound,                         // 재생할 사운드
		SoundLocation,                 // 재생 위치
		SoundVolumeMultiplier,         // 볼륨 배율
		1.0f,                          // 피치 배율 (음높이)
		0.0f,                          // 시작 시간
		nullptr,                       // Sound Attenuation (nullptr = 기본 거리 감쇠)
		nullptr                        // Sound Concurrency (nullptr = 제한 없음)
	);

	UE_LOG(LogTemp, Warning, TEXT("  ✅ PlaySoundAtLocation 호출 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

