#include "Core/UEGT2GameInstance.h"

#include "Misc/App.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"

void UUEGT2GameInstance::Init()
{
	Super::Init();

	// Load and apply the player's settings once, before any world exists.
	if (UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		Settings->LoadSettings();
		Settings->ApplyNonResolutionSettings();
	}

	UE_LOG(LogUEGT2, Log, TEXT("Fairhaven boot: build=%s"), FApp::GetBuildVersion());
}

void UUEGT2GameInstance::Shutdown()
{
	if (UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		Settings->SaveSettings();
	}
	UE_LOG(LogUEGT2, Log, TEXT("Fairhaven shutting down."));
	Super::Shutdown();
}
