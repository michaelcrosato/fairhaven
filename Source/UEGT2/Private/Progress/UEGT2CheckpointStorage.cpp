#include "Progress/UEGT2CheckpointStorage.h"

#include "Kismet/GameplayStatics.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"

namespace UEGT2CheckpointStorage
{
	class FPlatformStorage final : public IUEGT2CheckpointStorage
	{
	public:
		virtual bool Exists(const FString& Slot) override { return UGameplayStatics::DoesSaveGameExist(Slot, 0); }
		virtual bool Read(const FString& Slot, TArray<uint8>& Bytes) override { return UGameplayStatics::LoadDataFromSlot(Bytes, Slot, 0); }
		virtual bool Write(const FString& Slot, const TArray<uint8>& Bytes) override { return UGameplayStatics::SaveDataToSlot(Bytes, Slot, 0); }
		virtual void ExistsAsync(const FString& Slot, FExistsComplete Complete) override
		{
			check(IsInGameThread());
			ISaveGameSystem* System = IPlatformFeaturesModule::Get().GetSaveGameSystem();
			if (!System) { Complete(EPresence::Unreadable); return; }
			System->DoesSaveGameExistAsync(*Slot, FPlatformMisc::GetPlatformUserForUserIndex(0),
				[Complete = MoveTemp(Complete)](const FString&, FPlatformUserId, ISaveGameSystem::ESaveExistsResult Result)
				{
					check(IsInGameThread());
					Complete(Result == ISaveGameSystem::ESaveExistsResult::OK ? EPresence::Present
						: Result == ISaveGameSystem::ESaveExistsResult::DoesNotExist ? EPresence::Missing : EPresence::Unreadable);
				});
		}
		virtual void ReadAsync(const FString& Slot, FReadComplete Complete) override
		{
			check(IsInGameThread());
			ISaveGameSystem* System = IPlatformFeaturesModule::Get().GetSaveGameSystem();
			if (!System) { Complete(false, TArray<uint8>()); return; }
			System->LoadGameAsync(false, *Slot, FPlatformMisc::GetPlatformUserForUserIndex(0),
				[Complete = MoveTemp(Complete)](const FString&, FPlatformUserId, bool bSuccess, const TArray<uint8>& Bytes)
				{
					check(IsInGameThread());
					Complete(bSuccess, Bytes);
				});
		}
		virtual void WriteAsync(const FString& Slot, const TArray<uint8>& Bytes, FWriteComplete Complete) override
		{
			check(IsInGameThread());
			ISaveGameSystem* System = IPlatformFeaturesModule::Get().GetSaveGameSystem();
			if (!System) { Complete(false); return; }
			TSharedRef<TArray<uint8>> Data = MakeShared<TArray<uint8>>(Bytes);
			System->SaveGameAsync(false, *Slot, FPlatformMisc::GetPlatformUserForUserIndex(0), Data,
				[Complete = MoveTemp(Complete)](const FString&, FPlatformUserId, bool bSuccess)
				{
					check(IsInGameThread());
					Complete(bSuccess);
				});
		}
	};
}

TSharedRef<IUEGT2CheckpointStorage> UEGT2CreateCheckpointStorage()
{
	return MakeShared<UEGT2CheckpointStorage::FPlatformStorage>();
}
