#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HellunaTypes.h"
#include "HellunaLoginController.generated.h"

class UHellunaLoginWidget;
class AHellunaCharacterPreviewActor;
class AHellunaCharacterSelectSceneV2;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class USkeletalMesh;
class UMaterialInterface;

/**
 * ============================================
 * 📌 HellunaLoginController
 * ============================================
 * 
 * 로그인 전용 PlayerController
 * 로그인 UI 표시 및 서버와의 RPC 통신 담당
 * 
 * ============================================
 * 📌 역할:
 * ============================================
 * 1. 로그인 UI (LoginWidget) 생성 및 표시
 * 2. 클라이언트 → 서버 로그인 요청 (Server RPC)
 * 3. 서버 → 클라이언트 로그인 결과 전달 (Client RPC)
 * 4. 로그인 성공 후 GameController로 교체됨
 * 
 * ============================================
 * 📌 로그인 흐름:
 * ============================================
 * 
 * [클라이언트]                              [서버]
 * BeginPlay()                               
 *   └─ ShowLoginWidget() (0.3초 후)        
 *                                           
 * 사용자가 로그인 버튼 클릭                  
 *   ↓                                       
 * OnLoginButtonClicked()                    
 *   ↓                                       
 * Server_RequestLogin() ─────────────────→ DefenseGameMode::ProcessLogin()
 *                                             ├─ 계정 검증
 *                                             └─ OnLoginSuccess() 또는 OnLoginFailed()
 *                                           
 * Client_LoginResult() ←──────────────────  (결과 전달)
 *   └─ UI에 결과 표시                       
 *                                           
 * Client_PrepareControllerSwap() ←────────  (교체 준비)
 *   └─ UI 숨김, 입력 모드 변경              
 *                                           
 *                                          SwapToGameController()
 *                                             └─ 새 GameController 생성 및 Possess
 * 
 * ============================================
 * 📌 BP 설정 필수 항목:
 * ============================================
 * - LoginWidgetClass: 로그인 UI 위젯 클래스 (WBP_LoginWidget)
 * - GameControllerClass: 로그인 후 교체할 Controller (BP_InvPlayerController 등)
 * 
 * ============================================
 * 📌 사용 위치:
 * ============================================
 * - DefenseGameMode의 PlayerControllerClass로 설정
 * - 플레이어 접속 시 자동으로 이 Controller가 생성됨
 * - 로그인 성공 후 GameControllerClass로 교체됨
 * 
 * 📌 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API AHellunaLoginController : public APlayerController
{
	GENERATED_BODY()

public:
	AHellunaLoginController();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ============================================
	// 📌 UI 관리
	// ============================================
	
	/** 로그인 위젯 표시 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ShowLoginWidget();

	/** 로그인 위젯 숨김 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void HideLoginWidget();

	/** 로그인 버튼 클릭 시 호출 (LoginWidget에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void OnLoginButtonClicked(const FString& PlayerId, const FString& Password);

	/** 로그인 위젯 인스턴스 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	UHellunaLoginWidget* GetLoginWidget() const { return LoginWidget; }

	/** 로그인 성공 시 교체할 Controller 클래스 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	TSubclassOf<APlayerController> GetGameControllerClass() const { return GameControllerClass; }

	/** 로그인 결과 UI 표시 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ShowLoginResult(bool bSuccess, const FString& Message);

	// ============================================
	// 📌 RPC (서버 ↔ 클라이언트 통신)
	// ============================================

	/**
	 * [클라이언트 → 서버] 로그인 요청
	 * 
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 * 
	 * 내부 동작:
	 * - DefenseGameMode::ProcessLogin() 호출
	 * - 계정 검증 후 Client_LoginResult()로 결과 전달
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestLogin(const FString& PlayerId, const FString& Password);
	bool Server_RequestLogin_Validate(const FString& PlayerId, const FString& Password);

	// ============================================
	// 📌 SeamlessTravel 후 Controller 스왑 요청
	// ============================================
	// ShowLoginWidget()에서 이미 로그인된 상태 감지 시 호출
	// 서버에서 SwapToGameController() 실행
	// ============================================
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestSwapAfterTravel();
	bool Server_RequestSwapAfterTravel_Validate();

	/**
	 * [서버 → 클라이언트] 로그인 결과 전달
	 * 
	 * @param bSuccess - 로그인 성공 여부
	 * @param ErrorMessage - 실패 시 에러 메시지
	 * 
	 * 내부 동작:
	 * - UI에 결과 표시
	 * - 성공 시 로딩 상태 유지, 실패 시 버튼 다시 활성화
	 */
	UFUNCTION(Client, Reliable)
	void Client_LoginResult(bool bSuccess, const FString& ErrorMessage);

	/**
	 * [서버 → 클라이언트] Controller 교체 준비
	 * 
	 * 내부 동작:
	 * - 로그인 UI 숨김
	 * - 입력 모드를 GameOnly로 변경
	 * - 마우스 커서 숨김
	 */
	UFUNCTION(Client, Reliable)
	void Client_PrepareControllerSwap();

	// ============================================
	// 🎭 캐릭터 선택 시스템 (Phase 3)
	// ============================================

	/**
	 * [클라이언트 → 서버] 캐릭터 선택 요청
	 * 
	 * @param CharacterIndex - 선택한 캐릭터 인덱스 (0: Lui, 1: Luna, 2: Liam)
	 * 
	 * 내부 동작:
	 * - GameMode::ProcessCharacterSelection() 호출
	 * - 중복 체크 후 결과 전달
	 * - 성공 시 SwapToGameController → SpawnHeroCharacter
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SelectCharacter(int32 CharacterIndex);
	bool Server_SelectCharacter_Validate(int32 CharacterIndex);

	/**
	 * [서버 → 클라이언트] 캐릭터 선택 결과 전달
	 * 
	 * @param bSuccess - 선택 성공 여부
	 * @param ErrorMessage - 실패 시 에러 메시지 (예: "다른 플레이어가 사용 중")
	 */
	UFUNCTION(Client, Reliable)
	void Client_CharacterSelectionResult(bool bSuccess, const FString& ErrorMessage);

	/**
	 * [서버 → 클라이언트] 캐릭터 선택 UI 표시 요청
	 * 로그인 성공 후 서버에서 호출
	 * 
	 * @param AvailableCharacters - 각 캐릭터의 선택 가능 여부 (true: 선택 가능, false: 사용 중)
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowCharacterSelectUI(const TArray<bool>& AvailableCharacters);

protected:
	// ============================================
	// 📌 BP 설정 (에디터에서 설정 필요!)
	// ============================================
	
	/** 
	 * 로그인 UI 위젯 클래스
	 * BP에서 WBP_LoginWidget 등으로 설정
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "로그인 위젯 클래스"))
	TSubclassOf<UHellunaLoginWidget> LoginWidgetClass;

	/** 로그인 위젯 인스턴스 (런타임 생성) */
	UPROPERTY()
	TObjectPtr<UHellunaLoginWidget> LoginWidget;

	/** 위젯 표시 지연 타이머 핸들 */
	FTimerHandle RetryTimerHandle;

	/**
	 * 로그인 성공 후 교체할 Controller 클래스
	 * BP에서 BP_InvPlayerController 등으로 설정
	 *
	 * ※ 미설정 시 Controller 교체 없이 캐릭터만 소환됨
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "게임 Controller 클래스"))
	TSubclassOf<APlayerController> GameControllerClass;

	// ============================================
	// 📌 캐릭터 프리뷰 시스템
	// ============================================

	/** 프리뷰 액터 클래스 (BP에서 세팅) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 액터 클래스"))
	TSubclassOf<AHellunaCharacterPreviewActor> PreviewActorClass;

	/** 캐릭터 타입별 SkeletalMesh 매핑 (BP에서 Lui/Luna/Liam 각각 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 메시 맵"))
	TMap<EHellunaHeroType, TSoftObjectPtr<USkeletalMesh>> PreviewMeshMap;

	/** 캐릭터 타입별 프리뷰 AnimInstance 클래스 매핑 (스켈레톤이 다르므로 개별 지정 필요) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 AnimClass 맵"))
	TMap<EHellunaHeroType, TSubclassOf<UAnimInstance>> PreviewAnimClassMap;

	/** 프리뷰 배경으로 사용할 액터 태그 (이 태그를 가진 월드 액터를 SceneCapture ShowOnlyList에 추가) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 배경 액터 태그"))
	FName PreviewBackgroundActorTag = TEXT("PreviewBackground");

	/** 월드 지하 스폰 기준 위치 */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 스폰 기준 위치"))
	FVector PreviewSpawnBaseLocation = FVector(0.f, 0.f, -5000.f);

	/** 캐릭터 간 X축 간격 */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 스폰 간격"))
	float PreviewSpawnSpacing = 300.f;

	/** 캐릭터 타입별 하이라이트 오버레이 머티리얼 (호버 시 Fresnel 윤곽선) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "하이라이트 머티리얼 맵"))
	TMap<EHellunaHeroType, TObjectPtr<UMaterialInterface>> PreviewHighlightMaterialMap;

	/** RenderTarget 해상도 */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "렌더 타겟 해상도"))
	FIntPoint PreviewRenderTargetSize = FIntPoint(1024, 1024);

	// ============================================
	// 📌 캐릭터 프리뷰 V2 시스템
	// ============================================

	/** V2 씬 액터 클래스 (BP에서 세팅, nullptr이면 V1 사용) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview V2 (캐릭터 프리뷰V2)", meta = (DisplayName = "V2 씬 액터 클래스"))
	TSubclassOf<AHellunaCharacterSelectSceneV2> PreviewSceneV2Class;

	/** V2 RenderTarget 해상도 */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview V2 (캐릭터 프리뷰V2)", meta = (DisplayName = "V2 렌더 타겟 해상도"))
	FIntPoint PreviewV2RenderTargetSize = FIntPoint(1920, 1080);

	/** 스폰된 V2 씬 액터 */
	UPROPERTY()
	TObjectPtr<AHellunaCharacterSelectSceneV2> SpawnedPreviewSceneV2;

	/** V2 RenderTarget (GC 방지) */
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> PreviewV2RenderTarget;

	/** V2 SceneCapture (로그인 화면 전용 — 로비에서는 직접 뷰포트 사용) */
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> LoginSceneCapture;

	/** V2 프리뷰 씬 스폰 */
	void SpawnPreviewSceneV2();

	/** V2 프리뷰 씬 파괴 */
	void DestroyPreviewSceneV2();

	// ============================================
	// 📌 캐릭터 프리뷰 내부 상태
	// ============================================

	/** 스폰된 프리뷰 액터 배열 */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaCharacterPreviewActor>> SpawnedPreviewActors;

	/** 생성된 RenderTarget 배열 (GC 방지) */
	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> PreviewRenderTargets;

	// ============================================
	// 📌 캐릭터 프리뷰 함수
	// ============================================

	/** 프리뷰 액터 3개 스폰 (클라이언트 전용) */
	void SpawnPreviewActors();

	/** 프리뷰 액터 전부 파괴 */
	void DestroyPreviewActors();

	/** 인덱스로 프리뷰 액터 반환 */
	AHellunaCharacterPreviewActor* GetPreviewActor(int32 Index) const;

	/** 인덱스로 RenderTarget 반환 */
	UTextureRenderTarget2D* GetPreviewRenderTarget(int32 Index) const;
};
