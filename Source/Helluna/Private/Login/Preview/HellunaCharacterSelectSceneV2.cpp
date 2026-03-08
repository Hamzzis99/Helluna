#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "Helluna.h"
#include "AnimInstance/HellunaPreviewAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

// ============================================
// 생성자
// ============================================

AHellunaCharacterSelectSceneV2::AHellunaCharacterSelectSceneV2()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	bNetLoadOnClient = false;

	// ════════════════════════════════════════════
	// 컴포넌트 생성 및 계층 구성
	// ════════════════════════════════════════════

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 메인 조명
	MainLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MainLight"));
	MainLight->SetupAttachment(SceneRoot);
	MainLight->SetIntensity(50000.f);
	MainLight->SetAttenuationRadius(2000.f);
	MainLight->SetRelativeLocation(FVector(300.f, 0.f, 300.f));

	MainLight->LightingChannels.bChannel0 = false;  // 배경 안 밝힘
	MainLight->LightingChannels.bChannel1 = true;   // 캐릭터만 밝힘

	// 보조 조명
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetIntensity(20000.f);
	FillLight->SetAttenuationRadius(2000.f);
	FillLight->SetRelativeLocation(FVector(-200.f, -300.f, 150.f));

	FillLight->LightingChannels.bChannel0 = false;  // 배경 안 밝힘
	FillLight->LightingChannels.bChannel1 = true;   // 캐릭터만 밝힘
}

// ============================================
// 씬 초기화
// ============================================

