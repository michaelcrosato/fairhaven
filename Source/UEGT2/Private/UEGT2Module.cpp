#include "Modules/ModuleManager.h"
#include "UEGT2LogChannels.h"

class FUEGT2Module : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogUEGT2, Log, TEXT("UEGT2 runtime module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogUEGT2, Log, TEXT("UEGT2 runtime module shut down."));
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FUEGT2Module, UEGT2, "UEGT2");
