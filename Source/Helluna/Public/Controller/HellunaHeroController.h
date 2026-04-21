// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Player/Inv_PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "Loading/HellunaLoadingTypes.h"
#include "HellunaHeroController.generated.h"

class UNiagaraSystem;

class UVoteWidget;
class UVoteManagerComponent;
class UHellunaGameResultWidget;
class UHellunaChatWidget;
class UInputAction;
class UInputMappingContext;
class APuzzleCubeActor;
class ABossEncounterCube;
class UPuzzleGridWidget;
class UPostProcessComponent;
class UHellunaDebugHUDWidget;
class UHellunaGraphicsSettingsWidget;
class UHellunaPauseMenuWidget;
class UHellunaWorldMapWidget;
class UHellunaLoadingHUDWidget;
class ACameraActor;

/**
 * @brief   Helluna 영웅 전용 PlayerController
 * @details AInv_PlayerController를 상속받아 인벤토리 기능을 유지하면서
 *          팀 시스템(IGenericTeamAgentInterface)을 제공합니다.
 *          GAS(HellunaHeroGameplayAbility)에서 Cast 대상으로 사용됩니다.
 *
 *          상속 구조:
 *          APlayerController
 *            └── AInv_PlayerController (인벤토리/장비)
 *                  └── AHellunaHeroController (팀ID, GAS, 투표 시스템)
 *                        └── BP_HellunaHeroController (에디터 BP)
 */
UCLASS()
class HELLUNA_API AHellunaHeroController : public AInv_PlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AHellunaHeroController();

	//~ Begin IGenericTeamAgentInterface Interface.
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface Interface

	// =========================================================================================
	// [투표 시스템] Server RPC (김기현)
	// =========================================================================================

	/**
	 * @brief   투표 제출 Server RPC (클라이언트 → 서버)
	 *
	 * @details PlayerController는 클라이언트의 NetConnection을 소유하므로
	 *          Server RPC가 정상 작동합니다.
	 *          내부에서 VoteManagerComponent::ReceiveVote()를 호출합니다.
	 *
	 * @param   bAgree - true: 찬성, false: 반대
	 */
	// [Step3] WithValidation 추가 - 서버 RPC 보안 강화
	// bool 파라미터이므로 추가 검증은 없지만, UE 네트워크 안전성 보장
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SubmitVote(bool bAgree);

	// =========================================================================================
	// [Phase 10] 채팅 시스템 Server RPC
	// =========================================================================================

	/**
	 * 채팅 메시지 전송 Server RPC (클라이언트 → 서버)
	 * @param Message  전송할 메시지 (1~200자)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendChatMessage(const FString& Message);

	/**
	 * 채팅 입력 토글 (BP에서도 호출 가능)
	 * Enter 키 입력 시 호출 → 위젯의 입력창 활성/비활성 전환
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat (채팅)")
	void ToggleChatInput();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // C5: 타이머/델리게이트 정리

	// =========================================================================================
	// [투표 시스템] 위젯 자동 생성 (김기현)
	// =========================================================================================

	/**
	 * @brief   투표 UI 위젯 클래스 (BP에서 WBP_VoteWidget 지정)
	 * @note    None이면 투표 위젯을 생성하지 않음
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|UI", meta = (DisplayName = "Vote Widget Class (투표 위젯 클래스)"))
	TSubclassOf<UVoteWidget> VoteWidgetClass;

	// =========================================================================================
	// [Phase 10] 채팅 위젯 설정
	// =========================================================================================

	/** 채팅 위젯 클래스 (BP에서 WBP_HellunaChatWidget 지정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat|UI (채팅|UI)",
		meta = (DisplayName = "Chat Widget Class (채팅 위젯 클래스)"))
	TSubclassOf<UHellunaChatWidget> ChatWidgetClass;

	/** 채팅 토글 입력 액션 (Enter 키에 매핑된 IA 에셋) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat|Input (채팅|입력)",
		meta = (DisplayName = "Chat Toggle Action (채팅 토글 액션)"))
	TObjectPtr<UInputAction> ChatToggleAction;

	/** 채팅 입력 매핑 컨텍스트 (Enter 키 → ChatToggleAction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat|Input (채팅|입력)",
		meta = (DisplayName = "Chat Mapping Context (채팅 입력 매핑)"))
	TObjectPtr<UInputMappingContext> ChatMappingContext;

private:
	FGenericTeamId HeroTeamID;

	// =========================================================================================
	// [투표 시스템] 위젯 초기화 내부 함수 (김기현)
	// =========================================================================================

	/** 투표 위젯 생성 및 VoteManager 바인딩 */
	void InitializeVoteWidget();

	/** 타이머 핸들 (GameState 복제 대기용) */
	FTimerHandle VoteWidgetInitTimerHandle;

	/** [Fix26] 투표 위젯 초기화 재시도 횟수 (무한 루프 방지) */
	int32 VoteWidgetInitRetryCount = 0;

	/** 생성된 투표 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UVoteWidget> VoteWidgetInstance;

	// =========================================================================================
	// [Phase 10] 채팅 시스템 내부 함수
	// =========================================================================================

	/** 채팅 위젯 초기화 (GameState 복제 대기 + 재시도) */
	void InitializeChatWidget();

	/** 채팅 위젯 초기화 타이머 핸들 */
	FTimerHandle ChatWidgetInitTimerHandle;

	/** U30: 채팅 초기화 재시도 카운터 (무한 루프 방지) */
	int32 ChatWidgetInitRetryCount = 0;
	static constexpr int32 MaxChatWidgetInitRetries = 20; // 최대 10초 (0.5초 × 20회)

	/** 생성된 채팅 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaChatWidget> ChatWidgetInstance;

	/** 채팅 입력 핸들러 (Enhanced Input에서 호출) */
	void OnChatToggleInput(const struct FInputActionValue& Value);

	/** 위젯에서 메시지 제출 시 콜백 */
	UFUNCTION()
	void OnChatMessageSubmitted(const FString& Message);

	/** 스팸 방지: 마지막 메시지 시간 (서버 전용) */
	double LastChatMessageTime = 0.0;
	static constexpr double ChatCooldownSeconds = 0.5;

	// =========================================================================================
	// [디버그] 서버 치트 RPC — 클라이언트에서 서버 GameMode 함수 호출 (김기현)
	// =========================================================================================
