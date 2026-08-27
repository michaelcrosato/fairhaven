#include "Dev/UEGT2DevModeSubsystem.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2PlayerController.h"
#include "UEGT2LogChannels.h"
#include "UI/UEGT2HUD.h"
#include "World/UEGT2SkyController.h"

#define LOCTEXT_NAMESPACE "UEGT2Dev"

namespace UEGT2Dev
{
	/** Trace span for finding the ground under a teleport target. */
	const float TraceTop = 60000.0f;
	const float TraceBottom = -15000.0f;

	/** Read "1"/"0"/"true"/"on" out of a console argument, defaulting to true. */
	bool ArgToBool(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			return true;
		}
		const FString& Value = Args[0];
		return Value == TEXT("1") || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("on"), ESearchCase::IgnoreCase);
	}

	UUEGT2DevModeSubsystem* FromWorld(UWorld* World)
	{
		return World ? World->GetSubsystem<UUEGT2DevModeSubsystem>() : nullptr;
	}
}

UUEGT2DevModeSubsystem* UUEGT2DevModeSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UUEGT2DevModeSubsystem>() : nullptr;
}

void UUEGT2DevModeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RegisterConsoleCommands();
}

void UUEGT2DevModeSubsystem::Deinitialize()
{
	// Time dilation is global engine state; leaving it at 0.2 across a level
	// change would look like a hang.
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
	}
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
AUEGT2Character* UUEGT2DevModeSubsystem::GetCharacter() const
{
	return Cast<AUEGT2Character>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
}

AUEGT2PlayerController* UUEGT2DevModeSubsystem::GetPC() const
{
	return Cast<AUEGT2PlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0));
}

AUEGT2SkyController* UUEGT2DevModeSubsystem::GetSky() const
{
	return AUEGT2SkyController::Get(GetWorld());
}

bool UUEGT2DevModeSubsystem::HasPlayer() const
{
	return GetCharacter() != nullptr;
}

FVector UUEGT2DevModeSubsystem::GetPlayerLocation() const
{
	const AUEGT2Character* Character = GetCharacter();
	return Character ? Character->GetActorLocation() : FVector::ZeroVector;
}

void UUEGT2DevModeSubsystem::RunConsole(const TCHAR* Command) const
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PC->ConsoleCommand(Command, /*bWriteToLog*/ false);
	}
}

void UUEGT2DevModeSubsystem::Notify(const FString& Message) const
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (AUEGT2HUD* HUD = Cast<AUEGT2HUD>(PC->GetHUD()))
		{
			HUD->ShowMessage(FText::FromString(Message), 2.5f);
		}
	}
	UE_LOG(LogUEGT2Dev, Log, TEXT("%s"), *Message);
}

// ---------------------------------------------------------------------------
// Master
// ---------------------------------------------------------------------------
void UUEGT2DevModeSubsystem::SetDevModeEnabled(bool bEnabled)
{
	if (bDevModeEnabled == bEnabled)
	{
		return;
	}
	bDevModeEnabled = bEnabled;

	if (!bEnabled)
	{
		// Player and display state is restored; time of day and weather are
		// deliberately left alone. Those are world state, and snapping the sun
		// back under someone who just set it is surprising.
		if (AUEGT2Character* Character = GetCharacter())
		{
			Character->ClearDevMovement();
		}
		SetViewMode(EUEGT2ViewMode::Lit);
		SetShowCollision(false);
		SetStatFps(false);
		SetStatUnit(false);
		SetDrawInteractionProbe(false);
		SetGameSpeed(1.0f);
	}
	UE_LOG(LogUEGT2Dev, Log, TEXT("Dev mode %s."), bEnabled ? TEXT("on") : TEXT("off"));
}

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
void UUEGT2DevModeSubsystem::SetGodMode(bool bEnabled)
{
	if (AUEGT2Character* Character = GetCharacter()) { Character->SetGodMode(bEnabled); }
}

bool UUEGT2DevModeSubsystem::IsGodMode() const
{
	const AUEGT2Character* Character = GetCharacter();
	return Character && Character->IsGodMode();
}

void UUEGT2DevModeSubsystem::SetFlyEnabled(bool bEnabled)
{
	if (AUEGT2Character* Character = GetCharacter()) { Character->SetFlyEnabled(bEnabled); }
}

bool UUEGT2DevModeSubsystem::IsFlyEnabled() const
{
	const AUEGT2Character* Character = GetCharacter();
	return Character && Character->IsFlyEnabled();
}

void UUEGT2DevModeSubsystem::SetNoclipEnabled(bool bEnabled)
{
	if (AUEGT2Character* Character = GetCharacter()) { Character->SetNoclipEnabled(bEnabled); }
}

bool UUEGT2DevModeSubsystem::IsNoclipEnabled() const
{
	const AUEGT2Character* Character = GetCharacter();
	return Character && Character->IsNoclipEnabled();
}

void UUEGT2DevModeSubsystem::SetSpeedMultiplier(float Multiplier)
{
	if (AUEGT2Character* Character = GetCharacter()) { Character->SetSpeedMultiplier(Multiplier); }
}

