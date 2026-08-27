#include "UI/UEGT2HUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Dev/UEGT2DevModeSubsystem.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2InputConfig.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"

// Named rather than anonymous, and referenced qualified below. SUEGT2Menu.cpp
// ends its style block with a file-scope `using namespace UEGT2Menu;`, and a
// unity build concatenates that file ahead of this one. In an anonymous
// namespace these four would sit at global scope, where Ink/Muted/Accent are
// then ambiguous against the identically named UEGT2Menu ones (C2872).
namespace UEGT2Hud
{
	const FLinearColor Ink(0.95f, 0.96f, 0.98f, 1.0f);
	const FLinearColor Muted(0.72f, 0.75f, 0.79f, 1.0f);
	const FLinearColor Accent(0.45f, 0.82f, 0.84f, 1.0f);
	const FLinearColor Shade(0.0f, 0.0f, 0.0f, 0.55f);
}

AUEGT2HUD::AUEGT2HUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AUEGT2HUD::ShowMessage(const FText& Message, float Duration)
{
	CurrentMessage = Message;
	MessageExpiry = GetWorld() ? GetWorld()->GetTimeSeconds() + Duration : 0.0f;
	UE_LOG(LogUEGT2UI, Log, TEXT("HUD message: %s"), *Message.ToString());
}

void AUEGT2HUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(PlayerOwner);
	// The menu owns the screen when it is open.
	if (PC && PC->IsMenuOpen())
	{
		return;
	}

	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();

	const float CentreX = Canvas->ClipX * 0.5f;
	const float CentreY = Canvas->ClipY * 0.5f;

	const bool bHasFocus = Explorer && Explorer->GetInteraction()
		&& Explorer->GetInteraction()->GetFocusedActor() != nullptr;

	if (!Settings || Settings->GetShowCrosshair())
	{
		DrawCrosshair(CentreX, CentreY, bHasFocus);
	}
	if (!Settings || Settings->GetShowInteractPrompts())
	{
		DrawPrompt(CentreX, CentreY);
	}
	DrawMessage(CentreX, Canvas->ClipY);

	if (PC && PC->IsDiagnosticsVisible())
	{
		DrawDiagnostics(Explorer);
	}

	DrawDevStatus(Canvas->ClipX);
}

void AUEGT2HUD::DrawCrosshair(float CentreX, float CentreY, bool bHasFocus)
{
	const float Gap = bHasFocus ? 7.0f : 4.0f;
	const float Length = bHasFocus ? 7.0f : 4.0f;
	const float Thickness = 1.6f;
	const FLinearColor Colour = bHasFocus ? UEGT2Hud::Accent : FLinearColor(1, 1, 1, 0.55f);

	DrawLine(CentreX - Gap - Length, CentreY, CentreX - Gap, CentreY, Colour, Thickness);
	DrawLine(CentreX + Gap, CentreY, CentreX + Gap + Length, CentreY, Colour, Thickness);
	DrawLine(CentreX, CentreY - Gap - Length, CentreX, CentreY - Gap, Colour, Thickness);
	DrawLine(CentreX, CentreY + Gap, CentreX, CentreY + Gap + Length, Colour, Thickness);
}

void AUEGT2HUD::DrawPrompt(float CentreX, float CentreY)
{
	AUEGT2PlayerController* PC = Cast<AUEGT2PlayerController>(PlayerOwner);
	AUEGT2Character* Explorer = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr;
	if (!Explorer || !Explorer->GetInteraction())
	{
		return;
	}
	const FText Prompt = Explorer->GetInteraction()->GetFocusedPrompt();
	if (Prompt.IsEmpty())
	{
		return;
	}

	const FKey Key = UUEGT2InputConfig::GetEffectiveKey(EUEGT2InputSlot::Interact);
	const FString Text = FString::Printf(TEXT("[%s]  %s"),
		Key.IsValid() ? *Key.GetDisplayName().ToString() : TEXT("?"),
		*Prompt.ToString());

	UFont* Font = GEngine->GetMediumFont();
	float Width = 0.0f, Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, 1.0f);

	const float X = CentreX - Width * 0.5f;
	const float Y = CentreY + 62.0f;

	DrawRect(UEGT2Hud::Shade, X - 12.0f, Y - 6.0f, Width + 24.0f, Height + 12.0f);
	DrawText(Text, UEGT2Hud::Ink, X, Y, Font, 1.0f, false);
}

void AUEGT2HUD::DrawMessage(float CentreX, float ScreenHeight)
{
	if (CurrentMessage.IsEmpty() || !GetWorld() || GetWorld()->GetTimeSeconds() > MessageExpiry)
	{
		return;
	}
	const FString Text = CurrentMessage.ToString();
	UFont* Font = GEngine->GetMediumFont();
	float Width = 0.0f, Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, 1.0f);

	const float Remaining = MessageExpiry - GetWorld()->GetTimeSeconds();
	const float Alpha = FMath::Clamp(Remaining, 0.0f, 1.0f);
	const float X = CentreX - Width * 0.5f;
	const float Y = ScreenHeight - 140.0f;

	DrawRect(FLinearColor(UEGT2Hud::Shade.R, UEGT2Hud::Shade.G, UEGT2Hud::Shade.B, UEGT2Hud::Shade.A * Alpha),
		X - 14.0f, Y - 7.0f, Width + 28.0f, Height + 14.0f);
	DrawText(Text, FLinearColor(UEGT2Hud::Ink.R, UEGT2Hud::Ink.G, UEGT2Hud::Ink.B, Alpha), X, Y, Font, 1.0f, false);
}