public:
	/**
	 * [디버그] 클라이언트 → 서버: 강제 게임 종료
	 * BP에서 키보드 입력(F9 등)에 바인딩하여 사용
	 * @param ReasonIndex  0=Escaped, 1=AllDead, 2=ServerShutdown
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Debug(디버그)",
		meta = (DisplayName = "Cheat End Game (치트 게임 종료)"))
	void Server_CheatEndGame(uint8 ReasonIndex);

	// =========================================================================================
	// [Phase 7] 게임 결과 UI (김기현)
	// =========================================================================================
	/**
	 * [서버 → 클라이언트] 게임 결과 UI 표시 RPC
	 *
	 * @param ResultItems  보존된 아이템 목록 (사망자는 빈 배열)
	 * @param bSurvived    생존 여부
	 * @param Reason       종료 사유 문자열
	 * @param LobbyURL     로비 서버 접속 URL
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowGameResult(const TArray<FInv_SavedItemData>& ResultItems, bool bSurvived,
		const FString& Reason, const FString& LobbyURL);

protected:
	/** 결과 위젯 클래스 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameResult|UI",
		meta = (DisplayName = "Game Result Widget Class (결과 위젯 클래스)"))
	TSubclassOf<UHellunaGameResultWidget> GameResultWidgetClass;

private:
	/** 생성된 결과 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaGameResultWidget> GameResultWidgetInstance;

	// =========================================================================================
	// [Puzzle] 퍼즐 시스템
	// =========================================================================================
public:
	/** 퍼즐 모드 종료 (위젯/입력에서 호출) */
	void ExitPuzzle();

	/** 퍼즐 셀 회전 요청 (위젯에서 호출 → Server RPC) */
	void RequestPuzzleRotateCell(int32 CellIndex);

	/** 현재 퍼즐 모드 여부 */
	UPROPERTY(BlueprintReadOnly, Category = "Puzzle (퍼즐)")
	bool bInPuzzleMode = false;

	/** F키 홀드 상태 (3D 위젯 프로그레스용) */
	UFUNCTION(BlueprintCallable, Category = "Puzzle")
	bool IsHoldingPuzzleInteract() const { return bHoldingPuzzleInteract; }

	/** 현재 해킹 모드(퍼즐 풀기) 중인지 — 퍼즐 사용자만 true */
	bool IsInHackMode() const { return bInHackMode; }

	/**
	 * Desaturation 목표 설정 (PuzzleCubeActor Multicast에서 호출)
	 * @param TargetValue  0.0 = 완전 흑백, 1.0 = 완전 컬러
	 * @param Duration     전환에 걸리는 시간 (초)
	 */
	void SetDesaturation(float TargetValue, float Duration);

	/**
	 * F 홀드 프로그레스에 따른 로컬 Desaturation 즉시 적용
	 */
	void SetDesaturationByProgress(float HoldProgress);

	/**
	 * 색채의 개방 (The Reveal)
	 * 퍼즐 성공 시 호출 — 순백 섬광 → 페이드아웃 → 흑백에서 컬러 복원
	 * bInHackMode=true일 때만 실행 (ESC 퇴출 시 스킵)
	 *
	 * [보스전 로드맵]
	 * 보스 보호막 해제 성공 시에도 동일 연출.
	 * 나이아가라 파티클(빛 입자 산란)을 페이드아웃과 함께 재생하면 더 극적.
	 */
	void PlayColorReveal();

	/** 
	 * PuzzleCubeActor에서 F 홀드 완료 시 호출 (IMC Hold 트리거 우회용)
	 * LocalTargetPuzzleCube 설정 + Server RPC 호출
	 */
	void TryEnterPuzzleFromCube(class APuzzleCubeActor* Cube);

	/** 색채의 개방 나이아가라 이펙트 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|HackMode",
		meta = (DisplayName = "색채의 개방 VFX"))
	TObjectPtr<UNiagaraSystem> ColorRevealVFX;

	// --- Server RPCs ---
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_PuzzleTryEnter();

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_PuzzleRotateCell(int32 CellIndex);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_PuzzleExit();

	// --- Client RPCs ---
	UFUNCTION(Client, Reliable)
	void Client_PuzzleEntered();

	UFUNCTION(Client, Reliable)
	void Client_PuzzleForceExit();

protected:
	/** 퍼즐 위젯 클래스 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|UI (퍼즐|UI)",
		meta = (DisplayName = "Puzzle Grid Widget Class (퍼즐 그리드 위젯 클래스)"))
	TSubclassOf<UPuzzleGridWidget> PuzzleGridWidgetClass;

	/** 퍼즐 상호작용 입력 액션 (F 홀드에 매핑) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Interact Action (퍼즐 상호작용 액션)"))
	TObjectPtr<UInputAction> PuzzleInteractAction;

	/** 퍼즐 입력 매핑 컨텍스트 — 항상 활성 (F키 홀드용, priority=10) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Mapping Context (퍼즐 상호작용 매핑)"))
	TObjectPtr<UInputMappingContext> PuzzleMappingContext;

	/**
	 * 퍼즐 모드 전용 매핑 컨텍스트 — 퍼즐 모드 진입 시에만 추가 (priority=100)
	 * 방향키(WASD) + 회전 + 나가기(E) 등 일반 게임과 키가 겹치는 액션용.
	 * 퍼즐 모드가 아닐 때는 제거되어 IMC_Default와 키 충돌 없음.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Mode Mapping Context (퍼즐 모드 전용 매핑)"))
	TObjectPtr<UInputMappingContext> PuzzleModeMappingContext;

	/** 방향키 → 셀 이동 (개별 Boolean) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Up Action (퍼즐 위)"))
	TObjectPtr<UInputAction> PuzzleUpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Down Action (퍼즐 아래)"))
	TObjectPtr<UInputAction> PuzzleDownAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Left Action (퍼즐 왼쪽)"))
	TObjectPtr<UInputAction> PuzzleLeftAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Right Action (퍼즐 오른쪽)"))
	TObjectPtr<UInputAction> PuzzleRightAction;

	/** E키 → 셀 회전 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Rotate Action (퍼즐 회전)"))
	TObjectPtr<UInputAction> PuzzleRotateAction;

	/** E → 퍼즐 나가기 (PuzzleModeMappingContext에 매핑) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Input (퍼즐|입력)",
		meta = (DisplayName = "Puzzle Exit Action (퍼즐 나가기)"))
	TObjectPtr<UInputAction> PuzzleExitAction;

private:
	/** 현재 활성 퍼즐 위젯 */
	UPROPERTY()
	TObjectPtr<UPuzzleGridWidget> ActivePuzzleWidget;

	/** 클라이언트 측 타겟 퍼즐 큐브 */
	TWeakObjectPtr<APuzzleCubeActor> LocalTargetPuzzleCube;

	/** 서버 측 현재 퍼즐 큐브 */
	TWeakObjectPtr<APuzzleCubeActor> ServerPuzzleCube;

	/** 퍼즐 상호작용 입력 핸들러 (F 홀드 완료) */
	void OnPuzzleInteractInput(const struct FInputActionValue& Value);

	/** 퍼즐 방향키 핸들러 (Enhanced Input, 개별 Boolean) */
	void OnPuzzleUpInput(const struct FInputActionValue& Value);
	void OnPuzzleDownInput(const struct FInputActionValue& Value);
	void OnPuzzleLeftInput(const struct FInputActionValue& Value);
	void OnPuzzleRightInput(const struct FInputActionValue& Value);

	/** 퍼즐 회전 핸들러 (Enhanced Input) */
	void OnPuzzleRotateInput(const struct FInputActionValue& Value);

	/** 퍼즐 나가기 핸들러 (Enhanced Input) */
	void OnPuzzleExitInput(const struct FInputActionValue& Value);

	/** F키 홀드 시작 (3D 위젯 프로그레스용) */
	void OnPuzzleInteractOngoing(const struct FInputActionValue& Value);

	/** F키 홀드 종료 (3D 위젯 프로그레스용) */
	void OnPuzzleInteractReleased(const struct FInputActionValue& Value);

	/** F키 홀드 상태 추적 */
	bool bHoldingPuzzleInteract = false;

	// =========================================================================================
	// 해킹 모드 (화면 흑백 전환)
	// =========================================================================================

	/**
	 * PostProcess Desaturation 제어
	 * F 홀드 프로그레스 → 로컬 전환 (0.5초)
	 * 해킹 모드 → 전원 전환 (Multicast)
	 *
	 * [미래 작업: 영역 기반 흑백]
	 * 현재: 개인 PostProcessComponent로 전체 화면 Saturation 제어
	 * 레이드 전환 시: 이 함수 대신 PostProcessVolume 사용
	 * → F 홀드 프로그레스 중 로컬 흑백은 이 함수 유지 (로컬 프리뷰)
	 * → 해킹 모드 전환은 볼륨으로 대체
	 */

	/** 카메라에 부착된 PostProcess 컴포넌트 */
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> DesaturationPostProcess;

	/** 목표 Saturation 값 (0=흑백, 1=컬러) */
	float TargetSaturation = 1.f;

	/** 현재 Saturation 값 */
	float CurrentSaturation = 1.f;

	/** 전환 속도 (초당 변화량) */
	float SaturationLerpSpeed = 0.f;

	/** 해킹 모드 중인지 */
	bool bInHackMode = false;

	/** Tick에서 Saturation Lerp 업데이트 */
	void TickDesaturation(float DeltaTime);

	// =========================================================================================
	// 색채의 개방 (순백 섬광 → 페이드아웃 → 컬러 복원)
	// =========================================================================================

	/** 색채의 개방 연출 중인지 */
	bool bPlayingColorReveal = false;

	/** ColorReveal VFX already spawned flag */
	bool bColorRevealVFXSpawned = false;

	/** 색채의 개방 타임라인 진행도 (초) */
	float ColorRevealProgress = 0.f;

	/** Tick에서 색채의 개방 업데이트 */
	void TickColorReveal(float DeltaTime);

	/** 색채의 개방 완료 — PostProcess 기본값 복원 */
	void FinishColorReveal();

	// =========================================================================================
	// [DebugHUD] 디버그 HUD 시스템
	// =========================================================================================
