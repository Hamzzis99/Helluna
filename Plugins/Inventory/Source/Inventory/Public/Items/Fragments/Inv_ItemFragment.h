#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"

#include "Inv_ItemFragment.generated.h"

class APlayerController;
class AInv_EquipActor;

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_ItemFragment - 아이템 프래그먼트 기본 구조체
// ════════════════════════════════════════════════════════════════════════════════
// 모든 아이템 프래그먼트의 부모 클래스
// FragmentTag로 프래그먼트 종류를 구분함
// ════════════════════════════════════════════════════════════════════════════════
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

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories = "FragmentTags", DisplayName = "FragmentTag (프래그먼트 태그)", Tooltip = "이 프래그먼트를 식별하는 GameplayTag"))
	FGameplayTag FragmentTag = FGameplayTag::EmptyTag; //조각 태그
};


// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_GridFragment - 그리드 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// 아이템이 인벤토리 그리드에서 차지하는 크기 정의
// GridSize: 가로x세로 칸 수, GridPadding: 여백
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_GridFragment : public FInv_ItemFragment
{
	GENERATED_BODY()

	FIntPoint GetGridSize() const { return GridSize; } //그리드 크기 얻기
	void SetGridSize(const FIntPoint& Size) { GridSize = Size; } //그리드 크기 설정
	float GetGridPadding() const { return GridPadding; } //그리드 패딩 얻기
	void SetGridPadding(float Padding) { GridPadding = Padding; } //그리드 패딩 설정

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "GridSize (그리드 크기)", Tooltip = "인벤토리에서 차지하는 칸 수 (X=가로, Y=세로)"))
	FIntPoint GridSize{1, 1}; //그리드 크기

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "GridPadding (그리드 여백)", Tooltip = "아이템 주변 여백 (픽셀)"))
	float GridPadding{ 0.f }; //그리드 패딩
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_InventoryItemFragment - 인벤토리 아이템 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// 위젯에 정보를 표시하기 위한 프래그먼트 기본 클래스
// Assimilate(): 위젯에 데이터를 전달하는 함수
// ════════════════════════════════════════════════════════════════════════════════
class UInv_CompositeBase;
USTRUCT(BlueprintType)
struct FInv_InventoryItemFragment : public FInv_ItemFragment
{
	GENERATED_BODY()

