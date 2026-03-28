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
#include "GameMode/HellunaDefenseGameState.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData
// ⭐ [Phase 6 Fix] 맵 이동 중 저장 스킵용
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"

#include "Combat/MeleeTraceComponent.h"
#include "DebugHelper.h"
#include "Helluna.h"  // [Step3] HELLUNA_DEBUG_HERO 매크로 (EndPlay/Input/Weapon/Repair 디버그 로그)
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_GunParry.h"
#include "VFX/GhostTrailActor.h"
#include "Animation/AnimInstance.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"

#include "UI/Weapon/WeaponHUDWidget.h"
#include "UI/HUD/HellunaHealthHUDWidget.h"
#include "Blueprint/UserWidget.h"

#include "InventoryManagement/Components/Inv_LootContainerComponent.h"
#include "Items/Components/Inv_ItemComponent.h"  // [Step3] FindComponentByClass<UInv_ItemComponent> 완전한 타입 필요

#include "Components/WidgetComponent.h"
#include "Downed/HellunaReviveWidget.h"
#include "Downed/HellunaReviveProgressWidget.h"

// [Phase21-C] 다운 선혈 화면 효과
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

// [Phase18] 킥 3D 프롬프트 위젯
#include "HellunaFunctionLibrary.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Engine/OverlapResult.h"
#include "Widgets/HUD/Inv_InteractPromptWidget.h"
#include "Components/Image.h"



AHellunaHeroCharacter::AHellunaHeroCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// ⭐ 모든 캐릭터 BP가 UHellunaInputComponent를 사용하도록 보장
	// BP에서 개별 설정 누락 시 기본 UInputComponent → Cast 실패 → 입력 바인딩 스킵 버그 방지
	OverrideInputComponentClass = UHellunaInputComponent::StaticClass();

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 250.f;
	CameraBoom->SocketOffset = FVector(0.f, 60.f, 55.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 15.f;
	CameraBoom->CameraLagMaxDistance = 50.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 720.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 400.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	HeroCombatComponent = CreateDefaultSubobject<UHeroCombatComponent>(TEXT("HeroCombatComponent"));

	MeleeTraceComponent = CreateDefaultSubobject<UMeleeTraceComponent>(TEXT("MeleeTraceComponent"));

	FindResourceComponent = CreateDefaultSubobject<UHelluna_FindResourceComponent>(TEXT("파밍 자원 찾기 컴포넌트"));


	// ============================================
	// ⭐ [WeaponBridge] Inventory 연동 컴포넌트 생성
	// ============================================
	WeaponBridgeComponent = CreateDefaultSubobject<UWeaponBridgeComponent>(TEXT("WeaponBridgeComponent"));

	// ★ 추가: 플레이어 체력 컴포넌트
	HeroHealthComponent = CreateDefaultSubobject<UHellunaHealthComponent>(TEXT("HeroHealthComponent"));

	// Phase 9: 사체 루팅용 컨테이너 (비활성 상태로 생성, 사망 시 Activate)
	LootContainerComponent = CreateDefaultSubobject<UInv_LootContainerComponent>(TEXT("LootContainerComponent"));
	LootContainerComponent->bActivated = false;
	LootContainerComponent->bDestroyOwnerWhenEmpty = false;
	LootContainerComponent->ContainerDisplayName = FText::FromString(TEXT("사체"));

	// [Phase 21] 3D 부활 위젯 컴포넌트 (다운 시 머리 위 표시)
	ReviveWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("ReviveWidgetComp"));
	ReviveWidgetComp->SetupAttachment(GetMesh());
	ReviveWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 200.f)); // 캐릭터 머리 위
	ReviveWidgetComp->SetDrawSize(FVector2D(250.f, 80.f));
	ReviveWidgetComp->SetWidgetSpace(EWidgetSpace::Screen); // 항상 카메라를 바라봄
	ReviveWidgetComp->SetVisibility(false); // 기본 숨김
	ReviveWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// [Phase18] 킥 3D 프롬프트 위젯 컴포넌트 (Staggered 적 머리 위 표시)
	KickPromptWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("KickPromptWidgetComp"));
	KickPromptWidgetComp->SetupAttachment(GetRootComponent());
	KickPromptWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 200.f));
	KickPromptWidgetComp->SetDrawSize(FVector2D(200.f, 50.f));
	KickPromptWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	KickPromptWidgetComp->SetVisibility(false);
	KickPromptWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// [OTS Camera] 생성자 디버그 로그
	UE_LOG(LogTemp, Verbose, TEXT("[OTS Camera] Constructor — ArmLength=%.1f, SocketOffset=%s"),
		CameraBoom->TargetArmLength, *CameraBoom->SocketOffset.ToString());
}

void AHellunaHeroCharacter::BeginPlay()
{
	Super::BeginPlay();

	// ★ 추가: 서버에서만 피격/사망 델리게이트 바인딩
	if (HasAuthority() && HeroHealthComponent)
	{
		HeroHealthComponent->OnHealthChanged.AddUniqueDynamic(this, &AHellunaHeroCharacter::OnHeroHealthChanged);
		HeroHealthComponent->OnDeath.AddUniqueDynamic(this, &AHellunaHeroCharacter::OnHeroDeath);
		HeroHealthComponent->OnDowned.AddUniqueDynamic(this, &AHellunaHeroCharacter::OnHeroDowned);
	}

	// 로컬 플레이어 전용 무기 HUD 생성
	InitWeaponHUD();

	// [Phase 21] 3D 부활 위젯 클래스 설정 (데디케이티드 서버 제외)
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] BeginPlay: %s | NetMode=%d | ReviveWidgetComp=%s | ReviveWidgetClass=%s"),
		*GetName(),
		(int32)GetNetMode(),
		ReviveWidgetComp ? TEXT("Valid") : TEXT("NULL"),
		ReviveWidgetClass ? *ReviveWidgetClass->GetName() : TEXT("NULL"));
	if (GetNetMode() != NM_DedicatedServer && ReviveWidgetComp && ReviveWidgetClass)
	{
		ReviveWidgetComp->SetWidgetClass(ReviveWidgetClass);
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] BeginPlay: WidgetClass 설정 완료 → %s"), *ReviveWidgetClass->GetName());
	}
	else
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] BeginPlay: WidgetClass 설정 스킵! (DediServer=%s, Comp=%s, Class=%s)"),
			GetNetMode() == NM_DedicatedServer ? TEXT("Y") : TEXT("N"),
			ReviveWidgetComp ? TEXT("Y") : TEXT("N"),
			ReviveWidgetClass ? TEXT("Y") : TEXT("N"));
	}

	// ── OTS 카메라 기본값 캐싱 ──
	if (FollowCamera)
	{
		DefaultFOV = FollowCamera->FieldOfView;
	}
	if (CameraBoom)
	{
		DefaultTargetArmLength = CameraBoom->TargetArmLength;
		DefaultSocketOffset = CameraBoom->SocketOffset;
	}
	UE_LOG(LogTemp, Verbose, TEXT("[OTS Camera] BeginPlay — DefaultFOV=%.1f, DefaultArmLength=%.1f"),
		DefaultFOV, DefaultTargetArmLength);

	// [Phase18] 킥 프롬프트 3D 위젯 초기화 (클라이언트만)
	if (GetNetMode() != NM_DedicatedServer && KickPromptWidgetComp && KickPromptWidgetClass)
	{
		KickPromptWidgetComp->SetWidgetClass(KickPromptWidgetClass);

		// 위젯 인스턴스 캐시 + 텍스트 설정
		KickPromptWidgetInstance = KickPromptWidgetComp->GetWidget();
		if (UInv_InteractPromptWidget* Prompt = Cast<UInv_InteractPromptWidget>(KickPromptWidgetInstance))
		{
			Prompt->SetKeyText(TEXT("F"));
			Prompt->SetItemName(TEXT("처형"));
			Prompt->SetActionText(TEXT(""));
		}
		UE_LOG(LogHelluna, Log, TEXT("[Phase18] KickPrompt 위젯 초기화: %s"), *GetName());
	}

	// [Phase21] 피격 혈흔 전용 위젯 생성 (다운 오버레이와 분리)
	if (BloodHitWidgetClass)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC && PC->IsLocalController())
		{
			BloodHitWidget = CreateWidget<UUserWidget>(PC, BloodHitWidgetClass);
			if (BloodHitWidget)
			{
				BloodHitWidget->AddToViewport(1);

				// BloodHitImages 배열 초기화 — BloodHitWidget에서 가져옴
				BloodHitImages.SetNum(8);
				BloodHitImages[0] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodTop")));
				BloodHitImages[1] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerTR")));
				BloodHitImages[2] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodRight")));
				BloodHitImages[3] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerBR")));
				BloodHitImages[4] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodBottom")));
				BloodHitImages[5] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerBL")));
				BloodHitImages[6] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodLeft")));
				BloodHitImages[7] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerTL")));

				// 텍스처는 WBP에서 이미 할당됨 (T_Puzzle_FX_RedVignette)
				// BloodHitTexture 프로퍼티로 런타임 오버라이드도 가능
				if (BloodHitTexture)
				{
					for (UImage* Img : BloodHitImages)
					{
						if (Img)
						{
							FSlateBrush Brush;
							Brush.SetResourceObject(BloodHitTexture);
							Brush.DrawAs = ESlateBrushDrawType::Image;
							Brush.ImageSize = FVector2D(256, 256);
							Img->SetBrush(Brush);
							Img->SetColorAndOpacity(FLinearColor(1.0f, 0.1f, 0.05f, 1.0f));
							Img->SetRenderOpacity(0.f);
							Img->SetVisibility(ESlateVisibility::Collapsed);
						}
					}
				}

				const int32 ValidCount = BloodHitImages.FilterByPredicate([](UImage* I) { return I != nullptr; }).Num();
				UE_LOG(LogHelluna, Log, TEXT("[Phase21] BloodHitOverlay 위젯 생성 완료: %d/8 Images valid"), ValidCount);
			}
		}
	}
}

// ============================================================================
// Tick
// ============================================================================
void AHellunaHeroCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 카메라 줌 보간은 이제 GA의 AT_AimCameraInterp AbilityTask에서 처리

	// [Phase 21] 3D 부활 위젯 출혈 타이머 업데이트 (클라이언트)
	UpdateReviveWidgetBleedout();

	// [Phase 21] 부활 진행 HUD 업데이트 (부활 수행자 로컬)
	UpdateReviveProgressHUD();

	// [Phase21-C] 다운 선혈 화면 효과 업데이트
	if (bDownedEffectActive && IsLocallyControlled())
	{
		TickDownedScreenEffect(DeltaTime);
	}

	// [Phase18] 킥 프롬프트 업데이트 (로컬 플레이어만)
	if (IsLocallyControlled())
	{
		UpdateKickPrompt(DeltaTime);
	}
}

