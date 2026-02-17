// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/HellunaHeroCharacter.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputSubsystems.h"
#include "DataAsset/DataAsset_InputConfig.h"
#include "Conponent/HellunaInputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "HellunaGameplayTags.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "DataAsset/DataAsset_HeroStartUpData.h"
#include "Conponent/HeroCombatComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Component/RepairComponent.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Character/HeroComponent/Helluna_FindResourceComponent.h"
#include "Net/UnrealNetwork.h"
#include "Weapon/HeroWeapon_GunBase.h"
// ⭐ [WeaponBridge] 추가
#include "Component/WeaponBridgeComponent.h"
// ⭐ [Phase 4 개선] EndPlay 인벤토리 저장용
#include "Player/HellunaPlayerState.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData
// ⭐ [Phase 6 Fix] 맵 이동 중 저장 스킵용
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"

#include "DebugHelper.h"


AHellunaHeroCharacter::AHellunaHeroCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 200.f;
	CameraBoom->SocketOffset = FVector(0.f, 55.f, 65.f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 400.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	HeroCombatComponent = CreateDefaultSubobject<UHeroCombatComponent>(TEXT("HeroCombatComponent"));

	FindResourceComponent = CreateDefaultSubobject<UHelluna_FindResourceComponent>(TEXT("파밍 자원 찾기 컴포넌트"));


	// ============================================
	// ⭐ [WeaponBridge] Inventory 연동 컴포넌트 생성
	// ============================================
	WeaponBridgeComponent = CreateDefaultSubobject<UWeaponBridgeComponent>(TEXT("WeaponBridgeComponent"));
}

void AHellunaHeroCharacter::BeginPlay()
{
	Super::BeginPlay();

}