float UUEGT2DevModeSubsystem::GetSpeedMultiplier() const
{
	const AUEGT2Character* Character = GetCharacter();
	return Character ? Character->GetSpeedMultiplier() : 1.0f;
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
void UUEGT2DevModeSubsystem::SetTimeOfDay(float Hours)
{
	if (AUEGT2SkyController* Sky = GetSky()) { Sky->SetTimeOfDay(Hours); }
}

float UUEGT2DevModeSubsystem::GetTimeOfDay() const
{
	const AUEGT2SkyController* Sky = GetSky();
	return Sky ? Sky->GetTimeOfDay() : 0.0f;
}

void UUEGT2DevModeSubsystem::SetDayNightCycleEnabled(bool bEnabled)
{
	if (AUEGT2SkyController* Sky = GetSky()) { Sky->SetDayNightCycleEnabled(bEnabled); }
}

bool UUEGT2DevModeSubsystem::IsDayNightCycleEnabled() const
{
	const AUEGT2SkyController* Sky = GetSky();
	return Sky && Sky->IsDayNightCycleEnabled();
}

void UUEGT2DevModeSubsystem::SetDayLengthMinutes(float Minutes)
{
	if (AUEGT2SkyController* Sky = GetSky()) { Sky->SetDayLengthMinutes(Minutes); }
}

float UUEGT2DevModeSubsystem::GetDayLengthMinutes() const
{
	const AUEGT2SkyController* Sky = GetSky();
	return Sky ? Sky->GetDayLengthMinutes() : 0.0f;
}

void UUEGT2DevModeSubsystem::SetWeather(EUEGT2Weather NewWeather)
{
	if (AUEGT2SkyController* Sky = GetSky()) { Sky->SetWeather(NewWeather); }
}

EUEGT2Weather UUEGT2DevModeSubsystem::GetWeather() const
{
	const AUEGT2SkyController* Sky = GetSky();
	return Sky ? Sky->GetWeather() : EUEGT2Weather::Clear;
}

void UUEGT2DevModeSubsystem::SetFogDensity(float Density)
{
	if (AUEGT2SkyController* Sky = GetSky()) { Sky->SetFogDensityOverride(Density); }
}

float UUEGT2DevModeSubsystem::GetFogDensity() const
{
	const AUEGT2SkyController* Sky = GetSky();
	return Sky ? Sky->GetEffectiveFogDensity() : 0.0f;
}

void UUEGT2DevModeSubsystem::SetGameSpeed(float Scale)
{
	GameSpeed = FMath::Clamp(Scale, 0.1f, 5.0f);
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, GameSpeed);
	}
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
void UUEGT2DevModeSubsystem::SetViewMode(EUEGT2ViewMode Mode)
{
	ViewMode = Mode;
	switch (Mode)
	{
	case EUEGT2ViewMode::Unlit:     RunConsole(TEXT("viewmode unlit")); break;
	case EUEGT2ViewMode::Wireframe: RunConsole(TEXT("viewmode wireframe")); break;
	default:                        RunConsole(TEXT("viewmode lit")); break;
	}
}

void UUEGT2DevModeSubsystem::SetDiagnosticsVisible(bool bVisible)
{
	if (AUEGT2PlayerController* PC = GetPC()) { PC->SetDiagnosticsVisible(bVisible); }
}

bool UUEGT2DevModeSubsystem::IsDiagnosticsVisible() const
{
	const AUEGT2PlayerController* PC = GetPC();
	return PC && PC->IsDiagnosticsVisible();
}

void UUEGT2DevModeSubsystem::SetShowCollision(bool bVisible)
{
	if (bShowCollision == bVisible)
	{
		return;
	}
	bShowCollision = bVisible;
	// "show collision" toggles rather than taking a value, so it must only be
	// sent when the state actually changes.
	RunConsole(TEXT("show collision"));
}

void UUEGT2DevModeSubsystem::SetStatFps(bool bVisible)
{
	if (bStatFps == bVisible)
	{
		return;
	}
	bStatFps = bVisible;
	// "stat fps" toggles, so only send it when the state actually changes.
	RunConsole(TEXT("stat fps"));
}

void UUEGT2DevModeSubsystem::SetStatUnit(bool bVisible)
{
	if (bStatUnit == bVisible)
	{
		return;
	}
	bStatUnit = bVisible;
	RunConsole(TEXT("stat unit"));
}

void UUEGT2DevModeSubsystem::SetDrawInteractionProbe(bool bVisible)
{
	bDrawInteractionProbe = bVisible;
	if (IConsoleVariable* CVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("uegt2.Debug.DrawInteraction")))
	{
		CVar->Set(bVisible ? 1 : 0, ECVF_SetByConsole);
	}
}

