#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "UI/UEGT2HUDLayout.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

namespace UEGT2HUDLayoutTests
{
	bool CheckInside(FAutomationTestBase& Test, const FBox2D& Rect, const FVector2D& Viewport)
	{
		const bool bFinite = Rect.bIsValid && !Rect.Min.ContainsNaN() && !Rect.Max.ContainsNaN();
		if (!Test.TestTrue(TEXT("layout returns a finite valid rectangle"), bFinite)) { return false; }
		const FBox2D Screen(FVector2D::ZeroVector, Viewport);
		Test.TestTrue(TEXT("rectangle has positive area"), Rect.GetSize().X > 0.0 && Rect.GetSize().Y > 0.0);
		return Test.TestTrue(TEXT("whole rectangle fits the physical viewport"),
			Screen.IsInsideOrOn(Rect.Min) && Screen.IsInsideOrOn(Rect.Max));
	}

	FVector2D Measure(UCanvas& Canvas, UFont* Font, const FString& Text, float Scale)
	{
		float Width = 0.0f, Height = 0.0f;
		Canvas.TextSize(Font, Text, Width, Height, Scale, Scale);
		return FVector2D(Width, Height);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2HUDLayoutBaselineTest, "UEGT2.HUD.LayoutBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2HUDLayoutBaselineTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2HUDLayout;
	using namespace UEGT2HUDLayoutTests;
	for (const FVector2D Viewport : { FVector2D(1280, 720), FVector2D(1920, 1080) })
	{
		const FUEGT2HUDLayout Normal = Resolve(Viewport, 1.0f, true);
		const FUEGT2HUDLayout Off = Resolve(Viewport, 1.5f, false);
		TestEqual(TEXT("Normal is the original scale"), Normal.Scale, 1.0f);
		TestEqual(TEXT("hard gate overrides saved enlargement"), Off.Scale, 1.0f);
		TestFalse(TEXT("Normal leaves enhanced placement off"), Normal.bEnhanced);
		TestFalse(TEXT("hard off leaves enhanced placement off"), Off.bEnhanced);
		TestTrue(TEXT("Normal keeps physical coordinates"), Normal.ToScreen(Viewport * 0.5).Equals(Viewport * 0.5));
		TestTrue(TEXT("hard off keeps logical viewport identical"), Off.LogicalViewport().Equals(Viewport));
		const FBox2D Life = AnchorPanel(Normal, FVector2D(214, 140), EUEGT2HUDAnchor::BottomLeft, FVector2D(24, 26));
		TestTrue(TEXT("legacy life left inset is 24 pixels"), FMath::IsNearlyEqual(Life.Min.X, 24.0));
		TestTrue(TEXT("legacy life bottom inset is 26 pixels"), FMath::IsNearlyEqual(Life.Max.Y, Viewport.Y - 26.0));
		const FBox2D Survey = AnchorPanel(Normal, FVector2D(370, 82), EUEGT2HUDAnchor::BottomRight, FVector2D(24, 24));
		TestTrue(TEXT("legacy survey starts 394 pixels from right"), FMath::IsNearlyEqual(Survey.Min.X, Viewport.X - 394.0));
		TestTrue(TEXT("legacy survey starts 106 pixels from bottom"), FMath::IsNearlyEqual(Survey.Min.Y, Viewport.Y - 106.0));
		const FBox2D Almanac = AnchorPanel(Normal, FVector2D(240, 78), EUEGT2HUDAnchor::TopLeft, FVector2D(24, 20));
		TestTrue(TEXT("legacy almanac top-left origin"), Almanac.Min.Equals(FVector2D(24, 20)));
		const FBox2D Message = PlaceMessage(Normal, FVector2D(428, 36), { Life, Survey });
		TestTrue(TEXT("legacy message padded top is height minus 147"), FMath::IsNearlyEqual(Message.Min.Y, Viewport.Y - 147.0));
		TestTrue(TEXT("legacy message remains physically centered"), FMath::IsNearlyEqual(Message.GetCenter().X, Viewport.X * 0.5));
		const FBox2D OffMessage = PlaceMessage(Off, FVector2D(428, 36), { Life, Survey });
		TestTrue(TEXT("hard off reproduces Normal geometry"), OffMessage.Min.Equals(Message.Min) && OffMessage.Max.Equals(Message.Max));
		CheckInside(*this, Life, Viewport);
		CheckInside(*this, Survey, Viewport);
		CheckInside(*this, Almanac, Viewport);
		CheckInside(*this, Message, Viewport);
		for (const float Scale : { 1.25f, 1.5f })
		{
			const FUEGT2HUDLayout Layout = Resolve(Viewport, Scale, true);
			TestEqual(TEXT("offered scale fits 720p and 1080p exactly"), Layout.Scale, Scale);
			const FVector2D PhysicalPoint(Viewport.X - 93.75, Viewport.Y - 26.0);
			TestTrue(TEXT("physical anchors survive logical round trip"), Layout.ToScreen(Layout.ToLogical(PhysicalPoint)).Equals(PhysicalPoint, 0.001));
			const FBox2D Panel = AnchorPanel(Layout, FVector2D(370, 82), EUEGT2HUDAnchor::BottomRight, FVector2D(24, 24));
			TestTrue(TEXT("enlarging does not move physical right/bottom insets"), Panel.Max.Equals(Viewport - FVector2D(24, 24)));
			CheckInside(*this, Panel, Viewport);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2HUDLayoutBottomRowTest, "UEGT2.HUD.BottomRowAndMessages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2HUDLayoutBottomRowTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2HUDLayout;
	using namespace UEGT2HUDLayoutTests;
	if (!TestTrue(TEXT("engine fonts available"), GEngine && GEngine->GetMediumFont() && GEngine->GetSmallFont())) { return false; }
	TStrongObjectPtr<UCanvas> Canvas(NewObject<UCanvas>());
	const FString LongActivity = TEXT("Working at the Fairhaven harbour smithy while preparing deliveries for Newhaven and the distant mountain road");
	for (const FVector2D Viewport : { FVector2D(1280, 720), FVector2D(1920, 1080) })
	{
		for (const float Scale : { 1.25f, 1.5f })
		{
			const FUEGT2HUDLayout Layout = Resolve(Viewport, Scale, true);
			const FVector2D Measured = Measure(*Canvas.Get(), GEngine->GetSmallFont(), LongActivity + LongActivity, Layout.Scale);
			TestTrue(TEXT("real scaled activity text measured"), Measured.X > 0.0 && Measured.Y > 0.0);
			const float Budget = BottomLeftMaxWidth(Layout, 370.0f, true);
			TestTrue(TEXT("long measured text requires bounded wrapping"), Layout.ToLogical(Measured).X > Budget);
			const float WithoutSurvey = BottomLeftMaxWidth(Layout, 370.0f, false);
			TestTrue(TEXT("hidden survey releases its width"), WithoutSurvey > Budget);
			const FBox2D Life = AnchorPanel(Layout, FVector2D(Budget, 180), EUEGT2HUDAnchor::BottomLeft, FVector2D(24, 26));
			const FBox2D Survey = AnchorPanel(Layout, FVector2D(370, 82), EUEGT2HUDAnchor::BottomRight, FVector2D(24, 24));
			CheckInside(*this, Life, Viewport);
			CheckInside(*this, Survey, Viewport);
			TestFalse(TEXT("bottom panels fit together without overlap"), Life.Intersect(Survey));
			const FBox2D Message = PlaceMessage(Layout, FVector2D(520, 48), { Life, Survey });
			CheckInside(*this, Message, Viewport);
			TestTrue(TEXT("message clears full life panel"), Message.Max.Y < Life.Min.Y);
			TestFalse(TEXT("message never covers life"), Message.Intersect(Life));
			TestFalse(TEXT("message never covers survey"), Message.Intersect(Survey));
			const FBox2D Unobstructed = PlaceMessage(Layout, FVector2D(520, 48), {});
			TestTrue(TEXT("occupied row lifts message"), Message.Min.Y < Unobstructed.Min.Y);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2HUDLayoutBubblesTest, "UEGT2.HUD.BubbleBoundsAndCrowding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2HUDLayoutBubblesTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2HUDLayout;
	using namespace UEGT2HUDLayoutTests;
	for (const FVector2D Viewport : { FVector2D(1280, 720), FVector2D(1920, 1080) })
	{
		for (const float Scale : { 1.0f, 1.25f, 1.5f })
		{
			const FUEGT2HUDLayout Layout = Resolve(Viewport, Scale, true);
			for (const FVector2D Anchor : { FVector2D(-100, -100), FVector2D(Viewport.X + 100, -100),
				Viewport + FVector2D(100, 100), FVector2D(-100, Viewport.Y + 100) })
			{
				FBox2D Bubble;
				TestTrue(TEXT("edge bubble can be placed"), PlaceBubble(Layout, Anchor, FVector2D(322, 96), {}, Bubble));
				CheckInside(*this, Bubble, Viewport);
			}
			TArray<FBox2D> Occupied;
			bool bDropped = false;
			for (int32 Speaker = 0; Speaker < 20; ++Speaker)
			{
				FBox2D Bubble;
				if (!PlaceBubble(Layout, FVector2D(Viewport.X * 0.5, Viewport.Y - 20), FVector2D(322, 96), Occupied, Bubble))
				{
					TestFalse(TEXT("rejected bubble has no usable bounds"), Bubble.bIsValid);
					bDropped = true;
					break;
				}
				CheckInside(*this, Bubble, Viewport);
				for (const FBox2D& Prior : Occupied)
				{
					TestFalse(TEXT("whole body and tail clear earlier bubbles"), Bubble.Intersect(Prior));
				}
				Occupied.Add(Bubble);
			}
			TestTrue(TEXT("neighboring speakers use separate readable bubbles"), Occupied.Num() > 1);
			TestTrue(TEXT("crowded screen drops excess speech"), bDropped);
			FBox2D Rejected;
			TestFalse(TEXT("oversized bubble rejected"), PlaceBubble(Layout, Viewport * 0.5, Layout.LogicalViewport() + FVector2D(1, 1), {}, Rejected));
			TestFalse(TEXT("fully occupied screen rejects bubble"), PlaceBubble(Layout, Viewport * 0.5, FVector2D(100, 50),
				{ FBox2D(FVector2D::ZeroVector, Viewport) }, Rejected));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2HUDLayoutInvalidTest, "UEGT2.HUD.InvalidLayoutInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2HUDLayoutInvalidTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2HUDLayout;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();
	for (const FVector2D InvalidViewport : { FVector2D(0, 720), FVector2D(1280, -1), FVector2D(NaN, 720), FVector2D(1280, Infinity) })
	{
		const FUEGT2HUDLayout Layout = Resolve(InvalidViewport, 1.5f, true);
		TestTrue(TEXT("invalid viewport falls back to finite default"), Layout.Viewport.Equals(FVector2D(1280, 720)));
		TestEqual(TEXT("invalid viewport uses Normal scale"), Layout.Scale, 1.0f);
		TestFalse(TEXT("invalid viewport disables enhanced layout"), Layout.bEnhanced);
	}
	for (const float InvalidScale : { -1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
	{
		const FUEGT2HUDLayout Layout = Resolve(FVector2D(1280, 720), InvalidScale, true);
		TestEqual(TEXT("invalid scale uses Normal"), Layout.Scale, 1.0f);
		TestFalse(TEXT("invalid scale leaves enhanced placement off"), Layout.bEnhanced);
	}
	const FUEGT2HUDLayout Small = Resolve(FVector2D(800, 480), 1.5f, true);
	TestEqual(TEXT("small viewport does not shrink below the original HUD"), Small.Scale, 1.0f);
	const FUEGT2HUDLayout Layout = Resolve(FVector2D(1280, 720), 1.5f, true);
	for (const FVector2D InvalidSize : { FVector2D(0, 50), FVector2D(100, -1), FVector2D(NaN, 50), FVector2D(100, Infinity) })
	{
		TestFalse(TEXT("invalid panel dimensions rejected"), AnchorPanel(Layout, InvalidSize, EUEGT2HUDAnchor::BottomLeft, FVector2D(24, 26)).bIsValid);
		TestFalse(TEXT("invalid message dimensions rejected"), PlaceMessage(Layout, InvalidSize, {}).bIsValid);
		FBox2D Bubble(FVector2D(1, 1), FVector2D(20, 20));
		TestFalse(TEXT("invalid bubble dimensions rejected"), PlaceBubble(Layout, FVector2D(400, 300), InvalidSize, {}, Bubble));
		TestFalse(TEXT("failed bubble clears previous output"), Bubble.bIsValid);
	}
	TestFalse(TEXT("invalid physical inset rejected"), AnchorPanel(Layout, FVector2D(100, 50),
		EUEGT2HUDAnchor::BottomRight, FVector2D(Infinity, 24)).bIsValid);
	TestEqual(TEXT("invalid survey width returns no usable budget"), BottomLeftMaxWidth(Layout, std::numeric_limits<float>::quiet_NaN(), true), 0.0f);
	FBox2D Bubble;
	TestFalse(TEXT("nonfinite projected anchor rejected"), PlaceBubble(Layout, FVector2D(NaN, 300), FVector2D(100, 50), {}, Bubble));
	TestFalse(TEXT("infinite projected anchor rejected"), PlaceBubble(Layout, FVector2D(400, Infinity), FVector2D(100, 50), {}, Bubble));
	return true;
}

#endif