// ============================================
// ⭐ [Phase 4 개선] EndPlay - 인벤토리 자동 저장
// ============================================
// 
// 📌 호출 시점:
//    - 플레이어 연결 끊김 (Logout 전!)
//    - 캐릭터 사망
//    - 맵 이동 (SeamlessTravel)
// 
// 📌 목적:
//    - Pawn이 파괴되기 전에 인벤토리 저장
//    - Logout()에서는 Pawn이 이미 nullptr이 됨
// 
// ============================================
void AHellunaHeroCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [HeroCharacter] EndPlay - 인벤토리 저장 시도               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Character: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ EndPlayReason: %d"), (int32)EndPlayReason);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// 서버에서만 저장 처리
	if (HasAuthority())
	{
		// ⭐ [Phase 6 Fix] 맵 이동 중이면 저장 스킵 (SaveAllPlayersInventory에서 이미 저장했음!)
		UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld()));
		if (GI && GI->bIsMapTransitioning)
		{
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ 맵 이동 중! SaveAllPlayersInventory에서 이미 저장했으므로 스킵"));
			Super::EndPlay(EndPlayReason);
			return;
		}
		
		// ⭐ PlayerController에서 InventoryComponent 찾기 (Character가 아님!)
		APlayerController* PC = Cast<APlayerController>(GetController());
		UInv_InventoryComponent* InvComp = PC ? PC->FindComponentByClass<UInv_InventoryComponent>() : nullptr;
		
		if (InvComp)
		{
			// PlayerController에서 PlayerId 가져오기
			AHellunaPlayerState* PS = PC ? PC->GetPlayerState<AHellunaPlayerState>() : nullptr;
			FString PlayerId = PS ? PS->GetPlayerUniqueId() : TEXT("");

			if (!PlayerId.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ PlayerId: %s"), *PlayerId);
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ InventoryComponent 발견! 직접 저장 시작..."));

				// 인벤토리 데이터 수집
				TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();

				// GameMode 가져와서 저장 요청
				AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
				if (GM)
				{
					GM->SaveCollectedItems(PlayerId, CollectedItems);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[EndPlay] ❌ GameMode를 찾을 수 없음!"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ PlayerId가 비어있음 (저장 생략)"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ InventoryComponent 없음 (PC: %s)"), 
				PC ? TEXT("Valid") : TEXT("nullptr"));
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AHellunaHeroCharacter::Input_Move(const FInputActionValue& InputActionValue)
{
	if (bMoveInputLocked)
	{
		return;
	}

	const FVector2D MovementVector = InputActionValue.Get<FVector2D>();

	const FRotator MovementRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);

	if (MovementVector.Y != 0.f)
	{
		const FVector ForwardDirection = MovementRotation.RotateVector(FVector::ForwardVector);

		AddMovementInput(ForwardDirection, MovementVector.Y);
	}

	if (MovementVector.X != 0.f)
	{
		const FVector RightDirection = MovementRotation.RotateVector(FVector::RightVector);

		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AHellunaHeroCharacter::Input_Look(const FInputActionValue& InputActionValue)
{
	// ✅ [추가] 락 중이면 룩 입력 무시
	if (bLookInputLocked)
	{
		return;
	}


	const FVector2D LookAxisVector = InputActionValue.Get<FVector2D>();

	float SensitivityScale = 1.f;

	const float DefaultFov = 120.f;  
	const float AimFov = GetFollowCamera()->FieldOfView;  

	SensitivityScale = AimFov / DefaultFov; 

	if (LookAxisVector.X != 0.f)
	{
		AddControllerYawInput(LookAxisVector.X * SensitivityScale);
	}

	if (LookAxisVector.Y != 0.f)
	{
		AddControllerPitchInput(LookAxisVector.Y * SensitivityScale);
	}

}


void AHellunaHeroCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// ============================================
	// ⭐ 디버깅: 입력 바인딩 상태 확인
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [HeroCharacter] SetupPlayerInputComponent              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 캐릭터: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocallyControlled: %s"), IsLocallyControlled() ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE (서버)") : TEXT("FALSE (클라이언트)"));
	UE_LOG(LogTemp, Warning, TEXT("║ GetLocalRole: %d"), (int32)GetLocalRole());
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), GetController() ? *GetController()->GetName() : TEXT("nullptr"));
	
	if (APlayerController* PC = GetController<APlayerController>())
	{
		UE_LOG(LogTemp, Warning, TEXT("║ PC->IsLocalController: %s"), PC->IsLocalController() ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
		UE_LOG(LogTemp, Warning, TEXT("║ PC->GetLocalPlayer: %s"), PC->GetLocalPlayer() ? TEXT("Valid") : TEXT("nullptr"));
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// ============================================
	// ⭐ 로컬에서 제어하는 캐릭터만 입력 바인딩!
	// ⭐ 서버에서 클라이언트 캐릭터에 잘못 바인딩되는 것 방지
	// ============================================
	if (!IsLocallyControlled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[HeroCharacter] 입력 바인딩 스킵 - 로컬 캐릭터 아님"));
		return;
	}
	
	checkf(InputConfigDataAsset, TEXT("InputConfigDataAsset이 설정되지 않았습니다!"));

	ULocalPlayer* LocalPlayer = GetController<APlayerController>()->GetLocalPlayer();

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);

	check(Subsystem);

	Subsystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext, 0);

	UHellunaInputComponent* HellunaInputComponent = CastChecked<UHellunaInputComponent>(PlayerInputComponent);

	HellunaInputComponent->BindNativeInputAction(InputConfigDataAsset, HellunaGameplayTags::InputTag_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Move);
	HellunaInputComponent->BindNativeInputAction(InputConfigDataAsset, HellunaGameplayTags::InputTag_Look, ETriggerEvent::Triggered, this, &ThisClass::Input_Look);

	HellunaInputComponent->BindAbilityInputAction(InputConfigDataAsset, this, &ThisClass::Input_AbilityInputPressed, &ThisClass::Input_AbilityInputReleased);
}

void AHellunaHeroCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (!CharacterStartUpData.IsNull())
	{
		if (UDataAsset_BaseStartUpData* LoadedData = CharacterStartUpData.LoadSynchronous())
		{
			LoadedData->GiveToAbilitySystemComponent(HellunaAbilitySystemComponent);
		}
	}
}

void AHellunaHeroCharacter::Input_AbilityInputPressed(FGameplayTag InInputTag)
{
	// ============================================
	// 🔍 [디버깅] 입력 처리 추적
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎮 [HeroCharacter] Input_AbilityInputPressed 호출           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 캐릭터: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ InputTag: %s"), *InInputTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocallyControlled: %s"), IsLocallyControlled() ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE (서버)") : TEXT("FALSE (클라)"));
	UE_LOG(LogTemp, Warning, TEXT("║ ASC 유효: %s"), HellunaAbilitySystemComponent ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));

	if (HellunaAbilitySystemComponent)
	{	
		HellunaAbilitySystemComponent->OnAbilityInputPressed(InInputTag);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⛔ [HeroCharacter] ASC가 nullptr!"));
	}
}

void AHellunaHeroCharacter::Input_AbilityInputReleased(FGameplayTag InInputTag)
{

	if (HellunaAbilitySystemComponent)
	{
		HellunaAbilitySystemComponent->OnAbilityInputReleased(InInputTag);
	}

}