// ---------------------------------------------------------------------------
// Teleport
// ---------------------------------------------------------------------------
bool UUEGT2DevModeSubsystem::TeleportToGround(const FVector2D& WorldXY, float HeightMetres,
	float Yaw, float Pitch)
{
	AUEGT2Character* Character = GetCharacter();
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return false;
	}

	const FVector Start(WorldXY.X, WorldXY.Y, UEGT2Dev::TraceTop);
	const FVector End(WorldXY.X, WorldXY.Y, UEGT2Dev::TraceBottom);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UEGT2DevTeleport), /*bTraceComplex*/ true);
	Params.AddIgnoredActor(Character);

	float GroundZ = 0.0f;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		GroundZ = Hit.ImpactPoint.Z;
	}

	const FVector Target(WorldXY.X, WorldXY.Y, GroundZ + HeightMetres * 100.0f);

	// Teleport rather than SetActorLocation so the movement component's state
	// is reset; arriving mid-fall with stale velocity throws you off the cliff.
	Character->TeleportTo(Target, Character->GetActorRotation(), false, true);
	if (AController* OwningController = Character->GetController())
	{
		OwningController->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
	}
	return true;
}

bool UUEGT2DevModeSubsystem::TeleportToViewpoint(int32 Index)
{
	const TArray<FUEGT2Viewpoint>& Tour = UUEGT2CaptureSubsystem::GetTour();
	if (!Tour.IsValidIndex(Index))
	{
		return false;
	}
	const FUEGT2Viewpoint& Point = Tour[Index];
	if (!TeleportToGround(Point.Location, Point.HeightAboveGround, Point.Yaw, Point.Pitch))
	{
		return false;
	}
	Notify(FString::Printf(TEXT("Teleported to %s"), *Point.Name.ToString()));
	return true;
}

bool UUEGT2DevModeSubsystem::TeleportToViewpointNamed(const FString& Name)
{
	const TArray<FUEGT2Viewpoint>& Tour = UUEGT2CaptureSubsystem::GetTour();
	for (int32 Index = 0; Index < Tour.Num(); ++Index)
	{
		if (Tour[Index].Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return TeleportToViewpoint(Index);
		}
	}
	UE_LOG(LogUEGT2Dev, Warning, TEXT("No viewpoint called '%s'."), *Name);
	return false;
}

void UUEGT2DevModeSubsystem::SavePosition()
{
	const AUEGT2Character* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	SavedLocation = Character->GetActorLocation();
	SavedRotation = Character->GetController()
		? Character->GetController()->GetControlRotation()
		: Character->GetActorRotation();
	bHasSavedPosition = true;
	Notify(TEXT("Position saved"));
}

bool UUEGT2DevModeSubsystem::RestorePosition()
{
	AUEGT2Character* Character = GetCharacter();
	if (!Character || !bHasSavedPosition)
	{
		return false;
	}
	Character->TeleportTo(SavedLocation, Character->GetActorRotation(), false, true);
	if (AController* OwningController = Character->GetController())
	{
		OwningController->SetControlRotation(SavedRotation);
	}
	Notify(TEXT("Position restored"));
	return true;
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
void UUEGT2DevModeSubsystem::RegisterConsoleCommands()
{
	static FAutoConsoleCommandWithWorldAndArgs DevCommand(
		TEXT("uegt2.Dev"),
		TEXT("Enable or disable dev mode: uegt2.Dev 1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UUEGT2DevModeSubsystem* Dev = UEGT2Dev::FromWorld(World))
				{
					Dev->SetDevModeEnabled(UEGT2Dev::ArgToBool(Args));
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs GodCommand(
		TEXT("uegt2.Dev.God"),
		TEXT("Invulnerable and unlimited air jumps: uegt2.Dev.God 1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UUEGT2DevModeSubsystem* Dev = UEGT2Dev::FromWorld(World))
				{
					Dev->SetGodMode(UEGT2Dev::ArgToBool(Args));
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs FlyCommand(
		TEXT("uegt2.Dev.Fly"),
		TEXT("Free flight: uegt2.Dev.Fly 1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UUEGT2DevModeSubsystem* Dev = UEGT2Dev::FromWorld(World))
				{
					Dev->SetFlyEnabled(UEGT2Dev::ArgToBool(Args));
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs NoclipCommand(
		TEXT("uegt2.Dev.Noclip"),
		TEXT("Fly through geometry: uegt2.Dev.Noclip 1"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (UUEGT2DevModeSubsystem* Dev = UEGT2Dev::FromWorld(World))
				{
					Dev->SetNoclipEnabled(UEGT2Dev::ArgToBool(Args));
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs SpeedCommand(
		TEXT("uegt2.Dev.Speed"),
		TEXT("Movement speed multiplier, 1-50: uegt2.Dev.Speed 12"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1)
				{
					return;
				}
				if (UUEGT2DevModeSubsystem* Dev = UEGT2Dev::FromWorld(World))
				{
					Dev->SetSpeedMultiplier(FCString::Atof(*Args[0]));
				}
			}));

	static FAutoConsoleCommandWithWorldAndArgs TeleportCommand(
		TEXT("uegt2.Dev.Teleport"),
		TEXT("Teleport to a tour viewpoint: uegt2.Dev.Teleport Vista"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1)
				{
					return;
				}
				if (UUEGT2DevModeSubsystem* Dev = UEGT2Dev::FromWorld(World))
				{
					Dev->TeleportToViewpointNamed(Args[0]);
				}
			}));
}

#undef LOCTEXT_NAMESPACE
