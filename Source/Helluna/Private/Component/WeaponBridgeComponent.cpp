// ============================================
// ⭐ WeaponBridgeComponent
// ⭐ 작성자: Gihyeon
// ⭐ 목적: Inventory 플러그인 ↔ Helluna 무기 시스템 브릿지
// ⭐ 
// ⭐ [역할]
// ⭐ - EquipmentComponent의 델리게이트를 수신
// ⭐ - 무기 꺼내기: 팀원의 GA_SpawnWeapon 활성화
// ⭐ - 무기 집어넣기: CurrentWeapon Destroy
// ⭐ 
// ⭐ [흐름]
// ⭐ 1. BeginPlay에서 EquipmentComponent 찾기
// ⭐ 2. OnWeaponEquipRequested 델리게이트 바인딩
// ⭐ 3. 1키 입력 시 델리게이트 수신
// ⭐ 4. bEquip에 따라 SpawnHandWeapon(GA 활성화) 또는 DestroyHandWeapon 호출
// ============================================

#include "Component/WeaponBridgeComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Items/Fragments/Inv_AttachmentFragments.h" // 김기현 — 부착물 시각 전달용
#include "Weapon/HellunaHeroWeapon.h"
#include "Abilities/GameplayAbility.h"
#include "Net/UnrealNetwork.h"

// ============================================
// ⭐ 생성자
// ⭐ Tick 비활성화 (이벤트 기반으로만 동작)
// ============================================
UWeaponBridgeComponent::UWeaponBridgeComponent()
{
	// Tick 사용 안 함 - 델리게이트 이벤트 기반으로 동작
	PrimaryComponentTick.bCanEverTick = false;

	// RPC 지원을 위해 리플리케이션 활성화
	SetIsReplicatedByDefault(true);
}

// ============================================
// ⭐ BeginPlay
// ⭐ 컴포넌트 시작 시 초기화 함수 호출
// ============================================
void UWeaponBridgeComponent::BeginPlay()
{
	Super::BeginPlay();
	
	InitializeWeaponBridge();
}

// ============================================
// ⭐ SetEquipping
// ⭐ 장착 애니메이션 진행 상태 변경
// ⭐ true: 장착 중 (무기 전환 차단)
// ⭐ false: 장착 완료 (무기 전환 허용)
// ============================================
void UWeaponBridgeComponent::SetEquipping(bool bNewEquipping)
{
	bIsEquipping = bNewEquipping;
	
	// ⭐ EquipmentComponent의 플래그도 동기화
	if (EquipmentComponent.IsValid())
	{
		EquipmentComponent->SetWeaponEquipping(bNewEquipping);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SetEquipping: %s"), 
		bIsEquipping ? TEXT("true (장착 중 - 전환 차단)") : TEXT("false (장착 완료 - 전환 허용)"));
}

// ============================================
// ⭐ InitializeWeaponBridge
// ⭐ Character, ASC, EquipmentComponent 참조 획득
// ⭐ EquipmentComponent의 델리게이트에 바인딩
// ============================================
void UWeaponBridgeComponent::InitializeWeaponBridge()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] InitializeWeaponBridge 시작"));
	
	// ⭐ Character 참조 가져오기
	// 이 컴포넌트는 HellunaHeroCharacter에 부착됨
	OwningCharacter = Cast<AHellunaHeroCharacter>(GetOwner());
	if (!OwningCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] OwningCharacter가 null! GetOwner: %s"), 
			GetOwner() ? *GetOwner()->GetName() : TEXT("nullptr"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] OwningCharacter 찾음: %s"), *OwningCharacter->GetName());
	
	// ⭐ ASC 참조 가져오기 (GA 활성화에 필수!)
	AbilitySystemComponent = Cast<UHellunaAbilitySystemComponent>(
		OwningCharacter->GetAbilitySystemComponent()
	);
	if (AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] AbilitySystemComponent 찾음"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] AbilitySystemComponent를 찾을 수 없음!"));
	}
	
	// ⭐ EquipmentComponent 찾기
	// EquipmentComponent는 PlayerController에 부착되어 있음
	if (APlayerController* PC = Cast<APlayerController>(OwningCharacter->GetController()))
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] PlayerController 찾음: %s"), *PC->GetName());
		
		EquipmentComponent = PC->FindComponentByClass<UInv_EquipmentComponent>();
		
		if (EquipmentComponent.IsValid())
		{
			// ⭐ 델리게이트 바인딩
			// 이미 바인딩되어 있으면 중복 방지
			if (!EquipmentComponent->OnWeaponEquipRequested.IsAlreadyBound(this, &ThisClass::OnWeaponEquipRequested))
			{
				EquipmentComponent->OnWeaponEquipRequested.AddDynamic(this, &ThisClass::OnWeaponEquipRequested);
				UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 델리게이트 바인딩 성공!"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] EquipmentComponent를 찾을 수 없음!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] PlayerController를 찾을 수 없음!"));
	}
}

