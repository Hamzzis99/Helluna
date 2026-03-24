// Source/Helluna/Private/Puzzle/PuzzleCubeActor.cpp

#include "Puzzle/PuzzleCubeActor.h"
#include "Puzzle/PuzzleInteractWidget.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Net/UnrealNetwork.h"
#include "Controller/HellunaHeroController.h"

// ============================================================================
// 생성자
// ============================================================================

APuzzleCubeActor::APuzzleCubeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true; // 3D 위젯 표시용 + 데미지 타이머용

	bReplicates = true;

	// 루트 메시 (에디터에서 스태틱메시 수동 할당)
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);

	// 상호작용 구체 (반경 EditAnywhere, 기본 500)
	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetSphereRadius(500.f);
	InteractionSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	InteractionSphere->SetGenerateOverlapEvents(true);
	InteractionSphere->SetupAttachment(MeshComp);

	// 3D 상호작용 위젯
	InteractWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("InteractWidgetComp"));
	InteractWidgetComp->SetupAttachment(MeshComp);
	InteractWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, 120.f)); // 큐브 위에
	InteractWidgetComp->SetDrawSize(FVector2D(200.f, 60.f));
	InteractWidgetComp->SetWidgetSpace(EWidgetSpace::Screen); // 항상 카메라를 바라봄
	InteractWidgetComp->SetVisibility(false); // 기본 숨김
	InteractWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// ============================================================================
// 라이프사이클
// ============================================================================

void APuzzleCubeActor::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;

	if (HasAuthority())
	{
		GeneratePuzzle();
	}

	// 3D 상호작용 위젯 클래스 설정 (클라이언트+리슨서버)
	if (InteractWidgetComp && InteractWidgetClass)
	{
		InteractWidgetComp->SetWidgetClass(InteractWidgetClass);
	}
}

void APuzzleCubeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APuzzleCubeActor, PuzzleGrid);
	DOREPLIFETIME(APuzzleCubeActor, bPuzzleLocked);
	DOREPLIFETIME(APuzzleCubeActor, bPuzzleInUse);
	DOREPLIFETIME(APuzzleCubeActor, CurrentHealth);
	DOREPLIFETIME(APuzzleCubeActor, DamageableTime);
	DOREPLIFETIME(APuzzleCubeActor, PuzzleTimeLimit);
}

// ============================================================================
// 퍼즐 생성 알고리즘
// ============================================================================

