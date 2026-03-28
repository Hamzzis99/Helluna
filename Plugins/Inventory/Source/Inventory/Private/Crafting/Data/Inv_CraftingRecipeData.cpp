// Gihyeon's Inventory Project
// @author 김기현

#include "Crafting/Data/Inv_CraftingRecipeData.h"

TArray<FInv_CraftingRecipe> UInv_CraftingRecipeDA::GetRecipesByCategory(EInv_ItemCategory Category) const
{
	TArray<FInv_CraftingRecipe> FilteredRecipes;

	for (const FInv_CraftingRecipe& Recipe : Recipes)
	{
		if (Recipe.CraftCategory == Category)
		{
			FilteredRecipes.Add(Recipe);
		}
	}

	return FilteredRecipes;
}
