	// Fill out your copyright notice in the Description page of Project Settings.

#include "Component/RepairComponent.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Character/HellunaHeroCharacter.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "DebugHelper.h"
#include "Helluna.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Manifest/Inv_ItemManifest.h"

URepairComponent::URepairComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URepairComponent::BeginPlay()
{
	Super::BeginPlay();

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("[RepairComponent] BeginPlay - Owner: %s"), *GetOwner()->GetName());
#endif

	// ⭐ 서버에서만 델리게이트 바인딩
	if (GetOwner()->HasAuthority())
	{
		// ⭐ 모든 플레이어의 InventoryComponent 찾기 및 델리게이트 바인딩
		BindToAllPlayerInventories();
		
		// ⭐ 새 플레이어 접속 시 자동 바인딩 (타이머로 주기적 체크)
		GetWorld()->GetTimerManager().SetTimer(
			PlayerCheckTimerHandle,
			this,
			&URepairComponent::BindToAllPlayerInventories,
			5.0f,  // 5초마다 체크
			true   // 반복
		);
	}
}

void URepairComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URepairComponent, RepairQueue);
	DOREPLIFETIME(URepairComponent, bProcessingRepair);
}

// ========================================
// [서버 RPC] Repair 요청 처리
// ========================================

void URepairComponent::Server_ProcessRepairRequest_Implementation(
	APlayerController* PlayerController,
	FGameplayTag Material1Tag,
	int32 Material1Amount,
	FGameplayTag Material2Tag,
	int32 Material2Amount)
{
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [Server_ProcessRepairRequest] 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("  Player: %s"), PlayerController ? *PlayerController->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("  Material1: %s x %d"), *Material1Tag.ToString(), Material1Amount);
	UE_LOG(LogTemp, Warning, TEXT("  Material2: %s x %d"), *Material2Tag.ToString(), Material2Amount);
#endif

	// 서버가 아니면 실행 금지
	if (!GetOwner()->HasAuthority())
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ 서버가 아님! 실행 중단"));
#endif
		return;
	}

	// 유효성 검증
	if (!PlayerController)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ PlayerController가 nullptr!"));
#endif
		return;
	}

	// 요청 생성
	FRepairRequest NewRequest;
	NewRequest.PlayerController = PlayerController;
	NewRequest.Material1Tag = Material1Tag;
	NewRequest.Material1Amount = Material1Amount;
	NewRequest.Material2Tag = Material2Tag;
	NewRequest.Material2Amount = Material2Amount;

	// 큐에 추가
	RepairQueue.Add(NewRequest);
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ✅ Repair 요청 큐에 추가됨! 큐 크기: %d"), RepairQueue.Num());
#endif

	// 현재 처리 중이 아니면 즉시 처리 시작
	if (!bProcessingRepair)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  🔧 즉시 처리 시작!"));
#endif
		ProcessNextRepairRequest();
	}
	else
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  ⏳ 다른 Repair 처리 중... 큐에서 대기"));
#endif
	}
}

// ========================================
// [내부 함수] 다음 Repair 요청 처리
// ========================================

void URepairComponent::ProcessNextRepairRequest()
{
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [ProcessNextRepairRequest] 시작 ==="));
#endif

	// 큐가 비어있으면 종료
	if (RepairQueue.Num() == 0)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  ✅ Repair 큐 비어있음. 처리 완료!"));
#endif
		bProcessingRepair = false;
		return;
	}

	bProcessingRepair = true;

	// 큐에서 첫 번째 요청 가져오기
	FRepairRequest Request = RepairQueue[0];
	RepairQueue.RemoveAt(0);

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  📋 처리할 요청: Player=%s, Material1=%s x %d, Material2=%s x %d"),
		Request.PlayerController ? *Request.PlayerController->GetName() : TEXT("nullptr"),
		*Request.Material1Tag.ToString(), Request.Material1Amount,
		*Request.Material2Tag.ToString(), Request.Material2Amount);