void APuzzleCubeActor::GeneratePuzzle()
{
	const int32 Size = 4;
	PuzzleGrid.GridSize = Size;
	PuzzleGrid.Cells.SetNum(Size * Size);

	// 1. 모든 셀 초기화
	for (int32 i = 0; i < Size * Size; ++i)
	{
		FPuzzleCell& Cell = PuzzleGrid.Cells[i];
		Cell.Row = i / Size;
		Cell.Col = i % Size;
		Cell.PipeType = EPuzzlePipeType::None;
		Cell.Rotation = 0;
		Cell.bLocked = false;
	}

	// 2. 시작점/도착점 고정
	PuzzleGrid.StartIndex = 0; // (0,0)
	PuzzleGrid.EndIndex = Size * Size - 1; // (3,3)

	FPuzzleCell& StartCell = PuzzleGrid.Cells[PuzzleGrid.StartIndex];
	StartCell.PipeType = EPuzzlePipeType::Start;
	StartCell.Rotation = 0;
	StartCell.bLocked = true;

	FPuzzleCell& EndCell = PuzzleGrid.Cells[PuzzleGrid.EndIndex];
	EndCell.PipeType = EPuzzlePipeType::End;
	EndCell.Rotation = 0;
	EndCell.bLocked = true;

	// 3. 경로 생성: (0,0) → Right → (0,1) → DFS → (3,3)
	TArray<int32> Path;
	TSet<int32> Visited;

	// 시작 강제: (0,0) → (0,1)
	Path.Add(0);
	Path.Add(1);
	Visited.Add(0);
	Visited.Add(1);

	bool bFound = FindPathDFS(Path, Visited, 1);

	if (!bFound)
	{
		UE_LOG(LogTemp, Error, TEXT("[PuzzleCube] GeneratePuzzle: DFS 실패 — 폴백 경로 사용"));
		// 폴백: (0,0)→(0,1)→(0,2)→(0,3)→(1,3)→(2,3)→(3,3)
		Path.Empty();
		Path = {0, 1, 2, 3, 7, 11, 15};
	}

	// 4. 경로에 파이프 배치
	PlacePathPipes(Path);

	// 5. 경로 외 셀은 None 유지 (빈 셀로 표시)

	// 6. 경로 셀(비잠금)만 회전 셔플 — 정답 상태 방지
	constexpr int32 MaxShuffleAttempts = 10;
	for (int32 Attempt = 0; Attempt < MaxShuffleAttempts; ++Attempt)
	{
		for (int32 i = 0; i < Size * Size; ++i)
		{
			if (!PuzzleGrid.Cells[i].bLocked && PuzzleGrid.Cells[i].PipeType != EPuzzlePipeType::None)
			{
				PuzzleGrid.Cells[i].Rotation = FMath::RandRange(0, 3) * 90;
			}
		}

		if (!PuzzleUtils::CheckSolved(PuzzleGrid))
		{
			break;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[PuzzleCube] GeneratePuzzle: Shuffle attempt %d was already solved, re-shuffling"),
			Attempt + 1);

		// 마지막 시도에서도 정답이면 하나의 셀을 강제로 1회전
		if (Attempt == MaxShuffleAttempts - 1)
		{
			for (int32 i = 0; i < Size * Size; ++i)
			{
				if (!PuzzleGrid.Cells[i].bLocked && PuzzleGrid.Cells[i].PipeType != EPuzzlePipeType::None)
				{
					PuzzleGrid.Cells[i].Rotation = (PuzzleGrid.Cells[i].Rotation + 90) % 360;
					UE_LOG(LogTemp, Warning,
						TEXT("[PuzzleCube] GeneratePuzzle: Force-rotated cell %d to break solution"), i);
					break;
				}
			}
		}
	}

	// 로그 #1
	const int32 PathLength = Path.Num();
	int32 EmptyCells = 0;
	for (int32 i = 0; i < Size * Size; ++i)
	{
		if (PuzzleGrid.Cells[i].PipeType == EPuzzlePipeType::None) ++EmptyCells;
	}
	const FPuzzleCell& LogStart = PuzzleGrid.Cells[PuzzleGrid.StartIndex];
	const FPuzzleCell& LogEnd = PuzzleGrid.Cells[PuzzleGrid.EndIndex];
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleCube] GeneratePuzzle: PathLength=%d, EmptyCells=%d, Start=(%d,%d), End=(%d,%d)"),
		PathLength, EmptyCells, LogStart.Row, LogStart.Col, LogEnd.Row, LogEnd.Col);
}

bool APuzzleCubeActor::FindPathDFS(TArray<int32>& Path, TSet<int32>& Visited, int32 CurrentIndex)
{
	if (CurrentIndex == PuzzleGrid.EndIndex)
	{
		return true;
	}

	const int32 Row = CurrentIndex / PuzzleGrid.GridSize;
	const int32 Col = CurrentIndex % PuzzleGrid.GridSize;

	// 방향 랜덤 셔플
	TArray<FIntPoint> Dirs = {
		PuzzleUtils::DirUp, PuzzleUtils::DirDown,
		PuzzleUtils::DirLeft, PuzzleUtils::DirRight
	};
	for (int32 i = Dirs.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		Dirs.Swap(i, j);
	}

	for (const FIntPoint& Dir : Dirs)
	{
		const int32 NewRow = Row + Dir.X;
		const int32 NewCol = Col + Dir.Y;

		if (NewRow < 0 || NewRow >= PuzzleGrid.GridSize ||
			NewCol < 0 || NewCol >= PuzzleGrid.GridSize)
		{
			continue;
		}

		const int32 NewIndex = NewRow * PuzzleGrid.GridSize + NewCol;
		if (Visited.Contains(NewIndex))
		{
			continue;
		}

		// End 셀 진입은 위에서(Down 방향)만 허용 — End가 Up 연결이므로
		if (NewIndex == PuzzleGrid.EndIndex && Dir != PuzzleUtils::DirDown)
		{
			continue;
		}

		Path.Add(NewIndex);
		Visited.Add(NewIndex);

		if (FindPathDFS(Path, Visited, NewIndex))
		{
			return true;
		}

		Path.Pop();
		Visited.Remove(NewIndex);
	}

	return false;
}