	virtual void Assimilate(UInv_CompositeBase* Composite) const;
protected:
	bool MatchesWidgetTag(const UInv_CompositeBase* Composite) const;
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_ImageFragment - 이미지(아이콘) 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// 아이템 아이콘 이미지 정보를 담는 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_ImageFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	UTexture2D* GetIcon() const { return Icon; } //아이콘 텍스처 얻기
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "Icon (아이콘 이미지)", Tooltip = "인벤토리에 표시될 아이템 아이콘"))
	TObjectPtr<UTexture2D> Icon = nullptr; //아이콘 텍스처 {nullptr]도 표현 가능

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "IconDimensions (아이콘 크기)", Tooltip = "아이콘 표시 크기 (가로 x 세로 픽셀)"))
	FVector2D IconDimensions{ 44.f, 44.f }; //아이콘 크기
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_TextFragment - 텍스트 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// 아이템 이름, 설명 등 텍스트 정보를 담는 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_TextFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	FText GetText() const {return FragmentText;} // 텍스트 얻기
	void SetText(const FText& Text) {FragmentText = Text;} // 텍스트 받아오기 지정

	// 동화 함수 재정의 이거는 정말 중요한 역할. 인벤토리 Fragment 기반으로 정보를 얻어와주니까. UI를 받아오는데 정말 중요함.
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "FragmentText (표시 텍스트)", Tooltip = "UI에 표시될 텍스트 (아이템 이름, 설명 등)", MultiLine = true))
	FText FragmentText;
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_LabeledNumberFragment - 라벨+숫자 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// "공격력: 15" 같은 라벨과 숫자값을 표시하는 프래그먼트
// Min~Max 범위에서 랜덤값 생성 가능
// ════════════════════════════════════════════════════════════════════════════════
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
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "Text_Label (라벨 텍스트)", Tooltip = "숫자 앞에 표시될 라벨 (예: '공격력', '방어력')"))
	FText Text_Label{};

	UPROPERTY(VisibleAnywhere, Category = "Inventory", meta = (DisplayName = "Value (현재 값)", Tooltip = "Min~Max 범위에서 결정된 실제 값 (읽기 전용)"))
	float Value{0.f};

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "Min (최소값)", Tooltip = "랜덤 범위의 최소값"))
	float Min{0};

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "Max (최대값)", Tooltip = "랜덤 범위의 최대값"))
	float Max{0};

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "bCollapseLabel (라벨 숨기기)", Tooltip = "true면 라벨 텍스트를 숨김"))
	bool bCollapseLabel{false};

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "bCollapseValue (값 숨기기)", Tooltip = "true면 숫자 값을 숨김"))
	bool bCollapseValue{false};

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "MinFractionalDigits (소수점 최소 자릿수)", Tooltip = "표시할 소수점 이하 최소 자릿수", ClampMin = 0, ClampMax = 4))
	int32 MinFractionalDigits{1};

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "MaxFractionalDigits (소수점 최대 자릿수)", Tooltip = "표시할 소수점 이하 최대 자릿수", ClampMin = 0, ClampMax = 4))
	int32 MaxFractionalDigits{1};
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_StackableFragment - 스택 가능 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// 아이템 중첩(스택) 기능을 위한 프래그먼트
// 포션, 재료 등 여러 개를 하나의 슬롯에 쌓을 수 있는 아이템용
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_StackableFragment : public FInv_ItemFragment
{
	GENERATED_BODY()

	int32 GetMaxStackSize() const { return MaxStackSize; } //최대 아이템 스택 크기 얻기
	int32 GetStackCount() const { return StackCount; } //최대 아이템 스택 개수 얻기
	void SetStackCount(int32 Count) { StackCount = Count; } //최대 아이템 스택 개수 설정

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "MaxStackSize (최대 스택 크기)", Tooltip = "한 슬롯에 쌓을 수 있는 최대 개수", ClampMin = 1))
	int32 MaxStackSize{ 1 }; //최대 아이템 스택 크기

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "StackCount (현재 스택 개수)", Tooltip = "현재 쌓여있는 개수", ClampMin = 1))
	int32 StackCount{ 1 }; //최대 아이템 스택 개수
};



// ════════════════════════════════════════════════════════════════════════════════
// 📌 소비 아이템 프래그먼트들 (Consume Fragments)
// ════════════════════════════════════════════════════════════════════════════════

// 📌 FInv_ConsumeModifier - 소비 효과 기본 클래스
USTRUCT(BlueprintType)
struct FInv_ConsumeModifier : public FInv_LabeledNumberFragment
{
	GENERATED_BODY()

	virtual void OnConsume(APlayerController* PC){}
};

// 📌 FInv_ConsumableFragment - 소비 가능 아이템 프래그먼트
// 포션 등 사용 시 효과가 발동하는 아이템용
USTRUCT(BlueprintType)
struct FInv_ConsumableFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	//소비 호출?
	virtual void OnConsume(APlayerController* PC);
	virtual void Assimilate(UInv_CompositeBase* Composite) const override; //
	virtual void Manifest() override;
private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ExcludeBaseStruct, DisplayName = "ConsumeModifiers (소비 효과 목록)", Tooltip = "아이템 사용 시 적용될 효과들"))
	TArray<TInstancedStruct<FInv_ConsumeModifier>> ConsumeModifiers; // 입력 받은 랜덤 지정 값 적용 부분
};

// 📌 FInv_HealthPotionFragment - 체력 포션 프래그먼트
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

// 📌 FInv_ManaPotionFragment - 마나 포션 프래그먼트
USTRUCT(BlueprintType)
struct FInv_ManaPotionFragment : public FInv_ConsumeModifier
{
	GENERATED_BODY()

	virtual void OnConsume(APlayerController* PC) override;
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 장비 프래그먼트들 (Equipment Fragments)
// ════════════════════════════════════════════════════════════════════════════════

// 📌 FInv_EquipModifier - 장비 효과 기본 클래스
// 장착/해제 시 스탯 변화 등의 효과 정의
USTRUCT(BlueprintType)
struct FInv_EquipModifier : public FInv_LabeledNumberFragment
{
	GENERATED_BODY()