// ⭐ SpaceShip 수리 Server RPC (재료 개별 전달)
void AHellunaHeroCharacter::Server_RepairSpaceShip_Implementation(FGameplayTag Material1Tag, int32 Material1Amount, FGameplayTag Material2Tag, int32 Material2Amount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== [HeroCharacter::Server_RepairSpaceShip] 호출됨! ==="));
	UE_LOG(LogTemp, Warning, TEXT("  재료 1: %s x %d"), *Material1Tag.ToString(), Material1Amount);
	UE_LOG(LogTemp, Warning, TEXT("  재료 2: %s x %d"), *Material2Tag.ToString(), Material2Amount);
	UE_LOG(LogTemp, Warning, TEXT("  서버 여부: %s"), HasAuthority() ? TEXT("서버 ✅") : TEXT("클라이언트 ❌"));

	// 서버 권한 체크
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ 서버가 아님!"));
		return;
	}

	// 총 자원 계산
	int32 TotalResource = Material1Amount + Material2Amount;

	// 자원이 0 이하면 무시
	if (TotalResource <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ 자원이 0 이하! 무시"));
		return;
	}

	// World에서 "SpaceShip" 태그를 가진 Actor 찾기
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName("SpaceShip"), FoundActors);

	if (FoundActors.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ SpaceShip을 찾을 수 없음! 'SpaceShip' 태그 확인 필요"));
		return;
	}

	// SpaceShip 찾음
	if (AResourceUsingObject_SpaceShip* SpaceShip = Cast<AResourceUsingObject_SpaceShip>(FoundActors[0]))
	{
		UE_LOG(LogTemp, Warning, TEXT("  ✅ SpaceShip 찾음: %s"), *SpaceShip->GetName());
		
		// ⭐ RepairComponent 가져오기
		URepairComponent* RepairComp = SpaceShip->FindComponentByClass<URepairComponent>();
		if (RepairComp)
		{
			UE_LOG(LogTemp, Warning, TEXT("  ✅ RepairComponent 찾음!"));
			
			// ⭐ 애니메이션/사운드를 **한 번만** 재생 (멀티캐스트)
			FVector SpaceShipLocation = SpaceShip->GetActorLocation();
			RepairComp->Multicast_PlaySingleRepairEffect(SpaceShipLocation);
			UE_LOG(LogTemp, Warning, TEXT("  🎬 애니메이션/사운드 한 번 재생 요청!"));
		}
		
		// ⭐⭐⭐ SpaceShip에 자원 추가 (실제 추가된 양 반환)
		int32 ActualAdded = SpaceShip->AddRepairResource(TotalResource);
		UE_LOG(LogTemp, Warning, TEXT("  📊 SpaceShip->AddRepairResource(%d) 호출 → 실제 추가: %d"), TotalResource, ActualAdded);

		// ⭐⭐⭐ 실제 추가된 양만큼만 인벤토리에서 차감!
		if (ActualAdded > 0)
		{
			// ⭐ PlayerController 가져오기
			APlayerController* PC = Cast<APlayerController>(GetController());
			if (!PC)
			{
				UE_LOG(LogTemp, Error, TEXT("  ❌ PlayerController를 찾을 수 없음!"));
				return;
			}

			// ⭐ InventoryComponent 가져오기 (Statics 사용!)
			UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PC);
			if (!InvComp)
			{
				UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent를 찾을 수 없음!"));
				return;
			}

			UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryComponent 찾음!"));

			// 실제 차감량 계산 (비율로 분배)
			int32 ActualMaterial1 = 0;
			int32 ActualMaterial2 = 0;

			if (TotalResource > 0)
			{
				// 비율 계산: (요청량 / 총량) * 실제추가량
				float Ratio1 = (float)Material1Amount / (float)TotalResource;
				float Ratio2 = (float)Material2Amount / (float)TotalResource;

				ActualMaterial1 = FMath::RoundToInt(Ratio1 * ActualAdded);
				ActualMaterial2 = ActualAdded - ActualMaterial1; // 나머지는 재료2에

				UE_LOG(LogTemp, Warning, TEXT("  📊 비율 계산:"));
				UE_LOG(LogTemp, Warning, TEXT("    - 재료1 비율: %.2f → 차감: %d"), Ratio1, ActualMaterial1);
				UE_LOG(LogTemp, Warning, TEXT("    - 재료2 비율: %.2f → 차감: %d"), Ratio2, ActualMaterial2);
			}

			// 재료 1 차감
			if (ActualMaterial1 > 0 && Material1Tag.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("  🧪 재료 1 차감: %s x %d"), *Material1Tag.ToString(), ActualMaterial1);
				InvComp->Server_ConsumeMaterialsMultiStack(Material1Tag, ActualMaterial1);
			}

			// 재료 2 차감
			if (ActualMaterial2 > 0 && Material2Tag.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("  🧪 재료 2 차감: %s x %d"), *Material2Tag.ToString(), ActualMaterial2);
				InvComp->Server_ConsumeMaterialsMultiStack(Material2Tag, ActualMaterial2);
			}

			UE_LOG(LogTemp, Warning, TEXT("  ✅ 실제 차감 완료! 총 차감: %d"), ActualAdded);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("  ⚠️ SpaceShip에 추가된 자원이 없음! (이미 만원일 수 있음)"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ SpaceShip 캐스팅 실패!"));
	}

	UE_LOG(LogTemp, Warning, TEXT("=== [HeroCharacter::Server_RepairSpaceShip] 완료! ==="));
}

// ============================================================================
// 서버 RPC: 손에 드는 무기(손 무기)를 스폰해서 지정 소켓에 부착한다.
// - 서버에서만 스폰/부착을 수행하고, 무기 태그(WeaponTag)는 ASC(AbilitySystemComponent)에 반영한다.
// - 기존 무기를 파괴하지 않는 구조(등/허리 슬롯 등 다른 시스템에서 관리 가능).
// - EquipMontage는 서버에서 멀티캐스트로 "소유자 제외" 재생을 요청한다.
// ============================================================================

