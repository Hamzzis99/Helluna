#pragma once

UENUM()
enum class EHellunaConfirmType : uint8
{
	Yes,
	No
};

UENUM()
enum class EHellunaValidType : uint8
{
	Valid,
	Invalid
};

UENUM()
enum class EHellunaSuccessType : uint8
{
	Successful,
	Failed
};

UENUM()
enum class EHellunaCountDownActionInput : uint8
{
	Start,
	Cancel
};

UENUM()
enum class EHellunaCountDownActionOutput : uint8
{
	Updated,
	Completed,
	Cancelled
};

UENUM(BlueprintType)
enum class EHellunaGameDifficulty : uint8
{
	Easy,
	Normal,
	Hard,
	VeryHard
};

UENUM(BlueprintType)
enum class EHellunaInputMode : uint8
{
	GameOnly,
	UIOnly
};