public:
	/** 디버그 HUD 생성 (클라이언트 BeginPlay에서 자동 호출) */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void CreateDebugHUD();

protected:
	/** 디버그 HUD 위젯 클래스 (BP에서 WBP_HellunaDebugHUD 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "Debug",
		meta = (DisplayName = "Debug HUD Widget Class (디버그 HUD 위젯 클래스)"))
	TSubclassOf<UHellunaDebugHUDWidget> DebugHUDWidgetClass;

	/** F5 키 토글용 InputAction (에디터에서 IA_DebugHUD 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "Debug",
		meta = (DisplayName = "Debug HUD Toggle Action (F5)"))
	TObjectPtr<UInputAction> DebugHUDToggleAction;

private:
	/** 디버그 HUD 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaDebugHUDWidget> DebugHUDInstance;

	/** F5 키 입력 핸들러 */
	void OnDebugHUDToggle(const struct FInputActionValue& Value);

	// =========================================================================================
	// [BossEvent] 보스 조우 큐브 상호작용
	// =========================================================================================
public:
	/**
	 * 보스 조우 활성화 Server RPC (클라이언트 → 서버)
	 * BossEncounterCube에서 F키 홀드 완료 시 호출
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_BossEncounterActivate();

	// =========================================================================================
	// [일시정지 메뉴] 위젯 토글
	// =========================================================================================
public:
	/** 일시정지 메뉴 위젯 토글 (Escape 키에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "UI (유아이)",
		meta = (DisplayName = "Toggle Pause Menu (일시정지 메뉴 토글)"))
	void TogglePauseMenu();

	/** 일시정지 메뉴 인스턴스 해제 (위젯 내부에서 닫기 완료 시 호출) */
	void ClearPauseMenuInstance();

	/** 일시정지 메뉴 인스턴스만 해제 (커서/입력 복원 없이 — 설정 화면 전환용) */
	void ClearPauseMenuInstanceOnly();

