// Fairhaven (UEGT2) - process-lifetime hooks.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "UEGT2GameInstance.generated.h"

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API UUEGT2GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;
};
