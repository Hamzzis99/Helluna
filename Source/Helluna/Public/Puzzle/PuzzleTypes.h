// Source/Helluna/Public/Puzzle/PuzzleTypes.h

#pragma once

#include "CoreMinimal.h"
#include "PuzzleTypes.generated.h"

/**
 * 퍼즐 타입/유틸리티
 *
 * [보스전 로드맵]
 * FPuzzleGridData, FPuzzleCell, EPuzzlePipeType, PuzzleUtils는
 * 보스 보호막 시스템에서도 그대로 재사용.
 * 보스 페이즈별 GridSize 변경(4→5) 시 이 구조체 수정 불필요 (GridSize가 이미 변수).
 */

/**
 * 퍼즐 파이프 타입
 */
UENUM(BlueprintType)
enum class EPuzzlePipeType : uint8
{
	None     UMETA(DisplayName = "None"),
	Straight UMETA(DisplayName = "Straight (직선)"),
	Curve    UMETA(DisplayName = "Curve (곡선)"),
	Start    UMETA(DisplayName = "Start (시작점)"),
	End      UMETA(DisplayName = "End (도착점)")
};

/**
 * 퍼즐 그리드 단일 셀
 */
USTRUCT(BlueprintType)
struct FPuzzleCell
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	EPuzzlePipeType PipeType = EPuzzlePipeType::None;

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	int32 Rotation = 0; // 0, 90, 180, 270

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	bool bLocked = false; // 시작점/도착점은 회전 불가

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	int32 Row = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	int32 Col = 0;
};

/**
 * 4x4 퍼즐 그리드 데이터
 */
USTRUCT(BlueprintType)
struct FPuzzleGridData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	int32 GridSize = 4;

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	TArray<FPuzzleCell> Cells; // GridSize * GridSize 개

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	int32 StartIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Puzzle")
	int32 EndIndex = 15;
};

/**
 * 퍼즐 유틸리티 함수
 */
namespace PuzzleUtils
{
	// 방향 상수
	static const FIntPoint DirUp    = FIntPoint(-1, 0);
	static const FIntPoint DirDown  = FIntPoint(1, 0);
	static const FIntPoint DirLeft  = FIntPoint(0, -1);
	static const FIntPoint DirRight = FIntPoint(0, 1);

	/** 반대 방향 반환 */
	inline FIntPoint GetOppositeDirection(const FIntPoint& Dir)
	{
		return FIntPoint(-Dir.X, -Dir.Y);
	}

	/**
	 * 현재 회전 상태에서 셀의 연결 방향 반환
	 * - Straight 0°: Left, Right
	 * - Straight 90°: Up, Down
	 * - Curve 0°: Down, Right (하→우)
	 * - Curve 90°: Left, Down (좌→하)
	 * - Curve 180°: Left, Up (좌→상)
	 * - Curve 270°: Up, Right (상→우)
	 * - Start: Right (고정)
	 * - End: Up (고정)
	 */
	inline TArray<FIntPoint> GetConnections(const FPuzzleCell& Cell)
	{
		TArray<FIntPoint> Connections;

		switch (Cell.PipeType)
		{
		case EPuzzlePipeType::Straight:
			switch (Cell.Rotation)
			{
			case 0:
			case 180:
				Connections = {DirLeft, DirRight};
				break;
			case 90:
			case 270:
				Connections = {DirUp, DirDown};
				break;
			default:
				Connections = {DirLeft, DirRight};
				break;
			}
			break;

		case EPuzzlePipeType::Curve:
			switch (Cell.Rotation)
			{
			case 0:   Connections = {DirDown, DirRight};  break; // 하→우
			case 90:  Connections = {DirLeft, DirDown};    break; // 좌→하
			case 180: Connections = {DirLeft, DirUp};      break; // 좌→상
			case 270: Connections = {DirUp, DirRight};     break; // 상→우
			default:  Connections = {DirDown, DirRight};   break;
			}
			break;

		case EPuzzlePipeType::Start:
			Connections = {DirRight}; // 고정: Right
			break;

		case EPuzzlePipeType::End:
			Connections = {DirUp}; // 고정: Up
			break;

		default:
			break;
		}

		return Connections;
	}

