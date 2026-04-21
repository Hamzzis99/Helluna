/**
 * HellunaAIDebugSettings.h
 *
 * Project Settings > Helluna > AI Debug 에서 편집 가능한 AI 디버그 토글 모음.
 * 토글 변경 시 콘솔 변수(ai.debug.xxx)로 동기화 — 런타임 코드는 CVar만 읽으면 됨.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HellunaAIDebugSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings, defaultconfig,
	meta = (DisplayName = "AI Debug"))
class HELLUNA_API UHellunaAIDebugSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHellunaAIDebugSettings();

	virtual FName GetCategoryName() const override { return FName("Helluna"); }

	/**
	 * 몬스터 AttackZone 박스를 3D 뷰포트에 그립니다.
	 * 초록 = 타겟과 오버랩 (공격 가능), 빨강 = 미오버랩.
	 * 노란 화살표 = 몬스터 정면 방향.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Attack Zone",
		meta = (DisplayName = "공격존 디버그 박스 표시",
			ConsoleVariable = "ai.debug.attackzone"))
	bool bDrawAttackZone = false;

	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void SyncCVars() const;
};
