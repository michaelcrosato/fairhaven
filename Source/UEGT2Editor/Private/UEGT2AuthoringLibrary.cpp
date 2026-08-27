#include "UEGT2AuthoringLibrary.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "HAL/PlatformFileManager.h"
#include "Materials/Material.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UEGT2LogChannels.h"

bool UUEGT2AuthoringLibrary::EnsureMaterialUsage(UMaterial* Material, bool bInstancedStaticMeshes,
	bool bNanite, bool bStaticMesh, bool bSkeletalMesh)
{
	if (!Material)
	{
		return false;
	}

	auto Apply = [Material](bool bWanted, EMaterialUsage Usage)
	{
		if (bWanted)
		{
			Material->SetMaterialUsage(Usage);
		}
	};

	Apply(bInstancedStaticMeshes, MATUSAGE_InstancedStaticMeshes);
	Apply(bNanite, MATUSAGE_Nanite);
	Apply(bStaticMesh, MATUSAGE_StaticMesh);
	Apply(bSkeletalMesh, MATUSAGE_SkeletalMesh);

	Material->PostEditChange();
	Material->MarkPackageDirty();
	return true;
}

bool UUEGT2AuthoringLibrary::ConfigureGeneratedMesh(UStaticMesh* Mesh, bool bAllowCPUAccess,
	float DistanceFieldResolutionScale)
{
#if WITH_EDITOR
	if (!Mesh)
	{
		return false;
	}
	Mesh->bAllowCPUAccess = bAllowCPUAccess;

	if (Mesh->GetNumSourceModels() > 0)
	{
		FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.bRemoveDegenerates = true;
		Source.BuildSettings.bUseFullPrecisionUVs = false;
		// UV1 carries the foliage wind weight. Lightmap UV generation would
		// overwrite it, and static lighting is disabled project-wide anyway.
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.DistanceFieldResolutionScale = DistanceFieldResolutionScale;
	}

	Mesh->Build(/*bInSilent=*/true);
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UUEGT2AuthoringLibrary::AddSimpleBoxCollision(UStaticMesh* Mesh)
{
#if WITH_EDITOR
	if (!Mesh)
	{
		return false;
	}
	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		Mesh->CreateBodySetup();
		BodySetup = Mesh->GetBodySetup();
	}
	if (!BodySetup)
	{
		return false;
	}

	const FBox Bounds = Mesh->GetBoundingBox();
	if (!Bounds.IsValid)
	{
		return false;
	}

	BodySetup->Modify();
	BodySetup->RemoveSimpleCollision();

	FKBoxElem Box;
	Box.Center = Bounds.GetCenter();
	const FVector Size = Bounds.GetSize();
	Box.X = static_cast<float>(FMath::Max(Size.X, 1.0));
	Box.Y = static_cast<float>(FMath::Max(Size.Y, 1.0));
	Box.Z = static_cast<float>(FMath::Max(Size.Z, 1.0));
	BodySetup->AggGeom.BoxElems.Add(Box);

	BodySetup->CollisionTraceFlag = CTF_UseDefault;
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();

	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

int32 UUEGT2AuthoringLibrary::GetMeshTriangleCount(UStaticMesh* Mesh)
{
	if (!Mesh || Mesh->GetNumLODs() == 0)
	{
		return -1;
	}
	return Mesh->GetNumTriangles(0);
}

bool UUEGT2AuthoringLibrary::WriteTextFile(const FString& AbsolutePath, const FString& Contents)
{
	const FString Directory = FPaths::GetPath(AbsolutePath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!Directory.IsEmpty() && !PlatformFile.DirectoryExists(*Directory))
	{
		PlatformFile.CreateDirectoryTree(*Directory);
	}
	if (!FFileHelper::SaveStringToFile(Contents, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogUEGT2World, Error, TEXT("Cannot write '%s'."), *AbsolutePath);
		return false;
	}
	return true;
}
