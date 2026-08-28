#include "Diagnostics/UEGT2CaptureSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "UEGT2LogChannels.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* CaptureSwitch = TEXT("UEGT2Capture=");

	FString GetStringArg(const TCHAR* Key, const FString& Default)
	{
		FString Value;
		if (FParse::Value(FCommandLine::Get(), Key, Value))
		{
			return Value;
		}
		return Default;
	}

	float GetFloatArg(const TCHAR* Key, float Default)
	{
		float Value = Default;
		FParse::Value(FCommandLine::Get(), Key, Value);
		return Value;
	}
}

bool UUEGT2CaptureSubsystem::IsCaptureRequested()
{
	FString Unused;
	return FParse::Value(FCommandLine::Get(), CaptureSwitch, Unused);
}

bool UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("UEGT2SmokeWalk"));
}

const TArray<FUEGT2Viewpoint>& UUEGT2CaptureSubsystem::GetTour()
{
	// Coordinates follow the world convention: +X north, +Y east.
	// Keep this list in step with Tools/Terrain/world_config.py.
	static const TArray<FUEGT2Viewpoint> Points = []
	{
		TArray<FUEGT2Viewpoint> Result;
		auto Add = [&Result](const TCHAR* Name, float X, float Y, float Height,
			float Yaw, float Pitch, const TCHAR* Description)
		{
			FUEGT2Viewpoint Point;
			Point.Name = FName(Name);
			Point.Location = FVector2D(X, Y);
			Point.HeightAboveGround = Height;
			Point.Yaw = Yaw;
			Point.Pitch = Pitch;
			Point.Description = Description;
			Result.Add(Point);
		};

		// Indoors: Z is an absolute world height, and the coordinates come from
		// a house the content build placed. Like every other viewpoint in this
		// list they are tied to the current seed - Tools/Python/uegt2 has a
		// scratch script that re-derives them if the town ever moves.
		auto AddIndoor = [&Result](const TCHAR* Name, float X, float Y, float Z,
			float Yaw, float Pitch, const TCHAR* Description)
		{
			FUEGT2Viewpoint Point;
			Point.Name = FName(Name);
			Point.Location = FVector2D(X, Y);
			Point.HeightAboveGround = Z;
			Point.bAbsoluteHeight = true;
			Point.Yaw = Yaw;
			Point.Pitch = Pitch;
			Point.Description = Description;
			Result.Add(Point);
		};

		Add(TEXT("TownSquare"), 0.0f, 0.0f, 1.8f, 70.0f, -4.0f,
			TEXT("Town centre looking east toward the harbour"));
		Add(TEXT("MainStreet"), -6000.0f, -4000.0f, 1.8f, 55.0f, -2.0f,
			TEXT("Main street approach into town"));
		// Off the square itself, looking back across it. TownSquare stands the
		// camera on the well, four metres above everyone's head - a fine shot
		// of the roofs and a useless one of the people under them. Height is
		// measured from whatever the downward trace hits, so this sits a little
		// above the square rather than exactly in it.
		Add(TEXT("Market"), 2900.0f, 1500.0f, 1.8f, 207.0f, -3.0f,
			TEXT("The market square and the crowd around the well"));
		Add(TEXT("Waterfront"), -1200.0f, 26500.0f, 2.2f, 78.0f, -5.0f,
			TEXT("Shoreline and docks looking out to sea"));
		Add(TEXT("BeachSouth"), -26000.0f, 21000.0f, 2.0f, 20.0f, -3.0f,
			TEXT("Beach south of town looking back north"));
		Add(TEXT("Farmland"), -12000.0f, -46000.0f, 2.0f, 30.0f, -3.0f,
			TEXT("Farm road through the western fields"));
		Add(TEXT("MillPond"), -13000.0f, -37000.0f, 2.0f, 200.0f, -4.0f,
			TEXT("Pond in the farmland"));
		Add(TEXT("RiverValley"), 30000.0f, 15000.0f, 2.0f, 20.0f, -2.0f,
			TEXT("River valley on the mountain road"));
		Add(TEXT("MountainRoad"), 62000.0f, 12000.0f, 2.0f, 15.0f, 2.0f,
			TEXT("High on the mountain road"));
		Add(TEXT("Tropics"), -48000.0f, 2000.0f, 2.0f, 200.0f, -2.0f,
			TEXT("Southern tropical lowland"));
		Add(TEXT("Lagoon"), -64000.0f, 6000.0f, 2.0f, 75.0f, -3.0f,
			TEXT("Lagoon in the south"));
		Add(TEXT("Overlook"), 34000.0f, 6000.0f, 120.0f, 150.0f, -16.0f,
			TEXT("High overlook back toward the town and coast"));
		Add(TEXT("Vista"), 70000.0f, 20000.0f, 320.0f, 200.0f, -22.0f,
			TEXT("Mountain vista over the whole region"));
		// Newhaven, on the southern coastal shelf.
		// On the centre avenue, not on a block. The capture traces straight down
		// for the ground, so a point inside a block lands on a roof.
		Add(TEXT("Newhaven"), -123088.0f, 5153.0f, 1.8f, 99.0f, 3.0f,
			TEXT("Downtown Newhaven looking up the avenue"));
		Add(TEXT("NewhavenSkyline"), -162000.0f, -14000.0f, 70.0f, 40.0f, -3.0f,
			TEXT("The Newhaven skyline from the southern approach"));
		Add(TEXT("NewhavenWharf"), -125000.0f, 44000.0f, 4.0f, -90.0f, -2.0f,
			TEXT("The container wharf looking back at the city"));
		// The civic plaza: city hall behind the fountain. Worth its own
		// viewpoint because it is where the city's street life collects, and
		// because it spent a while being an empty rectangle - see the note on
		// check=False in city._place_plaza.
		// On the far side of the civic square, looking across the fountain to
		// the city hall on the block opposite. Note the height: the capture
		// traces down for the ground, so a point inside a building's footprint
		// puts the camera on its roof - which is where this viewpoint spent its
		// first attempt.
		Add(TEXT("NewhavenPlaza"), -128155.0f, 20238.0f, 1.8f, -81.0f, -1.0f,
			TEXT("Newhaven's civic square, looking across the fountain to the city hall"));
		Add(TEXT("NewhavenAerial"), -120220.0f, -12617.0f, 260.0f, 99.0f, -30.0f,
			TEXT("Newhaven street grid from above"));

		// Inside the houses. Eye height above the floor, so these frame what a
		// player sees on walking through a front door.
		AddIndoor(TEXT("HouseInterior"), 3319.1f, -585.5f, 1749.3f, 179.5f, -3.0f,
			TEXT("Inside a two storey town house, looking in from the front door"));
		AddIndoor(TEXT("HouseUpstairs"), 3299.1f, -585.3f, 2069.3f, 179.5f, -3.0f,
			TEXT("The bedroom floor of the same house"));
		AddIndoor(TEXT("CottageInterior"), 4615.5f, 3536.1f, 1737.9f, 2.1f, -3.0f,
			TEXT("Inside a one room cottage"));
		AddIndoor(TEXT("HouseHearth"), 1515.0f, -2056.3f, 1747.7f, -89.9f, -3.0f,
			TEXT("Looking back at the front door and windows from the far wall"));

		// Inside Newhaven. Every ground floor in the city is a business now,
		// so these are five different trades in five different archetypes.
		AddIndoor(TEXT("NewhavenShop"), -128884.6f, -3987.9f, 1210.6f, -80.8f, -4.0f,
			TEXT("A Newhaven grocery, from behind the counter"));
		AddIndoor(TEXT("NewhavenBarber"), -140842.0f, 4888.0f, 1214.4f, 98.8f, -4.0f,
			TEXT("A barber shop on the outer ring"));
		AddIndoor(TEXT("NewhavenSurgery"), -124467.3f, 5523.6f, 1244.1f, -81.0f, -4.0f,
			TEXT("A dental surgery on an office block ground floor"));
		AddIndoor(TEXT("NewhavenLobby"), -124749.5f, 9426.7f, 1255.0f, 9.3f, -4.0f,
			TEXT("The lobby of a downtown tower"));
		AddIndoor(TEXT("NewhavenDiner"), -124372.1f, -1054.4f, 1225.3f, 99.7f, -4.0f,
			TEXT("A restaurant off the avenue"));

		// Development-only: the asset showcase grid (see Tools/Python/uegt2/showcase.py).
		Add(TEXT("ShowcaseA"), 27200.0f, -57500.0f, 3.4f, 90.0f, -3.0f,
			TEXT("Asset showcase, front row"));
		Add(TEXT("ShowcaseB"), 27200.0f, -59500.0f, 16.0f, 90.0f, -13.0f,
			TEXT("Asset showcase, raised"));
		return Result;
	}();
	return Points;
}