// ============================================================================
// SetCurrentWeapon - 무기 교체 시 WeaponHUD 갱신
// ============================================================================
void AHellunaHeroCharacter::SetCurrentWeapon(AHellunaHeroWeapon* NewWeapon)
{
	CurrentWeapon = NewWeapon;

	if (IsLocallyControlled() && WeaponHUDWidget)
	{
		WeaponHUDWidget->UpdateWeapon(NewWeapon);
	}

	// [HealthHUD] 주무기 아이콘 갱신
	if (IsLocallyControlled() && HealthHUDWidget)
	{
		HealthHUDWidget->UpdatePrimaryWeapon(NewWeapon);
	}
}

// ============================================================================
// OnRep_CurrentWeapon — 클라이언트에서 무기 복제 수신 시 HUD 갱신
// ============================================================================
void AHellunaHeroCharacter::OnRep_CurrentWeapon()
{
	if (!IsLocallyControlled()) return;

	// 클라이언트에서도 SavedMag 기준으로 탄약을 즉시 복원한다.
	// (서버의 OnRep 복제가 BeginPlay의 MaxMag 초기화보다 늦게 올 수 있어서
	//  클라이언트 자체적으로 저장된 값을 반영해 딜레이를 없앤다.)
	ApplySavedCurrentMagByClass(CurrentWeapon);

	if (WeaponHUDWidget)
	{
		WeaponHUDWidget->UpdateWeapon(CurrentWeapon);
	}

	// [HealthHUD] 주무기 아이콘 갱신 (클라이언트 복제 수신)
	if (HealthHUDWidget)
	{
		HealthHUDWidget->UpdatePrimaryWeapon(CurrentWeapon);
	}
}

// ============================================================================
// InitWeaponHUD - 로컬 플레이어 전용 HUD 생성 (DefenseGameState일 때만)
// ============================================================================
void AHellunaHeroCharacter::InitWeaponHUD()
{
	if (!IsLocallyControlled()) return;

	// GameState로 판단 (GameMode는 클라이언트에서 nullptr이므로 GameState 사용)
	if (!Cast<AHellunaDefenseGameState>(UGameplayStatics::GetGameState(GetWorld()))) return;

	if (WeaponHUDWidgetClass)
	{
		WeaponHUDWidget = CreateWidget<UWeaponHUDWidget>(GetWorld(), WeaponHUDWidgetClass);
		if (WeaponHUDWidget)
		{
			WeaponHUDWidget->AddToViewport(0);
			if (CurrentWeapon)
				WeaponHUDWidget->UpdateWeapon(CurrentWeapon);
		}
	}

	// 낮/밤 HUD 생성
	if (DayNightHUDWidgetClass)
	{
		DayNightHUDWidget = CreateWidget<UUserWidget>(GetWorld(), DayNightHUDWidgetClass);
		if (DayNightHUDWidget)
			DayNightHUDWidget->AddToViewport(0);
	}

	// ── 체력 HUD (270도 Arc) 생성 ──
	if (HealthHUDWidgetClass)
	{
		HealthHUDWidget = CreateWidget<UHellunaHealthHUDWidget>(GetWorld(), HealthHUDWidgetClass);
		if (HealthHUDWidget)
		{
			HealthHUDWidget->AddToViewport(0);
			// 초기 체력 반영
			if (HeroHealthComponent)
			{
				HealthHUDWidget->UpdateHealth(HeroHealthComponent->GetHealthNormalized());
			}
			// 현재 무기 반영
			if (CurrentWeapon)
			{
				HealthHUDWidget->UpdatePrimaryWeapon(CurrentWeapon);
			}
		}
	}
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
	// [Downed/Revive] 타이머 + 관계 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReviveTickTimerHandle);
	}
	if (ReviveTarget)
	{
		ReviveTarget->CurrentReviver = nullptr;
		ReviveTarget->ReviveProgress = 0.f;
		ReviveTarget = nullptr;
	}
	if (CurrentReviver)
	{
		CurrentReviver->ReviveTarget = nullptr;
		CurrentReviver = nullptr;
	}

#if HELLUNA_DEBUG_HERO // [Step3] 프로덕션 빌드에서 디버그 로그 제거
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [HeroCharacter] EndPlay - 인벤토리 저장 시도               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Character: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ EndPlayReason: %d"), (int32)EndPlayReason);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// 서버에서만 저장 처리
	if (HasAuthority())
	{
		// ⭐ [Phase 6 Fix] 맵 이동 중이면 저장 스킵 (SaveAllPlayersInventory에서 이미 저장했음!)
		UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld()));
		if (GI && GI->bIsMapTransitioning)
		{
#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ 맵 이동 중! SaveAllPlayersInventory에서 이미 저장했으므로 스킵"));
#endif
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
#if HELLUNA_DEBUG_HERO
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ PlayerId: %s"), *PlayerId);
				UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ✅ InventoryComponent 발견! 직접 저장 시작..."));
#endif

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
	#if HELLUNA_DEBUG_HERO
				UE_LOG(LogTemp, Error, TEXT("[EndPlay] ❌ GameMode를 찾을 수 없음!"));
#endif
				}
			}
			else
			{
	#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ PlayerId가 비어있음 (저장 생략)"));
#endif
			}
		}
		else
		{
#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("[EndPlay] ⚠️ InventoryComponent 없음 (PC: %s)"),
				PC ? TEXT("Valid") : TEXT("nullptr"));
#endif
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

	// [Fix26] Controller null 체크 (Unpossess 상태에서 크래시 방지)
	if (!Controller) return;
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

	const float CurrentFov = GetFollowCamera()->FieldOfView;
	SensitivityScale = (DefaultFOV > 0.f) ? (CurrentFov / DefaultFOV) : 1.f; 

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
#if HELLUNA_DEBUG_HERO
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
#endif

	// ============================================
	// ⭐ 로컬에서 제어하는 캐릭터만 입력 바인딩!
	// ⭐ 서버에서 클라이언트 캐릭터에 잘못 바인딩되는 것 방지
	// ============================================
	if (!IsLocallyControlled())
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Warning, TEXT("[HeroCharacter] 입력 바인딩 스킵 - 로컬 캐릭터 아님"));
#endif
		return;
	}
	
	// [Fix26] check()/checkf()/CastChecked → safe return (데디서버 프로세스 종료 방지)
	if (!InputConfigDataAsset)
	{
		UE_LOG(LogHelluna, Error, TEXT("[HeroCharacter] InputConfigDataAsset이 설정되지 않았습니다! 입력 바인딩 스킵"));
		return;
	}

	APlayerController* PC = GetController<APlayerController>();
	if (!PC)
	{
		UE_LOG(LogHelluna, Error, TEXT("[HeroCharacter] GetController<APlayerController>() null — 입력 바인딩 스킵"));
		return;
	}
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);

	if (!Subsystem)
	{
		UE_LOG(LogHelluna, Error, TEXT("[HeroCharacter] EnhancedInputLocalPlayerSubsystem null — 입력 바인딩 스킵"));
		return;
	}

	Subsystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext, 0);

	UHellunaInputComponent* HellunaInputComponent = Cast<UHellunaInputComponent>(PlayerInputComponent);
	if (!HellunaInputComponent)
	{
		UE_LOG(LogHelluna, Error, TEXT("[HeroCharacter] PlayerInputComponent가 UHellunaInputComponent가 아닙니다! 입력 바인딩 스킵"));
		return;
	}

	HellunaInputComponent->BindNativeInputAction(InputConfigDataAsset, HellunaGameplayTags::InputTag_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Move);
	HellunaInputComponent->BindNativeInputAction(InputConfigDataAsset, HellunaGameplayTags::InputTag_Look, ETriggerEvent::Triggered, this, &ThisClass::Input_Look);

	// [F키 통합] InputTag.Interaction 하나로 Revive + BossEncounterCube 통합
	// InputTag_Revive 바인딩 제거 — Input_InteractionStarted에서 컨텍스트 분기
	HellunaInputComponent->BindNativeInputAction(InputConfigDataAsset, HellunaGameplayTags::InputTag_Interaction, ETriggerEvent::Started, this, &ThisClass::Input_InteractionStarted);
	HellunaInputComponent->BindNativeInputAction(InputConfigDataAsset, HellunaGameplayTags::InputTag_Interaction, ETriggerEvent::Completed, this, &ThisClass::Input_InteractionCompleted);

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
#if HELLUNA_DEBUG_HERO
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
#endif

	if (HellunaAbilitySystemComponent)
	{	
		HellunaAbilitySystemComponent->OnAbilityInputPressed(InInputTag);
	}
	else
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Error, TEXT("⛔ [HeroCharacter] ASC가 nullptr!"));
#endif
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
#if HELLUNA_DEBUG_HERO
	UE_LOG(LogTemp, Warning, TEXT("=== [HeroCharacter::Server_RepairSpaceShip] 호출됨! ==="));
	UE_LOG(LogTemp, Warning, TEXT("  재료 1: %s x %d"), *Material1Tag.ToString(), Material1Amount);
	UE_LOG(LogTemp, Warning, TEXT("  재료 2: %s x %d"), *Material2Tag.ToString(), Material2Amount);
	UE_LOG(LogTemp, Warning, TEXT("  서버 여부: %s"), HasAuthority() ? TEXT("서버 ✅") : TEXT("클라이언트 ❌"));
