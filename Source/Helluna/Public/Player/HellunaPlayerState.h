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
// 📌 사용 위치:
// - LoginLevel: 로그인 성공 시 ID 설정
// - GihyeonMap: 인벤토리 저장/복원 시 플레이어 식별
// 
// 📌 작성자: Claude & Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
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

protected:
	// ============================================
	// 📌 Replication 설정
	// ============================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
