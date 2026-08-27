#include "UEGT2LandscapeTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeEditTypes.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UEGT2LogChannels.h"
#include "UObject/Package.h"
#include "WorldPartition/WorldPartition.h"

namespace
{
	/** Load a binary file and verify it holds exactly ExpectedCount elements of ElementSize. */
	bool LoadRaw(const FString& Path, int64 ExpectedCount, int32 ElementSize, TArray<uint8>& OutBytes)
	{
		if (!FFileHelper::LoadFileToArray(OutBytes, *Path))
		{
			UE_LOG(LogUEGT2World, Error, TEXT("Landscape import: cannot read '%s'."), *Path);
			return false;
		}
		const int64 Expected = ExpectedCount * ElementSize;
		if (OutBytes.Num() != Expected)
		{
			UE_LOG(LogUEGT2World, Error,
				TEXT("Landscape import: '%s' is %d bytes, expected %lld."),
				*Path, OutBytes.Num(), Expected);
			return false;
		}
		return true;
	}
}

ALandscape* UUEGT2LandscapeTools::CreateLandscapeFromRaw(UObject* WorldContextObject,
	const FUEGT2LandscapeImportParams& Params)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		UE_LOG(LogUEGT2World, Error, TEXT("Landscape import: no world context."));
		return nullptr;
	}

	const int32 SizeX = Params.SizeX;
	const int32 SizeY = Params.SizeY;
	const int32 QuadsPerComponent = Params.QuadsPerSection * Params.SectionsPerComponent;
	if (SizeX < 2 || SizeY < 2 || ((SizeX - 1) % QuadsPerComponent) != 0 || ((SizeY - 1) % QuadsPerComponent) != 0)
	{
		UE_LOG(LogUEGT2World, Error,
			TEXT("Landscape import: %dx%d is not a whole number of %d-quad components."),
			SizeX, SizeY, QuadsPerComponent);
		return nullptr;
	}

	const int64 SampleCount = static_cast<int64>(SizeX) * SizeY;

	// ---- Heightmap ---------------------------------------------------------
	TArray<uint8> HeightBytes;
	if (!LoadRaw(Params.HeightmapPath, SampleCount, sizeof(uint16), HeightBytes))
	{
		return nullptr;
	}
	TArray<uint16> HeightData;
	HeightData.SetNumUninitialized(static_cast<int32>(SampleCount));
	FMemory::Memcpy(HeightData.GetData(), HeightBytes.GetData(), HeightBytes.Num());
	HeightBytes.Empty();

	// ---- Weightmaps --------------------------------------------------------
	const int32 LayerCount = Params.LayerNames.Num();
	if (Params.WeightmapPaths.Num() != LayerCount || Params.LayerInfos.Num() != LayerCount)
	{
		UE_LOG(LogUEGT2World, Error,
			TEXT("Landscape import: layer arrays disagree (names=%d weights=%d infos=%d)."),
			LayerCount, Params.WeightmapPaths.Num(), Params.LayerInfos.Num());
		return nullptr;
	}

	TArray<FLandscapeImportLayerInfo> ImportLayers;
	ImportLayers.Reserve(LayerCount);
	for (int32 Index = 0; Index < LayerCount; ++Index)
	{
		TArray<uint8> WeightBytes;
		if (!LoadRaw(Params.WeightmapPaths[Index], SampleCount, sizeof(uint8), WeightBytes))
		{
			return nullptr;
		}
		FLandscapeImportLayerInfo Layer(FName(*Params.LayerNames[Index]));
		Layer.LayerInfo = Params.LayerInfos[Index];
		Layer.LayerData = MoveTemp(WeightBytes);
		ImportLayers.Add(MoveTemp(Layer));
	}

	// ---- Spawn and import --------------------------------------------------
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transactional;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(Params.Location, FRotator::ZeroRotator, SpawnParams);
	if (!Landscape)
	{
		UE_LOG(LogUEGT2World, Error, TEXT("Landscape import: SpawnActor<ALandscape> failed."));
		return nullptr;
	}

	// Edit layers stay off: we author the heightmap offline and import it whole,
	// which keeps the landscape data small and the import path simple.
	Landscape->LandscapeMaterial = Params.LandscapeMaterial;
	Landscape->SetActorRelativeScale3D(Params.Scale);
	// Mirrors the engine's New Landscape tool: keeps Lightmass out of trouble.
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(
		FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), static_cast<uint32>(2));

	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(FGuid(), MoveTemp(HeightData));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerDataPerLayer;
	LayerDataPerLayer.Add(FGuid(), MoveTemp(ImportLayers));

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1,
		Params.SectionsPerComponent, Params.QuadsPerSection,
		HeightDataPerLayer, TEXT(""), LayerDataPerLayer,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogUEGT2World, Error, TEXT("Landscape import: no ULandscapeInfo after Import."));
		return nullptr;
	}
	Info->UpdateLayerInfoMap(Landscape);

	for (int32 Index = 0; Index < LayerCount; ++Index)
	{
		ULandscapeLayerInfoObject* LayerInfo = Params.LayerInfos[Index];
		if (!LayerInfo)
		{
			continue;
		}
		Landscape->AddTargetLayer(LayerInfo->GetLayerName(), FLandscapeTargetLayerSettings(LayerInfo));
		const int32 InfoIndex = Info->GetLayerInfoIndex(FName(*Params.LayerNames[Index]));
		if (InfoIndex != INDEX_NONE)
		{
			Info->Layers[InfoIndex].LayerInfoObj = LayerInfo;
		}
	}

	// bEnableNanite is protected but reflected; set it through the property system.
	if (FBoolProperty* NaniteProperty = FindFProperty<FBoolProperty>(
			ALandscapeProxy::StaticClass(), TEXT("bEnableNanite")))
	{
		NaniteProperty->SetPropertyValue_InContainer(Landscape, Params.bEnableNanite);
	}
	else
	{
		UE_LOG(LogUEGT2World, Warning, TEXT("Landscape import: bEnableNanite property not found."));
	}
	Landscape->PostEditChange();

	UE_LOG(LogUEGT2World, Log,
		TEXT("Landscape imported: %dx%d verts, %d layers, scale=(%.0f,%.0f,%.0f), nanite=%s."),
		SizeX, SizeY, LayerCount, Params.Scale.X, Params.Scale.Y, Params.Scale.Z,
		Params.bEnableNanite ? TEXT("on") : TEXT("off"));
	return Landscape;
}

