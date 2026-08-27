// Fairhaven (UEGT2) - editor-only authoring helpers exposed to Python.
//
// Small, focused utilities for things the stock Python API cannot reach.
// Anything that can already be done from unreal.* should be done there instead.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UEGT2AuthoringLibrary.generated.h"

class UMaterial;
class UStaticMesh;

UCLASS()
class UEGT2EDITOR_API UUEGT2AuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Flag a material as usable in the contexts we need. Headless builds never
	 * trigger the editor's automatic usage flagging, so we do it explicitly.
	 * Landscape needs no flag in UE 5.8, but Nanite rendering does.
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Authoring")
	static bool EnsureMaterialUsage(UMaterial* Material, bool bInstancedStaticMeshes, bool bNanite,
		bool bStaticMesh, bool bSkeletalMesh);

	/** Force a static mesh to keep its vertex colours and rebuild. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Authoring")
	static bool ConfigureGeneratedMesh(UStaticMesh* Mesh, bool bAllowCPUAccess, float DistanceFieldResolutionScale);

	/**
	 * Give a generated mesh a single box collision primitive matching its
	 * bounds. Physics simulation needs simple collision: a complex-as-simple
	 * mesh cannot be simulated.
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Authoring")
	static bool AddSimpleBoxCollision(UStaticMesh* Mesh);

	/** Number of triangles in LOD 0; -1 if unavailable. Used by the content report. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Authoring")
	static int32 GetMeshTriangleCount(UStaticMesh* Mesh);

	/** Write a UTF-8 text file. Used to emit the build report next to the logs. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Authoring")
	static bool WriteTextFile(const FString& AbsolutePath, const FString& Contents);
};