#endif

	// ========================================
	// 1. 재료 유효성 검증
	// ========================================

	bool bMaterial1Valid = false;
	bool bMaterial2Valid = false;

	if (Request.Material1Amount > 0 && Request.Material1Tag.IsValid())
	{
		bMaterial1Valid = ValidateMaterial(Request.PlayerController, Request.Material1Tag, Request.Material1Amount);
		if (!bMaterial1Valid)
		{
#if HELLUNA_DEBUG_REPAIR
			UE_LOG(LogTemp, Error, TEXT("  ❌ 재료 1 유효성 검증 실패!"));
#endif
		}
	}

	if (Request.Material2Amount > 0 && Request.Material2Tag.IsValid())
	{
		bMaterial2Valid = ValidateMaterial(Request.PlayerController, Request.Material2Tag, Request.Material2Amount);
		if (!bMaterial2Valid)
		{
#if HELLUNA_DEBUG_REPAIR
			UE_LOG(LogTemp, Error, TEXT("  ❌ 재료 2 유효성 검증 실패!"));
#endif
		}
	}

	// 하나라도 유효하면 진행
	if (!bMaterial1Valid && !bMaterial2Valid)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ 모든 재료 유효성 검증 실패! 다음 요청으로 넘어감"));
#endif
		ProcessNextRepairRequest();
		return;
	}

	// ========================================
	// 2. 인벤토리에서 재료 소비
	// ========================================

	int32 TotalResource = 0;

	if (bMaterial1Valid)
	{
		ConsumeMaterialFromInventory(Request.PlayerController, Request.Material1Tag, Request.Material1Amount);
		TotalResource += Request.Material1Amount * MaterialToResourceRatio;
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  ✅ 재료 1 소비 완료! 자원: +%d"), Request.Material1Amount * MaterialToResourceRatio);
#endif
	}

	if (bMaterial2Valid)
	{
		ConsumeMaterialFromInventory(Request.PlayerController, Request.Material2Tag, Request.Material2Amount);
		TotalResource += Request.Material2Amount * MaterialToResourceRatio;
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  ✅ 재료 2 소비 완료! 자원: +%d"), Request.Material2Amount * MaterialToResourceRatio);
#endif
	}

	// ========================================
	// 3. SpaceShip에 자원 추가
	// ========================================

	AddResourceToTarget(TotalResource);
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ✅ SpaceShip에 자원 추가 완료! 총 자원: %d"), TotalResource);
#endif

	// ========================================
	// 4. 애니메이션 재생 (멀티캐스트)
	// ========================================

	int32 TotalAmount = (bMaterial1Valid ? Request.Material1Amount : 0) + (bMaterial2Valid ? Request.Material2Amount : 0);
	FVector OwnerLocation = GetOwner()->GetActorLocation();

	Multicast_PlayRepairAnimation(OwnerLocation, TotalAmount);
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  🎬 애니메이션 재생 요청! 총 개수: %d"), TotalAmount);
#endif

	// ========================================
	// 5. 다음 요청 처리 (애니메이션 시간 후)
	// ========================================

	float AnimationDuration = MaterialInsertDelay * TotalAmount;
	FTimerHandle NextRequestTimerHandle;

	GetWorld()->GetTimerManager().SetTimer(
		NextRequestTimerHandle,
		this,
		&URepairComponent::ProcessNextRepairRequest,
		AnimationDuration,
		false
	);

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ⏱️ 다음 요청 처리 예약: %.2f초 후"), AnimationDuration);
	UE_LOG(LogTemp, Warning, TEXT("=== [ProcessNextRepairRequest] 완료 ==="));
#endif
}

// ========================================
// [내부 함수] 재료 유효성 검증
// ========================================

bool URepairComponent::ValidateMaterial(APlayerController* PlayerController, FGameplayTag MaterialTag, int32 Amount)
{
	// 1. AllowedMaterialTags에 포함되어 있는지 확인
	if (AllowedMaterialTags.Num() > 0 && !AllowedMaterialTags.Contains(MaterialTag))
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ 허용되지 않은 재료: %s"), *MaterialTag.ToString());
#endif
		return false;
	}

	// 2. PlayerController에서 Inventory Component 가져오기 (UInv_InventoryStatics 사용!)
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PlayerController);
	if (!InvComp)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ Inventory Component를 찾을 수 없음!"));
#endif
		return false;
	}

	// 3. 인벤토리에 재료가 충분한지 확인
	int32 AvailableAmount = InvComp->GetTotalMaterialCount(MaterialTag);
	if (AvailableAmount < Amount)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ 재료 부족! 필요: %d, 보유: %d"), Amount, AvailableAmount);
#endif
		return false;
	}

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ✅ 재료 유효성 검증 통과! %s x %d (보유: %d)"), *MaterialTag.ToString(), Amount, AvailableAmount);
#endif
	return true;
}

// ========================================
// [내부 함수] 인벤토리에서 재료 소비
// ========================================