float UUEGT2CaptureSubsystem::GroundHeightAt(float WorldX, float WorldY) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	FHitResult Hit;
	const FVector Start(WorldX, WorldY, 120000.0f);
	const FVector End(WorldX, WorldY, -60000.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2CaptureGround), true);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return static_cast<float>(Hit.ImpactPoint.Z);
	}
	// Fall back to sea level so a missing landscape still produces a frame.
	return 0.0f;
}

void UUEGT2CaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.WorldType != EWorldType::Game)
	{
		return;
	}

	if (IsWalkSmokeRequested())
	{
		const float SmokeDelay = GetFloatArg(TEXT("UEGT2CaptureDelay="), 6.0f);
		InWorld.GetTimerManager().SetTimer(TimerHandle,
			FTimerDelegate::CreateUObject(this, &UUEGT2CaptureSubsystem::BeginWalkSmoke),
			FMath::Max(SmokeDelay, 0.5f), false);
		return;
	}

	if (!IsCaptureRequested())
	{
		return;
	}

	OutputDirectory = GetStringArg(CaptureSwitch, FPaths::ProjectSavedDir() / TEXT("Screenshots"));
	HoldSeconds = GetFloatArg(TEXT("UEGT2CaptureHold="), 1.6f);
	const float Delay = GetFloatArg(TEXT("UEGT2CaptureDelay="), 6.0f);

	const FString Only = GetStringArg(TEXT("UEGT2CaptureOnly="), FString());
	Tour = GetTour();
	if (!Only.IsEmpty())
	{
		Tour = Tour.FilterByPredicate([&Only](const FUEGT2Viewpoint& Point)
		{
			return Point.Name.ToString().Equals(Only, ESearchCase::IgnoreCase);
		});
	}

	UE_LOG(LogUEGT2Diag, Log,
		TEXT("Capture tour requested: %d viewpoints -> %s (delay %.1fs, hold %.1fs)"),
		Tour.Num(), *OutputDirectory, Delay, HoldSeconds);

	ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(
		this, &UUEGT2CaptureSubsystem::HandleScreenshotCaptured);

	bMenuMode = FParse::Param(FCommandLine::Get(), TEXT("UEGT2CaptureMenu"));

	// Give Lumen, virtual shadow maps and streaming time to settle first.
	InWorld.GetTimerManager().SetTimer(TimerHandle,
		FTimerDelegate::CreateUObject(this, bMenuMode
			? &UUEGT2CaptureSubsystem::BeginMenuTour
			: &UUEGT2CaptureSubsystem::BeginTour),
		FMath::Max(Delay, 0.5f), false);
}

