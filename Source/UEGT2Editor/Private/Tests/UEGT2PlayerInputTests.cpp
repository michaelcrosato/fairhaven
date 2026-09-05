#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2PlayerPossessionInputTest,
	"UEGT2.Player.PossessionInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2PlayerPossessionInputTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AUEGT2PlayerController* Controller = World->SpawnActor<AUEGT2PlayerController>();
	AUEGT2Character* First = World->SpawnActor<AUEGT2Character>();
	AUEGT2Character* Second = World->SpawnActor<AUEGT2Character>(FVector(1000, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("controller"), Controller) || !TestNotNull(TEXT("first pawn"), First)
		|| !TestNotNull(TEXT("second pawn"), Second)) { return false; }
	Controller->SetupInputComponent();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Controller->InputComponent);
	if (!TestNotNull(TEXT("enhanced input component"), Input)) { return false; }
	const auto CountFor = [Input](const UObject* Owner)
	{
		int32 Count = 0;
		for (const auto& Binding : Input->GetActionEventBindings())
		{
			Count += Binding->IsBoundToObject(Owner) ? 1 : 0;
		}
		return Count;
	};
	const int32 ControllerBindings = CountFor(Controller);
	TestTrue(TEXT("controller has menu and diagnostics actions"), ControllerBindings > 0);
	Controller->Possess(First);
	const int32 PawnBindings = CountFor(First);
	TestTrue(TEXT("first possession binds pawn actions"), PawnBindings > 0);
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		Controller->Possess(Second);
		TestEqual(TEXT("old pawn has no live delegates"), CountFor(First), 0);
		TestEqual(TEXT("new pawn has one set of actions"), CountFor(Second), PawnBindings);
		TestEqual(TEXT("controller actions survive possession"), CountFor(Controller), ControllerBindings);
		Controller->UnPossess();
		TestEqual(TEXT("unpossess clears pawn delegates"), CountFor(Second), 0);
		Controller->Possess(First);
		TestEqual(TEXT("repossess does not duplicate delegates"), CountFor(First), PawnBindings);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