void AHellunaCharacterSelectSceneV2::InitializeScene(
	const TArray<USkeletalMesh*>& InMeshes,
	const TArray<TSubclassOf<UAnimInstance>>& InAnimClasses)
{
	// ════════════════════════════════════════════
	// 인자 검증
	// ════════════════════════════════════════════

	if (InMeshes.Num() != InAnimClasses.Num())
	{
		UE_LOG(LogHelluna, Error, TEXT("[프리뷰V2] InitializeScene 실패 - Meshes(%d)와 AnimClasses(%d) 수 불일치!"),
			InMeshes.Num(), InAnimClasses.Num());
		return;
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [프리뷰V2] InitializeScene                               ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ 캐릭터 수: %d"), InMeshes.Num());
	UE_LOG(LogHelluna, Warning, TEXT("║ CharacterSpacing: %.1f"), CharacterSpacing);
	UE_LOG(LogHelluna, Warning, TEXT("║ CameraOffset: %s"), *CameraOffset.ToString());
	UE_LOG(LogHelluna, Warning, TEXT("║ CameraFOV: %.1f"), CameraFOV);
#endif

	const int32 Num = InMeshes.Num();

	// ════════════════════════════════════════════
	// 캐릭터 메시 동적 생성
	// ════════════════════════════════════════════

	PreviewMeshes.Empty();

	for (int32 i = 0; i < Num; i++)
	{
		if (!InMeshes[i])
		{
			UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] InMeshes[%d]가 nullptr - 스킵"), i);
			PreviewMeshes.Add(nullptr);
			continue;
		}

		if (!InAnimClasses[i])
		{
			UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] InAnimClasses[%d]가 nullptr - 스킵"), i);
			PreviewMeshes.Add(nullptr);
			continue;
		}

		USkeletalMeshComponent* MeshComp = NewObject<USkeletalMeshComponent>(this,
			*FString::Printf(TEXT("PreviewMesh_%d"), i));
		MeshComp->RegisterComponent();
		MeshComp->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// ════════════════════════════════════════════
		// 위치: CharacterOffsets 우선, 없으면 CharacterSpacing 균등 배치
		// ════════════════════════════════════════════
		FVector MeshLocation;
		if (CharacterOffsets.IsValidIndex(i))
		{
			MeshLocation = CharacterOffsets[i];
		}
		else
		{
			const float XOffset = i * CharacterSpacing - (Num - 1) * CharacterSpacing * 0.5f;
			MeshLocation = FVector(XOffset, 0.f, 0.f);
		}
		MeshComp->SetRelativeLocation(MeshLocation);

		// ════════════════════════════════════════════
		// 회전: CharacterRotations 우선, 없으면 기본 -90도 (카메라 정면)
		// ════════════════════════════════════════════
		FRotator MeshRotation = CharacterRotations.IsValidIndex(i)
			? CharacterRotations[i]
			: FRotator(0.f, -90.f, 0.f);
		MeshComp->SetRelativeRotation(MeshRotation);

		// ════════════════════════════════════════════
		// 스케일: CharacterScales 우선, 없으면 기본 (1,1,1)
		// ════════════════════════════════════════════
		if (CharacterScales.IsValidIndex(i))
		{
			MeshComp->SetRelativeScale3D(CharacterScales[i]);
		}

		MeshComp->SetSkeletalMeshAsset(InMeshes[i]);
		MeshComp->SetAnimInstanceClass(InAnimClasses[i]);

		PreviewMeshes.Add(MeshComp);

		// LightChannel 1 활성화 — 프리뷰 전용 조명을 받기 위해
		MeshComp->LightingChannels.bChannel1 = true;

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
		UE_LOG(LogHelluna, Warning, TEXT("║ [%d] Mesh: %s, Loc: %s, Rot: %s"),
			i, *InMeshes[i]->GetName(), *MeshLocation.ToString(), *MeshRotation.ToString());
#endif
	}

	// ════════════════════════════════════════════
	// OriginalLocations 저장 + 캐릭터별 스포트라이트 생성
	// ════════════════════════════════════════════

	OriginalLocations.Empty();
	CharacterSpotLights.Empty();

	for (int32 i = 0; i < PreviewMeshes.Num(); i++)
	{
		if (PreviewMeshes[i])
		{
			OriginalLocations.Add(PreviewMeshes[i]->GetRelativeLocation());
		}
		else
		{
			OriginalLocations.Add(FVector::ZeroVector);
		}

		// 캐릭터 머리 위에서 아래로 비추는 스포트라이트
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this,
			*FString::Printf(TEXT("CharSpotLight_%d"), i));
		Spot->RegisterComponent();
		Spot->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);

		// 위치: 캐릭터 위 + 약간 앞에서 아래로 비춤
		FVector SpotLoc = OriginalLocations[i] + FVector(100.f, 0.f, 350.f);
		Spot->SetRelativeLocation(SpotLoc);
		Spot->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f)); // 위에서 아래로

		Spot->SetIntensity(80000.f);
		Spot->SetAttenuationRadius(1000.f);

		Spot->LightingChannels.bChannel0 = false;  // 배경 안 밝힘
		Spot->LightingChannels.bChannel1 = true;   // 캐릭터만 밝힘
		Spot->SetInnerConeAngle(15.f);
		Spot->SetOuterConeAngle(35.f);

		CharacterSpotLights.Add(Spot);
	}

	// ════════════════════════════════════════════
	// Solo 센터 메시 생성 (Play 탭용 — 항상 카메라 정중앙)
	// ════════════════════════════════════════════
	if (!SoloCenterMesh)
	{
		SoloCenterMesh = NewObject<USkeletalMeshComponent>(this, TEXT("SoloCenterMesh"));
		SoloCenterMesh->RegisterComponent();
		SoloCenterMesh->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		SoloCenterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SoloCenterMesh->SetRelativeLocation(SoloCenterOffset);
		SoloCenterMesh->SetRelativeRotation(SoloCenterRotation);
		SoloCenterMesh->SetRelativeScale3D(SoloCenterScale);
		SoloCenterMesh->SetVisibility(false); // 초기엔 숨김
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("║ 씬 초기화 완료 (직접 뷰포트 카메라 모드)"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

// ============================================
// 호버 ON/OFF
// ============================================

void AHellunaCharacterSelectSceneV2::SetCharacterHovered(int32 Index, bool bHovered)
{
	if (!PreviewMeshes.IsValidIndex(Index) || !PreviewMeshes[Index])
	{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
		UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetCharacterHovered 실패 - Index %d 범위 초과 또는 nullptr"), Index);
#endif
		return;
	}

	USkeletalMeshComponent* MeshComp = PreviewMeshes[Index];

	// 오버레이 머티리얼
	if (HighlightMaterials.IsValidIndex(Index))
	{
		MeshComp->SetOverlayMaterial(bHovered ? HighlightMaterials[Index] : nullptr);
	}

	// AnimBP 호버 상태
	UHellunaPreviewAnimInstance* AnimInst = Cast<UHellunaPreviewAnimInstance>(MeshComp->GetAnimInstance());
	if (AnimInst)
	{
		AnimInst->bIsHovered = bHovered;
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetCharacterHovered(%d, %s)"), Index, bHovered ? TEXT("true") : TEXT("false"));
#endif
}

// ============================================
// Getter
// ============================================

int32 AHellunaCharacterSelectSceneV2::GetCharacterCount() const
{
	return PreviewMeshes.Num();
}

// ============================================
// 선택 상태 연출
// ============================================

void AHellunaCharacterSelectSceneV2::SetCharacterSelected(int32 SelectedIndex)
{
	CurrentSelectedIndex = SelectedIndex;

	for (int32 i = 0; i < PreviewMeshes.Num(); i++)
	{
		if (!PreviewMeshes[i]) continue;

		const bool bIsSelected = (i == SelectedIndex);
		const bool bNoSelection = (SelectedIndex < 0);

		// 위치: 선택된 캐릭터는 카메라 쪽(Y+)으로 전진, 나머지는 원래 자리
		FVector TargetLoc = OriginalLocations.IsValidIndex(i) ? OriginalLocations[i] : FVector::ZeroVector;
		if (bIsSelected)
		{
			TargetLoc.Y += SelectedForwardOffset;
		}
		PreviewMeshes[i]->SetRelativeLocation(TargetLoc);

		// 스포트라이트: 선택된 캐릭터는 밝게, 나머지는 어둡게 (미선택 시 모두 밝게)
		if (CharacterSpotLights.IsValidIndex(i) && CharacterSpotLights[i])
		{
			const float FullIntensity = 80000.f;
			if (bNoSelection)
			{
				CharacterSpotLights[i]->SetIntensity(FullIntensity);
			}
			else
			{
				CharacterSpotLights[i]->SetIntensity(bIsSelected ? FullIntensity : FullIntensity * UnselectedBrightnessRatio);
			}
		}
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetCharacterSelected(%d)"), SelectedIndex);
#endif
}

// ============================================
// Solo 모드 — Play 탭에서 캐릭터 1개만 표시
// ============================================

void AHellunaCharacterSelectSceneV2::SetSoloCharacter(int32 CharacterIndex)
{
	if (CharacterIndex < 0 || CharacterIndex >= PreviewMeshes.Num())
	{
		UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetSoloCharacter: 잘못된 인덱스 %d (캐릭터 수=%d)"),
			CharacterIndex, PreviewMeshes.Num());
		return;
	}

	bSoloMode = true;
	SoloCharacterIndex = CharacterIndex;

	// ── 기존 PreviewMeshes + 스포트라이트 전체 숨김 ──
	for (int32 i = 0; i < PreviewMeshes.Num(); ++i)
	{
		if (PreviewMeshes[i])
		{
			PreviewMeshes[i]->SetVisibility(false);
		}
		if (CharacterSpotLights.IsValidIndex(i) && CharacterSpotLights[i])
		{
			CharacterSpotLights[i]->SetVisibility(false);
		}
	}

	// ── SoloCenterMesh에 선택 캐릭터 복사 ──
	if (SoloCenterMesh && PreviewMeshes[CharacterIndex])
	{
		USkeletalMeshComponent* SourceMesh = PreviewMeshes[CharacterIndex];

		// 메시 복사
		SoloCenterMesh->SetSkeletalMesh(SourceMesh->GetSkeletalMeshAsset());

		// 애님 클래스 복사
		SoloCenterMesh->SetAnimInstanceClass(SourceMesh->GetAnimClass());

	SoloCenterMesh->LightingChannels.bChannel1 = true;  // 프리뷰 조명 수신

		// 머티리얼 복사 (오버레이 등)
		for (int32 MatIdx = 0; MatIdx < SourceMesh->GetNumMaterials(); ++MatIdx)
		{
			SoloCenterMesh->SetMaterial(MatIdx, SourceMesh->GetMaterial(MatIdx));
		}

		// 가시성 ON
		SoloCenterMesh->SetVisibility(true);
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetSoloCharacter(%d) — SoloCenterMesh에 복사, 중앙 표시"), CharacterIndex);
#endif
}

void AHellunaCharacterSelectSceneV2::ClearSoloMode()
{
	if (!bSoloMode) return;

	bSoloMode = false;
	SoloCharacterIndex = -1;

	// ── SoloCenterMesh 숨김 ──
	if (SoloCenterMesh)
	{
		SoloCenterMesh->SetVisibility(false);
	}

	// ── 기존 PreviewMeshes + 스포트라이트 전체 복원 ──
	for (int32 i = 0; i < PreviewMeshes.Num(); ++i)
	{
		if (PreviewMeshes[i])
		{
			PreviewMeshes[i]->SetVisibility(true);
		}
		if (CharacterSpotLights.IsValidIndex(i) && CharacterSpotLights[i])
		{
			CharacterSpotLights[i]->SetVisibility(true);
		}
	}

	// 기존 Selected 상태 복원 (위치/밝기)
	SetCharacterSelected(CurrentSelectedIndex);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] ClearSoloMode — SoloCenterMesh 숨김, 전체 표시 복원"));
#endif
}

// ============================================
// [Phase 12g-2] Party 모드 — PUBG 스타일 나란히 서기
// ============================================

void AHellunaCharacterSelectSceneV2::SetPartyPreview(
	const TArray<FHellunaPartyMemberInfo>& PartyMembers,
	const FString& LocalPlayerId,
	const TMap<int32, USkeletalMesh*>& InMeshMap,
	const TMap<int32, TSubclassOf<UAnimInstance>>& InAnimClassMap)
{
	if (PartyMembers.Num() == 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2-Party] SetPartyPreview: 멤버 0명 → 무시"));
		return;
	}

	// ── 최적화: 영웅 타입 변화 없으면 스킵 ──
	TArray<int32> NewHeroTypes;
	NewHeroTypes.Reserve(PartyMembers.Num());
	for (const FHellunaPartyMemberInfo& M : PartyMembers)
	{
		NewHeroTypes.Add(M.SelectedHeroType);
	}

	if (bPartyMode && CachedPartyHeroTypes == NewHeroTypes)
	{
		return; // 영웅 타입 변화 없음 → 재생성 불필요
	}
	CachedPartyHeroTypes = NewHeroTypes;

	// ── 1) 기존 모드 정리 ──
	if (bSoloMode)
	{
		// ClearSoloMode 내부에서 PreviewMeshes 복원함 → 이후 다시 숨길 것
		bSoloMode = false;
		SoloCharacterIndex = -1;
		if (IsValid(SoloCenterMesh))
		{
			SoloCenterMesh->SetVisibility(false);
		}
	}

	// 기존 파티 프리뷰 정리 (있으면)
	ClearPartyPreview();
	bPartyMode = true;

	// ── 2) 기존 PreviewMeshes + SoloCenterMesh + CharacterSpotLights 숨김 ──
	for (int32 i = 0; i < PreviewMeshes.Num(); ++i)
	{
		if (IsValid(PreviewMeshes[i]))
		{
			PreviewMeshes[i]->SetVisibility(false);
		}
		if (CharacterSpotLights.IsValidIndex(i) && IsValid(CharacterSpotLights[i]))
		{
			CharacterSpotLights[i]->SetVisibility(false);
		}
	}
	if (IsValid(SoloCenterMesh))
	{
		SoloCenterMesh->SetVisibility(false);
	}

	// ── 3) 슬롯 할당: 리더 = Slot 1(중앙), 멤버 = Slot 0(좌) / Slot 2(우) ──
	struct FSlotAssignment
	{
		int32 SlotIndex;
		FHellunaPartyMemberInfo MemberInfo;
		bool bIsLocal;
	};

	TArray<FSlotAssignment> Assignments;

	// 리더 찾기
	int32 LeaderIdx = 0;
	for (int32 i = 0; i < PartyMembers.Num(); ++i)
	{
		if (PartyMembers[i].Role == EHellunaPartyRole::Leader)
		{
			LeaderIdx = i;
			break;
		}
	}

	// 멤버(리더 제외) 수집
	TArray<int32> MemberIndices;
	for (int32 i = 0; i < PartyMembers.Num(); ++i)
	{
		if (i != LeaderIdx)
		{
			MemberIndices.Add(i);
		}
	}

	// 할당: 리더→Slot1, 멤버1→Slot0, 멤버2→Slot2
	Assignments.Add({ 1, PartyMembers[LeaderIdx],
		PartyMembers[LeaderIdx].PlayerId == LocalPlayerId });

	if (MemberIndices.Num() >= 1)
	{
		Assignments.Add({ 0, PartyMembers[MemberIndices[0]],
			PartyMembers[MemberIndices[0]].PlayerId == LocalPlayerId });
	}
	if (MemberIndices.Num() >= 2)
	{
		Assignments.Add({ 2, PartyMembers[MemberIndices[1]],
			PartyMembers[MemberIndices[1]].PlayerId == LocalPlayerId });
	}

	// ── 4) 슬롯별 메시 + 스포트라이트 생성 ──
	for (const FSlotAssignment& Slot : Assignments)
	{
		const int32 S = Slot.SlotIndex;
		const EHellunaHeroType HeroType = IndexToHeroType(Slot.MemberInfo.SelectedHeroType);

		// 메시 결정
		USkeletalMesh* MeshAsset = nullptr;
		if (HeroType != EHellunaHeroType::None)
		{
			USkeletalMesh* const* Found = InMeshMap.Find(Slot.MemberInfo.SelectedHeroType);
			if (Found && *Found)
			{
				MeshAsset = *Found;
			}
		}
		if (!MeshAsset && IsValid(PlaceholderMesh))
		{
			MeshAsset = PlaceholderMesh;
		}

		// SkeletalMeshComponent 생성 (이름에 카운터 추가 — GC 전 재호출 시 이름 충돌 방지)
		// [Fix47-L1] static→멤버 변수 (ClearPartyPreview에서 리셋 가능)
		const FName CompName = *FString::Printf(TEXT("PartyMesh_%d_%d"), S, PartyMeshGenCounter++);
		USkeletalMeshComponent* MeshComp = NewObject<USkeletalMeshComponent>(this, CompName);
		if (!MeshComp)
		{
			UE_LOG(LogHelluna, Error, TEXT("[프리뷰V2-Party] NewObject<USkeletalMeshComponent> 실패 (Slot %d)"), S);
			continue;
		}

		MeshComp->RegisterComponent();
		MeshComp->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		if (MeshAsset)
		{
			MeshComp->SetSkeletalMeshAsset(MeshAsset);

			// 미선택 플레이스홀더 반투명 머티리얼
			if (HeroType == EHellunaHeroType::None && IsValid(PlaceholderMaterial))
			{
				for (int32 m = 0; m < MeshComp->GetNumMaterials(); ++m)
				{
					MeshComp->SetMaterial(m, PlaceholderMaterial);
				}
			}
		}

		// AnimInstance (캐릭터가 선택된 경우만)
		if (HeroType != EHellunaHeroType::None)
		{
			const TSubclassOf<UAnimInstance>* AnimFound = InAnimClassMap.Find(Slot.MemberInfo.SelectedHeroType);
			if (AnimFound && *AnimFound)
			{
				MeshComp->SetAnimInstanceClass(*AnimFound);
			}
		}

		// 위치 (로컬 플레이어 전진 오프셋 적용)
		FVector Pos = PartySlotOffsets.IsValidIndex(S)
			? PartySlotOffsets[S] : FVector::ZeroVector;
		if (Slot.bIsLocal)
		{
			Pos.X += LocalPlayerForwardOffset;
		}
		MeshComp->SetRelativeLocation(Pos);

		// 회전
		const FRotator Rot = PartySlotRotations.IsValidIndex(S)
			? PartySlotRotations[S] : FRotator(0.f, -90.f, 0.f);
		MeshComp->SetRelativeRotation(Rot);

		// 스케일
		const FVector Scale = PartySlotScales.IsValidIndex(S)
			? PartySlotScales[S] : FVector::OneVector;
		MeshComp->SetRelativeScale3D(Scale);

		MeshComp->SetVisibility(true);
		PartyPreviewMeshes.Add(MeshComp);

		MeshComp->LightingChannels.bChannel1 = true;  // 프리뷰 조명 수신

		// ── 스포트라이트 ── (카운터로 이름 충돌 방지)
		static int32 PartySpotGenCounter = 0;
		const FName LightName = *FString::Printf(TEXT("PartySpot_%d_%d"), S, PartySpotGenCounter++);
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this, LightName);
		if (Spot)
		{
			Spot->RegisterComponent();
			Spot->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
			Spot->SetRelativeLocation(Pos + FVector(100.f, 0.f, 350.f));
			Spot->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
			// 로컬 플레이어는 밝게, 다른 멤버는 보통 (기존 80000 기준 비율 적용)
			Spot->SetIntensity(Slot.bIsLocal ? 80000.f : 80000.f * 0.6f);
			Spot->SetAttenuationRadius(1000.f);

		Spot->LightingChannels.bChannel0 = false;  // 배경 안 밝힘
		Spot->LightingChannels.bChannel1 = true;   // 캐릭터만 밝힘
			Spot->SetInnerConeAngle(15.f);
			Spot->SetOuterConeAngle(35.f);
			Spot->SetVisibility(true);
			PartySpotLights.Add(Spot);
		}

		UE_LOG(LogHelluna, Log, TEXT("[프리뷰V2-Party] Slot %d: Hero=%d | Local=%s | Pos=%s"),
			S, Slot.MemberInfo.SelectedHeroType,
			Slot.bIsLocal ? TEXT("Y") : TEXT("N"),
			*Pos.ToString());
	}

	UE_LOG(LogHelluna, Log, TEXT("[프리뷰V2-Party] Party mode ON: %d members, Leader at center (Slot 1)"),
		PartyMembers.Num());
}

void AHellunaCharacterSelectSceneV2::ClearPartyPreview()
{
	// 파티 모드가 아니면 조기 반환 (불필요한 PreviewMeshes 복원 방지)
	if (!bPartyMode && PartyPreviewMeshes.Num() == 0)
	{
		return;
	}

	// 파티 메시 파괴
	for (USkeletalMeshComponent* Mesh : PartyPreviewMeshes)
	{
		if (IsValid(Mesh))
		{
			Mesh->DestroyComponent();
		}
	}
	PartyPreviewMeshes.Empty();

	// 파티 스포트라이트 파괴
	for (USpotLightComponent* Light : PartySpotLights)
	{
		if (IsValid(Light))
		{
			Light->DestroyComponent();
		}
	}
	PartySpotLights.Empty();

	CachedPartyHeroTypes.Empty();
	PartyMeshGenCounter = 0; // [Fix47-L1] FName 풀 누적 방지
	bPartyMode = false;

	// 기존 PreviewMeshes + CharacterSpotLights 복원
	for (int32 i = 0; i < PreviewMeshes.Num(); ++i)
	{
		if (IsValid(PreviewMeshes[i]))
		{
			PreviewMeshes[i]->SetVisibility(true);
		}
		if (CharacterSpotLights.IsValidIndex(i) && IsValid(CharacterSpotLights[i]))
		{
			CharacterSpotLights[i]->SetVisibility(true);
		}
	}

	UE_LOG(LogHelluna, Log, TEXT("[프리뷰V2-Party] Party mode OFF — PreviewMeshes 복원"));
}