void UUEGT2CaptureSubsystem::BeginTour()
{
	UWorld* World = GetWorld();
	if (!World || Tour.Num() == 0)
	{
		FinishTour();
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Capture tour: no player controller."));
		FinishTour();
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TourCamera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TourCamera)
	{
		FinishTour();
		return;
	}
	if (UCameraComponent* Component = TourCamera->GetCameraComponent())
	{
		Component->SetFieldOfView(88.0f);
	}
	PC->SetViewTarget(TourCamera);

	Index = 0;
	CaptureNext();
}

void UUEGT2CaptureSubsystem::CaptureNext()
{
	UWorld* World = GetWorld();
	if (!World || !Tour.IsValidIndex(Index) || !TourCamera)
	{
		FinishTour();
		return;
	}

	const FUEGT2Viewpoint& Point = Tour[Index];
	const float Z = Point.bAbsoluteHeight
		? Point.HeightAboveGround
		: GroundHeightAt(Point.Location.X, Point.Location.Y)
			+ Point.HeightAboveGround * 100.0f;
	const FVector Location(Point.Location.X, Point.Location.Y, Z);
	TourCamera->SetActorLocationAndRotation(Location, FRotator(Point.Pitch, Point.Yaw, 0.0f));

	// Move first, then wait a beat so temporal effects resolve before the shot.
	World->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda(
		[this, Point, Location]()
		{
			const FString FileName = FString::Printf(TEXT("%s/%02d_%s.png"),
				*OutputDirectory, Index + 1, *Point.Name.ToString());

			UE_LOG(LogUEGT2Diag, Log,
				TEXT("Capture %02d/%02d '%s' at (%.0f, %.0f, %.0f): %s"),
				Index + 1, Tour.Num(), *Point.Name.ToString(),
				Location.X, Location.Y, Location.Z, *Point.Description);

			// Ask for a raw capture and write the PNG ourselves: the built-in
			// filename path does not produce a file in an offscreen game build.
			PendingFileName = FileName;
			FScreenshotRequest::RequestScreenshot(false);

			// Give the screenshot a frame to be written, then advance.
			if (UWorld* Inner = GetWorld())
			{
				Inner->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda(
					[this]()
					{
						++Index;
						CaptureNext();
					}), 0.6f, false);
			}
		}), HoldSeconds, false);
}