// ============================================
// ⭐ OnWeaponEquipRequested (델리게이트 콜백)
// ⭐ EquipmentComponent에서 무기 꺼내기/집어넣기 요청 시 호출됨
// ⭐ 
// ⭐ @param WeaponTag: 무기 종류 태그 (예: GameItems.Equipment.Weapons.Axe)
// ⭐ @param BackWeaponActor: 등에 붙은 무기 Actor (Hidden 처리용)
// ⭐ @param SpawnWeaponAbility: 활성화할 GA 클래스 (팀원의 GA_SpawnWeapon)
// ⭐ @param bEquip: true=꺼내기, false=집어넣기
// ⭐ @param WeaponSlotIndex: 무기 슬롯 인덱스 (0=주무기, 1=보조무기)
// ============================================
void UWeaponBridgeComponent::OnWeaponEquipRequested(
	const FGameplayTag& WeaponTag,
	AInv_EquipActor* BackWeaponActor,
	TSubclassOf<UGameplayAbility> SpawnWeaponAbility,
	bool bEquip,
	int32 WeaponSlotIndex)
{

	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] OnWeaponEquipRequested 수신!"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - WeaponTag: %s"), *WeaponTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - BackWeaponActor: %s"), 
		BackWeaponActor ? *BackWeaponActor->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - SpawnWeaponAbility: %s"), 
		SpawnWeaponAbility ? *SpawnWeaponAbility->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - bEquip: %s"), bEquip ? TEXT("true (꺼내기)") : TEXT("false (집어넣기)"));
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] - WeaponSlotIndex: %d (%s)"), 
		WeaponSlotIndex, WeaponSlotIndex == 0 ? TEXT("주무기") : TEXT("보조무기"));
	
	if (bEquip)
	{
		// ⭐ 무기 꺼내기 - GA 활성화
		SpawnHandWeapon(SpawnWeaponAbility);

		// ⭐ [김기현] 부착물 시각 전달: Server RPC로 서버에 요청
		// 서버에서 EquipActor의 AttachmentMeshComponents 데이터를 읽어
		// Multicast RPC로 모든 클라이언트에 전송
		if (IsValid(BackWeaponActor))
		{
			Server_RequestAttachmentTransfer(BackWeaponActor);
		}
	}
	else
	{
		// ⭐ 무기 집어넣기
		DestroyHandWeapon();
	}
}

// ============================================
// ⭐ SpawnHandWeapon
// ⭐ 팀원의 GA_SpawnWeapon을 활성화하여 무기 스폰
// ⭐ 
// ⭐ @param SpawnWeaponAbility: 활성화할 GA 클래스
// ⭐ 
// ⭐ [처리 과정]
// ⭐ 1. ASC 유효성 검사
// ⭐ 2. GA 클래스 유효성 검사
// ⭐ 3. TryActivateAbilityByClass() 호출
// ⭐ 4. GA 내부에서 Server_RequestSpawnWeapon 등 처리
// ============================================
void UWeaponBridgeComponent::SpawnHandWeapon(TSubclassOf<UGameplayAbility> SpawnWeaponAbility)
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnHandWeapon 시작 (GA 방식)"));
	
	if (!SpawnWeaponAbility)
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] SpawnWeaponAbility가 null!"));
		return;
	}
	
	if (!AbilitySystemComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] AbilitySystemComponent가 null!"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] GA 활성화 시도: %s"), *SpawnWeaponAbility->GetName());
	
	// ⭐ GA 활성화!
	// 팀원의 GA_SpawnWeapon 내부에서 Server_RequestSpawnWeapon, 몽타주 등 처리됨
	bool bSuccess = AbilitySystemComponent->TryActivateAbilityByClass(SpawnWeaponAbility);
	
	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] GA 활성화 성공!"));
		
		// ⭐ 장착 애니메이션 시작 → 무기 전환 차단
		// GA_SpawnWeapon::OnEquipFinished/OnEquipInterrupted에서 SetEquipping(false) 호출
		bIsEquipping = true;
		if (EquipmentComponent.IsValid())
		{
			EquipmentComponent->SetWeaponEquipping(true);
		}
		UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] bIsEquipping = true (장착 중 - 전환 차단)"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] GA 활성화 실패!"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] SpawnHandWeapon 완료"));
}

