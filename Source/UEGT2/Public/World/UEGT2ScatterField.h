// Fairhaven (UEGT2) - GPU-instanced container for scattered world content.
//
// Vegetation, rocks, fences and other repeated props live in hierarchical
// instanced mesh components on a handful of these actors instead of thousands
// of individual actors. The content build creates the layers and fills them.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UEGT2ScatterField.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2ScatterField : public AActor
{
	GENERATED_BODY()

public:
	AUEGT2ScatterField();

	/**
	 * Create one instanced layer. Call from the content build, then push
	 * transforms with the standard AddInstances on the returned component.
	 *
	 * @param CullStart  distance where instances begin to fade out (cm)
	 * @param CullEnd    distance where instances disappear (cm); 0 = never cull
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Scatter")
	UHierarchicalInstancedStaticMeshComponent* AddLayer(UStaticMesh* Mesh, FName LayerName,
		float CullStart = 12000.0f, float CullEnd = 16000.0f,
		bool bCastShadow = true, bool bCollision = false);

	/** Total instances across every layer, for the content report and diagnostics. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Scatter")
	int32 GetTotalInstanceCount() const;

	UFUNCTION(BlueprintPure, Category = "UEGT2|Scatter")
	int32 GetLayerCount() const { return ScatterLayers.Num(); }

private:
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ScatterLayers;
};