void URepairComponent::ConsumeMaterialFromInventory(APlayerController* PlayerController, FGameplayTag MaterialTag, int32 Amount)
{
	// PlayerController에서 Inventory Component 가져오기 (UInv_InventoryStatics 사용!)
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PlayerController);
	if (!InvComp)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ ConsumeMaterial: InventoryComponent를 찾을 수 없음!"));
#endif
		return;
	}

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  🔧 인벤토리에서 재료 소비 시작: %s x %d"), *MaterialTag.ToString(), Amount);
#endif
	
	// ⭐ Component는 GetOwner()->HasAuthority()로 체크!
	AActor* Owner = GetOwner();
	bool bIsServer = Owner && Owner->HasAuthority();
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("      서버 여부: %s"), bIsServer ? TEXT("서버 ✅") : TEXT("클라이언트 ❌"));
#endif

	// ⭐ 소비 전 보유량 확인 (로그용)
	int32 BeforeAmount = InvComp->GetTotalMaterialCount(MaterialTag);
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("      소비 전 보유량: %d"), BeforeAmount);
#endif

	// ⭐ Inventory의 Server RPC 호출 (FastArray 리플리케이션 자동 실행!)
	InvComp->Server_ConsumeMaterialsMultiStack(MaterialTag, Amount);

	// ⭐ 소비 후 보유량 확인 (로그용) - 서버에서만 즉시 반영됨
	if (bIsServer)
	{
#if HELLUNA_DEBUG_REPAIR
		int32 AfterAmount = InvComp->GetTotalMaterialCount(MaterialTag);
		UE_LOG(LogTemp, Warning, TEXT("      소비 후 보유량: %d"), AfterAmount);
		UE_LOG(LogTemp, Warning, TEXT("      실제 소비량: %d"), BeforeAmount - AfterAmount);
#endif
	}

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ✅ 재료 소비 RPC 호출 완료! (FastArray 리플리케이션 자동 실행 예정)"));
#endif
}

// ========================================
// [내부 함수] SpaceShip에 자원 추가
// ========================================

void URepairComponent::AddResourceToTarget(int32 TotalResource)
{
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [AddResourceToTarget] 호출됨! ==="));
	UE_LOG(LogTemp, Warning, TEXT("  추가할 자원: %d"), TotalResource);
#endif

	// Owner가 SpaceShip인지 확인
	AResourceUsingObject_SpaceShip* SpaceShip = Cast<AResourceUsingObject_SpaceShip>(GetOwner());
	if (!SpaceShip)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ Owner가 SpaceShip이 아님! Owner: %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("nullptr"));
#endif
		return;
	}

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ✅ SpaceShip 찾음: %s"), *SpaceShip->GetName());
	UE_LOG(LogTemp, Warning, TEXT("  🔧 AddRepairResource(%d) 호출 전"), TotalResource);
#endif
	
	// SpaceShip에 자원 추가 (실제 추가된 양 반환)
	int32 ActualAdded = SpaceShip->AddRepairResource(TotalResource);

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  🔧 AddRepairResource 호출 후! 실제 추가: %d"), ActualAdded);
	UE_LOG(LogTemp, Warning, TEXT("  📊 현재 수리 진행도: %d / %d"), SpaceShip->GetCurrentResource(), SpaceShip->GetNeedResource());
	UE_LOG(LogTemp, Warning, TEXT("=== [AddResourceToTarget] 완료! ==="));
#endif
}

// ========================================
// [멀티캐스트] 단일 이펙트/사운드 재생
// ========================================

void URepairComponent::Multicast_PlaySingleRepairEffect_Implementation(FVector RepairLocation)
{
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [Multicast_PlaySingleRepairEffect] 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("  위치: %s"), *RepairLocation.ToString());
#endif

	// 파티클 이펙트 재생 (1회)
	if (RepairParticleEffect)
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			RepairParticleEffect,
			RepairLocation
		);
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  🎨 파티클 이펙트 재생!"));
#endif
	}

	// 3D 사운드 재생 (1회)
	if (RepairSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			GetWorld(),
			RepairSound,
			RepairLocation,
			1.0f,  // VolumeMultiplier
			1.0f,  // PitchMultiplier
			0.0f,  // StartTime
			nullptr,  // AttenuationSettings (3D 사운드 자동 적용)
			nullptr,  // ConcurrencySettings
			nullptr   // InitialOwner
		);
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  🔊 3D 사운드 재생!"));
#endif
	}

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [Multicast_PlaySingleRepairEffect] 완료 ==="));
#endif
}

