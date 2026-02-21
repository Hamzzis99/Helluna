// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Utils/Vote/VoteHandler.h
 * @brief   투표 결과를 처리할 액터가 구현해야 할 인터페이스
 *
 * @details MoveMapActor, KickVoteActor 등 투표 결과에 따라 특정 동작을 수행하는
 *          액터들이 이 인터페이스를 구현합니다. VoteManager는 투표가 완료되면
 *          이 인터페이스를 통해 결과를 전달합니다.
 *
 * @author  [작성자]
 * @date    2026-02-05
 *
 * @usage   C++ 구현 예시:
 * @code
 *          // MoveMapActor.h
 *          UCLASS()
 *          class AMoveMapActor : public AActor, public IVoteHandler
 *          {
 *              GENERATED_BODY()
 *
 *          public:
 *              // 필수 구현 - 투표 통과 시 맵 이동 실행
 *              virtual void ExecuteVoteResult_Implementation(const FVoteRequest& Request) override;
 *
 *              // 선택적 구현 - 투표 시작 전 맵 유효성 검사
 *              virtual bool OnVoteStarting_Implementation(const FVoteRequest& Request) override;
 *          };
 *
 *          // MoveMapActor.cpp
 *          void AMoveMapActor::ExecuteVoteResult_Implementation(const FVoteRequest& Request)
 *          {
 *              if (Request.VoteType == EVoteType::MapMove)
 *              {
 *                  UGameplayStatics::OpenLevel(this, Request.TargetMapName);
 *              }
 *          }
 *
 *          bool AMoveMapActor::OnVoteStarting_Implementation(const FVoteRequest& Request)
 *          {
 *              // 맵이 존재하는지 검증
 *              return FPackageName::DoesPackageExist(Request.TargetMapName.ToString());
 *          }
 * @endcode
 *
 * @usage   Blueprint 구현 예시:
 *          1. Actor 블루프린트 생성
 *          2. Class Settings > Interfaces > Add > IVoteHandler 추가
 *          3. My Blueprint > Interfaces에서 원하는 함수 우클릭 > Implement Event
 *          4. ExecuteVoteResult 이벤트에서 투표 결과 처리 로직 구현
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Utils/Vote/VoteTypes.h"
#include "VoteHandler.generated.h"

/**
 * @brief 투표 핸들러 UInterface 클래스
 * @note  이 클래스는 UE 리플렉션 시스템용이며 직접 사용하지 않습니다.
 *        실제 인터페이스는 IVoteHandler를 사용하세요.
 */
UINTERFACE(MinimalAPI, Blueprintable, BlueprintType)
class UVoteHandler : public UInterface
{
	GENERATED_BODY()
};

/**
 * @brief   투표 결과 처리 인터페이스
 * @details VoteManager가 투표 완료 후 결과를 전달할 때 사용하는 인터페이스입니다.
 *          투표 결과에 따라 특정 동작(맵 이동, 강퇴 등)을 수행하는 액터가 구현합니다.
 *
 * @note    모든 콜백은 서버에서만 호출됩니다.
 * @note    ExecuteVoteResult는 반드시 구현해야 하며, 나머지는 선택적입니다.
 */