void APuzzleCubeActor::PlacePathPipes(const TArray<int32>& Path)
{
	// Path[0] = Start, Path[Last] = End — 이미 설정됨
	// 중간 셀만 파이프 배치
	for (int32 i = 1; i < Path.Num() - 1; ++i)
	{
		const int32 PrevIdx = Path[i - 1];
		const int32 CurrIdx = Path[i];
		const int32 NextIdx = Path[i + 1];

		const FPuzzleCell& PrevCell = PuzzleGrid.Cells[PrevIdx];
		FPuzzleCell& CurrCell = PuzzleGrid.Cells[CurrIdx];
		const FPuzzleCell& NextCell = PuzzleGrid.Cells[NextIdx];

		// 이전 셀 방향 (Prev→Curr의 반대 = Curr에서 Prev 쪽 개구부)
		const FIntPoint DirFromPrev = FIntPoint(
			CurrCell.Row - PrevCell.Row,
			CurrCell.Col - PrevCell.Col
		);
		const FIntPoint ConnToPrev = PuzzleUtils::GetOppositeDirection(DirFromPrev);

		// 다음 셀 방향 (Curr→Next 개구부)
		const FIntPoint ConnToNext = FIntPoint(
			NextCell.Row - CurrCell.Row,
			NextCell.Col - CurrCell.Col
		);

		PuzzleUtils::DeterminePipeTypeAndRotation(ConnToPrev, ConnToNext, CurrCell);
	}
}

// ============================================================================
// 퍼즐 상호작용 (서버 권한)
// ============================================================================

bool APuzzleCubeActor::TryEnterPuzzle(AController* Player)
{
	if (!HasAuthority() || !IsValid(Player))
	{
		return false;
	}

	const FString PlayerName = GetNameSafe(Player);
	const bool bResult = !bPuzzleInUse;

	// 로그 #2
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleCube] TryEnterPuzzle: Player=%s, bInUse=%s → Result=%s"),
		*PlayerName,
		bPuzzleInUse ? TEXT("true") : TEXT("false"),
		bResult ? TEXT("Success") : TEXT("Fail"));

	if (!bResult)
	{
		return false;
	}

	bPuzzleInUse = true;
	CurrentPuzzleUser = Player;

	// 퍼즐 제한시간 타이머 시작
	StartPuzzleTimer();

	// 해킹 모드 시작 (전원 흑백)
	Multicast_HackModeStarted();

	return true;
}

void APuzzleCubeActor::ExitPuzzle(AController* Player)
{
	if (!HasAuthority())
	{
		return;
	}

	if (CurrentPuzzleUser.Get() == Player)
	{
		bPuzzleInUse = false;
		CurrentPuzzleUser = nullptr;

		// 퍼즐 제한시간 타이머 정지
		GetWorldTimerManager().ClearTimer(PuzzleTimeoutTimerHandle);

		// 해킹 모드 종료 (전원 컬러 복원)
		Multicast_HackModeEnded();
	}
}

void APuzzleCubeActor::RotateCell(int32 CellIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!PuzzleGrid.Cells.IsValidIndex(CellIndex))
	{
		return;
	}

	FPuzzleCell& Cell = PuzzleGrid.Cells[CellIndex];

	if (Cell.bLocked)
	{
		return;
	}

	if (Cell.PipeType == EPuzzlePipeType::None)
	{
		return;
	}

	const int32 BeforeRotation = Cell.Rotation;
	Cell.Rotation = (Cell.Rotation + 90) % 360;

	// 로그 #3
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleCube] RotateCell: CellIndex=%d, Before=%d° → After=%d°"),
		CellIndex, BeforeRotation, Cell.Rotation);

	// 리플리케이션을 위해 더티 마킹
	// (UPROPERTY Replicated 구조체는 전체 재전송)

	// 로그 #4
	const bool bSolved = PuzzleUtils::CheckSolved(PuzzleGrid);
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleCube] CheckSolved: Result=%s"),
		bSolved ? TEXT("Solved") : TEXT("NotSolved"));

	if (bSolved)
	{
		UnlockDamage();
	}

	// Standalone/Listen 서버에서는 OnRep이 자동 호출되지 않으므로 수동 브로드캐스트
	// 데디서버에서도 구조체 내부 변경의 더티 마킹 보장을 위해 안전하게 호출
	if (HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] RotateCell: Broadcasting OnRep_PuzzleGrid (server-side manual call)"));
		OnRep_PuzzleGrid();
	}
}

