// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HellunaEditorUtils.generated.h"

class USkeleton;

/**
 * UHellunaEditorUtils
 *
 * 에디터 전용 헬퍼 함수. Python 으로 접근 제한된 작업 (소켓 추가 등) 을 우회.
 *
 * @author 김민우
 */
UCLASS()
class HELLUNA_API UHellunaEditorUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Skeleton 에 새 소켓 추가. 이미 같은 이름이 있으면 false.
	 *
	 * @param Skeleton         대상 스켈레톤
	 * @param SocketName       새 소켓 이름
	 * @param BoneName         부착할 본 이름 (스켈레톤에 실제 존재해야 함)
	 * @param RelativeLocation 본 로컬 기준 위치
	 * @param RelativeRotation 본 로컬 기준 회전
	 * @return true = 추가됨, false = 이미 존재 또는 실패
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Editor")
	static bool AddSocketToSkeleton(
		USkeleton* Skeleton,
		FName SocketName,
		FName BoneName,
		FVector RelativeLocation,
		FRotator RelativeRotation);
};
