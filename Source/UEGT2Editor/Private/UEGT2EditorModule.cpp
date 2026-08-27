#include "Modules/ModuleManager.h"
#include "UEGT2LogChannels.h"

// Editor-only module. Everything that authors assets, imports landscape data or
// assembles the world lives here so the shipped game never links editor code.
class FUEGT2EditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogUEGT2, Log, TEXT("UEGT2Editor authoring module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogUEGT2, Log, TEXT("UEGT2Editor authoring module shut down."));
	}
};

IMPLEMENT_MODULE(FUEGT2EditorModule, UEGT2Editor);