#endif

	// 서버 권한 체크
	if (!HasAuthority())
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Error, TEXT("  ❌ 서버가 아님!"));
#endif
		return;
	}

	// 총 자원 계산
	int32 TotalResource = Material1Amount + Material2Amount;

	// 자원이 0 이하면 무시
	if (TotalResource <= 0)
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ 자원이 0 이하! 무시"));
#endif
		return;
	}

	// World에서 "SpaceShip" 태그를 가진 Actor 찾기
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName("SpaceShip"), FoundActors);

	if (FoundActors.Num() == 0)
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Error, TEXT("  ❌ SpaceShip을 찾을 수 없음! 'SpaceShip' 태그 확인 필요"));
#endif
		return;
	}

	// SpaceShip 찾음
	if (AResourceUsingObject_SpaceShip* SpaceShip = Cast<AResourceUsingObject_SpaceShip>(FoundActors[0]))
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Warning, TEXT("  ✅ SpaceShip 찾음: %s"), *SpaceShip->GetName());
#endif

		// ⭐ RepairComponent 가져오기
		URepairComponent* RepairComp = SpaceShip->FindComponentByClass<URepairComponent>();
		if (RepairComp)
		{
#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("  ✅ RepairComponent 찾음!"));
#endif

			// ⭐ 애니메이션/사운드를 **한 번만** 재생 (멀티캐스트)
			FVector SpaceShipLocation = SpaceShip->GetActorLocation();
			RepairComp->Multicast_PlaySingleRepairEffect(SpaceShipLocation);
#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("  🎬 애니메이션/사운드 한 번 재생 요청!"));
#endif
		}
		
		// ⭐⭐⭐ SpaceShip에 자원 추가 (실제 추가된 양 반환)
		int32 ActualAdded = SpaceShip->AddRepairResource(TotalResource);
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Warning, TEXT("  📊 SpaceShip->AddRepairResource(%d) 호출 → 실제 추가: %d"), TotalResource, ActualAdded);
#endif

		// ⭐⭐⭐ 실제 추가된 양만큼만 인벤토리에서 차감!
		if (ActualAdded > 0)
		{
			// ⭐ PlayerController 가져오기
			APlayerController* PC = Cast<APlayerController>(GetController());
			if (!PC)
			{
#if HELLUNA_DEBUG_HERO
				UE_LOG(LogTemp, Error, TEXT("  ❌ PlayerController를 찾을 수 없음!"));
#endif
				return;
			}

			// ⭐ InventoryComponent 가져오기 (Statics 사용!)
			UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(PC);
			if (!InvComp)
			{
#if HELLUNA_DEBUG_HERO
				UE_LOG(LogTemp, Error, TEXT("  ❌ InventoryComponent를 찾을 수 없음!"));
#endif
				return;
			}

#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("  ✅ InventoryComponent 찾음!"));
#endif

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

#if HELLUNA_DEBUG_HERO
				UE_LOG(LogTemp, Warning, TEXT("  📊 비율 계산:"));
				UE_LOG(LogTemp, Warning, TEXT("    - 재료1 비율: %.2f → 차감: %d"), Ratio1, ActualMaterial1);
				UE_LOG(LogTemp, Warning, TEXT("    - 재료2 비율: %.2f → 차감: %d"), Ratio2, ActualMaterial2);
#endif
			}

			// 재료 1 차감
			if (ActualMaterial1 > 0 && Material1Tag.IsValid())
			{
#if HELLUNA_DEBUG_HERO
				UE_LOG(LogTemp, Warning, TEXT("  🧪 재료 1 차감: %s x %d"), *Material1Tag.ToString(), ActualMaterial1);
#endif
				InvComp->Server_ConsumeMaterialsMultiStack(Material1Tag, ActualMaterial1);
			}

			// 재료 2 차감
			if (ActualMaterial2 > 0 && Material2Tag.IsValid())
			{
#if HELLUNA_DEBUG_HERO
				UE_LOG(LogTemp, Warning, TEXT("  🧪 재료 2 차감: %s x %d"), *Material2Tag.ToString(), ActualMaterial2);
#endif
				InvComp->Server_ConsumeMaterialsMultiStack(Material2Tag, ActualMaterial2);
			}

#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("  ✅ 실제 차감 완료! 총 차감: %d"), ActualAdded);
#endif
		}
		else
		{
#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Warning, TEXT("  ⚠️ SpaceShip에 추가된 자원이 없음! (이미 만원일 수 있음)"));
#endif
		}
	}
	else
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Error, TEXT("  ❌ SpaceShip 캐스팅 실패!"));
#endif
	}

#if HELLUNA_DEBUG_HERO
	UE_LOG(LogTemp, Warning, TEXT("=== [HeroCharacter::Server_RepairSpaceShip] 완료! ==="));
#endif
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

	// 탄약 먼저 복원 → SetCurrentWeapon 이전에 CurrentMag를 올바른 값으로 맞춰둔다.
	// (SetCurrentWeapon 내부에서 HUD가 갱신되므로 순서가 중요하다.)
	ApplySavedCurrentMagByClass(NewWeapon);

	// 현재 손에 새 무기 지정 — 내부에서 WeaponHUDWidget->UpdateWeapon() 호출
	SetCurrentWeapon(NewWeapon);
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
#if HELLUNA_DEBUG_HERO
	UE_LOG(LogTemp, Warning, TEXT("⭐ [HeroCharacter] Server_RequestDestroyWeapon 호출됨 (서버)"));
#endif



	if (IsValid(CurrentWeapon))
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Warning, TEXT("⭐ [HeroCharacter] CurrentWeapon Destroy: %s"), *CurrentWeapon->GetName());
#endif

		if (AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(CurrentWeapon))
		{
			SaveCurrentMagByClass(CurrentWeapon);
		}

		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}

	else
	{
#if HELLUNA_DEBUG_HERO
		UE_LOG(LogTemp, Warning, TEXT("⭐ [HeroCharacter] CurrentWeapon이 이미 null"));
#endif
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

	DOREPLIFETIME(AHellunaHeroCharacter, CurrentWeapon);  // OnRep_CurrentWeapon → HUD 갱신
	DOREPLIFETIME(AHellunaHeroCharacter, CurrentWeaponTag);
	DOREPLIFETIME(AHellunaHeroCharacter, PlayFullBody);   // 전신 몽타주 플래그 — CLIENT B ABP 동기화
	DOREPLIFETIME(AHellunaHeroCharacter, ReviveProgress);  // [Downed/Revive] 부활 진행률
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
	// 서버에서만 저장 (탄약은 서버가 권위를 가짐)
	if (!HasAuthority()) return;
	if (!IsValid(Weapon)) return;

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Weapon);
	if (!Gun) return;

	TSubclassOf<AHellunaHeroWeapon> WeaponClass = Weapon->GetClass();
	if (!WeaponClass) return;

	// 무기 클래스를 키로 현재 탄약 저장 → 다음에 같은 종류 무기를 들 때 복원
	SavedMagByWeaponClass.FindOrAdd(WeaponClass) = FMath::Clamp(Gun->CurrentMag, 0, Gun->MaxMag);
}

void AHellunaHeroCharacter::ApplySavedCurrentMagByClass(AHellunaHeroWeapon* Weapon)
{
	if (!IsValid(Weapon)) return;

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Weapon);
	if (!Gun) return;

	TSubclassOf<AHellunaHeroWeapon> WeaponClass = Weapon->GetClass();
	if (!WeaponClass) return;

	// 저장된 탄약이 없으면 (처음 드는 무기) 복원하지 않는다
	const int32* SavedMag = SavedMagByWeaponClass.Find(WeaponClass);
	if (!SavedMag) return;

	// 저장된 탄약을 현재 무기에 즉시 반영
	Gun->CurrentMag = FMath::Clamp(*SavedMag, 0, Gun->MaxMag);

	// 서버에서는 복제 트리거, 클라이언트에서는 로컬 값 직접 반영
	if (HasAuthority())
	{
		Gun->BroadcastAmmoChanged();
		Gun->ForceNetUpdate();
	}
}

// =========================================================
// ★ 추가: 플레이어 피격/사망 애니메이션
// =========================================================

void AHellunaHeroCharacter::OnHeroHealthChanged(
	UActorComponent* HealthComp,
	float OldHealth,
	float NewHealth,
	AActor* InstigatorActor)
{
	// [HealthHUD] 클라이언트에서도 HUD 갱신 (HasAuthority 체크 전)
	if (IsLocallyControlled() && HealthHUDWidget && HeroHealthComponent)
	{
		HealthHUDWidget->UpdateHealth(HeroHealthComponent->GetHealthNormalized());
	}

	if (!HasAuthority()) return;

	const float Delta = OldHealth - NewHealth;

	// 디버그: 체력 변화량 출력
	if (Delta > 0.f)
	{
		Debug::Print(FString::Printf(TEXT("[PlayerHP] %s: -%.1f 데미지 (%.1f → %.1f)"),
			*GetName(), Delta, OldHealth, NewHealth), FColor::Yellow);

		// [Phase21] 피격 방향 혈흔 → 클라이언트에 전송
		const uint8 DirIndex = CalcHitDirection(InstigatorActor);
		UE_LOG(LogHelluna, Warning, TEXT("[BloodHit] 서버: %s 피격! Dir=%d, Instigator=%s"),
			*GetName(), DirIndex, InstigatorActor ? *InstigatorActor->GetName() : TEXT("nullptr"));
		Multicast_ShowBloodHitDirection(DirIndex);
	}

	// 피격 애니메이션 (데미지를 받았고 살아있을 때만, 다운 중 제외)
	if (Delta > 0.f && NewHealth > 0.f && HitReactMontage)
	{
		if (!HeroHealthComponent || !HeroHealthComponent->IsDowned())
		{
			Multicast_PlayHeroHitReact();
		}
	}
}

void AHellunaHeroCharacter::OnHeroDeath(AActor* DeadActor, AActor* KillerActor)
{
	if (!HasAuthority()) return;

	// [Downed] 다운 태그 제거 (Downed→사망 경로)
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(HellunaGameplayTags::Player_State_Downed);
	}
	// [Downed] Revive 관계 정리
	if (CurrentReviver)
	{
		CurrentReviver->ReviveTarget = nullptr;
		if (UWorld* W = GetWorld())
		{
			W->GetTimerManager().ClearTimer(CurrentReviver->ReviveTickTimerHandle);
		}
		CurrentReviver = nullptr;
	}
	ReviveProgress = 0.f;

	// 사망 애니메이션
	if (DeathMontage)
	{
		Multicast_PlayHeroDeath();
	}

	// Phase 9: 사체에 인벤토리 아이템 이전
	if (IsValid(LootContainerComponent))
	{
		// B8: 사망 시 GetController()가 nullptr일 수 있음 (UnPossess 타이밍)
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC)
		{
			UE_LOG(LogHelluna, Warning, TEXT("[HeroCharacter] OnHeroDeath: GetController() nullptr — 사체에 아이템 없음 (%s)"), *GetName());
		}
		UInv_InventoryComponent* InvComp = PC ? PC->FindComponentByClass<UInv_InventoryComponent>() : nullptr;

		if (IsValid(InvComp))
		{
			// 인벤토리 데이터 수집
			TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();

			if (CollectedItems.Num() > 0)
			{
				// GameMode에서 Resolver 생성
				AHellunaBaseGameMode* GM = Cast<AHellunaBaseGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
				if (IsValid(GM))
				{
					FInv_ItemTemplateResolver Resolver;
					Resolver.BindLambda([GM](const FGameplayTag& ItemType) -> UInv_ItemComponent*
					{
						TSubclassOf<AActor> ActorClass = GM->ResolveItemClass(ItemType);
						if (!ActorClass) return nullptr;
						AActor* CDO = ActorClass->GetDefaultObject<AActor>();
						return CDO ? CDO->FindComponentByClass<UInv_ItemComponent>() : nullptr;
					});

					LootContainerComponent->InitializeWithItems(CollectedItems, Resolver);
				}
			}

			// 컨테이너 활성화
			LootContainerComponent->ActivateContainer();
			LootContainerComponent->SetContainerDisplayName(
				FText::FromString(FString::Printf(TEXT("%s의 사체"), *GetName())));

			// 사체 유지 (LifeSpan=0 → 파괴하지 않음)
			SetLifeSpan(0.f);

#if HELLUNA_DEBUG_HERO
			UE_LOG(LogTemp, Log, TEXT("[HeroCharacter] OnHeroDeath: %s → 사체 컨테이너 활성화 (%d아이템)"),
				*GetName(), CollectedItems.Num());
#endif
		}
	}

	// 전원 사망 체크 → GameMode에 사망 알림
	if (AHellunaDefenseGameMode* DefenseGM = Cast<AHellunaDefenseGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		DefenseGM->NotifyPlayerDied(PC);
	}
}