float APuzzleCubeActor::GetInteractionRadius() const
{
	return InteractionSphere ? InteractionSphere->GetScaledSphereRadius() : 500.f;
}

float APuzzleCubeActor::GetLocalHoldProgress() const
{
	return LocalHoldProgress;
}

// ============================================================================
// 데미지 게이팅
// ============================================================================

/**
 * 퍼즐 해제 성공 → 데미지 허용 상태 전환
 *
 * [보스전] DeactivateShield()로 대체:
 *   bShieldActive = false → 전원 보스에게 딜 가능
 *   ShieldDownDuration(5분) 타이머 시작
 *   보호막 해제 이펙트 재생
 */
void APuzzleCubeActor::UnlockDamage()
{
	// 퍼즐 성공 → 제한시간 타이머 정지
	GetWorldTimerManager().ClearTimer(PuzzleTimeoutTimerHandle);

	bPuzzleLocked = false;
	RelockTimer = DamageableTime;
	LastLoggedSecond = FMath::CeilToInt(RelockTimer);
	SetActorTickEnabled(true);

	// 퍼즐 사용 해제
	bPuzzleInUse = false;
	CurrentPuzzleUser = nullptr;

	// 해킹 모드 종료 (전원 컬러 복원)
	Multicast_HackModeEnded();

	// 로그 #5
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleCube] UnlockDamage: DamageableTime=%.1f"), DamageableTime);

	// OnRep은 서버에서 자동 호출되지 않으므로 서버 측 수동 호출
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] UnlockDamage: Broadcasting OnRep_bPuzzleLocked (server-side)"));
	OnRep_bPuzzleLocked();
}

/**
 * 딜타임 종료 → 퍼즐 재잠금
 *
 * [보스전] ReactivateShield()로 대체:
 *   bShieldActive = true → 보스 다시 무적
 *   퍼즐 리셋 (ReshufflePuzzle)
 *   보호막 재활성화 이펙트 재생
 */
void APuzzleCubeActor::RelockPuzzle()
{
	// 로그 #6
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] RelockPuzzle: Regenerating puzzle"));

	bPuzzleLocked = true;
	bPuzzleInUse = false;
	SetActorTickEnabled(false);

	// 현재 사용자 강제 퇴출
	if (CurrentPuzzleUser.IsValid())
	{
		AHellunaHeroController* Controller = Cast<AHellunaHeroController>(CurrentPuzzleUser.Get());
		if (IsValid(Controller))
		{
			Controller->Client_PuzzleForceExit();
		}
		CurrentPuzzleUser = nullptr;
	}

	// 새 퍼즐 생성
	GeneratePuzzle();

	// 서버 측 OnRep 수동 호출
	OnRep_bPuzzleLocked();
	OnRep_PuzzleGrid();
}

// ============================================================================
// Tick — 데미지 타이머 카운트다운
// ============================================================================

void APuzzleCubeActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// --- 서버: 데미지 타이머 ---
	if (HasAuthority() && !bPuzzleLocked)
	{
		RelockTimer -= DeltaTime;

		// 매초 1회 로그
		const int32 CurrSecond = FMath::CeilToInt(FMath::Max(RelockTimer, 0.f));
		if (CurrSecond != LastLoggedSecond)
		{
			LastLoggedSecond = CurrSecond;
			UE_LOG(LogTemp, Warning,
				TEXT("[PuzzleCube] DamageTimer: %.1f remaining"), RelockTimer);
		}

		if (RelockTimer <= 0.f)
		{
			RelockPuzzle();
		}
	}

	// --- 클라이언트: 3D 상호작용 위젯 ---
	if (!HasAuthority() || !IsRunningDedicatedServer())
	{
		UpdateInteractWidgetVisibility();

		// F키 홀드 프로그레스 업데이트
		if (bInteractWidgetVisible && InteractWidgetInstance)
		{
			UWorld* World = GetWorld();
			if (!World) { return; }

			APlayerController* PC = World->GetFirstPlayerController();
			AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);

			if (HeroPC && HeroPC->IsHoldingPuzzleInteract())
			{
				// 프로그레스 증가 (0.5초에 완료)
				LocalHoldProgress = FMath::Min(1.f, LocalHoldProgress + DeltaTime / 1.5f);
				InteractWidgetInstance->SetProgress(LocalHoldProgress);

				// 화면 흑백 전환 (F 홀드 프로그레스에 비례)
				HeroPC->SetDesaturationByProgress(LocalHoldProgress);

				if (LocalHoldProgress >= 1.f && !bHoldCompleted)
				{
					bHoldCompleted = true;
					InteractWidgetInstance->ShowCompleted();

					HeroPC->TryEnterPuzzleFromCube(this);
					UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] Hold complete (1.5s) — TryEnterPuzzleFromCube called"));
				}
			}
			else
			{
				// F키 안 누르면 프로그레스 빠르게 감소
				if (LocalHoldProgress > 0.f)
				{
					LocalHoldProgress = FMath::Max(0.f, LocalHoldProgress - DeltaTime * 3.f);
					InteractWidgetInstance->SetProgress(LocalHoldProgress);

					// 컬러 복원 (프로그레스 감소에 비례)
					if (HeroPC)
					{
						HeroPC->SetDesaturationByProgress(LocalHoldProgress);
					}
				}
			}
		}
	}
}

