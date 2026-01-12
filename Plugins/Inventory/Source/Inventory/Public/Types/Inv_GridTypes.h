#pragma once

#include "Inv_GridTypes.generated.h"

class UInv_InventoryItem;

UENUM(BlueprintType)
enum class EInv_ItemCategory : uint8 // 위젯 만들 때 정확한 이름을 작성해야해 enum으로 알겠지. 기억해. 다만 Build는 빼고 컴포넌트로 할까 고민중
{
	Equippable,
	Consumable,
	Craftable,
	None
};

USTRUCT()
struct FInv_SlotAvailability
{
	GENERATED_BODY()

	//초기화 부분인데 왜 굳이 두 개를 쓰지?
	FInv_SlotAvailability() {}
	FInv_SlotAvailability(int32 ItemIndex, int32 Room, bool bHasItem) : Index(ItemIndex), AmountToFill(Room), bItemAtIndex(bHasItem) {}

	int32 Index{INDEX_NONE}; // 아이템을 얼마나 채울지
	int32 AmountToFill{ 0 }; // 얼마나 채우고 있으며
	bool bItemAtIndex{ false }; // 아이콘 위젯을 만들어야 하는지
};

USTRUCT()
//어떤 항목인지 결정해주게 하는 부분들.
struct FInv_SlotAvailabilityResult
{
	GENERATED_BODY()

	FInv_SlotAvailabilityResult() {} //엥 왜 재귀적인 방법을 쓰지?


	TWeakObjectPtr<UInv_InventoryItem> Item; // 아이템이 유효한지 확인하는 부분.
	int32 TotalRoomToFill{ 0 }; // 채울 수 있는 공간 (0개면 불가능)
	int32 Remainder{ 0 };
	bool bStackable = {false }; // 쌓을 수 있는지
	int32 EntryIndex{ INDEX_NONE }; // ⭐ FastArray Entry Index (서버-클라이언트 동기화용)
	TArray<FInv_SlotAvailability> SlotAvailabilities; //슬롯 가능 여부를 만드는 것.
};

//타일에 대한 마우스 호버 부분 
UENUM(BlueprintType)
enum class EInv_TileQuadrant : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	None
};

//마우스 커서 위치가 어디인지에 대한 변수들. TileCoordinats, TileIndex, TileQuadrant
USTRUCT(BlueprintType)
struct FInv_TileParameters
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	FIntPoint TileCoordinats{};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	int32 TileIndex{ INDEX_NONE };

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	EInv_TileQuadrant TileQuadrant{ EInv_TileQuadrant::None };
};

inline bool operator==(const FInv_TileParameters& A, const FInv_TileParameters& B)
{
	return A.TileCoordinats == B.TileCoordinats && A.TileIndex == B.TileIndex && A.TileQuadrant == B.TileQuadrant; // 모두 동일해야! 
}

USTRUCT()
struct FInv_SpaceQueryResult
{
	GENERATED_BODY()

	// 결국 교체가 가능한지에 대해서 작성한 변수들.

	// True if the space queried has no items in it
	// 해당 공간에 항목이 없으면 true라고 설정
	bool bHasSpace{ false }; // 공간이 있는지 확인

	// Valid if there's a single item we can swap with
	// 교환할 수 있는 항목인지?
	TWeakObjectPtr<UInv_InventoryItem> ValidItem = nullptr; // 유효한 아이템 포인터

	// Upper left index of the valid item, if there is one.
	// 유효한 항목이 있는 경우 왼쪽 위 인덱스.
	int32 UpperLeftIndex{INDEX_NONE}; // 왼쪽 위 인덱스
};