void AHellunaHeroCharacter::Multicast_PlayHeroHitReact_Implementation()
{
	// [GunParry] 무적 상태 피격 모션 차단
	if (UHeroGameplayAbility_GunParry::ShouldBlockDamage(this)) return;

	if (!HitReactMontage) return;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return;

	AnimInst->Montage_Play(HitReactMontage);
}

void AHellunaHeroCharacter::Multicast_PlayHeroDeath_Implementation()
{
	// [Fix] PlayFullBody 원복 (다운→사망 경로)
	PlayFullBody = false;

	// [Phase21-C] 다운 선혈 화면 효과 종료 (로컬 전용, 다운→사망 경로)
	if (IsLocallyControlled())
	{
		StopDownedScreenEffect();
	}

	// [Phase 21] 사망 시 부활 위젯 숨김 (다운→사망 경로)
	HideReviveWidget();

	// ── [Phase21-Fix] 래그돌 전환 ──
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		// 진행 중 몽타주 즉시 정지 (다운 몽타주 포함)
		if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
		{
			AnimInst->Montage_Stop(0.f);
		}

		// 래그돌 활성화
		SkelMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		SkelMesh->SetAllBodiesSimulatePhysics(true);
		SkelMesh->SetSimulatePhysics(true);
		SkelMesh->WakeAllRigidBodies();
		SkelMesh->bBlendPhysics = true;

		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] 래그돌 전환: %s"), *GetName());
	}

	// 캡슐 콜리전 비활성화 (래그돌과 겹치지 않도록)
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] 캡슐 콜리전 비활성화: %s"), *GetName());
	}

	// 이동 컴포넌트 비활성화
	if (UCharacterMovementComponent* MovComp = GetCharacterMovement())
	{
		MovComp->StopMovementImmediately();
		MovComp->DisableMovement();
		MovComp->SetComponentTickEnabled(false);
	}
}

// =========================================================
// ★ 건패링 워프 VFX 멀티캐스트 (Step 2b)
// 서버에서 호출 → 모든 클라이언트에서 나이아가라 이펙트 스폰
// =========================================================
void AHellunaHeroCharacter::Multicast_PlayParryWarpVFX_Implementation(
	UNiagaraSystem* Effect, FVector Location, FRotator Rotation, float Scale, FLinearColor Color, bool bGhostMesh, float GhostOpacity)
{
	if (!Effect)
	{
		return;
	}

	// 데디케이티드 서버에서는 렌더링 불필요 — 스킵
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		Effect,
		Location,
		Rotation,
		FVector(Scale),
		true,  // bAutoDestroy
		true,  // bAutoActivate
		ENCPoolMethod::None
	);

	if (Comp)
	{
		Comp->SetNiagaraVariableLinearColor(TEXT("WarpColor"), Color);

		// Step 5: 고스트 메시 — Hero의 SkeletalMesh를 나이아가라에 전달
		if (bGhostMesh)
		{
			if (USkeletalMeshComponent* HeroMesh = GetMesh())
			{
				UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(
					Comp, TEXT("SkeletalMesh"), HeroMesh);
			}
			Comp->SetNiagaraVariableFloat(TEXT("GhostOpacity"), GhostOpacity);
			Comp->SetNiagaraVariableBool(TEXT("bGhostMesh"), true);
		}
		else
		{
			Comp->SetNiagaraVariableBool(TEXT("bGhostMesh"), false);
		}

		ActiveParryVFX.Add(Comp);
	}

	UE_LOG(LogGunParry, Verbose,
		TEXT("[Multicast_PlayParryWarpVFX] VFX 스폰 — Effect=%s, Location=%s, Scale=%.1f, Ghost=%s"),
		*Effect->GetName(),
		*Location.ToString(),
		Scale,
		bGhostMesh ? TEXT("Y") : TEXT("N"));
}

// =========================================================
// ★ 건패링 워프 VFX 중단 (Step 2b-5)
// AN_ParryExecutionFire 타이밍에 호출 → 기존 파티클만 페이드아웃
// =========================================================
void AHellunaHeroCharacter::Multicast_StopParryWarpVFX_Implementation()
{
	int32 DeactivatedCount = 0;

	for (TWeakObjectPtr<UNiagaraComponent>& WeakComp : ActiveParryVFX)
	{
		if (UNiagaraComponent* Comp = WeakComp.Get())
		{
			Comp->Deactivate();
			++DeactivatedCount;
		}
	}

	UE_LOG(LogGunParry, Verbose,
		TEXT("[Multicast_StopParryWarpVFX] VFX Deactivate — %d개 컴포넌트"),
		DeactivatedCount);

	ActiveParryVFX.Empty();
}

// =========================================================
// Multicast_SpawnParryGhostTrail — 패링 잔상(PoseableMesh) 전 클라이언트 스폰
// =========================================================
void AHellunaHeroCharacter::Multicast_SpawnParryGhostTrail_Implementation(
	int32 Count, float FadeDuration,
	FVector StartLocation, FVector EndLocation, FRotator TrailRotation,
	FLinearColor GhostColor, UMaterialInterface* TrailMaterial)
{
	// 데디케이티드 서버에서는 렌더링 불필요
	if (GetNetMode() == NM_DedicatedServer) return;

	USkeletalMeshComponent* HeroMesh = GetMesh();
	if (!HeroMesh || !HeroMesh->GetSkeletalMeshAsset()) return;

	// 머티리얼 폴백
	UMaterialInterface* Mat = TrailMaterial;
	if (!Mat)
	{
		Mat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Gihyeon/Combat/Materials/M_GhostTrail"));
	}
	if (!Mat) return;

	for (int32 i = 0; i < Count; i++)
	{
		const float Alpha = (float)(i + 1) / (float)(Count + 1);
		// 도착지(StartLocation)에서 출발지(EndLocation) 방향으로 잔상 배치 — 카메라 시야 안에 들어옴
		FVector TrailLoc = FMath::Lerp(StartLocation, EndLocation, Alpha * 0.4f);
		// [Fix: 공중 부유] 캐릭터 위치는 캡슐 중심이므로 메시 오프셋만큼 Z 보정
		if (USkeletalMeshComponent* MyMesh = GetMesh())
		{
			TrailLoc.Z += MyMesh->GetRelativeLocation().Z;
		}
		const float OpacityMul = 1.f - Alpha * 0.3f;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AGhostTrailActor* Ghost = GetWorld()->SpawnActor<AGhostTrailActor>(
			AGhostTrailActor::StaticClass(), TrailLoc, TrailRotation, SpawnParams);

		if (Ghost)
		{
			Ghost->Initialize(HeroMesh, Mat, FadeDuration, 0.85f * OpacityMul, GhostColor);
		}
	}

	UE_LOG(LogGunParry, Warning,
		TEXT("[Multicast_SpawnParryGhostTrail] 잔상 %d개 스폰 — Start=%s, FadeDuration=%.1f"),
		Count, *StartLocation.ToString(), FadeDuration);
}

// =========================================================
// ★ Downed/Revive System (다운/부활)
// =========================================================

void AHellunaHeroCharacter::OnHeroDowned(AActor* DownedActor, AActor* InstigatorActor)
{
	if (!HasAuthority()) return;

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] OnHeroDowned 진입: %s | DownedMontage=%s | ReviveWidgetClass=%s"),
		*GetName(),
		DownedMontage ? *DownedMontage->GetName() : TEXT("NULL"),
		ReviveWidgetClass ? *ReviveWidgetClass->GetName() : TEXT("NULL"));

	// 솔로 체크: 접속자 1명이면 즉사
	if (AHellunaDefenseGameMode* DefenseGM = Cast<AHellunaDefenseGameMode>(
		UGameplayStatics::GetGameMode(GetWorld())))
	{
		bool bSkip = DefenseGM->ShouldSkipDowned();
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ShouldSkipDowned=%s"), bSkip ? TEXT("TRUE (즉사)") : TEXT("FALSE (다운 진입)"));

		if (bSkip)
		{
			if (HeroHealthComponent)
			{
				HeroHealthComponent->ForceKillFromDowned();
			}
			return;
		}

		// 마지막 생존자 체크
		DefenseGM->NotifyPlayerDowned(Cast<APlayerController>(GetController()));
	}

	// ASC에 다운 태그 추가 + 진행 중 어빌리티 전체 취소
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AddLooseGameplayTag(HellunaGameplayTags::Player_State_Downed);
		AbilitySystemComponent->CancelAllAbilities();
	}

	// 이동/시야 잠금
	LockMoveInput();
	LockLookInput();

	// 이동 비활성화
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->DisableMovement();
	}

	// 무기 파괴
	if (CurrentWeapon)
	{
		Server_RequestDestroyWeapon();
	}

	// 다운 몽타주 + 카메라
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Multicast_PlayHeroDowned 호출 직전: %s | DownedMontage=%s | ReviveWidgetComp=%s"),
		*GetName(),
		DownedMontage ? *DownedMontage->GetName() : TEXT("NULL"),
		ReviveWidgetComp ? TEXT("Valid") : TEXT("NULL"));
	Multicast_PlayHeroDowned();

	UE_LOG(LogHelluna, Log, TEXT("[Downed] %s → 다운 상태 진입"), *GetName());
}

