#include "World/UEGT2ScatterField.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UEGT2LogChannels.h"

AUEGT2ScatterField::AUEGT2ScatterField()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

UHierarchicalInstancedStaticMeshComponent* AUEGT2ScatterField::AddLayer(UStaticMesh* Mesh, FName LayerName,
	float CullStart, float CullEnd, bool bCastShadow, bool bCollision)
{
	if (!Mesh)
	{
		UE_LOG(LogUEGT2World, Warning, TEXT("ScatterField '%s': layer '%s' has no mesh."),
			*GetName(), *LayerName.ToString());
		return nullptr;
	}

	UHierarchicalInstancedStaticMeshComponent* Component =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(this, LayerName);
	if (!Component)
	{
		return nullptr;
	}

	Component->SetStaticMesh(Mesh);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetupAttachment(RootComponent);
	Component->SetCullDistances(FMath::RoundToInt(CullStart), FMath::RoundToInt(CullEnd));
	Component->SetCastShadow(bCastShadow);
	// Distant instanced foliage does not need to cast into shadow maps.
	Component->bCastDynamicShadow = bCastShadow;
	Component->SetCollisionEnabled(bCollision
		? ECollisionEnabled::QueryOnly
		: ECollisionEnabled::NoCollision);
	Component->SetCollisionProfileName(bCollision
		? UCollisionProfile::BlockAllDynamic_ProfileName
		: UCollisionProfile::NoCollision_ProfileName);
	Component->bDisableCollision = !bCollision;
	Component->bAffectDistanceFieldLighting = false;
	Component->bEnableDensityScaling = true;

	Component->RegisterComponent();
	// Instance components serialise with the actor, so the map keeps the layer.
	AddInstanceComponent(Component);

	ScatterLayers.Add(Component);
	return Component;
}

int32 AUEGT2ScatterField::GetTotalInstanceCount() const
{
	int32 Total = 0;
	for (const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Layer : ScatterLayers)
	{
		if (Layer)
		{
			Total += Layer->GetInstanceCount();
		}
	}
	return Total;
}
