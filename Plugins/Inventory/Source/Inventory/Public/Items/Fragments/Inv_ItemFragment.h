#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"

#include "Inv_ItemFragment.generated.h"

class APlayerController;
class AInv_EquipActor;

USTRUCT(BlueprintType)
struct FInv_ItemFragment
{
	GENERATED_BODY()

	//스페셜 함수 이동 연산자 및 이동 할당 연산자 윤정현 교수님 도와줘!!

	FInv_ItemFragment() {} //기본생성자

	//복사연산자들
	FInv_ItemFragment(const FInv_ItemFragment&) = default; // 복사생성자
	FInv_ItemFragment& operator=(const FInv_ItemFragment&) = default; // 복사 할당 연산자

	//이동연산자들
	FInv_ItemFragment(FInv_ItemFragment&&) = default; // 이동 생성자
	FInv_ItemFragment& operator=(FInv_ItemFragment&&) = default; // 이동 할당 연산자

	virtual ~FInv_ItemFragment() {} // 소멸자인데 뭐 어차피 다 필요하잖아 (다형성 소멸자)

	FGameplayTag GetFragmentTag() const { return FragmentTag; } // 조각 태그 가져오기
	void SetFragmentTag(FGameplayTag Tag) { FragmentTag = Tag; } // 조각 태그 설정
	
	virtual void Manifest(){}
	
private:

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories = "FragmentTags")) 
	FGameplayTag FragmentTag = FGameplayTag::EmptyTag; //조각 태그
};



USTRUCT(BlueprintType)
struct FInv_GridFragment : public FInv_ItemFragment
{
	GENERATED_BODY()

	FIntPoint GetGridSize() const { return GridSize; } //그리드 크기 얻기
	void SetGridSize(const FIntPoint& Size) { GridSize = Size; } //그리드 크기 설정
	float GetGridPadding() const { return GridPadding; } //그리드 패딩 얻기
	void SetGridPadding(float Padding) { GridPadding = Padding; } //그리드 패딩 설정

private:
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FIntPoint GridSize{1, 1}; //그리드 크기

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float GridPadding{ 0.f }; //그리드 패딩
};

/*
 *  Item fragment specifically for assimilation into a widget.
 *  아이템 프래그먼트는 위젯에 동화됩니다.
 */
class UInv_CompositeBase;
USTRUCT(BlueprintType)
struct FInv_InventoryItemFragment : public FInv_ItemFragment
{
	GENERATED_BODY()
	
	virtual void Assimilate(UInv_CompositeBase* Composite) const;
protected:
	bool MatchesWidgetTag(const UInv_CompositeBase* Composite) const;
};

// Image Fragment 아이템 정보보기 칸
USTRUCT(BlueprintType) 
struct FInv_ImageFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	UTexture2D* GetIcon() const { return Icon; } //아이콘 텍스처 얻기
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;
	
private:
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TObjectPtr<UTexture2D> Icon = nullptr; //아이콘 텍스처 {nullptr]도 표현 가능

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FVector2D IconDimensions{ 44.f, 44.f }; //아이콘 크기
};

// Text Fragment 아이템 정보보기 칸
USTRUCT(BlueprintType) 
struct FInv_TextFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	FText GetText() const {return FragmentText;} // 텍스트 얻기
	void SetText(const FText& Text) {FragmentText = Text;} // 텍스트 받아오기 지정
	
	// 동화 함수 재정의 이거는 정말 중요한 역할. 인벤토리 Fragment 기반으로 정보를 얻어와주니까. UI를 받아오는데 정말 중요함.
	virtual void Assimilate(UInv_CompositeBase* Composite) const override; 
	
private:
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FText FragmentText;
};

// LabeledNumberFragment 아이템 정보보기 칸
USTRUCT(BlueprintType) 
struct FInv_LabeledNumberFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()
	
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;
	virtual void Manifest() override;
	float GetValue() const { return Value; } // UI에서 지정 한 값 얻기
	
	// When manifesting for the first time, this fragment will randomize. However, one equipped
	// and dropped, an item should retain the same value, so randomization should not occur.
	// 최초 매니페스트 시 이 프래그먼트는 무작위화됩니다. 그러나 장착 및 드롭된 경우 아이템은 동일한 값을 유지해야 하므로 무작위화가 발생하지 않아야 합니다.
	bool bRandomizeOnManifest{true}; //매니페스트 시 무작위화?
	