protected:
	/** 일시정지 메뉴 위젯 클래스 (BP에서 WBP_HellunaPauseMenu 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "UI (유아이)",
		meta = (DisplayName = "Pause Menu Widget Class (일시정지 메뉴 위젯 클래스)"))
	TSubclassOf<UHellunaPauseMenuWidget> PauseMenuWidgetClass;

	/** 일시정지 메뉴 토글 입력 액션 (ESC/U 키에 매핑된 IA 에셋) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input (유아이|입력)",
		meta = (DisplayName = "Pause Menu Toggle Action (일시정지 메뉴 토글 액션)"))
	TObjectPtr<UInputAction> PauseMenuToggleAction;

	/** 일시정지 메뉴 입력 매핑 컨텍스트 (ESC+U → PauseMenuToggleAction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Input (유아이|입력)",
		meta = (DisplayName = "Pause Menu Mapping Context (일시정지 메뉴 입력 매핑)"))
	TObjectPtr<UInputMappingContext> PauseMenuMappingContext;

private:
	/** 일시정지 메뉴 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaPauseMenuWidget> PauseMenuInstance;

	/** PauseMenu Enhanced Input 핸들러 */
	void OnPauseMenuToggleInput(const struct FInputActionValue& Value);

	// =========================================================================================
	// [그래픽 설정] 위젯 토글
	// =========================================================================================
