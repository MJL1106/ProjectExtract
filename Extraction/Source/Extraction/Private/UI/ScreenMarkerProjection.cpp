// FScreenMarkerViewContext / FScreenMarkerProjection implementation.

#include "UI/ScreenMarkerProjection.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Half-extent floor -- a tiny or mis-reported viewport must never yield a zero/inverted rect. */
	constexpr double MinHalfExtent = 1.0;

	/** Unreal units per metre. */
	constexpr double UnrealUnitsPerMetre = 100.0;

	/** Ray/rectangle intersection: how far along Dir the padded rectangle boundary sits. */
	double RectangleEdgeScale(const FVector2D& Dir, const FVector2D& HalfExtents)
	{
		const double AbsX = FMath::Abs(Dir.X);
		const double AbsY = FMath::Abs(Dir.Y);
		const double ScaleX = AbsX > UE_DOUBLE_KINDA_SMALL_NUMBER
			? HalfExtents.X / AbsX : TNumericLimits<double>::Max();
		const double ScaleY = AbsY > UE_DOUBLE_KINDA_SMALL_NUMBER
			? HalfExtents.Y / AbsY : TNumericLimits<double>::Max();
		return FMath::Min(ScaleX, ScaleY);
	}

	FVector2D InsetHalfExtents(const FVector2D& ViewportSize, double Margin)
	{
		return FVector2D(
			FMath::Max(ViewportSize.X * 0.5 - Margin, MinHalfExtent),
			FMath::Max(ViewportSize.Y * 0.5 - Margin, MinHalfExtent));
	}
}

// --- FScreenMarkerViewContext ---

bool FScreenMarkerViewContext::IsValid() const
{
	return PlayerController.IsValid()
		&& ViewportSize.X > UE_DOUBLE_KINDA_SMALL_NUMBER
		&& ViewportSize.Y > UE_DOUBLE_KINDA_SMALL_NUMBER
		&& DpiScale > UE_KINDA_SMALL_NUMBER;
}

bool FScreenMarkerViewContext::Build(const UUserWidget* Widget, FScreenMarkerViewContext& OutContext)
{
	OutContext = FScreenMarkerViewContext();

	if (!::IsValid(Widget)) return false;

	APlayerController* PC = Widget->GetOwningPlayer();
	if (!::IsValid(PC)) return false;

	const float Scale = UWidgetLayoutLibrary::GetViewportScale(Widget);
	if (Scale <= UE_KINDA_SMALL_NUMBER) return false;

	// Player screen geometry, not the raw viewport: its local size is already DPI-divided, which is
	// the space render translations live in.
	const FVector2D ScreenSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC).GetLocalSize();
	if (ScreenSize.X <= UE_DOUBLE_KINDA_SMALL_NUMBER || ScreenSize.Y <= UE_DOUBLE_KINDA_SMALL_NUMBER) return false;

	OutContext.PlayerController = PC;
	OutContext.ViewportSize = ScreenSize;
	OutContext.DpiScale = Scale;
	PC->GetPlayerViewPoint(OutContext.ViewLocation, OutContext.ViewRotation);
	return true;
}

// --- FScreenMarkerProjection ---

FVector2D FScreenMarkerProjection::BearingFromCameraSpace(const FVector& CameraSpaceOffset)
{
	// A symmetric perspective projection maps camera space to a screen offset from centre of
	// (Y, -Z) scaled by 1/X -- the SAME scalar on both axes, because (Width/2)/tan(HalfFovX) and
	// (Height/2)/tan(HalfFovY) are equal. So the normalised bearing is independent of FOV, aspect
	// AND of X, which is why it stays continuous as the target crosses the camera plane.
	FVector2D Bearing(CameraSpaceOffset.Y, -CameraSpaceOffset.Z);
	if (!Bearing.Normalize())
		Bearing = FVector2D(0.0, 1.0); // Dead centre behind the camera -- park the chevron below.

	return Bearing;
}

FVector2D FScreenMarkerProjection::EdgeIntersection(const FVector2D& Bearing,
	const FVector2D& ViewportSize, float Margin)
{
	FVector2D Direction = Bearing;
	if (!Direction.Normalize())
		Direction = FVector2D(0.0, 1.0);

	const FVector2D HalfExtents = InsetHalfExtents(ViewportSize, Margin);
	return ViewportSize * 0.5 + Direction * RectangleEdgeScale(Direction, HalfExtents);
}

FScreenMarkerProjection FScreenMarkerProjection::Project(const FScreenMarkerViewContext& Context,
	const FVector& WorldLocation, const FScreenMarkerClampParams& Params)
{
	FScreenMarkerProjection Result;

	APlayerController* PC = Context.PlayerController.Get();
	if (!Context.IsValid() || !::IsValid(PC)) return Result;

	Result.bIsValid = true;

	const FVector ToTarget = WorldLocation - Context.ViewLocation;
	Result.DistanceMeters = static_cast<float>(ToTarget.Size() / UnrealUnitsPerMetre);

	// The behind-camera fix lives in BearingFromCameraSpace: ProjectWorldLocationToScreen mirrors
	// its answer once the target is behind the plane, which is what threw markers onto the wrong
	// side of the screen.
	const FVector CameraSpace = Context.ViewRotation.UnrotateVector(ToTarget);
	Result.bIsBehind = CameraSpace.X <= UE_DOUBLE_KINDA_SMALL_NUMBER;

	const FVector2D Bearing = BearingFromCameraSpace(CameraSpace);
	Result.EdgeDirection = Bearing;
	Result.EdgeAngleDegrees = static_cast<float>(FRotator::ClampAxis(
		FMath::RadiansToDegrees(FMath::Atan2(Bearing.Y, Bearing.X))));

	const FVector2D Centre = Context.ViewportSize * 0.5;
	const FVector2D HalfExtents = InsetHalfExtents(Context.ViewportSize, Params.EdgeMargin);

	// Hysteresis applies to the RE-ENTRY test only. A target parked on the boundary would otherwise
	// flip bIsOffScreen -- and the chevron with it -- every frame.
	const FVector2D TestExtents = Params.bWasOffScreen
		? FVector2D(FMath::Max(HalfExtents.X - Params.ReentryHysteresis, MinHalfExtent),
			FMath::Max(HalfExtents.Y - Params.ReentryHysteresis, MinHalfExtent))
		: HalfExtents;

	FVector2D ScreenPixels = FVector2D::ZeroVector;
	if (!Result.bIsBehind && PC->ProjectWorldLocationToScreen(WorldLocation, ScreenPixels, true))
	{
		// The real projection while on screen, so the marker sits exactly on the object rather than
		// on a bearing approximation of it.
		const FVector2D Local = ScreenPixels / Context.DpiScale;
		const FVector2D Offset = Local - Centre;
		if (FMath::Abs(Offset.X) <= TestExtents.X && FMath::Abs(Offset.Y) <= TestExtents.Y)
		{
			Result.ScreenPosition = Local;
			return Result;
		}
	}

	Result.bIsOffScreen = true;
	Result.ScreenPosition = EdgeIntersection(Bearing, Context.ViewportSize, Params.EdgeMargin);
	return Result;
}

FVector2D FScreenMarkerProjection::InterpolatePosition(const FVector2D& Current, const FVector2D& Target,
	float DeltaTime, float Speed)
{
	if (DeltaTime <= 0.f) return Current;
	if (Speed <= 0.f) return Target;

	const float Alpha = 1.f - FMath::Exp(-Speed * DeltaTime);
	return FMath::Lerp(Current, Target, Alpha);
}