// ---------------------------------------------------------------------------
// Menu capture: proves the front end, every settings tab and the pause screen
// actually render, without a human opening the game.
// ---------------------------------------------------------------------------
namespace
{
	struct FMenuShot { const TCHAR* Name; int32 SettingsTab; bool bPause; };

	const FMenuShot MenuShots[] = {
		{ TEXT("MainMenu"),        -1, false },
		{ TEXT("SettingsGraphics"), 0, false },
		{ TEXT("SettingsAudio"),    1, false },
		{ TEXT("SettingsControls"), 2, false },
		{ TEXT("SettingsGameplay"), 3, false },
		{ TEXT("PauseMenu"),       -1, true  },
	};
}

void UUEGT2CaptureSubsystem::BeginMenuTour()
{
	MenuIndex = 0;
	UE_LOG(LogUEGT2Diag, Log, TEXT("Menu capture: %d screens -> %s"),
		(int32)UE_ARRAY_COUNT(MenuShots), *OutputDirectory);
	RunMenuStep();
}

void UUEGT2CaptureSubsystem::RunMenuStep()
{
	UWorld* World = GetWorld();
	if (!World || MenuIndex >= (int32)UE_ARRAY_COUNT(MenuShots))
	{
		FinishTour();
		return;
	}

	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(
		UGameplayStatics::GetPlayerController(World, 0));
	if (!PC)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Menu capture: no UEGT2 player controller."));
		FinishTour();
		return;
	}

	const FMenuShot& Shot = MenuShots[MenuIndex];
	if (Shot.bPause)
	{
		PC->CloseMenu();
		PC->ShowPauseMenu();
	}
	else if (Shot.SettingsTab >= 0)
	{
		PC->ShowSettingsPage(Shot.SettingsTab);
	}
	else
	{
		PC->ShowMainMenu();
	}

	// Real-time ticker, not the world timer: the pause screen actually pauses
	// the world, which would stop world timers and hang the capture.
	const FString Name = Shot.Name;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this, Name](float) -> bool
		{
			PendingFileName = FString::Printf(TEXT("%s/menu_%02d_%s.png"),
				*OutputDirectory, MenuIndex + 1, *Name);
			UE_LOG(LogUEGT2Diag, Log, TEXT("Menu capture %02d: %s"), MenuIndex + 1, *Name);
			FScreenshotRequest::RequestScreenshot(true);

			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[this](float) -> bool
				{
					++MenuIndex;
					RunMenuStep();
					return false;
				}), 0.9f);
			return false;
		}), FMath::Max(HoldSeconds, 0.8f));
}