void AHellunaHeroCharacter::Multicast_PlayHeroDowned_Implementation()
{
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Multicast_PlayHeroDowned: %s | IsLocal=%s | HasAuth=%s | NetMode=%d"),
		*GetName(),
		IsLocallyControlled() ? TEXT("Y") : TEXT("N"),
		HasAuthority() ? TEXT("Y") : TEXT("N"),
		(int32)GetNetMode());

	// 다운 몽타주
	if (DownedMontage)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] DownedMontage: %s"), *DownedMontage->GetName());
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				// [Fix] PlayFullBody=true → ABP가 locomotion 대신 전신 몽타주 슬롯 사용
				PlayFullBody = true;

				float Duration = AnimInst->Montage_Play(DownedMontage);
				UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Montage_Play 결과: Duration=%.2f (0이면 실패) | PlayFullBody=true"), Duration);
				if (Duration <= 0.f)
				{
					UE_LOG(LogHelluna, Error, TEXT("[Phase21-Debug] 몽타주 재생 실패! MontageSkel=%s, MeshSkel=%s"),
						DownedMontage->GetSkeleton() ? *DownedMontage->GetSkeleton()->GetName() : TEXT("NULL"),
						SkelMesh->GetSkeletalMeshAsset() ? (SkelMesh->GetSkeletalMeshAsset()->GetSkeleton() ? *SkelMesh->GetSkeletalMeshAsset()->GetSkeleton()->GetName() : TEXT("NULL")) : TEXT("NoMesh"));
				}
			}
			else
			{
				UE_LOG(LogHelluna, Error, TEXT("[Phase21-Debug] AnimInstance NULL!"));
			}
		}
		else
		{
			UE_LOG(LogHelluna, Error, TEXT("[Phase21-Debug] Mesh NULL!"));
		}
	}
	else
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] DownedMontage NULL! (몽타주 미설정)"));
	}

	// 로컬 클라에서만 카메라 낮추기
	if (IsLocallyControlled() && CameraBoom)
	{
		CameraBoom->TargetArmLength = DownedCameraArmLength;
		CameraBoom->SocketOffset = DownedCameraSocketOffset;
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] 카메라 다운 전환: ArmLength=%.1f, Offset=%s"),
			DownedCameraArmLength, *DownedCameraSocketOffset.ToString());
	}

	// [Phase 21] 3D 부활 위젯 표시 (모든 클라이언트)
	ShowReviveWidget();

	// [Phase21-C] 다운 선혈 화면 효과 시작 (로컬 전용)
	if (IsLocallyControlled())
	{
		StartDownedScreenEffect();
	}
}

void AHellunaHeroCharacter::Multicast_PlayHeroRevived_Implementation()
{
	// [Fix] PlayFullBody 원복 → locomotion 복귀
	PlayFullBody = false;

	// 부활 몽타주
	if (ReviveMontage)
	{
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			if (UAnimInstance* AnimInst = SkelMesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(ReviveMontage);
			}
		}
	}

	// 로컬 클라에서 카메라 복구
	if (IsLocallyControlled() && CameraBoom)
	{
		CameraBoom->TargetArmLength = DefaultTargetArmLength;
		CameraBoom->SocketOffset = DefaultSocketOffset;
	}

	// [Phase21-C] 다운 선혈 화면 효과 종료 (로컬 전용)
	if (IsLocallyControlled())
	{
		StopDownedScreenEffect();
	}

	// [Phase 21] 3D 부활 위젯 숨김 (모든 클라이언트)
	HideReviveWidget();
}

// ── Revive 입력 ──

void AHellunaHeroCharacter::Input_ReviveStarted(const FInputActionValue& Value)
{
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Input_ReviveStarted 호출! %s | IsLocal=%s"),
		*GetName(), IsLocallyControlled() ? TEXT("Y") : TEXT("N"));

	// 자신이 다운/사망이면 부활 불가
	if (HeroHealthComponent && (HeroHealthComponent->IsDowned() || HeroHealthComponent->IsDead()))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Input_ReviveStarted: 자신이 다운/사망 → 리턴"));
		return;
	}

	// 근처 다운 팀원 탐색
	AHellunaHeroCharacter* Target = FindNearestDownedHero();
	if (!Target)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Input_ReviveStarted: 근처 다운 팀원 없음 → 리턴"));
		return;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Input_ReviveStarted: 타겟=%s → Server_StartRevive 호출"),
		*Target->GetName());
	Server_StartRevive(Target);

	// [Phase 21] 부활 진행 HUD 표시 (로컬)
	ShowReviveProgressHUD(Target->GetName());
}

void AHellunaHeroCharacter::Input_ReviveCompleted(const FInputActionValue& Value)
{
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Input_ReviveCompleted: %s → Server_StopRevive"),
		*GetName());
	Server_StopRevive();
	HideReviveProgressHUD();
}

// ============================================================================
// [BossEvent] Interaction 입력 — F키 홀드 상태 전달
// ============================================================================

void AHellunaHeroCharacter::Input_InteractionStarted(const FInputActionValue& Value)
{
	// [F키 통합] 다운/사망 상태면 무시
	if (HeroHealthComponent && (HeroHealthComponent->IsDowned() || HeroHealthComponent->IsDead()))
	{
		return;
	}

	bHoldingInteraction = true;
	bIsRevivingLocal = false;

	// 우선순위 1: 근처 다운된 팀원 → 부활
	AHellunaHeroCharacter* DownedTarget = FindNearestDownedHero();
	if (DownedTarget)
	{
		bIsRevivingLocal = true;
		Server_StartRevive(DownedTarget);
		ShowReviveProgressHUD(DownedTarget->GetName());
		return;
	}

	// 우선순위 2: 보스큐브 등 기타 상호작용
	// → bHoldingInteraction = true 상태로 BossEncounterCube::Tick이 처리
	// → IsReviving() == false 이므로 큐브 프로그레스 정상 증가
}

void AHellunaHeroCharacter::Input_InteractionCompleted(const FInputActionValue& Value)
{
	bHoldingInteraction = false;

	// [F키 통합] 부활 중이었으면 중단
	if (bIsRevivingLocal)
	{
		bIsRevivingLocal = false;
		Server_StopRevive();
		HideReviveProgressHUD();
	}
}

AHellunaHeroCharacter* AHellunaHeroCharacter::FindNearestDownedHero() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	const FVector MyLoc = GetActorLocation();
	AHellunaHeroCharacter* Best = nullptr;
	float BestDistSq = ReviveRange * ReviveRange;

	int32 CheckedCount = 0;

	// [Fix] 클라이언트에서도 동작하도록 PlayerControllerIterator 대신 캐릭터 직접 탐색
	TArray<AActor*> AllHeroes;
	UGameplayStatics::GetAllActorsOfClass(World, AHellunaHeroCharacter::StaticClass(), AllHeroes);

	for (AActor* Actor : AllHeroes)
	{
		AHellunaHeroCharacter* OtherHero = Cast<AHellunaHeroCharacter>(Actor);
		if (!OtherHero || OtherHero == this) continue;

		CheckedCount++;
		UHellunaHealthComponent* HC = OtherHero->HeroHealthComponent;
		bool bIsDowned = HC ? HC->IsDowned() : false;
		bool bHasReviver = OtherHero->CurrentReviver != nullptr;
		float Dist = FVector::Dist(MyLoc, OtherHero->GetActorLocation());

		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] FindNearestDownedHero: %s | Downed=%s | HasReviver=%s | Dist=%.0f (Range=%.0f)"),
			*OtherHero->GetName(),
			bIsDowned ? TEXT("Y") : TEXT("N"),
			bHasReviver ? TEXT("Y") : TEXT("N"),
			Dist, ReviveRange);

		if (!HC || !bIsDowned) continue;

		// 이미 다른 사람이 부활 중이면 스킵 (1:1 제한)
		if (bHasReviver) continue;

		const float DistSq = Dist * Dist;
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = OtherHero;
		}
	}

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] FindNearestDownedHero: 체크=%d명, 결과=%s"),
		CheckedCount, Best ? *Best->GetName() : TEXT("NULL"));
	return Best;
}

void AHellunaHeroCharacter::Server_StartRevive_Implementation(AHellunaHeroCharacter* TargetHero)
{
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Server_StartRevive: %s → %s"),
		*GetName(), IsValid(TargetHero) ? *TargetHero->GetName() : TEXT("INVALID"));

	if (!IsValid(TargetHero)) return;
	if (!TargetHero->HeroHealthComponent || !TargetHero->HeroHealthComponent->IsDowned()) return;
	if (HeroHealthComponent && (HeroHealthComponent->IsDowned() || HeroHealthComponent->IsDead())) return;

	// 거리 재검증 (서버 측)
	const float DistSq = FVector::DistSquared(GetActorLocation(), TargetHero->GetActorLocation());
	if (DistSq > ReviveRange * ReviveRange)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] Server_StartRevive: 거리 초과 (%.0f > %.0f)"),
			FMath::Sqrt(DistSq), ReviveRange);
		return;
	}

	// 1:1 제한 체크
	if (TargetHero->CurrentReviver != nullptr && TargetHero->CurrentReviver != this) return;

	// 이전 타겟 정리
	if (ReviveTarget && ReviveTarget != TargetHero)
	{
		ReviveTarget->CurrentReviver = nullptr;
		ReviveTarget->ReviveProgress = 0.f;
	}

	ReviveTarget = TargetHero;
	TargetHero->CurrentReviver = this;
	TargetHero->ReviveProgress = 0.f;

	// 0.1초 간격 Revive 틱
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ReviveTickTimerHandle, this, &ThisClass::TickRevive, 0.1f, true);
	}

	UE_LOG(LogHelluna, Log, TEXT("[Revive] %s → %s 부활 시작"), *GetName(), *TargetHero->GetName());
}

void AHellunaHeroCharacter::Server_StopRevive_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReviveTickTimerHandle);
	}

	if (ReviveTarget)
	{
		ReviveTarget->CurrentReviver = nullptr;
		ReviveTarget->ReviveProgress = 0.f;
		ReviveTarget = nullptr;
	}
}

