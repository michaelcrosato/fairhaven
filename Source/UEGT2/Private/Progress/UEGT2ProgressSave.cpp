#include "Progress/UEGT2ProgressSave.h"

#include "Player/UEGT2NeedsComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"

#define LOCTEXT_NAMESPACE "UEGT2ProgressSave"

namespace UEGT2ProgressEncoding
{
	constexpr uint32 Magic = 0x50474846; // FHGP, little-endian Win64
	constexpr uint32 Version = 1;
	constexpr int32 HeaderBytes = 4 * sizeof(uint32);
}

bool UUEGT2ProgressSave::Encode(TArray<uint8>& OutBytes)
{
	using namespace UEGT2ProgressEncoding;
	TArray<uint8> Payload;
	if (!UGameplayStatics::SaveGameToMemory(this, Payload) || Payload.IsEmpty()
		|| Payload.Num() > MaxEncodedBytes - HeaderBytes)
	{
		return false;
	}
	const uint32 Header[] = { Magic, Version, static_cast<uint32>(Payload.Num()),
		FCrc::MemCrc32(Payload.GetData(), Payload.Num()) };
	OutBytes.SetNumUninitialized(HeaderBytes + Payload.Num());
	FMemory::Memcpy(OutBytes.GetData(), Header, HeaderBytes);
	FMemory::Memcpy(OutBytes.GetData() + HeaderBytes, Payload.GetData(), Payload.Num());
	return true;
}

UUEGT2ProgressSave* UUEGT2ProgressSave::Decode(const TArray<uint8>& Bytes, FText& OutReason)
{
	using namespace UEGT2ProgressEncoding;
	OutReason = LOCTEXT("DamagedEnvelope", "The journey checkpoint is damaged or incomplete.");
	if (Bytes.Num() <= HeaderBytes || Bytes.Num() > MaxEncodedBytes) { return nullptr; }
	uint32 Header[4];
	FMemory::Memcpy(Header, Bytes.GetData(), HeaderBytes);
	const int32 PayloadBytes = Bytes.Num() - HeaderBytes;
	if (Header[0] != Magic || Header[1] != Version || Header[2] != static_cast<uint32>(PayloadBytes)
		|| Header[3] != FCrc::MemCrc32(Bytes.GetData() + HeaderBytes, PayloadBytes))
	{
		return nullptr;
	}
	// UE's generic reader trusts FString/array lengths inside a save. A damaged
	// file must never reach it: size limits alone cannot bound those allocations.
	TArray<uint8> Payload;
	Payload.Append(Bytes.GetData() + HeaderBytes, PayloadBytes);
	UUEGT2ProgressSave* Save = Cast<UUEGT2ProgressSave>(UGameplayStatics::LoadGameFromMemory(Payload));
	if (Save) { OutReason = FText::GetEmpty(); }
	return Save;
}

bool UUEGT2ProgressSave::Validate(const FString& ExpectedMap,
	const TSet<FName>& KnownLandmarks, FText& OutReason) const
{
	if (SchemaVersion != CurrentSchemaVersion || ContentRevision != CurrentContentRevision
		|| ExpectedMap.IsEmpty() || MapPackageName != ExpectedMap)
	{
		OutReason = LOCTEXT("Incompatible", "This checkpoint belongs to a different map or game version.");
		return false;
	}
	// Fairhaven is four kilometres across. This deliberately generous bound
	// rejects corrupt coordinates without pretending that a valid point is safe
	// to occupy; the live standing-capsule query makes that decision separately.
	const bool bPositionValid = FMath::IsFinite(PlayerLocation.X) && FMath::IsFinite(PlayerLocation.Y)
		&& FMath::IsFinite(PlayerLocation.Z) && PlayerLocation.GetAbsMax() <= 10000000.0;
	const bool bViewValid = FMath::IsFinite(ViewRotation.Pitch) && FMath::IsFinite(ViewRotation.Yaw)
		&& FMath::IsFinite(ViewRotation.Roll) && FMath::Abs(ViewRotation.Pitch) <= 360.0
		&& FMath::Abs(ViewRotation.Yaw) <= 360.0 && FMath::Abs(ViewRotation.Roll) <= 360.0;
	if (Sequence <= 0 || Sequence == MAX_int64 || !bPositionValid || !bViewValid
		|| !UUEGT2NeedsComponent::IsValidProgress(Needs, Purse, Trade)
		|| DayIndex < 0 || DayIndex > 1000000 || !FMath::IsFinite(Hour) || Hour < 0.0f || Hour >= 24.0f
		|| static_cast<uint8>(Weather) >= static_cast<uint8>(EUEGT2Weather::Count))
	{
		OutReason = LOCTEXT("InvalidValues", "The checkpoint contains invalid player or calendar data.");
		return false;
	}
	TSet<FName> Seen;
	for (FName Id : DiscoveredLandmarks)
	{
		if (Id.IsNone() || !KnownLandmarks.Contains(Id) || Seen.Contains(Id))
		{
			OutReason = LOCTEXT("InvalidLandmarks", "The checkpoint's surveyed places do not match this world.");
			return false;
		}
		Seen.Add(Id);
	}
	OutReason = FText::GetEmpty();
	return true;
}

#undef LOCTEXT_NAMESPACE