void AUEGT2HUD::DrawDevStatus(float ScreenWidth)
{
	const UUEGT2DevModeSubsystem* Dev = UUEGT2DevModeSubsystem::Get(GetWorld());
	if (!Dev || !Dev->IsDevModeEnabled())
	{
		return;
	}

	TArray<FString> Parts;
	if (Dev->IsGodMode())   { Parts.Add(TEXT("god")); }
	if (Dev->IsNoclipEnabled()) { Parts.Add(TEXT("noclip")); }
	else if (Dev->IsFlyEnabled()) { Parts.Add(TEXT("fly")); }
	if (Dev->GetSpeedMultiplier() > 1.01f)
	{
		Parts.Add(FString::Printf(TEXT("%.0fx"), Dev->GetSpeedMultiplier()));
	}
	if (!FMath::IsNearlyEqual(Dev->GetGameSpeed(), 1.0f))
	{
		Parts.Add(FString::Printf(TEXT("time %.2fx"), Dev->GetGameSpeed()));
	}

	const float Hours = Dev->GetTimeOfDay();
	const int32 Whole = FMath::Clamp(FMath::FloorToInt(Hours), 0, 23);
	const int32 Minutes = FMath::Clamp(FMath::RoundToInt((Hours - Whole) * 60.0f), 0, 59);

	const FString Text = FString::Printf(TEXT("DEV  %02d:%02d  %s%s%s"),
		Whole, Minutes,
		*GetWeatherDisplayName(Dev->GetWeather()).ToString(),
		Parts.Num() > 0 ? TEXT("  ") : TEXT(""),
		*FString::Join(Parts, TEXT("  ")));

	UFont* Font = GEngine->GetSmallFont();
	float Width = 0.0f, Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, 1.0f);

	const float X = ScreenWidth - Width - 28.0f;
	const float Y = 20.0f;
	DrawRect(UEGT2Hud::Shade, X - 10.0f, Y - 5.0f, Width + 20.0f, Height + 10.0f);
	DrawText(Text, UEGT2Hud::Accent, X, Y, Font, 1.0f, false);
}

void AUEGT2HUD::DrawDiagnostics(AUEGT2Character* Explorer)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float DeltaMs = World->GetDeltaSeconds() * 1000.0f;
	SmoothedDeltaMs = FMath::Lerp(SmoothedDeltaMs, DeltaMs, 0.08f);
	const float Fps = SmoothedDeltaMs > KINDA_SMALL_NUMBER ? 1000.0f / SmoothedDeltaMs : 0.0f;

	TArray<FString> Lines;
	Lines.Add(TEXT("FAIRHAVEN DIAGNOSTICS  (F3)"));
	Lines.Add(FString::Printf(TEXT("%.1f fps   %.2f ms"), Fps, SmoothedDeltaMs));

	if (Explorer)
	{
		const FVector Location = Explorer->GetActorLocation();
		Lines.Add(FString::Printf(TEXT("pos  X %.0f  Y %.0f  Z %.0f"), Location.X, Location.Y, Location.Z));
		Lines.Add(FString::Printf(TEXT("     %.0f m N   %.0f m E   %.0f m up"),
			Location.X / 100.0f, Location.Y / 100.0f, Location.Z / 100.0f));
		Lines.Add(FString::Printf(TEXT("speed %.0f cm/s%s"),
			Explorer->GetHorizontalSpeed(), Explorer->IsSprinting() ? TEXT("  [sprint]") : TEXT("")));

		if (const UUEGT2InteractionComponent* Interaction = Explorer->GetInteraction())
		{
			const AActor* Focus = Interaction->GetFocusedActor();
			Lines.Add(FString::Printf(TEXT("focus %s"), Focus ? *Focus->GetName() : TEXT("-")));
		}
	}

	if (const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get())
	{
		Lines.Add(FString::Printf(TEXT("quality  view %d  shadow %d  gi %d  refl %d"),
			Settings->GetViewDistanceQuality(), Settings->GetShadowQuality(),
			Settings->GetGlobalIlluminationQuality(), Settings->GetReflectionQuality()));
		Lines.Add(FString::Printf(TEXT("res scale %.0f%%   fov %.0f"),
			Settings->GetResolutionScalePercent(), Settings->GetFieldOfView()));
	}

	UFont* Font = GEngine->GetSmallFont();
	const float LineHeight = 15.0f;
	const float PanelWidth = 340.0f;
	const float PanelHeight = LineHeight * Lines.Num() + 18.0f;

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 16.0f, 16.0f, PanelWidth, PanelHeight);

	float Y = 25.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawText(Lines[Index], Index == 0 ? UEGT2Hud::Accent : UEGT2Hud::Muted, 28.0f, Y, Font, 1.0f, false);
		Y += LineHeight;
	}
}
