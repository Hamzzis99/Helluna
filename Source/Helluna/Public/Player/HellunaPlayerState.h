// HellunaPlayerState.h
// 플레이어 고유 ID를 저장하는 PlayerState 클래스
// 
// ============================================
// 📌 역할:
// - 로그인된 플레이어의 고유 ID (PlayerUniqueId) 저장
// - 로그인 상태 (bIsLoggedIn) 관리
// - 서버 ↔ 클라이언트 간 Replicated (동기화)
// - Seamless Travel 시에도 유지됨
// 
// ============================================
// 📌 핵심 변수:
// ============================================
// 
// FString PlayerUniqueId (Replicated)
//   - 로그인한 플레이어의 고유 ID
//   - 로그인 전: "" (빈 문자열)
//   - 로그인 후: 사용자가 입력한 아이디 (예: "test123")
//   - ★ 인벤토리 저장 시 이 ID를 키로 사용함!
// 
// bool bIsLoggedIn (Replicated)
//   - 로그인 상태 플래그
//   - 로그인 전: false
//   - 로그인 후: true
// 
// ============================================
// 📌 사용 위치:
// ============================================
// 
// [로그인 성공 시] - DefenseGameMode::OnLoginSuccess()
//   PlayerState->SetLoginInfo(PlayerId);
//   → PlayerUniqueId = "test123", bIsLoggedIn = true
// 
// [로그아웃 시] - DefenseGameMode::Logout()
//   PlayerState->ClearLoginInfo();
//   → PlayerUniqueId = "", bIsLoggedIn = false
// 
// [인벤토리 저장 시]
//   FString PlayerId = PlayerState->GetPlayerUniqueId();
//   InventorySaveGame->SavePlayerInventory(PlayerId, InventoryComponent);
// 
// [인벤토리 로드 시]
//   FString PlayerId = PlayerState->GetPlayerUniqueId();
//   InventorySaveGame->LoadPlayerInventory(PlayerId, InventoryComponent);
// 
// ============================================
// 📌 주의사항:
// ============================================
// - SetLoginInfo(), ClearLoginInfo()는 서버에서만 호출해야 함
// - Replicated이므로 클라이언트에서 값을 변경해도 서버에 반영 안 됨
// - HasAuthority() 체크 후 호출할 것
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "HellunaTypes.h"
#include "HellunaPlayerState.generated.h"

/**
 * Helluna 프로젝트 전용 PlayerState
 * 플레이어 로그인 정보를 저장하고 레벨 간 유지
 */
UCLASS()
class HELLUNA_API AHellunaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AHellunaPlayerState();

	// ============================================
	// 📌 Replicated 속성 (서버 ↔ 클라이언트 동기화)
	// ============================================

	/** 
	 * 플레이어 고유 ID (로그인 아이디)
	 * 로그인 전: 빈 문자열 ""
	 * 로그인 후: 사용자가 입력한 아이디
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Login", meta = (DisplayName = "플레이어 고유 ID"))
	FString PlayerUniqueId;

	/**
	 * 로그인 상태
	 * 로그인 전: false
	 * 로그인 후: true
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Login", meta = (DisplayName = "로그인 여부"))
	bool bIsLoggedIn;

	// ============================================
	// 📌 캐릭터 선택 시스템
	// ============================================
	
	/**
	 * 선택한 캐릭터 타입
	 * None: 아직 캐릭터 미선택 (캐릭터 선택 UI 필요)
	 * Lui/Luna/Liam: 해당 캐릭터 선택됨
	 * 
	 * SeamlessTravel 시에도 유지됨 (맵 이동 후 같은 캐릭터로 스폰)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Character Select (캐릭터 선택)",
		meta = (DisplayName = "선택한 캐릭터 타입"))
	EHellunaHeroType SelectedHeroType;

	// ============================================
	// 📌 월드맵/미니맵 핑 (서버 권위, 전원 복제)
	// ============================================

	/** 월드맵 핑 위치 (bHasPing=false면 의미 없음) */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "World Map Ping (월드맵 핑)",
		meta = (DisplayName = "Ping Location (핑 월드 좌표)"))
	FVector_NetQuantize PingLocation;

	/** 핑 존재 여부 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "World Map Ping (월드맵 핑)",
		meta = (DisplayName = "Has Ping (핑 존재 여부)"))
	bool bHasPing;

	/** 핑 설정 (서버 권위, HeroController RPC에서 호출) */
	void Server_AuthoritativeSetPing(const FVector& WorldLocation);

	/** 핑 제거 (서버 권위) */
	void Server_AuthoritativeClearPing();

	UFUNCTION(BlueprintPure, Category = "World Map Ping (월드맵 핑)")
	bool HasPing() const { return bHasPing; }

	UFUNCTION(BlueprintPure, Category = "World Map Ping (월드맵 핑)")
	FVector GetPingLocation() const { return PingLocation; }

	// ============================================
	// 📌 유틸리티 함수
	// ============================================

	/**
	 * 로그인 정보 설정 (서버에서만 호출)
	 * @param InPlayerId - 로그인한 플레이어 ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void SetLoginInfo(const FString& InPlayerId);

	/**
	 * 로그아웃 처리 (서버에서만 호출)
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ClearLoginInfo();

	/**
	 * 로그인 여부 확인
	 * @return 로그인 되어있으면 true
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsLoggedIn() const { return bIsLoggedIn; }

	/**
	 * 플레이어 ID 반환
	 * @return 플레이어 고유 ID (로그인 전이면 빈 문자열)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	FString GetPlayerUniqueId() const { return PlayerUniqueId; }

	// ============================================
	// 📌 캐릭터 선택 관련 함수
	// ============================================

	/**
	 * 선택한 캐릭터 타입 설정 (서버에서만 호출)
	 * @param InHeroType - 캐릭터 타입 (EHellunaHeroType)
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetSelectedHeroType(EHellunaHeroType InHeroType);

	/**
	 * 선택한 캐릭터 타입 반환
	 * @return 캐릭터 타입 (None이면 미선택)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Character Select (캐릭터 선택)")
	EHellunaHeroType GetSelectedHeroType() const { return SelectedHeroType; }

	/**
	 * 캐릭터가 선택되었는지 확인
	 * @return 캐릭터가 선택되었으면 true
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Character Select (캐릭터 선택)")
	bool HasSelectedCharacter() const { return SelectedHeroType != EHellunaHeroType::None; }

	/**
	 * 캐릭터 선택 초기화 (로그아웃 시 호출)
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void ClearSelectedCharacter();

	// ============================================
	// 📌 [호환성] 기존 Index 기반 함수
	// ============================================
	
	/**
	 * [호환성] 선택한 캐릭터 인덱스 설정
	 * 내부적으로 EHellunaHeroType으로 변환됨
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetSelectedCharacterIndex(int32 InIndex);

	/**
	 * [호환성] 선택한 캐릭터 인덱스 반환
	 * @return 캐릭터 인덱스 (-1이면 미선택, 0=Lui, 1=Luna, 2=Liam)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Character Select (캐릭터 선택)")
	int32 GetSelectedCharacterIndex() const;

protected:
	// ============================================
	// 📌 Replication 설정
	// ============================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