void AHellunaHeroCharacter::Server_RequestSpawnWeapon_Implementation(
	TSubclassOf<AHellunaHeroWeapon> InWeaponClass,
	FName InAttachSocket,
	UAnimMontage* EquipMontage)
{
	// 서버에서만 실행 (권한 없는 클라가 직접 실행 못 함)
	if (!HasAuthority())
	{
		return;
	}


	// 다른 클라이언트들에게만 장착 애니 재생(소유자 제외)
	// - 소유자는 로컬에서 이미 처리하거나, 별도 흐름에서 재생할 수 있음
	Multicast_PlayEquipMontageExceptOwner(EquipMontage);

	// 스폰할 무기 클래스가 없으면 종료
	if (!InWeaponClass)
	{
		return;
	}

	// 캐릭터 메쉬가 없으면 소켓 부착이 불가하므로 종료
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh)
	{
		return;
	}

	// 소켓 유효성 검사(없거나 이름이 None이면 부착 불가)
	if (InAttachSocket.IsNone() || !CharacterMesh->DoesSocketExist(InAttachSocket))
	{
		return;
	}

	// ------------------------------------------------------------------------
	// ✅ ASC(AbilitySystemComponent) 연동 여부 확인
	// - 테스트/안전성 목적: ASC가 없더라도 "무기 스폰/부착 자체"는 진행 가능하게 함.
	// - 단, ASC가 없으면 무기태그(LooseGameplayTag) 반영은 스킵.
	// ------------------------------------------------------------------------
	UHellunaAbilitySystemComponent* ASC = GetHellunaAbilitySystemComponent();
	const bool bHasASC = (ASC != nullptr);

	// 기존 손 무기(현재 무기)의 태그를 가져온다.
	// - 태그 교체(Old 제거 + New 추가) 목적
	AHellunaHeroWeapon* OldWeapon = GetCurrentWeapon();
	const FGameplayTag OldTag = (bHasASC && IsValid(OldWeapon)) ? OldWeapon->GetWeaponTag() : FGameplayTag();

	if (IsValid(OldWeapon))
	{
		SaveCurrentMagByClass(CurrentWeapon);
		OldWeapon->Destroy();
		SetCurrentWeapon(nullptr);            // SetCurrentWeapon이 nullptr 허용해야 함
		// CurrentWeaponTag는 아래 NewTag 세팅에서 갱신되거나,
		// 스폰 실패 시 아래 실패 처리에서 비워짐.
	}

	// ------------------------------------------------------------------------
	// 새 무기 스폰
	// - 스폰 위치/회전은 소켓 트랜스폼을 사용(부착 직후 Snap 규칙이라 큰 의미는 없지만,
	//   초기 스폰 안정성/디버그에 유리)
	// ------------------------------------------------------------------------
	const FTransform SocketTM = CharacterMesh->GetSocketTransform(InAttachSocket);

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHellunaHeroWeapon* NewWeapon = GetWorld()->SpawnActor<AHellunaHeroWeapon>(InWeaponClass, SocketTM, Params);
	if (!IsValid(NewWeapon))
	{
		// 스폰 실패 시:
		// - ASC가 있으면 "기존 태그만 제거"하고 상태를 초기화
		// - ASC가 없으면 태그 처리 자체를 하지 않고 종료
		if (bHasASC)
		{
			ApplyTagToASC(OldTag, FGameplayTag());
			CurrentWeaponTag = FGameplayTag();
			LastAppliedWeaponTag = FGameplayTag();
		}
		return;
	}

	// 새 무기를 메쉬 소켓에 부착(Snap)
	NewWeapon->AttachToComponent(
		CharacterMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		InAttachSocket
	);

	// 현재 손에 든 무기 포인터 갱신
	SetCurrentWeapon(NewWeapon);

	ApplySavedCurrentMagByClass(NewWeapon); // [ADD]

	// ------------------------------------------------------------------------
	// ✅ 무기 태그 처리(ASC가 있을 때만)
	// - OldTag 제거, NewTag 추가
	// - CurrentWeaponTag는 복제 변수로 가정(클라에서 OnRep로 태그 반영)
	// ------------------------------------------------------------------------
	if (bHasASC)
	{
		const FGameplayTag NewTag = NewWeapon->GetWeaponTag();
		ApplyTagToASC(OldTag, NewTag);

		// 서버에서 현재 태그 갱신 → 클라에서 OnRep_CurrentWeaponTag()로 반영
		CurrentWeaponTag = NewTag;
	}


	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true);


	// 네트워크 업데이트 힌트(즉시 반영에 도움)
	NewWeapon->ForceNetUpdate();
	ForceNetUpdate();
}