// ============================================
// ⭐ DestroyHandWeapon
// ⭐ 손에 든 무기를 제거하는 함수
// ⭐ Server RPC를 통해 서버에서 CurrentWeapon Destroy
// ⭐ 
// ⭐ [호출 시점]
// ⭐ - 무기 집어넣기 (1키 다시 누름)
// ⭐ - 무기 전환 (주무기 → 보조무기)
// ============================================
void UWeaponBridgeComponent::DestroyHandWeapon()
{
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] DestroyHandWeapon 시작"));

	// ⭐ [김기현] 부착물 전달 대기 중이면 취소 (서버 타이머)
	if (AttachmentTransferTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AttachmentTransferTimerHandle);
		}
		PendingAttachmentSource.Reset();
	}

	// ⭐ Multicast 수신 후 대기 타이머도 취소 (클라이언트 로컬)
	if (MulticastVisualTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(MulticastVisualTimerHandle);
		}
		PendingMulticastVisuals.Empty();
	}

	if (!OwningCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("⭐ [WeaponBridge] OwningCharacter가 null!"));
		return;
	}

	// ⭐ Server RPC 호출하여 서버에서 Destroy
	// CurrentWeapon은 서버에서 스폰되어 리플리케이트된 액터이므로 서버에서만 Destroy 가능
	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] Server_RequestDestroyWeapon 호출"));
	OwningCharacter->Server_RequestDestroyWeapon();

	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] DestroyHandWeapon 완료"));
}

// ============================================
// ⭐ Server_RequestAttachmentTransfer (Server RPC)
// ⭐ 소유 클라이언트 → 서버: 부착물 전달 요청
// ⭐ 서버에서 EquipActor 데이터가 존재하므로 서버에서 읽어서 Multicast
// ============================================
void UWeaponBridgeComponent::Server_RequestAttachmentTransfer_Implementation(AInv_EquipActor* EquipActor)
{
	if (!IsValid(EquipActor)) return;

	PendingAttachmentSource = EquipActor;
	AttachmentTransferRetryCount = 0;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AttachmentTransferTimerHandle,
			this,
			&UWeaponBridgeComponent::WaitAndTransferAttachments,
			0.05f,
			true  // 반복 타이머
		);
	}
}

// ============================================
// ⭐ WaitAndTransferAttachments (서버 타이머 콜백)
// 작성: 김기현 (인벤토리 부착물 시스템 연동)
// ⭐ 서버에서 실행: HandWeapon 스폰 대기 후 Multicast RPC로 부착물 데이터 전송
// ⭐ 0.05초 간격 반복, 최대 5초(100회) 시도
// ============================================
void UWeaponBridgeComponent::WaitAndTransferAttachments()
{
	++AttachmentTransferRetryCount;

	// 참조 유효성 검사 — 무효하면 타이머 종료
	if (!OwningCharacter.IsValid() || !PendingAttachmentSource.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AttachmentTransferTimerHandle);
		}
		PendingAttachmentSource.Reset();
		return;
	}

	// HandWeapon(손 무기)이 서버에 스폰되었는지 확인
	AHellunaHeroWeapon* HandWeapon = OwningCharacter->GetCurrentWeapon();
	if (IsValid(HandWeapon))
	{
		// 서버에서 EquipActor의 부착물 데이터 읽기 (서버에만 데이터 존재!)
		TArray<FInv_AttachmentVisualInfo> Visuals = PendingAttachmentSource->GetAttachmentVisualInfos();

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AttachmentTransferTimerHandle);
		}
		PendingAttachmentSource.Reset();

		if (Visuals.Num() > 0)
		{
			// Multicast RPC로 모든 클라이언트에 부착물 시각 데이터 전송
			Multicast_ApplyAttachmentVisuals(Visuals);
		}
		return;
	}

	// 타임아웃 체크
	if (AttachmentTransferRetryCount >= MaxAttachmentTransferRetries)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("⭐ [WeaponBridge] 부착물 전달 타임아웃: HandWeapon이 %d회(%.1f초) 시도 후에도 유효하지 않음"),
			MaxAttachmentTransferRetries, MaxAttachmentTransferRetries * 0.05f);

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AttachmentTransferTimerHandle);
		}
		PendingAttachmentSource.Reset();
	}
}