private:
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FText Text_Label{};

	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	float Value{0.f};

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float Min{0};

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float Max{0};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	bool bCollapseLabel{false};

	UPROPERTY(EditAnywhere, Category = "Inventory")
	bool bCollapseValue{false};

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MinFractionalDigits{1};
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MaxFractionalDigits{1};
};


USTRUCT(BlueprintType)
struct FInv_StackableFragment : public FInv_ItemFragment
{
	GENERATED_BODY()

	int32 GetMaxStackSize() const { return MaxStackSize; } //최대 아이템 스택 크기 얻기
	int32 GetStackCount() const { return StackCount; } //최대 아이템 스택 개수 얻기
	void SetStackCount(int32 Count) { StackCount = Count; } //최대 아이템 스택 개수 설정

private:
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MaxStackSize{ 1 }; //최대 아이템 스택 크기

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 StackCount{ 1 }; //최대 아이템 스택 개수
};




// Consume Fragments 
//아이템 사용 프래그먼트
USTRUCT(BlueprintType)
struct FInv_ConsumeModifier : public FInv_LabeledNumberFragment
{
	GENERATED_BODY()
	
	virtual void OnConsume(APlayerController* PC){}
};

USTRUCT(BlueprintType)
struct FInv_ConsumableFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()
	
	//소비 호출?
	virtual void OnConsume(APlayerController* PC);
	virtual void Assimilate(UInv_CompositeBase* Composite) const override; // 
	virtual void Manifest() override;
private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ExcludeBaseStruct))
	TArray<TInstancedStruct<FInv_ConsumeModifier>> ConsumeModifiers; // 입력 받은 랜덤 지정 값 적용 부분
};

USTRUCT(BlueprintType)
struct FInv_HealthPotionFragment : public FInv_ConsumeModifier
{
	GENERATED_BODY()
	
	// 기존에 사용하던 고정값 참조 방식
	// UPROPERTY(EditAnywhere, Category = "Inventory")
	// float HealAmount = 20.f; //회복량	

	//소비 호출?
	virtual void OnConsume(APlayerController* PC) override;
};

USTRUCT(BlueprintType)
struct FInv_ManaPotionFragment : public FInv_ConsumeModifier
{
	GENERATED_BODY()
	
	virtual void OnConsume(APlayerController* PC) override;
};

// Equipment 
// 장비 프래그먼트

USTRUCT(BlueprintType)
struct FInv_EquipModifier : public FInv_LabeledNumberFragment // 장비를 안 보이게 해주는 역할인가? 나중에 분석 필요.
{
	GENERATED_BODY()

	// 장착과 해제 가상함수들
	virtual void OnEquip(APlayerController* PC) {}
	virtual void OnUnequip(APlayerController* PC) {}
};

USTRUCT(BlueprintType)
struct FInv_StrengthModifier : public FInv_EquipModifier // 힘 스탯 관련 <- 이거 나중에 쓸 수 있곘다
{
	GENERATED_BODY()

	virtual void OnEquip(APlayerController* PC) override;
	virtual void OnUnequip(APlayerController* PC) override;
};


USTRUCT(BlueprintType)
struct FInv_EquipmentFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()
	
	bool bEquipped{false}; //장착 여부
	void OnEquip(APlayerController* PC);
	void OnUnequip(APlayerController* PC);
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;
	virtual void Manifest() override;
	
	AInv_EquipActor* SpawnAttachedActor(USkeletalMeshComponent* AttachMesh) const; // 장착 장비 스폰
	void DestroyAttachedActor() const; // 장착 장비 파괴 (해제)
	FGameplayTag GetEquipmentType() const {return EquipmentType;}
	void SetEquippedActor(AInv_EquipActor* EquipActor);
	
private:
	UPROPERTY(EditAnywhere, Category = "Inventory") // 인벤토리 장착 아이템
	TArray<TInstancedStruct<FInv_EquipModifier>> EquipModifiers;
	
	//장착장비 변수들
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<AInv_EquipActor> EquipActorClass = nullptr; // 장착 장비 클래스
	
	TWeakObjectPtr<AInv_EquipActor> EquippedActor = nullptr; // 장착 장비 포인터 (플레이어 Pawn을 말하는 것인가)
	
	//장비 부착물 지정?
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FName SocketAttachPoint{NAME_None}; // Mesh의 소켓 부착 지점
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FGameplayTag EquipmentType = FGameplayTag::EmptyTag; // 장비 타입 태그
};