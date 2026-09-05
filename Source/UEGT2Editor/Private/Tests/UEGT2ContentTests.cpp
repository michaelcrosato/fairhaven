// Fairhaven (UEGT2) - automation tests over the generated content.
//
// These guard the things that have actually broken during development:
// materials that fail to compile, meshes that lose their vertex colours, a map
// that loses its landscape or its scatter. Run with Scripts/Test.ps1.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Contracts/UEGT2SurveyContract.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2InteractableActor.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Landscape.h"
#include "Materials/Material.h"
#include "NPC/UEGT2NPCActor.h"
#include "NPC/UEGT2RouteNetwork.h"
#include "Player/UEGT2Character.h"
#include "Editor.h"
#include "StaticMeshResources.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UObjectGlobals.h"
#include "World/UEGT2ScatterField.h"

namespace UEGT2Tests
{
	const TCHAR* MapPath = TEXT("/Game/Maps/L_Fairhaven");

	const TCHAR* RequiredMaterials[] = {
		TEXT("/Game/Fairhaven/Materials/M_Prop"),
		TEXT("/Game/Fairhaven/Materials/M_PropEmissive"),
		TEXT("/Game/Fairhaven/Materials/M_Foliage"),
		TEXT("/Game/Fairhaven/Materials/M_Glass"),
		TEXT("/Game/Fairhaven/Materials/M_Landscape"),
		TEXT("/Game/Fairhaven/Materials/M_WaterStylised"),
	};