void AHellunaHeroCharacter::TickRevive()
{
	// 유효성 체크: 대상 유효 + 다운 + 본인 생존
	if (!IsValid(ReviveTarget) || !ReviveTarget->HeroHealthComponent
		|| !ReviveTarget->HeroHealthComponent->IsDowned()
		|| (HeroHealthComponent && (HeroHealthComponent->IsDowned() || HeroHealthComponent->IsDead())))
	{
		Server_StopRevive_Implementation();
		return;
	}

	// 거리 체크
	const float DistSq = FVector::DistSquared(GetActorLocation(), ReviveTarget->GetActorLocation());
	if (DistSq > ReviveRange * ReviveRange)
	{
		Server_StopRevive_Implementation();
		return;
	}

	// 진행률 증가 (0.1초 / ReviveDuration)
	const float ProgressPerTick = (ReviveDuration > 0.f) ? (0.1f / ReviveDuration) : 1.f;
	ReviveTarget->ReviveProgress = FMath::Clamp(ReviveTarget->ReviveProgress + ProgressPerTick, 0.f, 1.f);

	UE_LOG(LogHelluna, Log, TEXT("[Phase21-Debug] TickRevive: %s → %s | Progress=%.1f%% | Duration=%.1f"),
		*GetName(), *ReviveTarget->GetName(), ReviveTarget->ReviveProgress * 100.f, ReviveDuration);

	// 완료
	if (ReviveTarget->ReviveProgress >= 1.f)
	{
		AHellunaHeroCharacter* Target = ReviveTarget;

		// 타이머 클리어 먼저 (ClearTimer 후 캡처 접근 금지 규칙)
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ReviveTickTimerHandle);
		}

		// 참조 정리
		Target->CurrentReviver = nullptr;
		Target->ReviveProgress = 0.f;
		ReviveTarget = nullptr;

		// HealthComponent 부활 처리
		if (Target->HeroHealthComponent)
		{
			Target->HeroHealthComponent->Revive(Target->ReviveHealthPercent);
		}

		// 다운 태그 제거
		if (Target->AbilitySystemComponent)
		{
			Target->AbilitySystemComponent->RemoveLooseGameplayTag(HellunaGameplayTags::Player_State_Downed);
		}

		// 이동/시야 잠금 해제
		Target->UnlockMoveInput();
		Target->UnlockLookInput();
		if (UCharacterMovementComponent* CMC = Target->GetCharacterMovement())
		{
			CMC->SetMovementMode(MOVE_Walking);
		}

		// 부활 몽타주 + 카메라 복구
		Target->Multicast_PlayHeroRevived();

		UE_LOG(LogHelluna, Log, TEXT("[Revive] %s → %s 부활 완료"), *GetName(), *Target->GetName());
	}
}

void AHellunaHeroCharacter::OnRep_ReviveProgress()
{
	// [Phase 21] 3D 부활 위젯에 프로그레스 반영
	if (ReviveWidgetInstance)
	{
		ReviveWidgetInstance->SetReviveProgress(ReviveProgress);
	}
}

// =========================================================
// 3D 부활 위젯 — Show/Hide/Update
// =========================================================

void AHellunaHeroCharacter::ShowReviveWidget()
{
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ShowReviveWidget 진입: %s | ReviveWidgetComp=%s | NetMode=%d"),
		*GetName(),
		ReviveWidgetComp ? TEXT("Valid") : TEXT("NULL"),
		(int32)GetNetMode());

	if (!ReviveWidgetComp) return;

	// 데디케이티드 서버에서는 위젯 불필요
	if (GetNetMode() == NM_DedicatedServer) return;

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ShowReviveWidget: WidgetClass=%s, CurrentWidget=%s, Visibility 전=%s"),
		ReviveWidgetComp->GetWidgetClass() ? *ReviveWidgetComp->GetWidgetClass()->GetName() : TEXT("NULL"),
		ReviveWidgetComp->GetWidget() ? *ReviveWidgetComp->GetWidget()->GetName() : TEXT("NULL"),
		ReviveWidgetComp->IsVisible() ? TEXT("Visible") : TEXT("Hidden"));

	ReviveWidgetComp->SetVisibility(true);

	// 위젯 인스턴스 캐시
	if (!ReviveWidgetInstance)
	{
		ReviveWidgetInstance = Cast<UHellunaReviveWidget>(ReviveWidgetComp->GetWidget());
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ShowReviveWidget: ReviveWidgetInstance 캐시 → %s"),
			ReviveWidgetInstance ? *ReviveWidgetInstance->GetName() : TEXT("NULL (캐스트 실패 또는 위젯 없음)"));
	}

	if (ReviveWidgetInstance)
	{
		ReviveWidgetInstance->ResetState();
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ShowReviveWidget: ResetState 완료"));
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[Phase21-Debug] ShowReviveWidget: ReviveWidgetInstance가 여전히 NULL! DrawSize=%s, Space=%d"),
			*ReviveWidgetComp->GetDrawSize().ToString(),
			(int32)ReviveWidgetComp->GetWidgetSpace());
	}

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ReviveWidget 표시 완료: %s | Visible=%s"),
		*GetName(),
		ReviveWidgetComp->IsVisible() ? TEXT("Y") : TEXT("N"));
}

void AHellunaHeroCharacter::HideReviveWidget()
{
	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] HideReviveWidget 호출: %s"), *GetName());

	if (!ReviveWidgetComp) return;
	if (GetNetMode() == NM_DedicatedServer) return;

	ReviveWidgetComp->SetVisibility(false);
	ReviveWidgetInstance = nullptr;
}

void AHellunaHeroCharacter::UpdateReviveWidgetBleedout()
{
	if (!ReviveWidgetInstance) return;
	if (!HeroHealthComponent) return;

	// 다운 상태일 때만 출혈 타이머 업데이트
	if (HeroHealthComponent->IsDowned())
	{
		ReviveWidgetInstance->SetBleedoutTime(HeroHealthComponent->GetBleedoutTimeRemaining());
	}
}

// =========================================================
// 부활 진행 HUD — Show/Hide/Update (부활 수행자 화면)
// =========================================================

void AHellunaHeroCharacter::ShowReviveProgressHUD(const FString& TargetName)
{
	if (!IsLocallyControlled()) return;

	if (!ReviveProgressWidget && ReviveProgressWidgetClass)
	{
		// [Phase21-Fix] As A Client 모드에서 CreateWidget(World) 실패 → PC 기반으로 변경
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC)
		{
			PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		}
		if (PC)
		{
			ReviveProgressWidget = CreateWidget<UHellunaReviveProgressWidget>(PC, ReviveProgressWidgetClass);
		}
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ReviveProgressHUD CreateWidget: PC=%s | Widget=%s"),
			PC ? *PC->GetName() : TEXT("NULL"),
			ReviveProgressWidget ? *ReviveProgressWidget->GetName() : TEXT("NULL"));
		if (ReviveProgressWidget)
		{
			ReviveProgressWidget->AddToViewport(50);
		}
	}

	if (ReviveProgressWidget)
	{
		ReviveProgressWidget->SetProgress(0.f);
		ReviveProgressWidget->SetRemainingTime(ReviveDuration);
		ReviveProgressWidget->SetTargetName(TargetName);
		ReviveProgressWidget->SetVisibility(ESlateVisibility::Visible);
		if (UWorld* World = GetWorld())
		{
			ReviveProgressShowTime = World->GetTimeSeconds();
		}
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ReviveProgressHUD Show: Time=%.2f, 대상=%s, Duration=%.1f초"),
			ReviveProgressShowTime, *TargetName, ReviveDuration);
	}
}

void AHellunaHeroCharacter::HideReviveProgressHUD()
{
	if (ReviveProgressWidget)
	{
		ReviveProgressWidget->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ReviveProgressHUD 숨김"));
	}
}

void AHellunaHeroCharacter::UpdateReviveProgressHUD()
{
	if (!IsLocallyControlled()) return;
	if (!ReviveProgressWidget) return;
	if (ReviveProgressWidget->GetVisibility() != ESlateVisibility::Visible) return;

	// 가장 가까운 다운 팀원의 ReviveProgress 읽기
	AHellunaHeroCharacter* NearestDowned = FindNearestDownedHero();
	if (NearestDowned && NearestDowned->ReviveProgress > 0.f)
	{
		float Progress = NearestDowned->ReviveProgress;
		float Remaining = ReviveDuration * (1.f - Progress);
		ReviveProgressWidget->SetProgress(Progress);
		ReviveProgressWidget->SetRemainingTime(Remaining);

		// 완료 시 자동 숨김
		if (Progress >= 1.f)
		{
			HideReviveProgressHUD();
		}
	}
	else
	{
		// Grace Period: Show 직후 0.5초간은 Progress=0이어도 숨기지 않음
		// (서버 TickRevive 시작 + 네트워크 Replication 지연 대기)
		const UWorld* World = GetWorld();
		const float ElapsedSinceShow = World ? (World->GetTimeSeconds() - ReviveProgressShowTime) : 999.f;
		if (ElapsedSinceShow > REVIVE_HUD_GRACE_PERIOD)
		{
			UE_LOG(LogHelluna, Warning, TEXT("[Phase21-Debug] ReviveProgressHUD Hide: Grace만료, Elapsed=%.2f"),
				ElapsedSinceShow);
			HideReviveProgressHUD();
		}
		// else: Grace Period 내 → 대기 (숨기지 않음)
	}
}

// =========================================================
// ★ [Phase21-C] 다운 선혈 화면 효과
// =========================================================