// ---------------------------------------------------------------------------
// Walk smoke: injects real input through Enhanced Input and checks the player
// actually moved. This is what catches "the game looks fine and nothing is
// bound", which is exactly what a missing DefaultInputComponentClass causes.
// ---------------------------------------------------------------------------
void UUEGT2CaptureSubsystem::BeginWalkSmoke()
{
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = World
		? Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(World, 0))
		: nullptr;
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;

	if (!PC || !Explorer)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SMOKE_WALK_FAILED: no player pawn."));
		FinishTour();
		return;
	}

	PC->CloseMenu();
	WalkStart = Explorer->GetActorLocation();
	WalkElapsed = 0.0f;

	UE_LOG(LogUEGT2Diag, Log, TEXT("Walk smoke starting at %s"),
		*WalkStart.ToCompactString());

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(
		this, &UUEGT2CaptureSubsystem::TickWalkSmoke), 0.0f);
}

bool UUEGT2CaptureSubsystem::TickWalkSmoke(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	AUEGT2PlayerController* PC = World
		? Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(World, 0))
		: nullptr;
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	UUEGT2InputConfig* Config = PC ? PC->GetInputConfig() : nullptr;

	if (!PC || !Explorer || !Config || !Config->MoveAction)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SMOKE_WALK_FAILED: input config unavailable."));
		FinishTour();
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* Input =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!Input)
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_SMOKE_WALK_FAILED: no Enhanced Input subsystem."));
		FinishTour();
		return false;
	}

	// Straight forward. Goes through the real mapping context and binding.
	static const TArray<UInputModifier*> NoModifiers;
	static const TArray<UInputTrigger*> NoTriggers;
	Input->InjectInputForAction(Config->MoveAction,
		FInputActionValue(FVector2D(0.0f, 1.0f)), NoModifiers, NoTriggers);
	if (Config->SprintAction && WalkElapsed > 1.5f)
	{
		Input->InjectInputForAction(Config->SprintAction,
			FInputActionValue(true), NoModifiers, NoTriggers);
	}

	WalkElapsed += DeltaSeconds;
	if (WalkElapsed < 4.0f)
	{
		return true;
	}

	const FVector End = Explorer->GetActorLocation();
	const float Distance = FVector::Dist2D(WalkStart, End);
	UE_LOG(LogUEGT2Diag, Log,
		TEXT("UEGT2_SMOKE_WALK_COMPLETE distance=%.0f start=%s end=%s speed=%.0f"),
		Distance, *WalkStart.ToCompactString(), *End.ToCompactString(),
		Explorer->GetHorizontalSpeed());

	if (Distance < 300.0f)
	{
		UE_LOG(LogUEGT2Diag, Error,
			TEXT("UEGT2_SMOKE_WALK_FAILED: moved only %.0f cm in 4s; input is not reaching the pawn."),
			Distance);
	}
	FinishTour();
	return false;
}

void UUEGT2CaptureSubsystem::FinishTour()
{
	UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_CAPTURE_TOUR_COMPLETE (%d viewpoints)"), Tour.Num());

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}
	// Real-time ticker so this still fires when the pause menu paused the world.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
	{
		FPlatformMisc::RequestExit(false);
		return false;
	}), 1.6f);
}

void UUEGT2CaptureSubsystem::HandleScreenshotCaptured(int32 Width, int32 Height,
	const TArray<FColor>& Bitmap)
{
	if (PendingFileName.IsEmpty() || Width <= 0 || Height <= 0)
	{
		return;
	}
	const FString FileName = PendingFileName;
	PendingFileName.Reset();

	// Offscreen captures come back with zero alpha; force it opaque.
	TArray<FColor> Opaque = Bitmap;
	for (FColor& Pixel : Opaque)
	{
		Pixel.A = 255;
	}

	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (FFileHelper::SaveArrayToFile(Png, *FileName))
	{
		UE_LOG(LogUEGT2Diag, Log, TEXT("Wrote %dx%d screenshot: %s"), Width, Height, *FileName);
	}
	else
	{
		UE_LOG(LogUEGT2Diag, Error, TEXT("Failed to write screenshot: %s"), *FileName);
	}
}