	/**
	 * BFS로 Start에서 End까지 연결 경로 존재 여부 확인
	 * 인접 셀의 연결 방향이 서로 맞물려야 연결 성립
	 */
	inline bool CheckSolved(const FPuzzleGridData& Grid)
	{
		const int32 TotalCells = Grid.GridSize * Grid.GridSize;
		if (Grid.Cells.Num() != TotalCells)
		{
			return false;
		}

		if (!Grid.Cells.IsValidIndex(Grid.StartIndex) || !Grid.Cells.IsValidIndex(Grid.EndIndex))
		{
			return false;
		}

		// BFS (TArray 기반 큐)
		TArray<int32> Queue;
		TSet<int32> Visited;

		Queue.Add(Grid.StartIndex);
		Visited.Add(Grid.StartIndex);
		int32 QueueHead = 0;

		while (QueueHead < Queue.Num())
		{
			const int32 CurrentIndex = Queue[QueueHead++];

			if (CurrentIndex == Grid.EndIndex)
			{
				return true;
			}

			const FPuzzleCell& CurrentCell = Grid.Cells[CurrentIndex];
			const TArray<FIntPoint> Connections = GetConnections(CurrentCell);

			for (const FIntPoint& Dir : Connections)
			{
				const int32 NewRow = CurrentCell.Row + Dir.X;
				const int32 NewCol = CurrentCell.Col + Dir.Y;

				// 경계 검사
				if (NewRow < 0 || NewRow >= Grid.GridSize || NewCol < 0 || NewCol >= Grid.GridSize)
				{
					continue;
				}

				const int32 NewIndex = NewRow * Grid.GridSize + NewCol;
				if (Visited.Contains(NewIndex))
				{
					continue;
				}

				// 인접 셀이 역방향으로 연결되는지 확인
				const FPuzzleCell& AdjacentCell = Grid.Cells[NewIndex];
				const TArray<FIntPoint> AdjConnections = GetConnections(AdjacentCell);
				const FIntPoint ReverseDir = GetOppositeDirection(Dir);

				bool bMutualConnection = false;
				for (const FIntPoint& AdjDir : AdjConnections)
				{
					if (AdjDir == ReverseDir)
					{
						bMutualConnection = true;
						break;
					}
				}

				if (bMutualConnection)
				{
					Visited.Add(NewIndex);
					Queue.Add(NewIndex);
				}
			}
		}

		return false;
	}

	/**
	 * Start에서 BFS로 현재 연결된 셀 인덱스 집합 반환
	 * CheckSolved와 동일 로직이지만 End 도달 여부와 무관하게 연결된 모든 셀 반환
	 */
	inline TSet<int32> GetConnectedCells(const FPuzzleGridData& Grid)
	{
		TSet<int32> Connected;
		const int32 TotalCells = Grid.GridSize * Grid.GridSize;
		if (Grid.Cells.Num() != TotalCells || !Grid.Cells.IsValidIndex(Grid.StartIndex))
		{
			return Connected;
		}

		TArray<int32> Queue;
		Queue.Add(Grid.StartIndex);
		Connected.Add(Grid.StartIndex);
		int32 QueueHead = 0;

		while (QueueHead < Queue.Num())
		{
			const int32 CurrentIndex = Queue[QueueHead++];
			const FPuzzleCell& CurrentCell = Grid.Cells[CurrentIndex];
			const TArray<FIntPoint> Connections = GetConnections(CurrentCell);

			for (const FIntPoint& Dir : Connections)
			{
				const int32 NewRow = CurrentCell.Row + Dir.X;
				const int32 NewCol = CurrentCell.Col + Dir.Y;

				if (NewRow < 0 || NewRow >= Grid.GridSize || NewCol < 0 || NewCol >= Grid.GridSize)
				{
					continue;
				}

				const int32 NewIndex = NewRow * Grid.GridSize + NewCol;
				if (Connected.Contains(NewIndex))
				{
					continue;
				}

				const FPuzzleCell& AdjacentCell = Grid.Cells[NewIndex];
				const TArray<FIntPoint> AdjConnections = GetConnections(AdjacentCell);
				const FIntPoint ReverseDir = GetOppositeDirection(Dir);

				bool bMutualConnection = false;
				for (const FIntPoint& AdjDir : AdjConnections)
				{
					if (AdjDir == ReverseDir)
					{
						bMutualConnection = true;
						break;
					}
				}

				if (bMutualConnection)
				{
					Connected.Add(NewIndex);
					Queue.Add(NewIndex);
				}
			}
		}

		return Connected;
	}

	/**
	 * 두 연결 방향으로부터 파이프 타입과 회전 결정
	 * @param Conn1  첫 번째 연결 방향
	 * @param Conn2  두 번째 연결 방향
	 * @param OutCell  결과가 기록될 셀
	 */
	inline void DeterminePipeTypeAndRotation(const FIntPoint& Conn1, const FIntPoint& Conn2, FPuzzleCell& OutCell)
	{
		// 방향 쌍을 Set으로 비교
		auto HasPair = [&](const FIntPoint& A, const FIntPoint& B) -> bool
		{
			return (Conn1 == A && Conn2 == B) || (Conn1 == B && Conn2 == A);
		};

		if (HasPair(DirLeft, DirRight))         // 좌 + 우
		{
			OutCell.PipeType = EPuzzlePipeType::Straight;
			OutCell.Rotation = 0;
		}
		else if (HasPair(DirUp, DirDown))       // 상 + 하
		{
			OutCell.PipeType = EPuzzlePipeType::Straight;
			OutCell.Rotation = 90;
		}
		else if (HasPair(DirDown, DirRight))    // 하 + 우
		{
			OutCell.PipeType = EPuzzlePipeType::Curve;
			OutCell.Rotation = 0;
		}
		else if (HasPair(DirLeft, DirDown))     // 좌 + 하
		{
			OutCell.PipeType = EPuzzlePipeType::Curve;
			OutCell.Rotation = 90;
		}
		else if (HasPair(DirLeft, DirUp))       // 좌 + 상
		{
			OutCell.PipeType = EPuzzlePipeType::Curve;
			OutCell.Rotation = 180;
		}
		else if (HasPair(DirUp, DirRight))      // 상 + 우
		{
			OutCell.PipeType = EPuzzlePipeType::Curve;
			OutCell.Rotation = 270;
		}
	}
}
