// Fairhaven (UEGT2) - the movement keys go the way they say they do.
//
// This exists because S walked the player forward. The key was bound, the
// modifier list was right, the action fired - and the value came out identical
// to W, because the negate was applied to X after the swizzle had already moved
// the value onto Y. Nothing logged, nothing crashed, and the only symptom was
// that you could not back away from anything.
//
// So the test runs the modifiers the way Enhanced Input runs them, in order, on
// the value a key press actually produces, and checks the sign that comes out.
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/AutomationTest.h"
#include "Player/UEGT2InputConfig.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UEGT2InputTests
{
	/** What one key press becomes after a swizzle and an optional negate. */
	FVector2D Resolve(bool bSwizzle, bool bNegate, bool bNegateSwizzledAxis)
	{
		// A digital key press is (1, 0, 0) before any modifier touches it.
		FInputActionValue Value(FVector(1.0f, 0.0f, 0.0f));

		if (bSwizzle)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>();
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Value = Swizzle->ModifyRaw(nullptr, Value, 0.0f);
		}
		if (bNegate)
		{
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>();
			Negate->bX = bNegateSwizzledAxis ? false : true;
			Negate->bY = bNegateSwizzledAxis ? true : false;
			Negate->bZ = false;
			Value = Negate->ModifyRaw(nullptr, Value, 0.0f);
		}
		const FVector Axis = Value.Get<FVector>();
		return FVector2D(Axis.X, Axis.Y);
	}
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2MoveAxisTest,
	"UEGT2.Player.MoveAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2MoveAxisTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2InputTests;

	// AUEGT2Character::OnMove reads Y as forward and X as strafe.
	const FVector2D Forward = Resolve(/*Swizzle*/ true, /*Negate*/ false, false);
	const FVector2D Back = Resolve(/*Swizzle*/ true, /*Negate*/ true, /*OnY*/ true);
	const FVector2D Right = Resolve(/*Swizzle*/ false, /*Negate*/ false, false);
	const FVector2D Left = Resolve(/*Swizzle*/ false, /*Negate*/ true, /*OnY*/ false);

	TestTrue(TEXT("forward drives +Y"), Forward.Y > 0.5f);
	TestTrue(TEXT("and not the strafe axis"), FMath::IsNearlyZero(Forward.X));

	TestTrue(TEXT("back drives -Y"), Back.Y < -0.5f);
	TestTrue(TEXT("and is not the same as forward"), Back.Y * Forward.Y < 0.0f);

	TestTrue(TEXT("right drives +X"), Right.X > 0.5f);
	TestTrue(TEXT("and not the forward axis"), FMath::IsNearlyZero(Right.Y));

	TestTrue(TEXT("left drives -X"), Left.X < -0.5f);
	TestTrue(TEXT("and is not the same as right"), Left.X * Right.X < 0.0f);

	// The bug itself, stated as a test: negating X after the swizzle has moved
	// the value to Y does nothing at all, and back becomes forward.
	const FVector2D BrokenBack = Resolve(/*Swizzle*/ true, /*Negate*/ true, /*OnY*/ false);
	TestTrue(TEXT("negating the wrong axis leaves back pointing forward"),
		BrokenBack.Y > 0.5f);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2MoveBindingsTest,
	"UEGT2.Player.MoveBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2MoveBindingsTest::RunTest(const FString& Parameters)
{
	// The four movement slots must be bound to four different keys. A duplicate
	// here is the other way two directions end up doing the same thing.
	const EUEGT2InputSlot Slots[] = {
		EUEGT2InputSlot::MoveForward, EUEGT2InputSlot::MoveBack,
		EUEGT2InputSlot::MoveLeft, EUEGT2InputSlot::MoveRight,
	};

	TSet<FName> Seen;
	for (EUEGT2InputSlot Slot : Slots)
	{
		const FKey Key = UUEGT2InputConfig::GetEffectiveKey(Slot);
		TestTrue(TEXT("every movement slot is bound"), Key.IsValid());
		TestFalse(TEXT("and no two share a key"), Seen.Contains(Key.GetFName()));
		Seen.Add(Key.GetFName());
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2MoveContextTest,
	"UEGT2.Player.MoveContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2MoveContextTest::RunTest(const FString& Parameters)
{
	// The test above proves the arithmetic. This one proves the game uses it:
	// it builds the real mapping context and runs the real modifier chain that
	// the real W and S bindings carry, in the order Enhanced Input runs them.
	UUEGT2InputConfig* Config = NewObject<UUEGT2InputConfig>();
	UInputMappingContext* Context = Config ? Config->BuildMappingContext() : nullptr;
	TestNotNull(TEXT("the mapping context builds"), Context);
	if (!Context)
	{
		return false;
	}

	const auto Resolve = [Context](const FKey& Key) -> TOptional<FVector2D>
	{
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Key != Key)
			{
				continue;
			}
			FInputActionValue Value(FVector(1.0f, 0.0f, 0.0f));
			for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
			{
				if (Modifier)
				{
					Value = Modifier->ModifyRaw(nullptr, Value, 0.0f);
				}
			}
			const FVector Axis = Value.Get<FVector>();
			return FVector2D(Axis.X, Axis.Y);
		}
		return TOptional<FVector2D>();
	};

	const TOptional<FVector2D> Forward = Resolve(UUEGT2InputConfig::GetEffectiveKey(
		EUEGT2InputSlot::MoveForward));
	const TOptional<FVector2D> Back = Resolve(UUEGT2InputConfig::GetEffectiveKey(
		EUEGT2InputSlot::MoveBack));
	const TOptional<FVector2D> Left = Resolve(UUEGT2InputConfig::GetEffectiveKey(
		EUEGT2InputSlot::MoveLeft));
	const TOptional<FVector2D> Right = Resolve(UUEGT2InputConfig::GetEffectiveKey(
		EUEGT2InputSlot::MoveRight));

	TestTrue(TEXT("forward is mapped"), Forward.IsSet());
	TestTrue(TEXT("back is mapped"), Back.IsSet());
	TestTrue(TEXT("left is mapped"), Left.IsSet());
	TestTrue(TEXT("right is mapped"), Right.IsSet());
	if (!Forward.IsSet() || !Back.IsSet() || !Left.IsSet() || !Right.IsSet())
	{
		return false;
	}

	// The bug, in the terms it was reported in: S moved the player forward.
	TestTrue(TEXT("the forward key drives forward"), Forward->Y > 0.5f);
	TestTrue(TEXT("the back key drives backward"), Back->Y < -0.5f);
	TestTrue(TEXT("the right key strafes right"), Right->X > 0.5f);
	TestTrue(TEXT("the left key strafes left"), Left->X < -0.5f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