public:
	/** 그래픽 설정 위젯 토글 (CapsLock / ESC 메뉴에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "Settings (설정)",
		meta = (DisplayName = "Toggle Graphics Settings (그래픽 설정 토글)"))
	void ToggleGraphicsSettings();

protected:
	/** 그래픽 설정 위젯 클래스 (BP에서 WBP_HellunaGraphicsSettings 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "Settings (설정)",
		meta = (DisplayName = "Graphics Settings Widget Class (그래픽 설정 위젯 클래스)"))
	TSubclassOf<UHellunaGraphicsSettingsWidget> GraphicsSettingsWidgetClass;

private:
	/** 그래픽 설정 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaGraphicsSettingsWidget> GraphicsSettingsInstance;

	// =========================================================================================
	// [WorldMap] 풀스크린 월드맵 + 핑 시스템 (서버 권위 복제, 팀 공유)
	// =========================================================================================
public:
	/** 월드맵 좌클릭 → 서버에 핑 설정 요청. 0.1초 쿨다운 적용. PlayerState의 PingLocation/bHasPing 갱신 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "WorldMap (월드맵)")
	void Server_SetWorldPing(const FVector& WorldLocation);

	/** 월드맵 우클릭 → 서버에 핑 제거 요청. 쿨다운 없음 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "WorldMap (월드맵)")
	void Server_ClearWorldPing();

protected:
	/** 월드맵 위젯 클래스 (BP에서 WBP_HellunaWorldMap 지정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldMap|UI (월드맵|UI)",
		meta = (DisplayName = "World Map Widget Class (월드맵 위젯 클래스)"))
	TSubclassOf<UHellunaWorldMapWidget> WorldMapWidgetClass;

	/** M키 토글 입력 액션 (IA_ToggleMap 에셋) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldMap|Input (월드맵|입력)",
		meta = (DisplayName = "Toggle Map Action (맵 토글 액션)"))
	TObjectPtr<UInputAction> ToggleMapAction = nullptr;

	/** 월드맵 입력 매핑 컨텍스트 (M키 → ToggleMapAction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldMap|Input (월드맵|입력)",
		meta = (DisplayName = "World Map Mapping Context (월드맵 입력 매핑)"))
	TObjectPtr<UInputMappingContext> WorldMapMappingContext = nullptr;

private:
	/** 월드맵 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaWorldMapWidget> WorldMapWidgetInstance = nullptr;

	/** 서버 측 마지막 핑 설정 시점 (0.1초 쿨다운용, 서버에서만 사용) */
	double LastPingServerTime = 0.0;
	static constexpr double PingCooldownSeconds = 0.1;

	/** M키 입력 핸들러 (Enhanced Input) */
	void OnToggleWorldMapInput(const struct FInputActionValue& Value);

	// =========================================================================================
	// [Loading Barrier] 로딩 씬 진입 / Ready 조건 폴링 / 페이드 전환 (Reedme/loading/*)
	// =========================================================================================
