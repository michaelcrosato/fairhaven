// Fairhaven (UEGT2) - editor-only landscape authoring.
//
// Wraps ALandscape::Import so the Python content build can turn the raw
// heightmap and weightmaps produced by Tools/Terrain/generate_terrain.py into a
// real Landscape actor. Nothing here ships in the game target.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UEGT2LandscapeTools.generated.h"

class ALandscape;
class ULandscapeLayerInfoObject;
class UMaterialInterface;

/** Everything needed to import one landscape in a single call from Python. */
USTRUCT(BlueprintType)
struct FUEGT2LandscapeImportParams
{
	GENERATED_BODY()

	/** Absolute path to a uint16 little-endian heightmap, row-major [Y][X]. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	FString HeightmapPath;

	/** Paint layer names, parallel with WeightmapPaths and LayerInfos. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	TArray<FString> LayerNames;

	/** Absolute paths to uint8 weightmaps, one per layer. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	TArray<FString> WeightmapPaths;

	/** Layer info assets, one per layer. Create them with CreateOrLoadLayerInfo. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	TArray<TObjectPtr<ULandscapeLayerInfoObject>> LayerInfos;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	int32 SizeX = 2017;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	int32 SizeY = 2017;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	int32 QuadsPerSection = 63;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	int32 SectionsPerComponent = 2;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	FVector Scale = FVector(100.0, 100.0, 200.0);

	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	TObjectPtr<UMaterialInterface> LandscapeMaterial = nullptr;

	/** Nanite landscape is a large win at this resolution on modern GPUs. */
	UPROPERTY(BlueprintReadWrite, Category = "UEGT2")
	bool bEnableNanite = true;
};

UCLASS()
class UEGT2EDITOR_API UUEGT2LandscapeTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a Landscape actor from raw heightmap/weightmap files.
	 * Returns nullptr and logs the reason on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Landscape", meta = (WorldContext = "WorldContextObject"))
	static ALandscape* CreateLandscapeFromRaw(UObject* WorldContextObject,
		const FUEGT2LandscapeImportParams& Params);

	/** Find or create a ULandscapeLayerInfoObject asset at PackageFolder/LI_<LayerName>. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Landscape")
	static ULandscapeLayerInfoObject* CreateOrLoadLayerInfo(const FString& PackageFolder,
		const FString& LayerName, FLinearColor DebugColor);

	/** Sample landscape height (world Z) by tracing straight down; -1e9 if nothing hit. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Landscape", meta = (WorldContext = "WorldContextObject"))
	static float ProbeGroundHeight(UObject* WorldContextObject, float WorldX, float WorldY,
		float TraceFromZ = 200000.0f, float TraceToZ = -100000.0f);
};