// ============================================================================
// ASC에 무기 태그를 반영하는 공용 함수
// - LooseGameplayTag 방식(상태/장비 태그 토글용)
// - OldTag 제거 후 NewTag 추가
// - 즉시 반영을 위해 ForceReplication/ForceNetUpdate 호출
// ============================================================================

void AHellunaHeroCharacter::ApplyTagToASC(const FGameplayTag& OldTag, const FGameplayTag& NewTag)
{
	UHellunaAbilitySystemComponent* ASC = GetHellunaAbilitySystemComponent();
	if (!ASC)
		return;

	// 이전 무기 태그 제거
	if (OldTag.IsValid())
	{
		ASC->RemoveLooseGameplayTag(OldTag);
	}

	// 새 무기 태그 추가
	if (NewTag.IsValid())
	{
		ASC->AddLooseGameplayTag(NewTag);
	}

	

	// 태그 변경을 네트워크에 빠르게 반영(가능하면 도움)
	ASC->ForceReplication();
	ForceNetUpdate();
}


// ============================================================================
// RepNotify: CurrentWeaponTag가 클라이언트로 복제되었을 때 호출됨
// - 클라 측에서도 ASC 태그 상태를 서버와 동일하게 맞춰준다.
// - LastAppliedWeaponTag를 사용해 "이전 태그 제거 → 새 태그 추가"를 정확히 수행.
// ============================================================================

void AHellunaHeroCharacter::OnRep_CurrentWeaponTag()
{
	// 클라에서: 이전 태그 제거 + 새 태그 추가
	ApplyTagToASC(LastAppliedWeaponTag, CurrentWeaponTag);

	// 다음 OnRep에서 이전값을 알 수 있도록 캐시 갱신
	LastAppliedWeaponTag = CurrentWeaponTag;
}

void AHellunaHeroCharacter::Multicast_PlayEquipMontageExceptOwner_Implementation(UAnimMontage* Montage)
{
	if (!Montage) return;

	// ✅ 소유 클라이언트(=클라 A)는 GA가 이미 재생하니 스킵
	// OwningClient는 이 Pawn이 "자기 것"이면 IsLocallyControlled()가 true
	if (IsLocallyControlled())
	{
		return;
	}

	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh) return;

	UAnimInstance* AnimInst = CharacterMesh->GetAnimInstance();
	if (!AnimInst) return;

	PlayAnimMontage(Montage);
}

// ============================================
// ⭐ [WeaponBridge] 무기 제거 Server RPC
// ⭐ 클라이언트에서 호출 → 서버에서 CurrentWeapon Destroy
// ============================================
void AHellunaHeroCharacter::Server_RequestDestroyWeapon_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [HeroCharacter] Server_RequestDestroyWeapon 호출됨 (서버)"));



	if (IsValid(CurrentWeapon))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [HeroCharacter] CurrentWeapon Destroy: %s"), *CurrentWeapon->GetName());

		if (AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(CurrentWeapon))
		{
			SaveCurrentMagByClass(CurrentWeapon);
		}

		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}

	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [HeroCharacter] CurrentWeapon이 이미 null"));
	}

	//== 김민우 수정(디스트로이 웨폰을 할 때 무기 태그 제거) ==
	if (CurrentWeaponTag.IsValid())
	{
		ApplyTagToASC(CurrentWeaponTag, FGameplayTag());  // Old 제거, New 없음
		LastAppliedWeaponTag = CurrentWeaponTag;          // (서버 쪽 캐시가 필요하면 유지)
		CurrentWeaponTag = FGameplayTag();                // ✅ 클라로 "태그 비워짐" 복제
	}

}

void AHellunaHeroCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const //서버에서 클라로 복제
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHellunaHeroCharacter, CurrentWeapon);
	DOREPLIFETIME(AHellunaHeroCharacter, CurrentWeaponTag);
}



void AHellunaHeroCharacter::LockMoveInput()
{
	if (bMoveInputLocked)
	{
		return;
	}

	bMoveInputLocked = true;

	// 1) 앞으로 들어오는 이동 입력 무시
	if (AController* C = GetController())
	{
		C->SetIgnoreMoveInput(true);
	}

	// 2) 이미 쌓인 이동 입력 제거
	// - 엔진 버전에 따라 ClearPendingMovementInputVector()가 없을 수 있어서
	//   ConsumeMovementInputVector()를 함께 사용 (호출 자체는 안전)
	ConsumeMovementInputVector();

	// 3) 현재 속도/가속 즉시 정지 (미끄러짐 방지)
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}
}

// ✅ [추가] 이동 입력 잠금 해제
void AHellunaHeroCharacter::UnlockMoveInput()
{
	if (!bMoveInputLocked)
	{
		return;
	}

	bMoveInputLocked = false;

	if (AController* C = GetController())
	{
		C->SetIgnoreMoveInput(false);
	}
}

void AHellunaHeroCharacter::LockLookInput()
{
	if (bLookInputLocked)
	{
		return;
	}

	bLookInputLocked = true;

	if (!Controller)
	{
		return;
	}

	// 1) 현재 회전 캐싱
	CachedLockedControlRotation = Controller->GetControlRotation();

	// 2) 앞으로 들어오는 Look 입력 무시
	Controller->SetIgnoreLookInput(true);

	// 3) 락 걸리는 프레임에 이미 살짝 돌아간 것처럼 보이는 걸 방지 (즉시 복구)
	Controller->SetControlRotation(CachedLockedControlRotation);
}

// ✅ [추가] Look 입력 잠금 해제
void AHellunaHeroCharacter::UnlockLookInput()
{
	if (!bLookInputLocked)
	{
		return;
	}

	bLookInputLocked = false;

	if (Controller)
	{
		Controller->SetIgnoreLookInput(false);
	}
}

// 클라에서 실행되는 코드에서 다른 클라로 애니메이션 재생할 때 사용
void AHellunaHeroCharacter::Server_RequestPlayMontageExceptOwner_Implementation(UAnimMontage* Montage)
{
	Multicast_PlayEquipMontageExceptOwner(Montage);
}

void AHellunaHeroCharacter::SaveCurrentMagByClass(AHellunaHeroWeapon* Weapon)
{
	if (!HasAuthority()) return;
	if (!IsValid(Weapon)) return;

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Weapon);
	if (!Gun) return;

	TSubclassOf<AHellunaHeroWeapon> WeaponClass = Weapon->GetClass();
	if (!WeaponClass) return;

	SavedMagByWeaponClass.FindOrAdd(WeaponClass) = FMath::Clamp(Gun->CurrentMag, 0, Gun->MaxMag);
}

void AHellunaHeroCharacter::ApplySavedCurrentMagByClass(AHellunaHeroWeapon* Weapon)
{
	if (!HasAuthority()) return;
	if (!IsValid(Weapon)) return;

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Weapon);
	if (!Gun) return;

	TSubclassOf<AHellunaHeroWeapon> WeaponClass = Weapon->GetClass();
	if (!WeaponClass) return;

	const int32* SavedMag = SavedMagByWeaponClass.Find(WeaponClass);
	if (!SavedMag)
		return;

	Gun->CurrentMag = FMath::Clamp(*SavedMag, 0, Gun->MaxMag);
	Gun->ForceNetUpdate();
}