// ============================================
// ⭐ Multicast_ApplyAttachmentVisuals (NetMulticast RPC)
// ⭐ 서버 → 모든 클라이언트: 부착물 시각 데이터 수신 및 적용
// ⭐ HandWeapon이 아직 없으면 로컬 타이머로 대기
// ============================================
void UWeaponBridgeComponent::Multicast_ApplyAttachmentVisuals_Implementation(const TArray<FInv_AttachmentVisualInfo>& Visuals)
{
	if (!OwningCharacter.IsValid()) return;

	// HandWeapon이 이미 유효하면 즉시 적용
	AHellunaHeroWeapon* HandWeapon = OwningCharacter->GetCurrentWeapon();
	if (IsValid(HandWeapon))
	{
		ApplyVisualsToHandWeapon(Visuals);
		return;
	}

	// HandWeapon이 아직 없으면 로컬 타이머로 대기
	PendingMulticastVisuals = Visuals;
	MulticastVisualRetryCount = 0;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			MulticastVisualTimerHandle,
			this,
			&UWeaponBridgeComponent::OnMulticastVisualTimerTick,
			0.05f,
			true
		);
	}
}

// ============================================
// ⭐ OnMulticastVisualTimerTick (클라이언트 로컬 타이머)
// ⭐ Multicast 수신 후 HandWeapon 대기 → 유효해지면 적용
// ============================================
void UWeaponBridgeComponent::OnMulticastVisualTimerTick()
{
	++MulticastVisualRetryCount;

	if (!OwningCharacter.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(MulticastVisualTimerHandle);
		}
		PendingMulticastVisuals.Empty();
		return;
	}

	AHellunaHeroWeapon* HandWeapon = OwningCharacter->GetCurrentWeapon();
	if (IsValid(HandWeapon))
	{
		ApplyVisualsToHandWeapon(PendingMulticastVisuals);

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(MulticastVisualTimerHandle);
		}
		PendingMulticastVisuals.Empty();
		return;
	}

	// 타임아웃 (100회 = 5초)
	if (MulticastVisualRetryCount >= MaxAttachmentTransferRetries)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("⭐ [WeaponBridge] Multicast 부착물 적용 타임아웃: HandWeapon이 %d회 시도 후에도 유효하지 않음"),
			MaxAttachmentTransferRetries);

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(MulticastVisualTimerHandle);
		}
		PendingMulticastVisuals.Empty();
	}
}

// ============================================
// ⭐ ApplyVisualsToHandWeapon
// ⭐ 부착물 시각 데이터 배열을 HandWeapon에 적용
// ============================================
void UWeaponBridgeComponent::ApplyVisualsToHandWeapon(const TArray<FInv_AttachmentVisualInfo>& Visuals)
{
	if (!OwningCharacter.IsValid()) return;

	AHellunaHeroWeapon* HandWeapon = OwningCharacter->GetCurrentWeapon();
	if (!IsValid(HandWeapon)) return;

	for (const FInv_AttachmentVisualInfo& Info : Visuals)
	{
		if (!IsValid(Info.Mesh)) continue;

		HandWeapon->ApplyAttachmentVisual(
			Info.SlotIndex,
			Info.Mesh,
			Info.SocketName,
			Info.Offset
		);
	}

	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 부착물 시각 적용 완료: %d개 → HandWeapon: %s"),
		Visuals.Num(), *HandWeapon->GetName());
}

// ============================================
// ⭐ TransferAttachmentVisuals
// 작성: 김기현 (인벤토리 부착물 시스템 연동)
// ⭐ EquipActor(등 무기) → HandWeapon(손 무기)으로 부착물 메시 복제
// ⭐ 인벤토리 플러그인의 GetAttachmentVisualInfos() Getter 사용
// ============================================
void UWeaponBridgeComponent::TransferAttachmentVisuals(AInv_EquipActor* EquipActor, AHellunaHeroWeapon* HandWeapon)
{
	if (!IsValid(EquipActor) || !IsValid(HandWeapon)) return;

	// EquipActor에서 부착물 시각 정보 읽기 (인벤토리 플러그인 Getter)
	TArray<FInv_AttachmentVisualInfo> Visuals = EquipActor->GetAttachmentVisualInfos();

	if (Visuals.Num() == 0) return;

	for (const FInv_AttachmentVisualInfo& Info : Visuals)
	{
		if (!IsValid(Info.Mesh)) continue;

		HandWeapon->ApplyAttachmentVisual(
			Info.SlotIndex,
			Info.Mesh,
			Info.SocketName,
			Info.Offset
		);
	}

	UE_LOG(LogTemp, Warning, TEXT("⭐ [WeaponBridge] 부착물 시각 전달 완료: %d개 (EquipActor: %s → HandWeapon: %s)"),
		Visuals.Num(), *EquipActor->GetName(), *HandWeapon->GetName());
}