// ============================================================================
// TakeDamage — 퍼즐 잠금 해제 시에만 데미지 적용
// ============================================================================

float APuzzleCubeActor::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float OldHealth = CurrentHealth;

	if (bPuzzleLocked)
	{
		// 로그 #7 (잠금 상태)
		UE_LOG(LogTemp, Warning,
			TEXT("[PuzzleCube] TakeDamage: bLocked=%s, Damage=%.1f, Health: %.1f → %.1f"),
			TEXT("true"), DamageAmount, OldHealth, OldHealth);
		return 0.f;
	}

	// 데미지 적용
	CurrentHealth = FMath::Max(CurrentHealth - DamageAmount, 0.f);

	// 로그 #7
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleCube] TakeDamage: bLocked=%s, Damage=%.1f, Health: %.1f → %.1f"),
		TEXT("false"), DamageAmount, OldHealth, CurrentHealth);

	return DamageAmount;
}

// ============================================================================
// OnRep 콜백
// ============================================================================

void APuzzleCubeActor::OnRep_PuzzleGrid()
{
	OnPuzzleGridUpdated.Broadcast();
}

void APuzzleCubeActor::OnRep_bPuzzleLocked()
{
	OnPuzzleLockChanged.Broadcast(bPuzzleLocked);
}

void APuzzleCubeActor::OnRep_CurrentHealth()
{
	// 필요 시 체력 변경 비주얼 처리
}

// ============================================================================
// 퍼즐 제한시간
// ============================================================================

void APuzzleCubeActor::StartPuzzleTimer()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PuzzleTimeoutTimerHandle);
	GetWorldTimerManager().SetTimer(
		PuzzleTimeoutTimerHandle, this,
		&APuzzleCubeActor::OnPuzzleTimeout,
		PuzzleTimeLimit, false);

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] Timer started: %.0f seconds"), PuzzleTimeLimit);
}

void APuzzleCubeActor::OnPuzzleTimeout()
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] Time's up! Reshuffling in 3 seconds..."));

	// 클라이언트에 실패 알림
	Multicast_PuzzleTimedOut();

	// 3초 후 셔플 + 타이머 재시작
	FTimerHandle ReshuffleDelayHandle;
	GetWorldTimerManager().SetTimer(ReshuffleDelayHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ReshufflePuzzle();

		// 사용자가 아직 퍼즐 내에 있으면 타이머 재시작
		if (bPuzzleInUse)
		{
			StartPuzzleTimer();
		}

		// 서버 측 OnRep 수동 호출
		if (HasAuthority())
		{
			OnRep_PuzzleGrid();
		}
	}), 3.0f, false);
}