// ========================================
// [멀티캐스트] 애니메이션 재생
// ========================================

void URepairComponent::Multicast_PlayRepairAnimation_Implementation(FVector RepairLocation, int32 TotalAmount)
{
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [Multicast_PlayRepairAnimation] 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("  위치: %s, 개수: %d"), *RepairLocation.ToString(), TotalAmount);
#endif

	// 애니메이션 설정
	AnimationLocation = RepairLocation;
	CurrentAnimationCount = 0;
	TargetAnimationCount = TotalAmount;

	// 타이머 시작
	if (TargetAnimationCount > 0)
	{
		GetWorld()->GetTimerManager().SetTimer(
			AnimationTimerHandle,
			this,
			&URepairComponent::PlayMaterialInsertAnimationStep,
			MaterialInsertDelay,
			true  // 반복
		);
	}
}

// ========================================
// [내부 함수] 애니메이션 단계별 재생
// ========================================

void URepairComponent::PlayMaterialInsertAnimationStep()
{
	if (CurrentAnimationCount >= TargetAnimationCount)
	{
		// 애니메이션 완료
		GetWorld()->GetTimerManager().ClearTimer(AnimationTimerHandle);
		OnAnimationComplete();
		return;
	}

	CurrentAnimationCount++;

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Log, TEXT("  🎬 애니메이션 재생: %d/%d"), CurrentAnimationCount, TargetAnimationCount);
#endif

	// 파티클 이펙트 재생
	if (RepairParticleEffect)
	{
		UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			RepairParticleEffect,
			AnimationLocation
		);
	}

	// 사운드 재생
	if (RepairSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			GetWorld(),
			RepairSound,
			AnimationLocation
		);
	}
}

// ========================================
// [내부 함수] 애니메이션 완료
// ========================================

void URepairComponent::OnAnimationComplete()
{
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ✅ 애니메이션 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("=== [Multicast_PlayRepairAnimation] 완료 ==="));
#endif
}

// ========================================
// [Public Functions]
// ========================================

bool URepairComponent::IsMaterialAllowed(FGameplayTag MaterialTag) const
{
	// AllowedMaterialTags가 비어있으면 모든 재료 허용
	if (AllowedMaterialTags.Num() == 0)
		return true;

	return AllowedMaterialTags.Contains(MaterialTag);
}

// ========================================
// [Public Functions] 재료 표시 이름 가져오기
// ========================================

FText URepairComponent::GetMaterialDisplayName(int32 MaterialIndex) const
{
	if (MaterialIndex == 1)
	{
		return Material1DisplayName;
	}
	else if (MaterialIndex == 2)
	{
		return Material2DisplayName;
	}

	return FText::FromString(TEXT("알 수 없는 재료"));
}

// ========================================
// [테스트용 단순 재료 소비]
// ========================================

void URepairComponent::Server_TestConsumeMaterial_Implementation(APlayerController* PlayerController, FGameplayTag MaterialTag, int32 Amount)
{
#if UE_BUILD_SHIPPING
	// 프로덕션(Shipping) 빌드에서는 테스트 RPC 무효화
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("[RepairComponent] Server_TestConsumeMaterial: Shipping 빌드에서 차단"));
#endif
	return;
#endif

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [Server_TestConsumeMaterial] 테스트 시작 ==="));
	UE_LOG(LogTemp, Warning, TEXT("  PlayerController: %s"), PlayerController ? *PlayerController->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("  MaterialTag: %s"), *MaterialTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  Amount: %d"), Amount);
#endif

	// ⭐ Component는 GetOwner()->HasAuthority()로 체크!
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ 서버가 아님!"));
#endif
		return;
	}

	if (!PlayerController)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ PlayerController가 nullptr!"));
#endif
		return;
	}

	// InventoryComponent 가져오기
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PlayerController);
	if (!InvComp)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent를 찾을 수 없음!"));
#endif
		return;
	}

	// 소비 전 보유량
	int32 BeforeAmount = InvComp->GetTotalMaterialCount(MaterialTag);
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  📦 소비 전 보유량: %d"), BeforeAmount);
#endif

	if (BeforeAmount < Amount)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ 재료 부족! 필요: %d, 보유: %d"), Amount, BeforeAmount);
