// Fairhaven - restoring a visit releases only the current player's held prop.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PickupReleaseTest,
	"UEGT2.Interaction.PickupRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PickupReleaseTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AActor* FirstCarrier = World->SpawnActor<AActor>();
	AActor* SecondCarrier = World->SpawnActor<AActor>();
	AUEGT2Pickup* FirstProp = World->SpawnActor<AUEGT2Pickup>(FVector(0, 0, 200), FRotator::ZeroRotator);
	AUEGT2Pickup* SecondProp = World->SpawnActor<AUEGT2Pickup>(FVector(400, 0, 200), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("physics mesh"), Cube) || !TestNotNull(TEXT("first carrier"), FirstCarrier)
		|| !TestNotNull(TEXT("second carrier"), SecondCarrier) || !TestNotNull(TEXT("first prop"), FirstProp)
		|| !TestNotNull(TEXT("second prop"), SecondProp)) { return false; }
	for (AUEGT2Pickup* Prop : { FirstProp, SecondProp })
	{
		Prop->SetInteractableMesh(Cube);
		Prop->DispatchBeginPlay();
	}
	FirstProp->Interact(FirstCarrier);
	SecondProp->Interact(SecondCarrier);
	TestTrue(TEXT("real interaction starts carrying"), FirstProp->IsCarried());
	TestTrue(TEXT("carried prop follows its carrier each tick"), FirstProp->IsActorTickEnabled());
	TestFalse(TEXT("carried prop is not simulated"), FirstProp->GetMeshComponent()->IsSimulatingPhysics());
	TestFalse(TEXT("null carrier cannot release a held prop"), FirstProp->ReleaseIfCarriedBy(nullptr));
	TestFalse(TEXT("another carrier cannot release a held prop"), FirstProp->ReleaseIfCarriedBy(SecondCarrier));
	TestTrue(TEXT("mismatched release preserves carrying"), FirstProp->IsCarried());
	const FVector Before = FirstProp->GetActorLocation();
	TestTrue(TEXT("matching carrier releases the held prop"), FirstProp->ReleaseIfCarriedBy(FirstCarrier));
	TestFalse(TEXT("released prop no longer has a carrier"), FirstProp->IsCarried());
	TestFalse(TEXT("released prop stops following"), FirstProp->IsActorTickEnabled());
	TestTrue(TEXT("release restores physics"), FirstProp->GetMeshComponent()->IsSimulatingPhysics());
	TestEqual(TEXT("release restores physical collision"), FirstProp->GetMeshComponent()->GetCollisionEnabled(),
		ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("release does not move the prop to a checkpoint"), FirstProp->GetActorLocation().Equals(Before));
	TestFalse(TEXT("repeated release is a no-op"), FirstProp->ReleaseIfCarriedBy(FirstCarrier));
	TestFalse(TEXT("restoring the first carrier cannot release the second carrier's prop"),
		SecondProp->ReleaseIfCarriedBy(FirstCarrier));
	TestTrue(TEXT("the other carrier keeps their prop"), SecondProp->IsCarried());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