public:
	/** 서버 → 클라: 로딩 씬 진입 (ViewTarget 전환 + HUD 추가). PostLogin 직후 호출. */
	UFUNCTION(Client, Reliable)
	void Client_EnterLoadingScene(const TArray<FString>& ExpectedIds, int32 PartyId);

	/** 서버 → 클라: 내 Ready 송신 직후 파티 현황 스냅샷 1회 전달. */
	UFUNCTION(Client, Reliable)
	void Client_DeliverPartyStatus(const TArray<FHellunaReadyInfo>& Snapshot);

	/** 서버 → 클라: 구독자에게 파티원 Ready 변경 개별 알림. */
	UFUNCTION(Client, Reliable)
	void Client_UpdateReadyStatus(const FString& PlayerId, bool bReady);

	/** 서버 → 클라: 배리어 해제 — 페이드 아웃 + Pawn ViewTarget 전환. */
	UFUNCTION(Client, Reliable)
	void Client_FadeToGame();

	/** 클라 → 서버: Ready 조건 충족 보고. */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReportClientReady();

	/** BP 노출용 — 현재 로딩 HUD 인스턴스. */
	UFUNCTION(BlueprintPure, Category = "Loading Barrier")
	UHellunaLoadingHUDWidget* GetLoadingHUDWidget() const { return LoadingHUDWidgetInstance; }

protected:
	/** BP에서 설정할 로딩 HUD 위젯 클래스. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Barrier|UI",
		meta = (DisplayName = "Loading HUD Widget Class (로딩 HUD 위젯)"))
	TSubclassOf<UHellunaLoadingHUDWidget> LoadingHUDWidgetClass;

	/** BP에서 설정: 관람 카메라로 사용할 태그. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Barrier|Scene",
		meta = (DisplayName = "Loading Camera Tag (관람 카메라 태그)"))
	FName LoadingCameraTag = TEXT("LoadingCamera");

	/** BP에서 설정: 페이드 시 감쇠시킬 LoadingShipActor 태그. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading Barrier|Scene",
		meta = (DisplayName = "Loading Ship Tag (로딩 우주선 태그)"))
	FName LoadingShipTag = TEXT("LoadingShip");

	/** Ready 조건 폴링 주기(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Loading Barrier|Ready",
		meta = (DisplayName = "Ready Poll Interval (초)", ClampMin = "0.05"))
	float ReadyPollInterval = 0.25f;

private:
	/** 로딩 HUD 위젯 인스턴스. */
	UPROPERTY()
	TObjectPtr<UHellunaLoadingHUDWidget> LoadingHUDWidgetInstance = nullptr;

	/** 관람 카메라 캐시. */
	UPROPERTY()
	TWeakObjectPtr<ACameraActor> LoadingCameraActor;

	/** 내 Ready 송신 여부 — 이중 안전장치. */
	UPROPERTY()
	bool bMyReadyReported = false;

	/** 로딩 씬 진입 여부 — HUD 복원/정리 안전장치. */
	UPROPERTY()
	bool bInLoadingScene = false;

	/** Ready 조건 폴링 타이머. */
	FTimerHandle ReadyPollTimerHandle;

	/** 배리어 상대 파티 ID. */
	UPROPERTY()
	int32 CachedBarrierPartyId = 0;

	/** 이번 배리어 기대 PlayerIds. */
	UPROPERTY()
	TArray<FString> CachedExpectedIds;

	/** 클라 Ready 조건 7개 체크 (Reedme/loading/04 §Ready의 정의). */
	bool IsClientReadyForBarrier() const;

	/** 폴링 콜백. */
	void PollReadyConditions();

	/** 로딩 씬 정리 (페이드 시점). */
	void LeaveLoadingScene();

	/** 관람 카메라 ViewTarget 전환. 성공 시 true. */
	bool TryActivateLoadingCamera();
};
