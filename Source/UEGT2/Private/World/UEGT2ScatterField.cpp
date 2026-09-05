#include "World/UEGT2ScatterField.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"

AUEGT2ScatterField::AUEGT2ScatterField()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

void AUEGT2ScatterField::BeginPlay()
{
	Super::BeginPlay();
	for (UHierarchicalInstancedStaticMeshComponent* Layer : ScatterLayers)
	{
		if (Layer)
		{
			AuthoredCullDistances.Add(Layer,
				FIntPoint(Layer->InstanceStartCullDistance, Layer->InstanceEndCullDistance));
		}
	}
	UUEGT2GameUserSettings::OnSettingsApplied.AddUObject(this, &AUEGT2ScatterField::RefreshFromSettings);
	RefreshFromSettings();
}

void AUEGT2ScatterField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UUEGT2GameUserSettings::OnSettingsApplied.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

void AUEGT2ScatterField::RefreshFromSettings()
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const float Scale = bUseFoliageDrawDistance && Settings ? Settings->GetFoliageDrawDistanceScale() : 1.0f;
	for (const auto& Entry : AuthoredCullDistances)
	{
		if (UHierarchicalInstancedStaticMeshComponent* Layer = Entry.Key.Get())
		{
			// LODDistanceScale does not multiply HISM's explicit end distance.
			// Preserve zero (unlimited) and avoid rebuilding unchanged render state.
			const int32 Start = FMath::RoundToInt(Entry.Value.X * Scale);
			const int32 End = FMath::RoundToInt(Entry.Value.Y * Scale);
			if (Start != Layer->InstanceStartCullDistance || End != Layer->InstanceEndCullDistance)
			{
				Layer->SetCullDistances(Start, End);
			}
		}
	}
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
	if (HasActorBegunPlay())
	{
		AuthoredCullDistances.Add(Component,
			FIntPoint(Component->InstanceStartCullDistance, Component->InstanceEndCullDistance));
		RefreshFromSettings();
	}
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