void AHellunaHeroCharacter::StartDownedScreenEffect()
{
	if (GetNetMode() == NM_DedicatedServer) return;

	bDownedEffectActive = true;
	DownedEffectLogTimer = 0.f;

	// v2: 50%에서 시작 (다운 즉시 위기감)
	DownedIR = IR_START;
	DownedIRTarget = IR_START;
	DownedOpacity = OP_START;
	DownedOpacityTarget = OP_START;
	DownedBrightness = 1.0f;
	DownedBrightnessTarget = 1.0f;
	DownedBlackout = 0.f;
	DownedBlackoutTarget = 0.f;

	// 첫 심장박동 펄스 즉시 발생
	PulseTimer = 0.f;
	PulsePeriod = PULSE_PERIOD_MAX;
	PulseIRBoost = PULSE_IR_AMOUNT;
	PulseOpacityBoost = PULSE_OP_AMOUNT;

	// PostProcessComponent 생성 (재활용: 이미 있으면 새로 만들지 않음)
	if (!DownedPostProcess)
	{
		DownedPostProcess = NewObject<UPostProcessComponent>(this, TEXT("DownedPostProcess"));
		if (DownedPostProcess)
		{
			DownedPostProcess->RegisterComponent();
			DownedPostProcess->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			DownedPostProcess->bUnbound = true;
			DownedPostProcess->Priority = 15.f;
		}
	}

	if (DownedPostProcess)
	{
		DownedPostProcess->Settings.bOverride_VignetteIntensity = true;
		DownedPostProcess->Settings.VignetteIntensity = 0.f;
		DownedPostProcess->Settings.bOverride_ColorSaturation = true;
		DownedPostProcess->Settings.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
		DownedPostProcess->Settings.bOverride_SceneFringeIntensity = true;
		DownedPostProcess->Settings.SceneFringeIntensity = 0.f;
		DownedPostProcess->bEnabled = true;
		DownedPostProcess->MarkRenderStateDirty();
	}

	// MID (기존 로직 유지)
	if (DownedPPMaterial && DownedPostProcess)
	{
		DownedPPMID = UMaterialInstanceDynamic::Create(DownedPPMaterial, this);
		if (DownedPPMID)
		{
			DownedPostProcess->Settings.WeightedBlendables.Array.Empty();
			DownedPostProcess->Settings.WeightedBlendables.Array.Add(
				FWeightedBlendable(0.f, DownedPPMID));
		}
	}

	// 위젯 생성 — PlayerController 기반
	if (DownedOverlayWidgetClass)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC)
		{
			PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		}
		if (PC)
		{
			DownedOverlayWidget = CreateWidget<UUserWidget>(PC, DownedOverlayWidgetClass);
			if (DownedOverlayWidget)
			{
				DownedOverlayWidget->AddToViewport(50);
				DownedOverlayWidget->SetRenderOpacity(DownedOpacity);
			}
		}
		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-C] CreateWidget: PC=%s | Widget=%s"),
			PC ? *PC->GetName() : TEXT("NULL"),
			DownedOverlayWidget ? *DownedOverlayWidget->GetName() : TEXT("NULL"));
	}

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-C] StartDownedEffect: %s | PP=%s | Widget=%s | IR=%.2f | Op=%.2f"),
		*GetName(),
		DownedPostProcess ? TEXT("Valid") : TEXT("NULL"),
		DownedOverlayWidget ? TEXT("Valid") : TEXT("NULL"),
		DownedIR, DownedOpacity);
}

void AHellunaHeroCharacter::StopDownedScreenEffect()
{
	if (!bDownedEffectActive) return;

	UE_LOG(LogHelluna, Warning, TEXT("[Phase21-C] StopDownedEffect: %s | IR=%.2f | Op=%.2f | Blackout=%.2f"),
		*GetName(), DownedIR, DownedOpacity, DownedBlackout);

	bDownedEffectActive = false;

	// 모든 값 초기화
	DownedIR = IR_START;
	DownedOpacity = 0.f;
	DownedBrightness = 1.0f;
	DownedBlackout = 0.f;
	PulseIRBoost = 0.f;
	PulseOpacityBoost = 0.f;

	// PP 복원
	if (DownedPostProcess)
	{
		DownedPostProcess->Settings.bOverride_VignetteIntensity = true;
		DownedPostProcess->Settings.VignetteIntensity = 0.f;
		DownedPostProcess->Settings.bOverride_ColorSaturation = true;
		DownedPostProcess->Settings.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
		DownedPostProcess->Settings.bOverride_ColorGain = true;
		DownedPostProcess->Settings.ColorGain = FVector4(1.f, 1.f, 1.f, 1.f);
		DownedPostProcess->MarkRenderStateDirty();
		DownedPostProcess->bEnabled = false;

		UE_LOG(LogHelluna, Warning, TEXT("[Phase21-C] PP 복원: Vignette=0 | Saturation=1.0 | Brightness=1.0"));
	}

	// MID 리셋
	if (DownedPPMID && DownedPostProcess)
	{
		if (DownedPostProcess->Settings.WeightedBlendables.Array.Num() > 0)
		{
			DownedPostProcess->Settings.WeightedBlendables.Array[0].Weight = 0.f;
		}
		DownedPPMID = nullptr;
	}

	// 다운 오버레이 위젯 제거 (피격 혈흔은 BloodHitWidget으로 분리됨)
	if (DownedOverlayWidget)
	{
		DownedOverlayWidget->RemoveFromParent();
		DownedOverlayWidget = nullptr;
	}
}

void AHellunaHeroCharacter::TickDownedScreenEffect(float DeltaTime)
{
	UHellunaHealthComponent* HC = FindComponentByClass<UHellunaHealthComponent>();
	if (!HC) return;

	const float BleedoutDuration = HC->GetBleedoutDuration();
	if (BleedoutDuration <= 0.f) return;

	// 출혈 잔여 비율: 1.0(방금 다운) → 0.0(사망 직전)
	const float BleedoutRatio = FMath::Clamp(
		HC->GetBleedoutTimeRemaining() / BleedoutDuration, 0.f, 1.f);

	// ── 1단계: InnerRadius + Opacity (전 구간) ──
	DownedIRTarget = IR_END + (IR_START - IR_END) * BleedoutRatio;
	DownedOpacityTarget = OP_END - (OP_END - OP_START) * BleedoutRatio;

	// ── 2단계: 밝기 (40% 이하에서 어두워짐) ──
	if (BleedoutRatio > DARKEN_START)
	{
		DownedBrightnessTarget = 1.0f;
	}
	else if (BleedoutRatio > BLACKOUT_START)
	{
		const float DarkenProgress = 1.0f - ((BleedoutRatio - BLACKOUT_START) / (DARKEN_START - BLACKOUT_START));
		DownedBrightnessTarget = 1.0f - (DarkenProgress * 0.7f);
	}
	else
	{
		DownedBrightnessTarget = 0.3f;
	}

	// ── 3단계: 암전 (5% 이하) ──
	if (BleedoutRatio <= BLACKOUT_START)
	{
		const float BlackoutProgress = 1.0f - (BleedoutRatio / BLACKOUT_START);
		DownedBlackoutTarget = BlackoutProgress;
	}
	else
	{
		DownedBlackoutTarget = 0.f;
	}

	// ── 심장박동 펄스 ──
	PulsePeriod = PULSE_PERIOD_MIN + (PULSE_PERIOD_MAX - PULSE_PERIOD_MIN) * BleedoutRatio;

	PulseTimer += DeltaTime;
	if (PulseTimer >= PulsePeriod)
	{
		PulseTimer -= PulsePeriod;
		PulseIRBoost = PULSE_IR_AMOUNT;
		PulseOpacityBoost = PULSE_OP_AMOUNT;
	}
	PulseIRBoost = FMath::Max(0.f, PulseIRBoost - PULSE_DECAY_SPEED * DeltaTime * PULSE_IR_AMOUNT);
	PulseOpacityBoost = FMath::Max(0.f, PulseOpacityBoost - PULSE_DECAY_SPEED * DeltaTime * PULSE_OP_AMOUNT);

	// ── 보간 ──
	DownedIR = FMath::FInterpTo(DownedIR, DownedIRTarget, DeltaTime, EFFECT_INTERP_SPEED);
	DownedOpacity = FMath::FInterpTo(DownedOpacity, DownedOpacityTarget, DeltaTime, EFFECT_INTERP_SPEED);
	DownedBrightness = FMath::FInterpTo(DownedBrightness, DownedBrightnessTarget, DeltaTime, EFFECT_INTERP_SPEED);
	DownedBlackout = FMath::FInterpTo(DownedBlackout, DownedBlackoutTarget, DeltaTime, 4.0f);

	// ── 최종 값 (펄스 적용) ──
	const float FinalIR = FMath::Max(0.02f, DownedIR - PulseIRBoost);
	const float FinalOpacity = FMath::Min(1.0f, DownedOpacity + PulseOpacityBoost);

	// ── PP 적용 ──
	if (DownedPostProcess)
	{
		const float SaturationValue = FMath::Lerp(0.45f, 1.0f, BleedoutRatio);

		// PP VignetteIntensity 사용 안 함 — 빨간 비네트는 위젯(M_BloodVignette)이 담당
		DownedPostProcess->Settings.bOverride_VignetteIntensity = true;
		DownedPostProcess->Settings.VignetteIntensity = 0.f;
		DownedPostProcess->Settings.bOverride_ColorSaturation = true;
		DownedPostProcess->Settings.ColorSaturation = FVector4(SaturationValue, SaturationValue, SaturationValue, 1.0f);

		// 밝기: ColorGain으로 제어
		DownedPostProcess->Settings.bOverride_ColorGain = true;
		DownedPostProcess->Settings.ColorGain = FVector4(DownedBrightness, DownedBrightness, DownedBrightness, 1.0f);

		DownedPostProcess->MarkRenderStateDirty();
	}

	// ── MID 파라미터 (Hurt PP 전용) ──
	if (DownedPPMID)
	{
		// Hurt PP의 "VIGNETTE INTENSITY" 파라미터로 비네트 강도 제어
		DownedPPMID->SetScalarParameterValue(FName("VIGNETTE INTENSITY"), FinalOpacity);
		// HeartBeat 파라미터로 심장박동 펄스 연동 (0.0~1.0)
		DownedPPMID->SetScalarParameterValue(FName("HeartBeat"), PulseOpacityBoost / PULSE_OP_AMOUNT);
		// HeartbeatStrength: 출혈 진행에 따라 0.25→1.0 (사망 직전 심장박동 극대화)
		const float HBStrength = FMath::Lerp(1.0f, 0.25f, BleedoutRatio);
		DownedPPMID->SetScalarParameterValue(FName("HeartbeatStrength"), HBStrength);
		// 혈관 흔들림 축소 (기본 0.18 → 0.05로 줄여 부드럽게)
		DownedPPMID->SetScalarParameterValue(FName("VeinsOffsetX"), DownedVeinsOffsetX);
		DownedPPMID->SetScalarParameterValue(FName("DropsNormalStrength"), DownedDropsNormalStrength);
	}

	// ── MID Weight ──
	if (DownedPPMID && DownedPostProcess)
	{
		if (DownedPostProcess->Settings.WeightedBlendables.Array.Num() > 0)
		{
			// Hurt PP는 PostProcess/Opaque → Weight=1.0이면 원본 화면 100% 대체
			// 혈흔 비네트를 부드럽게 보여주려면 0.0~0.35 범위로 제한
			const float PPWeight = FinalOpacity * PP_WEIGHT_MAX;
			DownedPostProcess->Settings.WeightedBlendables.Array[0].Weight = PPWeight;
		}
	}

	// 위젯 오버레이는 PP 머티리얼이 대체 — 위젯은 숨김 유지
	// (WBP_DownedOverlay의 모든 Image는 Collapsed 상태)

	// ── 암전: PP ColorGain에 Blackout 추가 반영 ──
	if (DownedPostProcess && DownedBlackout > 0.01f)
	{
		const float BlackBrightness = DownedBrightness * (1.0f - DownedBlackout);
		DownedPostProcess->Settings.ColorGain = FVector4(BlackBrightness, BlackBrightness, BlackBrightness, 1.0f);
		DownedPostProcess->MarkRenderStateDirty();
	}

	// ── 로그 (1초마다) ──
	DownedEffectLogTimer += DeltaTime;
	if (DownedEffectLogTimer >= 1.0f)
	{
		DownedEffectLogTimer = 0.f;
		UE_LOG(LogHelluna, Warning,
			TEXT("[Phase21-C] Tick: Bleed=%.0f%% | IR=%.2f | Op=%.2f | Bright=%.2f | Blackout=%.2f | Pulse=%.1fs"),
			BleedoutRatio * 100.f, FinalIR, FinalOpacity, DownedBrightness, DownedBlackout, PulsePeriod);
	}
}