#endif
		return;
	}

	// ⭐ 재료 소비 RPC 호출
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  🔧 Server_ConsumeMaterialsMultiStack() 호출..."));
#endif
	InvComp->Server_ConsumeMaterialsMultiStack(MaterialTag, Amount);

	// 소비 후 보유량 (서버에서만 즉시 확인 가능)
	int32 AfterAmount = InvComp->GetTotalMaterialCount(MaterialTag);
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  📦 소비 후 보유량: %d"), AfterAmount);
	UE_LOG(LogTemp, Warning, TEXT("  ✅ 실제 소비량: %d"), BeforeAmount - AfterAmount);

	UE_LOG(LogTemp, Warning, TEXT("=== [Server_TestConsumeMaterial] 완료 ==="));
#endif
}

// ========================================
// [델리게이트 바인딩] 모든 플레이어 Inventory 감지
// ========================================

void URepairComponent::BindToAllPlayerInventories()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return; // 서버에서만 실행
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// 모든 PlayerController 찾기
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		// InventoryComponent 가져오기
		UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
		if (!IsValid(InvComp)) continue;

		// 이미 바인딩되어 있으면 스킵
		if (BoundInventoryComponents.Contains(InvComp))
		{
			continue;
		}

		// ⭐ OnMaterialStacksChanged 델리게이트 바인딩!
		if (!InvComp->OnMaterialStacksChanged.IsAlreadyBound(this, &URepairComponent::OnMaterialConsumed))
		{
			InvComp->OnMaterialStacksChanged.AddDynamic(this, &URepairComponent::OnMaterialConsumed);
			BoundInventoryComponents.Add(InvComp);

#if HELLUNA_DEBUG_REPAIR
			UE_LOG(LogTemp, Warning, TEXT("[RepairComponent] ✅ InventoryComponent 델리게이트 바인딩 완료! (Player: %s)"),
				*PC->GetName());
#endif
		}
	}
}

// ========================================
// [델리게이트 콜백] 재료 소비 감지
// ========================================

void URepairComponent::OnMaterialConsumed(const FGameplayTag& MaterialTag)
{
	// 서버에서만 실행
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [RepairComponent] OnMaterialConsumed 호출됨! ==="));
	UE_LOG(LogTemp, Warning, TEXT("  재료 Tag: %s"), *MaterialTag.ToString());
#endif

	// ⭐ 허용된 재료인지 체크
	if (!IsMaterialAllowed(MaterialTag))
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ 허용되지 않은 재료입니다. 스킵"));
#endif
		return;
	}

	// ⭐⭐⭐ 문제: 재료가 얼마나 소비되었는지 알 수 없음!
	// 해결책: RepairMaterialWidget에서 재료 차감 시 개수도 함께 전달해야 함
	// 일단은 1:1로 처리 (MaterialToResourceRatio 적용)
	int32 ResourceToAdd = MaterialToResourceRatio;

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  🔧 SpaceShip에 자원 추가: +%d"), ResourceToAdd);
#endif

	// SpaceShip에 자원 추가
	AddResourceToTarget(ResourceToAdd);

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [RepairComponent] OnMaterialConsumed 완료! ==="));
#endif
}

// ========================================
// [Server RPC] 재료로부터 자원 추가
// ========================================

void URepairComponent::Server_AddRepairResourceFromMaterials_Implementation(int32 TotalResource)
{
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("=== [Server_AddRepairResourceFromMaterials] 호출됨! ==="));
	UE_LOG(LogTemp, Warning, TEXT("  추가할 자원: %d"), TotalResource);
	UE_LOG(LogTemp, Warning, TEXT("  서버 여부: %s"), GetOwner()->HasAuthority() ? TEXT("서버 ✅") : TEXT("클라이언트 ❌"));
#endif

	// 서버 권한 체크
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Error, TEXT("  ❌ 서버가 아님!"));
#endif
		return;
	}

	// 자원이 0 이하면 무시
	if (TotalResource <= 0)
	{
#if HELLUNA_DEBUG_REPAIR
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ 자원이 0 이하! 무시"));
#endif
		return;
	}

	// MaterialToResourceRatio 적용
	int32 FinalResource = TotalResource * MaterialToResourceRatio;
#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  MaterialToResourceRatio 적용: %d x %d = %d"),
		TotalResource, MaterialToResourceRatio, FinalResource);
#endif

	// SpaceShip에 자원 추가
	AddResourceToTarget(FinalResource);

#if HELLUNA_DEBUG_REPAIR
	UE_LOG(LogTemp, Warning, TEXT("  ✅ SpaceShip에 자원 추가 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("=== [Server_AddRepairResourceFromMaterials] 완료! ==="));
#endif
}