class HELLUNA_API IVoteHandler
{
	GENERATED_BODY()

public:
	/**
	 * @brief   투표 통과 시 호출되는 함수 (필수 구현)
	 * @details 투표가 성공적으로 통과되면 VoteManager가 이 함수를 호출합니다.
	 *          구현 클래스에서 실제 동작(맵 이동, 강퇴 등)을 수행합니다.
	 *
	 * @param   Request - 통과된 투표 요청 정보
	 *                    VoteType에 따라 TargetMapName, TargetPlayer, TargetDifficulty 등 참조
	 *
	 * @note    서버에서만 호출됩니다. (Authority)
	 * @note    C++에서는 ExecuteVoteResult_Implementation을 오버라이드하세요.
	 * @note    Blueprint에서는 Event Execute Vote Result를 구현하세요.
	 *
	 * @code
	 *          void AMyActor::ExecuteVoteResult_Implementation(const FVoteRequest& Request)
	 *          {
	 *              switch (Request.VoteType)
	 *              {
	 *              case EVoteType::MapMove:
	 *                  // 맵 이동 처리
	 *                  break;
	 *              case EVoteType::Kick:
	 *                  // 강퇴 처리
	 *                  break;
	 *              }
	 *          }
	 * @endcode
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Vote")
	void ExecuteVoteResult(const FVoteRequest& Request);

	/**
	 * @brief   투표 실패 시 호출되는 함수 (선택적 구현)
	 * @details 투표가 시간 초과, 반대 다수 등의 이유로 실패하면 호출됩니다.
	 *          실패 시 특별한 처리가 필요한 경우에만 구현하세요.
	 *
	 * @param   Request - 실패한 투표 요청 정보
	 * @param   Reason  - 실패 사유 문자열
	 *                    예: "시간 초과", "반대 다수", "찬성 부족" 등
	 *
	 * @note    서버에서만 호출됩니다. (Authority)
	 * @note    기본 구현은 아무 동작도 하지 않습니다.
	 *
	 * @code
	 *          void AMyActor::OnVoteFailed_Implementation(const FVoteRequest& Request, const FString& Reason)
	 *          {
	 *              UE_LOG(LogHellunaVote, Log, TEXT("투표 실패: %s - 사유: %s"),
	 *                     *Request.GetVoteTypeName(), *Reason);
	 *
	 *              // 실패 UI 표시 등 추가 처리
	 *          }
	 * @endcode
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Vote")
	void OnVoteFailed(const FVoteRequest& Request, const FString& Reason);

	/**
	 * @brief   투표 시작 시 호출되는 함수 (선택적 구현)
	 * @details 투표가 시작되기 전에 호출되어 추가 검증이나 준비 작업을 수행합니다.
	 *          false를 반환하면 투표가 취소됩니다.
	 *
	 * @param   Request - 시작될 투표 요청 정보
	 *
	 * @return  true  - 투표 진행 허용
	 * @return  false - 투표 취소 (검증 실패 등)
	 *
	 * @note    서버에서만 호출됩니다. (Authority)
	 * @note    기본 구현은 항상 true를 반환합니다.
	 *
	 * @code
	 *          bool AMapMoveActor::OnVoteStarting_Implementation(const FVoteRequest& Request)
	 *          {
	 *              // 목표 맵이 존재하는지 검증
	 *              if (Request.VoteType == EVoteType::MapMove)
	 *              {
	 *                  FString MapPath = FString::Printf(TEXT("/Game/Maps/%s"), *Request.TargetMapName.ToString());
	 *                  if (!FPackageName::DoesPackageExist(MapPath))
	 *                  {
	 *                      UE_LOG(LogHellunaVote, Warning, TEXT("맵을 찾을 수 없음: %s"), *MapPath);
	 *                      return false;
	 *                  }
	 *              }
	 *              return true;
	 *          }
	 * @endcode
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Vote")
	bool OnVoteStarting(const FVoteRequest& Request);

	/**
	 * @brief   투표 취소 시 호출되는 함수 (선택적 구현)
	 * @details 중도 퇴장, 투표 시작자 연결 끊김 등으로 투표가 취소되면 호출됩니다.
	 *          실패(OnVoteFailed)와 달리 외부 요인으로 인한 강제 종료입니다.
	 *
	 * @param   Request - 취소된 투표 요청 정보
	 * @param   Reason  - 취소 사유 문자열
	 *                    예: "투표 시작자 퇴장", "참여 인원 부족", "중도 퇴장 정책에 의한 취소" 등
	 *
	 * @note    서버에서만 호출됩니다. (Authority)
	 * @note    기본 구현은 아무 동작도 하지 않습니다.
	 *
	 * @code
	 *          void AMyActor::OnVoteCancelled_Implementation(const FVoteRequest& Request, const FString& Reason)
	 *          {
	 *              UE_LOG(LogHellunaVote, Warning, TEXT("투표 취소됨: %s - 사유: %s"),
	 *                     *Request.GetVoteTypeName(), *Reason);
	 *
	 *              // 리소스 정리 등 추가 처리
	 *          }
	 * @endcode
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Vote")
	void OnVoteCancelled(const FVoteRequest& Request, const FString& Reason);
};
