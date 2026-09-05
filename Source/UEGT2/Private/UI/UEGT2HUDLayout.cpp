#include "UI/UEGT2HUDLayout.h"

namespace UEGT2HUDLayout
{
	static bool ValidSize(const FVector2D& Size)
	{
		return FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) && Size.X > 0.0 && Size.Y > 0.0;
	}

	FUEGT2HUDLayout Resolve(const FVector2D& Viewport, float RequestedScale, bool bGateEnabled)
	{
		FUEGT2HUDLayout Result;
		if (!ValidSize(Viewport)) { return Result; }
		Result.Viewport = Viewport;
		Result.bEnhanced = bGateEnabled && FMath::IsFinite(RequestedScale) && RequestedScale > 1.0f;
		if (Result.bEnhanced)
		{
			// Below the offered 720p minimum, reduce enlargement before laying out
			// wrapped panels. Never make the default HUD smaller than its baseline.
			const double Fit = FMath::Min(Result.Viewport.X / 800.0, Result.Viewport.Y / 480.0);
			Result.Scale = static_cast<float>(FMath::Clamp(FMath::Min(static_cast<double>(RequestedScale), Fit), 1.0, 1.5));
		}
		return Result;
	}

	FBox2D AnchorPanel(const FUEGT2HUDLayout& Layout, const FVector2D& Size,
		EUEGT2HUDAnchor Anchor, const FVector2D& Inset)
	{
		if (!ValidSize(Size) || !FMath::IsFinite(Inset.X) || !FMath::IsFinite(Inset.Y)) { return FBox2D(ForceInit); }
		const FVector2D Extent = Size * Layout.Scale;
		FVector2D Origin = Inset;
		if (Anchor == EUEGT2HUDAnchor::BottomRight) { Origin.X = Layout.Viewport.X - Inset.X - Extent.X; }
		if (Anchor != EUEGT2HUDAnchor::TopLeft) { Origin.Y = Layout.Viewport.Y - Inset.Y - Extent.Y; }
		return FBox2D(Origin, Origin + Extent);
	}

	float BottomLeftMaxWidth(const FUEGT2HUDLayout& Layout, float RightWidth, bool bRightVisible)
	{
		if (!FMath::IsFinite(RightWidth) || RightWidth < 0.0f) { return 0.0f; }
		const double Available = Layout.Viewport.X - 48.0 - (bRightVisible ? RightWidth * Layout.Scale + 20.0 : 0.0);
		return static_cast<float>(FMath::Max(1.0, Available / Layout.Scale));
	}

	FBox2D PlaceMessage(const FUEGT2HUDLayout& Layout, const FVector2D& Size,
		const TArray<FBox2D>& BottomPanels)
	{
		if (!ValidSize(Size)) { return FBox2D(ForceInit); }
		const FVector2D Extent = Size * Layout.Scale;
		FVector2D Origin((Layout.Viewport.X - Extent.X) * 0.5, Layout.Viewport.Y - 147.0 * Layout.Scale);
		if (Layout.bEnhanced)
		{
			for (const FBox2D& BottomBounds : BottomPanels)
			{
				if (BottomBounds.bIsValid && Origin.X < BottomBounds.Max.X && Origin.X + Extent.X > BottomBounds.Min.X)
				{
					Origin.Y = FMath::Min(Origin.Y, BottomBounds.Min.Y - Extent.Y - 12.0);
				}
			}
			Origin.Y = FMath::Clamp(Origin.Y, 8.0, FMath::Max(8.0, Layout.Viewport.Y - Extent.Y - 8.0));
		}
		return FBox2D(Origin, Origin + Extent);
	}

	bool PlaceBubble(const FUEGT2HUDLayout& Layout, const FVector2D& ScreenAnchor,
		const FVector2D& Size, const TArray<FBox2D>& Occupied, FBox2D& OutBounds)
	{
		OutBounds = FBox2D(ForceInit);
		if (!ValidSize(Size) || !FMath::IsFinite(ScreenAnchor.X) || !FMath::IsFinite(ScreenAnchor.Y)) { return false; }
		const FVector2D Extent = Size * Layout.Scale;
		constexpr double Margin = 8.0;
		if (Extent.X > Layout.Viewport.X - 2.0 * Margin || Extent.Y > Layout.Viewport.Y - 2.0 * Margin) { return false; }
		FVector2D Origin(
			FMath::Clamp(ScreenAnchor.X - Extent.X * 0.5, Margin, Layout.Viewport.X - Extent.X - Margin),
			FMath::Clamp(ScreenAnchor.Y - Extent.Y, Margin, Layout.Viewport.Y - Extent.Y - Margin));
		for (int32 Attempt = 0; Attempt <= 8; ++Attempt)
		{
			bool bClear = true;
			for (const FBox2D& Taken : Occupied)
			{
				if (Taken.bIsValid && Origin.X < Taken.Max.X && Origin.X + Extent.X > Taken.Min.X
					&& Origin.Y < Taken.Max.Y && Origin.Y + Extent.Y > Taken.Min.Y)
				{
					Origin.Y = Taken.Min.Y - Extent.Y - 6.0 * Layout.Scale;
					bClear = false;
					break;
				}
			}
			if (Origin.Y < Margin) { return false; }
			if (bClear) { OutBounds = FBox2D(Origin, Origin + Extent); return true; }
		}
		return false;
	}
}
