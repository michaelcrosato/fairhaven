// Fairhaven (UEGT2) - wires the default pawn, controller and HUD together.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UEGT2GameMode.generated.h"

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AUEGT2GameMode();

	virtual void StartPlay() override;
};
