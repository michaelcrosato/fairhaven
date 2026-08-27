#include "Core/UEGT2GameMode.h"

#include "Player/UEGT2Character.h"
#include "Player/UEGT2PlayerController.h"
#include "UEGT2LogChannels.h"
#include "UI/UEGT2HUD.h"

AUEGT2GameMode::AUEGT2GameMode()
{
	DefaultPawnClass = AUEGT2Character::StaticClass();
	PlayerControllerClass = AUEGT2PlayerController::StaticClass();
	HUDClass = AUEGT2HUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

void AUEGT2GameMode::StartPlay()
{
	Super::StartPlay();
	UE_LOG(LogUEGT2, Log, TEXT("Fairhaven game mode ready."));
}