	const TCHAR* SampleMeshes[] = {
		TEXT("/Game/Fairhaven/Meshes/Nature/SM_Tree_Oak_A"),
		TEXT("/Game/Fairhaven/Meshes/Nature/SM_Grass_A"),
		TEXT("/Game/Fairhaven/Meshes/Nature/SM_Rock_M"),
		TEXT("/Game/Fairhaven/Meshes/Town/SM_House_A"),
		TEXT("/Game/Fairhaven/Meshes/Town/SM_Lighthouse_A"),
		TEXT("/Game/Fairhaven/Meshes/Props/SM_Crate_A"),
		TEXT("/Game/Fairhaven/Meshes/Town/SM_Villager_A"),
		TEXT("/Game/Fairhaven/Meshes/Town/SM_Child_A"),
		TEXT("/Game/Fairhaven/Meshes/City/SM_Citizen_A"),
		TEXT("/Game/Fairhaven/Meshes/Fauna/SM_Dog_A"),
		TEXT("/Game/Fairhaven/Meshes/Fauna/SM_Cow_A"),
		TEXT("/Game/Fairhaven/Meshes/Fauna/SM_Seagull_A"),
	};
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2MaterialsCompileTest,
	"UEGT2.Content.MaterialsCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2MaterialsCompileTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : UEGT2Tests::RequiredMaterials)
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("material %s loads"), Path), Material))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("material %s has expressions"), Path),
			Material->GetExpressions().Num() > 0);

		// The real failure mode this guards: an unconnected BaseColor renders
		// pure black, and MaterialEditingLibrary reports a bad pin name by
		// returning false rather than raising, so it is easy to miss.
		if (const UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData())
		{
			TestTrue(FString::Printf(TEXT("material %s has BaseColor connected"), Path),
				EditorData->BaseColor.Expression != nullptr);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2MeshesTest,
	"UEGT2.Content.GeneratedMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2MeshesTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : UEGT2Tests::SampleMeshes)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("mesh %s loads"), Path), Mesh))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("mesh %s has triangles"), Path),
			Mesh->GetNumTriangles(0) > 0);
		TestTrue(FString::Printf(TEXT("mesh %s has a material"), Path),
			Mesh->GetStaticMaterials().Num() > 0
			&& Mesh->GetStaticMaterials()[0].MaterialInterface != nullptr);

		// Vertex colours carry the entire palette; without them everything is black.
		const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
		TestTrue(FString::Printf(TEXT("mesh %s has vertex colours"), Path),
			LOD.VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0);
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2WorldTest,
	"UEGT2.Content.WorldComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2WorldTest::RunTest(const FString& Parameters)
{
	// LoadMap returns void; success is judged by what ends up in the world.
	FAutomationEditorCommonUtils::LoadMap(UEGT2Tests::MapPath);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("editor world exists"), World))
	{
		return false;
	}

	int32 Landscapes = 0;
	for (TActorIterator<ALandscape> It(World); It; ++It) { ++Landscapes; }
	TestEqual(TEXT("exactly one landscape"), Landscapes, 1);

	int32 PlayerStarts = 0;
	for (TActorIterator<APlayerStart> It(World); It; ++It) { ++PlayerStarts; }
	TestTrue(TEXT("at least one player start"), PlayerStarts >= 1);

	int32 ScatterFields = 0;
	int32 NatureFields = 0;
	int32 FenceInstances = 0;
	int32 Instances = 0;
	for (TActorIterator<AUEGT2ScatterField> It(World); It; ++It)
	{
		++ScatterFields;
		Instances += It->GetTotalInstanceCount();
		if (It->bUseFoliageDrawDistance) { ++NatureFields; }
		if (It->GetActorLabel() == TEXT("Town Fences"))
		{
			FenceInstances += It->GetTotalInstanceCount();
			TestFalse(TEXT("fences keep independent cull distances"), It->bUseFoliageDrawDistance);
		}
	}
	TestTrue(TEXT("scatter fields exist"), ScatterFields >= 5);
	TestTrue(TEXT("nature fields persist their foliage-distance opt-in"), NatureFields >= 5);
	TestTrue(TEXT("nature rebuilding preserves the town's fence instances"), FenceInstances > 0);
	TestTrue(FString::Printf(TEXT("scatter has plenty of instances (got %d)"), Instances),
		Instances > 50000);

	int32 Interactables = 0;
	for (TActorIterator<AUEGT2InteractableActor> It(World); It; ++It) { ++Interactables; }
	TestTrue(FString::Printf(TEXT("interactables placed (got %d)"), Interactables),
		Interactables >= 20);

	TSet<FName> LandmarkIds;
	int32 Landmarks = 0;
	for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
	{
		++Landmarks;
		const FName Id = It->GetPersistentId();
		TestFalse(FString::Printf(TEXT("landmark %s has a persistent ID"), *It->GetName()), Id.IsNone());
		TestFalse(FString::Printf(TEXT("landmark ID %s is unique"), *Id.ToString()), LandmarkIds.Contains(Id));
		TestFalse(FString::Printf(TEXT("landmark %s has a display name"), *Id.ToString()), It->GetLandmarkName().IsEmpty());
		LandmarkIds.Add(Id);
	}
	TestEqual(TEXT("all authored survey landmarks are placed"), Landmarks, 11);

	// The amenities, by kind. This is the check that catches the quiet failure
	// the whole survey is prone to: a label prefix changes in the town stage,
	// an anchor set comes back empty, and the world builds cleanly with nowhere
	// in it for anybody - player included - to eat.
	int32 ByKind[(int32)EUEGT2AmenityKind::Count] = {};
	for (TActorIterator<AUEGT2Amenity> It(World); It; ++It)
	{
		const int32 Index = (int32)It->GetKind();
		if (Index >= 0 && Index < (int32)EUEGT2AmenityKind::Count)
		{
			++ByKind[Index];
		}
	}

	// Every need the player has must have somewhere to answer it, and there
	// must be somewhere to earn what answering them costs.
	for (EUEGT2AmenityKind Required : { EUEGT2AmenityKind::Food, EUEGT2AmenityKind::Washroom,
		EUEGT2AmenityKind::Seat, EUEGT2AmenityKind::Work, EUEGT2AmenityKind::Bed })
	{
		TestTrue(FString::Printf(TEXT("%s amenities placed (got %d)"),
			UEGT2AmenityKindName(Required), ByKind[(int32)Required]),
			ByKind[(int32)Required] > 0);
	}

	FString Amenities;
	for (int32 Index = 0; Index < (int32)EUEGT2AmenityKind::Count; ++Index)
	{
		Amenities += FString::Printf(TEXT("%s %d  "),
			UEGT2AmenityKindName((EUEGT2AmenityKind)Index), ByKind[Index]);
	}

	AddInfo(FString::Printf(
		TEXT("world: %d landscape, %d starts, %d scatter fields, %d instances, %d interactables"),
		Landscapes, PlayerStarts, ScatterFields, Instances, Interactables));
	AddInfo(FString::Printf(TEXT("amenities: %s"), *Amenities));
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SurveyContractContentTest,
	"UEGT2.Content.SurveyContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SurveyContractContentTest::RunTest(const FString& Parameters)
{
	FAutomationEditorCommonUtils::LoadMap(UEGT2Tests::MapPath);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("editor world exists"), World)) { return false; }

	AUEGT2SurveyContract* Board = nullptr;
	int32 Boards = 0;
	for (TActorIterator<AUEGT2SurveyContract> It(World); It; ++It) { Board = *It; ++Boards; }
	TestEqual(TEXT("exactly one generated town survey contract"), Boards, 1);
	if (!TestNotNull(TEXT("contract sign exists"), Board)) { return false; }
	TestEqual(TEXT("contract belongs to the gameplay stage"), Board->GetActorLabel(), FString(TEXT("Play Town Survey Contract")));
	const UStaticMeshComponent* BoardMesh = Board->GetMeshComponent();
	if (TestNotNull(TEXT("contract has a mesh component"), BoardMesh))
	{
		const UStaticMesh* Mesh = BoardMesh->GetStaticMesh();
		if (TestNotNull(TEXT("contract has its generated sign mesh"), Mesh))
		{
			TestEqual(TEXT("contract reuses the signpost catalog asset"), Mesh->GetName(), FString(TEXT("SM_SignPost_A")));
		}
		TestEqual(TEXT("contract sign remains query only"), BoardMesh->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		TestEqual(TEXT("the real probe can find the contract"), BoardMesh->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
		TestEqual(TEXT("contract sign does not block players"), BoardMesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
	}

	AUEGT2Landmark* Square = nullptr;
	TMap<FName, int32> Counts;
	for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
	{
		++Counts.FindOrAdd(It->GetPersistentId());
		if (It->GetPersistentId() == TEXT("fairhaven_square")) { Square = *It; }
	}
	for (FName Id : UUEGT2SurveyContractSubsystem::RequiredLandmarkIds())
	{
		TestEqual(FString::Printf(TEXT("contract destination %s resolves uniquely"), *Id.ToString()), Counts.FindRef(Id), 1);
	}
	if (TestNotNull(TEXT("Fairhaven Square marker exists"), Square))
	{
		TestTrue(TEXT("contract stands near Fairhaven Square"), FVector::Dist2D(Board->GetActorLocation(), Square->GetActorLocation()) < 6000.0);
		TestTrue(TEXT("contract stays clear of the square's discovery marker"), FVector::Dist2D(Board->GetActorLocation(), Square->GetActorLocation()) > 600.0);
	}

	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(ContractGround), true, Board);
	FCollisionObjectQueryParams StaticObjects(ECC_WorldStatic);
	const FVector BoardLocation = Board->GetActorLocation();
	FHitResult Ground;
	if (TestTrue(TEXT("contract sign is terrain grounded"), World->LineTraceSingleByObjectType(Ground,
		BoardLocation + FVector(0.0, 0.0, 120.0), BoardLocation - FVector(0.0, 0.0, 120.0), StaticObjects, GroundParams)))
	{
		TestTrue(TEXT("sign base follows sampled terrain"), FMath::Abs(Ground.ImpactPoint.Z - (BoardLocation.Z + 10.0)) < 20.0);
	}

	const AUEGT2Character* DefaultPlayer = GetDefault<AUEGT2Character>();
	const UCapsuleComponent* Capsule = DefaultPlayer->GetCapsuleComponent();
	const float Radius = Capsule->GetUnscaledCapsuleRadius();
	const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	FCollisionQueryParams ProbeParams(SCENE_QUERY_STAT(ContractProbe), true);
	// NPCs deliberately answer Visibility but never block a player. Their bake
	// positions must not make a static sign's content check population dependent.
	for (TActorIterator<AUEGT2NPCActor> It(World); It; ++It) { ProbeParams.AddIgnoredActor(*It); }
	for (float Side : { -1.0f, 1.0f })
	{
		const FVector ApproachXY = BoardLocation + Board->GetActorForwardVector() * (Side * 200.0f);
		FHitResult Footing;
		if (!TestTrue(TEXT("each broad face has ground to stand on"), World->LineTraceSingleByObjectType(Footing,
			ApproachXY + FVector(0.0, 0.0, 120.0), ApproachXY - FVector(0.0, 0.0, 120.0), StaticObjects, GroundParams))) { continue; }
		const FVector Stand = Footing.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 3.0f);
		TestTrue(TEXT("standing approach is within the authored use range"), FVector::Dist(Stand, BoardLocation) < Board->GetUseRange());
		TestFalse(TEXT("a real standing capsule fits at each sign face"), World->OverlapBlockingTestByChannel(
			Stand, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), GroundParams));
		FHitResult Probe;
		const FVector Eye = Stand + FVector(0.0, 0.0, DefaultPlayer->GetCamera()->GetRelativeLocation().Z);
		const bool bHit = World->LineTraceSingleByChannel(Probe, Eye, Board->GetInteractionPoint(), ECC_Visibility, ProbeParams);
		TestTrue(TEXT("each clear approach probes the contract instead of a neighbouring amenity"), bHit && Probe.GetActor() == Board);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The population, and the roads it walks on.
//
// UEGT2NPCTests covers the decisions; this covers the bake. The failure this
// exists to catch is the quiet one: a stage that runs, reports success, and
// produces an empty town because a label prefix changed underneath it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PopulationTest,
	"UEGT2.Content.Population",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PopulationTest::RunTest(const FString& Parameters)
{
	FAutomationEditorCommonUtils::LoadMap(UEGT2Tests::MapPath);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("editor world exists"), World))
	{
		return false;
	}

	// --- the route network --------------------------------------------------
	AUEGT2RouteNetwork* Routes = nullptr;
	int32 Networks = 0;
	for (TActorIterator<AUEGT2RouteNetwork> It(World); It; ++It)
	{
		Routes = *It;
		++Networks;
	}
	TestEqual(TEXT("exactly one route network"), Networks, 1);

	if (Routes)
	{
		TestTrue(FString::Printf(TEXT("route network has nodes (got %d)"), Routes->GetNodeCount()),
			Routes->GetNodeCount() > 500);
		// Links matter more than nodes: a network of unconnected points would
		// pass a node count and path nowhere.
		TestTrue(FString::Printf(TEXT("route network is connected (got %d links)"),
			Routes->GetLinkCount()), Routes->GetLinkCount() >= Routes->GetNodeCount());

		// And it has to actually path. Two nodes a good way apart, through
		// whatever the graph has in between.
		const FVector Start = Routes->GetNodeLocation(0);
		const FVector Goal = Routes->GetNodeLocation(Routes->GetNodeCount() / 2);
		TArray<FVector> Path;
		const bool bPathed = Routes->FindPath(Start, Goal, Path);
		AddInfo(FString::Printf(TEXT("route: %d nodes, %d links, sample path %s (%d hops)"),
			Routes->GetNodeCount(), Routes->GetLinkCount(),
			bPathed ? TEXT("found") : TEXT("not found"), Path.Num()));

		TestTrue(TEXT("a node can be found near a node"),
			Routes->FindNearestNode(Start, 400.0f) != INDEX_NONE);

		// --- the search's own parent links ---------------------------------
		// A* builds a tree of parent pointers and then walks it backwards from
		// the goal. If those links ever contain a cycle the walk never reaches
		// the start, and what the player sees is the game freezing for twenty
		// seconds and then dying inside the allocator - it was appending to an
		// array the whole time, about four gigabytes of it.
		//
		// The cause was a pointer into the cost map held across an Add to that
		// same map. Hundreds of searches across the real baked graph is what it
		// takes to hit it: it needs the map to rehash midway through one node's
		// neighbours, which is why it only ever showed up while flying, when
		// hundreds of inhabitants change tier at once and all repath together.
		int32 Repeats = 0;
		int32 Pathed = 0;
		const int32 Nodes = Routes->GetNodeCount();
		for (int32 Attempt = 0; Attempt < 400 && Nodes > 1; ++Attempt)
		{
			// Spread across the whole graph, deterministically.
			const int32 A = (Attempt * 7919) % Nodes;
			const int32 B = (Attempt * 104729 + Nodes / 3) % Nodes;
			TArray<FVector> Route;
			if (!Routes->FindPath(Routes->GetNodeLocation(A), Routes->GetNodeLocation(B), Route))
			{
				continue;
			}
			++Pathed;
			// A path that visits the same point twice is a cycle that happened
			// to terminate, which is the same defect wearing a hat.
			TSet<FVector> Seen;
			for (const FVector& Point : Route)
			{
				bool bAlready = false;
				Seen.Add(Point, &bAlready);
				if (bAlready) { ++Repeats; break; }
			}
		}

		AddInfo(FString::Printf(TEXT("route: %d searches, %d found a path"), 400, Pathed));
		TestEqual(TEXT("no search found a cycle in its own parent links"),
			Routes->GetCycleCount(), 0);
		TestEqual(TEXT("no path visits the same node twice"), Repeats, 0);
		// A floor, not a target. Plenty of these pairs are genuinely unreachable
		// from each other - the town and the city are 130 km apart and the
		// search is capped - so the number that succeed depends on the seed.
		// What matters is that the graph paths at all, and that none of the
		// ones that do come back with a cycle in them.
		TestTrue(FString::Printf(TEXT("the graph paths at all (%d of 400)"), Pathed),
			Pathed > 100);
	}

	// --- the inhabitants ----------------------------------------------------
	int32 People = 0, Animals = 0, WithoutMesh = 0, WithoutAnchors = 0, Unnamed = 0;
	TSet<int32> Seeds;
	for (TActorIterator<AUEGT2NPCActor> It(World); It; ++It)
	{
		AUEGT2NPCActor* NPC = *It;
		if (NPC->IsAnimal()) { ++Animals; } else { ++People; }

		const UStaticMeshComponent* Body = NPC->GetBody();
		if (!Body || !Body->GetStaticMesh()) { ++WithoutMesh; }
		// Home at the very least; without anchors an NPC has nowhere to go and
		// spends the whole game standing on its spawn point.
		if (NPC->GetAnchorCount() < 2) { ++WithoutAnchors; }
		if (NPC->GetDisplayName().IsEmpty()) { ++Unnamed; }
		Seeds.Add(NPC->GetSeed());
	}

	// Five times the 26 static villagers 0.1 shipped is the floor, not the aim.
	TestTrue(FString::Printf(TEXT("the town is populated (got %d people)"), People),
		People >= 130);
	TestTrue(FString::Printf(TEXT("there are animals too (got %d)"), Animals),
		Animals >= 60);
	TestEqual(TEXT("every inhabitant has a mesh"), WithoutMesh, 0);
	TestEqual(TEXT("every inhabitant has somewhere to go"), WithoutAnchors, 0);
	TestEqual(TEXT("every inhabitant has a name"), Unnamed, 0);
	// Seeds drive personality and speech: a collision means two identical
	// people, and a wholesale collision means one person copied everywhere.
	TestEqual(TEXT("every inhabitant has its own seed"), Seeds.Num(), People + Animals);

	AddInfo(FString::Printf(TEXT("population: %d people, %d animals, %d distinct seeds"),
		People, Animals, Seeds.Num()));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