void APuzzleCubeActor::ReshufflePuzzle()
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 Size = PuzzleGrid.GridSize;

	// 비잠금 셀만 회전 셔플 — 정답 방지
	constexpr int32 MaxShuffleAttempts = 10;
	for (int32 Attempt = 0; Attempt < MaxShuffleAttempts; ++Attempt)
	{
		for (int32 i = 0; i < Size * Size; ++i)
		{
			if (!PuzzleGrid.Cells[i].bLocked && PuzzleGrid.Cells[i].PipeType != EPuzzlePipeType::None)
			{
				PuzzleGrid.Cells[i].Rotation = FMath::RandRange(0, 3) * 90;
			}
		}

		if (!PuzzleUtils::CheckSolved(PuzzleGrid))
		{
			break;
		}

		if (Attempt == MaxShuffleAttempts - 1)
		{
			for (int32 i = 0; i < Size * Size; ++i)
			{
				if (!PuzzleGrid.Cells[i].bLocked && PuzzleGrid.Cells[i].PipeType != EPuzzlePipeType::None)
				{
					PuzzleGrid.Cells[i].Rotation = (PuzzleGrid.Cells[i].Rotation + 90) % 360;
					break;
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] ReshufflePuzzle: Cells reshuffled"));
}

void APuzzleCubeActor::Multicast_PuzzleTimedOut_Implementation()
{
	OnPuzzleTimedOutDelegate.Broadcast();
}

// ============================================================================
// 해킹 모드 — 화면 흑백 전환
// ============================================================================

void APuzzleCubeActor::Multicast_HackModeStarted_Implementation()
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	APlayerController* PC = World->GetFirstPlayerController();
	if (PC && PC->IsLocalController())
	{
		AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
		if (HeroPC)
		{
			HeroPC->SetDesaturation(0.f, 0.3f); // 0.3초에 걸쳐 흑백
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] Multicast_HackModeStarted"));
}

void APuzzleCubeActor::Multicast_HackModeEnded_Implementation()
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	APlayerController* PC = World->GetFirstPlayerController();
	if (PC && PC->IsLocalController())
	{
		AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
		if (HeroPC)
		{
			// 성공: PlayColorReveal (순백 섬광 → 페이드아웃 → 컬러 복원)
			// ESC: PlayColorReveal 내부에서 bInHackMode=false 확인 → 스킵
			//       → ExitPuzzle의 SetDesaturation(1.f, 1.0f)이 이미 처리함
			HeroPC->PlayColorReveal();
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleCube] Multicast_HackModeEnded"));
}

// ============================================================================
// 3D 상호작용 위젯 표시/숨김 (클라이언트 전용)
// ============================================================================

void APuzzleCubeActor::UpdateInteractWidgetVisibility()
{
	if (!InteractWidgetComp) { return; }

	UWorld* World = GetWorld();
	if (!World) { return; }

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->IsLocalController()) { return; }

	APawn* Pawn = PC->GetPawn();
	if (!Pawn) { return; }

	// 조건 1: 거리 체크 (InteractionRadius 내)
	const float Dist = FVector::Dist(Pawn->GetActorLocation(), GetActorLocation());
	const bool bInRange = Dist < GetInteractionRadius();

	// 조건 2: 퍼즐이 사용 중이 아님
	const bool bNotInUse = !bPuzzleInUse;

	// 조건 3: 퍼즐이 잠겨있음 (이미 해제 상태면 표시 불필요)
	const bool bIsLocked = bPuzzleLocked;

	// 조건 4: 카메라 뷰포트에 보이는지
	const bool bOnScreen = WasRecentlyRendered(0.2f);

	const bool bShouldShow = bInRange && bNotInUse && bIsLocked && bOnScreen;

	if (bShouldShow && !bInteractWidgetVisible)
	{
		// 위젯 표시
		InteractWidgetComp->SetVisibility(true);
		bInteractWidgetVisible = true;

		// 위젯 인스턴스 캐시
		if (!InteractWidgetInstance)
		{
			InteractWidgetInstance = Cast<UPuzzleInteractWidget>(InteractWidgetComp->GetWidget());
		}
		if (InteractWidgetInstance)
		{
			InteractWidgetInstance->ResetState();
		}

		UE_LOG(LogTemp, Log, TEXT("[PuzzleCube] 3D Interact widget shown (dist=%.0f)"), Dist);
	}
	else if (!bShouldShow && bInteractWidgetVisible)
	{
		// 위젯 숨김
		InteractWidgetComp->SetVisibility(false);
		bInteractWidgetVisible = false;
		LocalHoldProgress = 0.f;
		bHoldCompleted = false;

		UE_LOG(LogTemp, Log, TEXT("[PuzzleCube] 3D Interact widget hidden"));
	}
}