ULandscapeLayerInfoObject* UUEGT2LandscapeTools::CreateOrLoadLayerInfo(const FString& PackageFolder,
	const FString& LayerName, FLinearColor DebugColor)
{
	const FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
	const FString PackageName = FString::Printf(TEXT("%s/%s"), *PackageFolder, *AssetName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);

	if (ULandscapeLayerInfoObject* Existing = LoadObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath))
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogUEGT2World, Error, TEXT("Cannot create package '%s'."), *PackageName);
		return nullptr;
	}
	Package->FullyLoad();

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
		Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!LayerInfo)
	{
		return nullptr;
	}

	LayerInfo->SetLayerName(FName(*LayerName), false);
	LayerInfo->SetBlendMethod(ELandscapeTargetLayerBlendMethod::FinalWeightBlending, false);
	LayerInfo->SetLayerUsageDebugColor(DebugColor, false, EPropertyChangeType::ValueSet);

	FAssetRegistryModule::AssetCreated(LayerInfo);
	Package->MarkPackageDirty();

	UE_LOG(LogUEGT2World, Log, TEXT("Created landscape layer info '%s'."), *ObjectPath);
	return LayerInfo;
}

float UUEGT2LandscapeTools::ProbeGroundHeight(UObject* WorldContextObject, float WorldX, float WorldY,
	float TraceFromZ, float TraceToZ)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return -1.0e9f;
	}
	FHitResult Hit;
	const FVector Start(WorldX, WorldY, TraceFromZ);
	const FVector End(WorldX, WorldY, TraceToZ);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UEGT2ProbeGround), true);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams))
	{
		return static_cast<float>(Hit.ImpactPoint.Z);
	}
	return -1.0e9f;
}