	// 장착과 해제 가상함수들
	virtual void OnEquip(APlayerController* PC) {}
	virtual void OnUnequip(APlayerController* PC) {}
};

// 📌 FInv_StrengthModifier - 힘 스탯 수정자
USTRUCT(BlueprintType)
struct FInv_StrengthModifier : public FInv_EquipModifier
{
	GENERATED_BODY()

	virtual void OnEquip(APlayerController* PC) override;
	virtual void OnUnequip(APlayerController* PC) override;
};

// 📌 FInv_ArmorModifier - 방어력 수정자
USTRUCT(BlueprintType)
struct FInv_ArmorModifier : public FInv_EquipModifier
{
	GENERATED_BODY()

	virtual void OnEquip(APlayerController* PC) override;
	virtual void OnUnequip(APlayerController* PC) override;
};

// 📌 FInv_DamageModifier - 공격력 수정자
USTRUCT(BlueprintType)
struct FInv_DamageModifier : public FInv_EquipModifier
{
	GENERATED_BODY()

	virtual void OnEquip(APlayerController* PC) override;
	virtual void OnUnequip(APlayerController* PC) override;
};

// ════════════════════════════════════════════════════════════════════════════════
// 📌 FInv_EquipmentFragment - 장비 아이템 프래그먼트
// ════════════════════════════════════════════════════════════════════════════════
// 무기, 방어구 등 장착 가능한 아이템용 프래그먼트
// EquipActorClass: 장착 시 스폰될 액터 (무기 메시 등)
// SocketAttachPoint: 캐릭터 메시의 부착 소켓 이름
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FInv_EquipmentFragment : public FInv_InventoryItemFragment
{
	GENERATED_BODY()

	bool bEquipped{false}; //장착 여부
	void OnEquip(APlayerController* PC);
	void OnUnequip(APlayerController* PC);
	virtual void Assimilate(UInv_CompositeBase* Composite) const override;
	virtual void Manifest() override;

	AInv_EquipActor* SpawnAttachedActor(USkeletalMeshComponent* AttachMesh, int32 WeaponSlotIndex = -1) const; // 장착 장비 스폰 (WeaponSlotIndex로 소켓 결정)
	void DestroyAttachedActor() const; // 장착 장비 파괴 (해제)
	FGameplayTag GetEquipmentType() const {return EquipmentType;}
	void SetEquippedActor(AInv_EquipActor* EquipActor);

	// ============================================
	// ⭐ [WeaponBridge] EquippedActor Getter 추가
	// ⭐ 클라이언트에서 리플리케이트된 액터 접근용
	// ============================================
	AInv_EquipActor* GetEquippedActor() const { return EquippedActor.Get(); }

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ExcludeBaseStruct, DisplayName = "EquipModifiers (장비 효과 목록)", Tooltip = "장착 시 적용될 스탯 효과들 (공격력, 방어력 등)"))
	TArray<TInstancedStruct<FInv_EquipModifier>> EquipModifiers;

	//장착장비 변수들
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "EquipActorClass (장비 액터 클래스)", Tooltip = "장착 시 스폰될 액터 클래스 (무기 메시 등)"))
	TSubclassOf<AInv_EquipActor> EquipActorClass = nullptr; // 장착 장비 클래스

	TWeakObjectPtr<AInv_EquipActor> EquippedActor = nullptr; // 장착 장비 포인터 (플레이어 Pawn을 말하는 것인가)

	//장비 부착물 지정?
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "SocketAttachPoint (소켓 부착 위치)", Tooltip = "캐릭터 스켈레탈 메시의 소켓 이름 (예: hand_r, spine_01)"))
	FName SocketAttachPoint{NAME_None}; // Mesh의 소켓 부착 지점

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (DisplayName = "EquipmentType (장비 타입 태그)", Tooltip = "장비 종류를 구분하는 GameplayTag (예: Weapon.Sword, Armor.Helmet)"))
	FGameplayTag EquipmentType = FGameplayTag::EmptyTag; // 장비 타입 태그
};