// ============================================================================
// [Phase18] 킥 3D 프롬프트 — Staggered 적 머리 위에 "F: 처형" 표시
// ============================================================================
void AHellunaHeroCharacter::UpdateKickPrompt(float DeltaTime)
{
	if (!KickPromptWidgetComp) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const FVector MyLocation = GetActorLocation();
	const FVector MyForward = GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(KickPromptHalfAngle));
	const float RangeSq = KickPromptRange * KickPromptRange;

	AHellunaEnemyCharacter* BestEnemy = nullptr;
	float BestDistSq = RangeSq;

	// GetAllActorsOfClass로 모든 적을 순회 (OverlapMulti보다 안정적)
	TArray<AActor*> AllEnemies;
	UGameplayStatics::GetAllActorsOfClass(World, AHellunaEnemyCharacter::StaticClass(), AllEnemies);

	for (AActor* Actor : AllEnemies)
	{
		AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Actor);
		if (!Enemy) continue;

		// 거리 체크
		const float DistSq = FVector::DistSquared(MyLocation, Enemy->GetActorLocation());
		if (DistSq > RangeSq) continue;

		// Staggered 태그 체크
		if (!UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_Staggered))
			continue;

		// 사망 체크
		if (UHellunaHealthComponent* HC = Enemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			if (HC->IsDead()) continue;
		}

		// 전방각 체크
		const FVector ToEnemy = (Enemy->GetActorLocation() - MyLocation).GetSafeNormal();
		if (FVector::DotProduct(MyForward, ToEnemy) < CosHalfAngle) continue;

		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestEnemy = Enemy;
		}
	}

	if (BestEnemy)
	{
		const FVector EnemyHead = BestEnemy->GetActorLocation() + FVector(0.f, 0.f, 150.f);
		KickPromptWidgetComp->SetWorldLocation(EnemyHead);

		if (!bKickPromptVisible)
		{
			KickPromptWidgetComp->SetVisibility(true);
			bKickPromptVisible = true;
		}
	}
	else
	{
		if (bKickPromptVisible)
		{
			KickPromptWidgetComp->SetVisibility(false);
			bKickPromptVisible = false;
		}
	}

	// 디버그 로그 (1초마다) — 가장 가까운 적의 Staggered 여부도 출력
	KickPromptLogTimer += DeltaTime;
	if (KickPromptLogTimer >= 1.0f)
	{
		KickPromptLogTimer = 0.f;

		float ClosestDist = MAX_FLT;
		AHellunaEnemyCharacter* ClosestEnemy = nullptr;
		for (AActor* Actor : AllEnemies)
		{
			AHellunaEnemyCharacter* E = Cast<AHellunaEnemyCharacter>(Actor);
			if (!E) continue;
			float D = FVector::Dist(MyLocation, E->GetActorLocation());
			if (D < ClosestDist) { ClosestDist = D; ClosestEnemy = E; }
		}

		if (ClosestEnemy)
		{
			bool bIsStaggered = UHellunaFunctionLibrary::NativeDoesActorHaveTag(ClosestEnemy, HellunaGameplayTags::Enemy_State_Staggered);
			UE_LOG(LogHelluna, Verbose, TEXT("[Phase18] KickPrompt: Closest=%s Dist=%.0f Staggered=%s | Best=%s Visible=%s"),
				*ClosestEnemy->GetName(), ClosestDist,
				bIsStaggered ? TEXT("Y") : TEXT("N"),
				BestEnemy ? *BestEnemy->GetName() : TEXT("None"),
				bKickPromptVisible ? TEXT("Y") : TEXT("N"));
		}
	}
}

// =========================================================
// ★ [Phase21] 8방향 피격 혈흔 시스템
// =========================================================

uint8 AHellunaHeroCharacter::CalcHitDirection(AActor* InstigatorActor) const
{
	if (!InstigatorActor) return 0; // 기본: Top (전방)

	const FVector HitDir = (InstigatorActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	const float Forward = FVector::DotProduct(HitDir, GetActorForwardVector());
	const float Right = FVector::DotProduct(HitDir, GetActorRightVector());

	// atan2(Right, Forward) → 각도 (라디안)
	// 0=전방, PI/2=우측, PI=후방, -PI/2=좌측
	float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Right, Forward));
	if (AngleDeg < 0.f) AngleDeg += 360.f;

	// 8방향 매핑 (45도 단위, 22.5도 오프셋)
	// 0=Top(전방), 1=TR, 2=Right, 3=BR, 4=Bottom(후방), 5=BL, 6=Left, 7=TL
	const int32 Index = FMath::RoundToInt(AngleDeg / 45.f) % 8;
	return static_cast<uint8>(Index);
}

void AHellunaHeroCharacter::Multicast_ShowBloodHitDirection_Implementation(uint8 DirIndex)
{
	// Multicast는 모든 클라이언트에서 실행 — 로컬 플레이어만 혈흔 표시
	if (!IsLocallyControlled()) return;

	if (DirIndex >= 8) return;
	PlayBloodHitFade(DirIndex);
}

void AHellunaHeroCharacter::PlayBloodHitFade(uint8 DirIndex)
{
	// [Phase21] 지연 초기화 — BeginPlay 시점에 Controller가 없을 수 있음 (멀티플레이)
	if (!BloodHitWidget && BloodHitWidgetClass)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC && PC->IsLocalController())
		{
			BloodHitWidget = CreateWidget<UUserWidget>(PC, BloodHitWidgetClass);
			if (BloodHitWidget)
			{
				BloodHitWidget->AddToViewport(1);

				BloodHitImages.SetNum(8);
				BloodHitImages[0] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodTop")));
				BloodHitImages[1] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerTR")));
				BloodHitImages[2] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodRight")));
				BloodHitImages[3] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerBR")));
				BloodHitImages[4] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodBottom")));
				BloodHitImages[5] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerBL")));
				BloodHitImages[6] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodLeft")));
				BloodHitImages[7] = Cast<UImage>(BloodHitWidget->GetWidgetFromName(TEXT("BloodCornerTL")));

				if (BloodHitTexture)
				{
					for (UImage* Img : BloodHitImages)
					{
						if (Img)
						{
							FSlateBrush Brush;
							Brush.SetResourceObject(BloodHitTexture);
							Brush.DrawAs = ESlateBrushDrawType::Image;
							Brush.ImageSize = FVector2D(256, 256);
							Img->SetBrush(Brush);
							Img->SetColorAndOpacity(FLinearColor(1.0f, 0.1f, 0.05f, 1.0f));
							Img->SetRenderOpacity(0.f);
							Img->SetVisibility(ESlateVisibility::Collapsed);
						}
					}
				}

				BloodHitWidget->SetRenderOpacity(1.0f);
				UE_LOG(LogHelluna, Warning, TEXT("[Phase21] BloodHitWidget 지연 초기화 완료: %s, WidgetOpacity=%.2f"),
					*GetName(), BloodHitWidget->GetRenderOpacity());
			}
		}
	}

	UE_LOG(LogHelluna, Warning, TEXT("[BloodHit] PlayFade: Dir=%d, Widget=%s, Class=%s, ImgNum=%d"),
		DirIndex, BloodHitWidget ? TEXT("Y") : TEXT("N"),
		BloodHitWidgetClass ? TEXT("Y") : TEXT("N"),
		BloodHitImages.Num());
	if (!BloodHitWidget) return;
	if (DirIndex >= static_cast<uint8>(BloodHitImages.Num())) return;

	UImage* Img = BloodHitImages[DirIndex];
	UE_LOG(LogHelluna, Warning, TEXT("[BloodHit] PlayFade: Img[%d]=%s"), DirIndex, Img ? TEXT("VALID") : TEXT("NULL"));
	if (!Img) return;

	UWorld* W = GetWorld();
	if (!W) return;

	// 기존 페이드 중이면 리셋
	W->GetTimerManager().ClearTimer(BloodHitTimers[DirIndex]);

	// 즉시 표시
	Img->SetVisibility(ESlateVisibility::HitTestInvisible);
	Img->SetRenderOpacity(1.0f);
	
	UE_LOG(LogHelluna, Warning, TEXT("[BloodHit] Image SHOW: Dir=%d, Vis=%d, Opacity=%.2f, DesiredSize=%.0fx%.0f"),
		DirIndex,
		(int32)Img->GetVisibility(),
		Img->GetRenderOpacity(),
		Img->GetDesiredSize().X, Img->GetDesiredSize().Y);

	// 페이드아웃 (0.016초 간격 루핑 타이머)
	const float FadeStep = 0.016f;
	const float DecayPerStep = FadeStep / BloodHitFadeDuration;

	W->GetTimerManager().SetTimer(
		BloodHitTimers[DirIndex],
		FTimerDelegate::CreateWeakLambda(this, [this, DirIndex, DecayPerStep]()
		{
			if (DirIndex >= static_cast<uint8>(BloodHitImages.Num())) return;
			UImage* FadeImg = BloodHitImages[DirIndex];
			if (!FadeImg) return;

			float NewOpacity = FadeImg->GetRenderOpacity() - DecayPerStep;
			if (NewOpacity <= 0.f)
			{
				FadeImg->SetRenderOpacity(0.f);
				FadeImg->SetVisibility(ESlateVisibility::Collapsed);
				if (UWorld* TimerWorld = GetWorld())
				{
					TimerWorld->GetTimerManager().ClearTimer(BloodHitTimers[DirIndex]);
				}
			}
			else
			{
				FadeImg->SetRenderOpacity(NewOpacity);
			}
		}),
		FadeStep,
		true  // looping
	);
}
