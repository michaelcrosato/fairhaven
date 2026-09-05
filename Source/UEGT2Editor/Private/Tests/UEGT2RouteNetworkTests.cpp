#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "NPC/UEGT2RouteNetwork.h"

namespace UEGT2RouteNetworkTests
{
	struct FTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
		AUEGT2RouteNetwork* Routes = World ? World->SpawnActor<AUEGT2RouteNetwork>() : nullptr;

		~FTestWorld() { if (World) { World->DestroyWorld(false); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RouteOrphansTest,
	"UEGT2.NPC.Routes.Orphans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RouteOrphansTest::RunTest(const FString& Parameters)
{
	UEGT2RouteNetworkTests::FTestWorld Fixture;
	AUEGT2RouteNetwork* Routes = Fixture.Routes;
	if (!TestNotNull(TEXT("route network exists"), Routes)) { return false; }

	const int32 Orphan = Routes->AddNode(FVector::ZeroVector);
	const int32 Start = Routes->AddNode(FVector(1000.0, 0.0, 0.0));
	const int32 Goal = Routes->AddNode(FVector(5000.0, 0.0, 0.0));
	Routes->LinkNodes(Start, Goal);
	Routes->FinaliseNetwork();

	TestEqual(TEXT("finalising preserves authored node IDs"), Routes->GetNodeCount(), 3);
	TestTrue(TEXT("orphan position remains addressable"), Routes->GetNodeLocation(Orphan).IsZero());
	TestEqual(TEXT("a closer orphan cannot mask the road"),
		Routes->FindNearestNode(FVector::ZeroVector, 2000.0f), Start);
	TestEqual(TEXT("road lookup measures XY without an altitude band"),
		Routes->FindNearestNode(FVector(0.0, 0.0, 100000.0), 2000.0f), Start);

	TArray<FVector> Path;
	TestTrue(TEXT("a journey near an orphan still uses the connected road"),
		Routes->FindPath(FVector::ZeroVector, Routes->GetNodeLocation(Goal), Path));
	if (TestEqual(TEXT("the route includes both linked endpoints"), Path.Num(), 2))
	{
		TestTrue(TEXT("the route starts on the road"), Path[0].Equals(Routes->GetNodeLocation(Start)));
		TestTrue(TEXT("the route reaches the road destination"), Path.Last().Equals(Routes->GetNodeLocation(Goal)));
	}

	AUEGT2RouteNetwork* Isolated = Fixture.World->SpawnActor<AUEGT2RouteNetwork>();
	if (!TestNotNull(TEXT("isolated network exists"), Isolated)) { return false; }
	Isolated->AddNode(FVector::ZeroVector);
	Isolated->AddNode(FVector(1000.0, 0.0, 0.0));
	Isolated->FinaliseNetwork();
	TestEqual(TEXT("an all-orphan network has no usable nearest node"),
		Isolated->FindNearestNode(FVector::ZeroVector, 2000.0f), INDEX_NONE);
	TestFalse(TEXT("an all-orphan network cannot route"),
		Isolated->FindPath(FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), Path));
	TestTrue(TEXT("an all-orphan network leaves wanderers in place"),
		Isolated->GetWanderTarget(FVector::ZeroVector, 2000.0f, 42).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RouteLinkMutationTest,
	"UEGT2.NPC.Routes.LinkMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RouteLinkMutationTest::RunTest(const FString& Parameters)
{
	UEGT2RouteNetworkTests::FTestWorld Fixture;
	AUEGT2RouteNetwork* Routes = Fixture.Routes;
	if (!TestNotNull(TEXT("route network exists"), Routes)) { return false; }

	const int32 A = Routes->AddNode(FVector::ZeroVector);
	const int32 B = Routes->AddNode(FVector(1000.0, 0.0, 0.0));
	TestEqual(TEXT("a query before linking excludes unlinked nodes"),
		Routes->FindNearestNode(FVector::ZeroVector, 2000.0f), INDEX_NONE);
	Routes->LinkNodes(A, B);
	TestEqual(TEXT("linking invalidates the prior spatial lookup"),
		Routes->FindNearestNode(FVector::ZeroVector, 2000.0f), A);
	TArray<FVector> Path;
	TestTrue(TEXT("newly linked nodes are immediately routable"),
		Routes->FindPath(Routes->GetNodeLocation(A), Routes->GetNodeLocation(B), Path));
	TestEqual(TEXT("both newly linked endpoints appear in the route"), Path.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2RouteWanderRadiusTest,
	"UEGT2.NPC.Routes.WanderRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2RouteWanderRadiusTest::RunTest(const FString& Parameters)
{
	UEGT2RouteNetworkTests::FTestWorld Fixture;
	AUEGT2RouteNetwork* Routes = Fixture.Routes;
	if (!TestNotNull(TEXT("route network exists"), Routes)) { return false; }

	const int32 A = Routes->AddNode(FVector(3000.0, 0.0, 0.0));
	const int32 B = Routes->AddNode(FVector(3500.0, 0.0, 0.0));
	Routes->LinkNodes(A, B);
	TestTrue(TEXT("an out-of-radius road cannot pull a wanderer away from home"),
		Routes->GetWanderTarget(FVector::ZeroVector, 2500.0f, 42).IsZero());
	TestTrue(TEXT("zero radius leaves the wanderer in place"),
		Routes->GetWanderTarget(FVector::ZeroVector, 0.0f, 42).IsZero());
	TestTrue(TEXT("negative radius leaves the wanderer in place"),
		Routes->GetWanderTarget(FVector::ZeroVector, -2500.0f, 42).IsZero());

	const int32 C = Routes->AddNode(FVector(3000.0, 600.0, 0.0));
	const int32 Outside = Routes->AddNode(FVector(5000.0, 0.0, 0.0));
	Routes->LinkNodes(A, C);
	Routes->LinkNodes(B, C);
	Routes->LinkNodes(B, Outside);
	for (uint32 Seed = 0; Seed < 16; ++Seed)
	{
		const FVector Target = Routes->GetWanderTarget(FVector::ZeroVector, 4000.0f, Seed);
		TestTrue(TEXT("a valid road is used when it is in range"), !Target.IsZero());
		TestTrue(TEXT("every random walk stays inside the requested radius"),
			FVector::Dist2D(Target, FVector::ZeroVector) <= 4000.0);
		TestTrue(TEXT("the same seed repeats the same walk"),
			Target.Equals(Routes->GetWanderTarget(FVector::ZeroVector, 4000.0f, Seed)));
	}
	return true;
}

#endif
