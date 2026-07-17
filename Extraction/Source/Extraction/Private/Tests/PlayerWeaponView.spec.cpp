// Structural automation coverage for passive player weapon presentation views.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "Weapon/KitWeaponInterface.h"
#include "Weapon/PlayerWeaponView.h"
#include "Weapon/WeaponBase.h"

namespace PlayerWeaponViewTest
{
	template <typename TView>
	TView* SpawnView(UWorld* World)
	{
		return IsValid(World)
			? World->SpawnActor<TView>(TView::StaticClass(), FTransform::Identity)
			: nullptr;
	}

	UPlayerWeaponMarkerComponent* AddMovingPartMarker(
		APlayerWeaponView& View, EPlayerWeaponMovingPart Part, FName Name)
	{
		UPlayerWeaponMarkerComponent* Marker =
			NewObject<UPlayerWeaponMarkerComponent>(&View, Name);
		if (!Marker) return nullptr;

		Marker->SetupAttachment(View.GetArtRoot());
		Marker->ConfigureMarker(EPlayerWeaponMarkerKind::MovingPart, Part);
		View.AddInstanceComponent(Marker);
		Marker->RegisterComponent();
		return Marker;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponViewPassiveDefaultsTest,
	"Extraction.PlayerWeapon.View.PassiveDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponViewPassiveDefaultsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	APlayerWeaponView* WeaponView =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponView>(World);
	APlayerWeaponAttachmentView* AttachmentView =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponAttachmentView>(World);
	TestNotNull(TEXT("weapon view spawns"), WeaponView);
	TestNotNull(TEXT("attachment view spawns"), AttachmentView);
	if (!WeaponView || !AttachmentView) return false;

	const TArray<AActor*> Views = {WeaponView, AttachmentView};
	for (const AActor* View : Views)
	{
		TestFalse(TEXT("actor tick is disabled"), View->PrimaryActorTick.bCanEverTick);
		TestFalse(TEXT("actor replication is disabled"), View->GetIsReplicated());
		TestFalse(TEXT("movement replication is disabled"), View->IsReplicatingMovement());
		TestEqual(TEXT("automatic input is disabled"), View->AutoReceiveInput, EAutoReceiveInput::Disabled);
		TestFalse(TEXT("damage is disabled"), View->CanBeDamaged());
		TestFalse(TEXT("actor collision is disabled"), View->GetActorEnableCollision());
	}

	APlayerWeaponView* DeferredView =
		World->SpawnActorDeferred<APlayerWeaponView>(
			APlayerWeaponView::StaticClass(), FTransform::Identity);
	TestNotNull(TEXT("deferred weapon view spawns"), DeferredView);
	if (DeferredView)
	{
		DeferredView->AutoReceiveInput = EAutoReceiveInput::Player0;
		DeferredView->FinishSpawning(FTransform::Identity);
		TestEqual(TEXT("construction override cannot enable automatic input"),
			DeferredView->AutoReceiveInput, EAutoReceiveInput::Disabled);
		TestNull(TEXT("passive view never creates an input component"),
			DeferredView->InputComponent.Get());
	}

	TestFalse(TEXT("weapon view is not gameplay weapon"),
		WeaponView->IsA(AWeaponBase::StaticClass()));
	TestFalse(TEXT("weapon view does not implement kit gameplay interface"),
		WeaponView->GetClass()->ImplementsInterface(UKitWeaponInterface::StaticClass()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponViewWeaponMarkersTest,
	"Extraction.PlayerWeapon.View.WeaponMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponViewWeaponMarkersTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	APlayerWeaponView* View =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponView>(World);
	TestNotNull(TEXT("weapon view spawns"), View);
	if (!View) return false;
	TestTrue(TEXT("weapon view initializes in editor world"), View->InitializeView());

	TArray<TPair<UPlayerWeaponMarkerComponent*, EPlayerWeaponMarkerKind>> Markers;
	Markers.Reserve(8);
	Markers.Emplace(View->GetWeaponSeatMarker(), EPlayerWeaponMarkerKind::WeaponSeat);
	Markers.Emplace(View->GetSupportHandTargetMarker(), EPlayerWeaponMarkerKind::SupportHandTarget);
	Markers.Emplace(View->GetSupportHandHintMarker(), EPlayerWeaponMarkerKind::SupportHandHint);
	Markers.Emplace(View->GetIronRearMarker(), EPlayerWeaponMarkerKind::IronRear);
	Markers.Emplace(View->GetIronFrontMarker(), EPlayerWeaponMarkerKind::IronFront);
	Markers.Emplace(View->GetOpticMountMarker(), EPlayerWeaponMarkerKind::OpticMount);
	Markers.Emplace(View->GetMuzzleMarker(), EPlayerWeaponMarkerKind::Muzzle);
	Markers.Emplace(View->GetCasingMarker(), EPlayerWeaponMarkerKind::Casing);

	TSet<const UPlayerWeaponMarkerComponent*> UniqueMarkers;
	UniqueMarkers.Reserve(Markers.Num());
	for (const TPair<UPlayerWeaponMarkerComponent*, EPlayerWeaponMarkerKind>& Entry : Markers)
	{
		TestNotNull(TEXT("required weapon marker exists"), Entry.Key);
		if (!Entry.Key) continue;
		UniqueMarkers.Add(Entry.Key);
		TestEqual(TEXT("weapon marker kind is typed"), Entry.Key->GetMarkerKind(), Entry.Value);
		TestEqual(TEXT("weapon marker is under view root"), Entry.Key->GetAttachParent(), View->GetViewRoot());
		TestFalse(TEXT("weapon marker does not tick"),
			Entry.Key->PrimaryComponentTick.bCanEverTick);
		TestFalse(TEXT("weapon marker does not replicate"), Entry.Key->GetIsReplicated());
		TestTrue(TEXT("+X is downrange"), Entry.Key->GetForwardAxis().Equals(FVector::ForwardVector));
		TestTrue(TEXT("+Y is right"), Entry.Key->GetRightAxis().Equals(FVector::RightVector));
		TestTrue(TEXT("+Z is up"), Entry.Key->GetUpAxis().Equals(FVector::UpVector));
	}
	TestEqual(TEXT("required weapon markers are unique"), UniqueMarkers.Num(), Markers.Num());
	TestEqual(TEXT("weapon marker cache is complete"), View->GetRegisteredMarkerCount(), Markers.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponViewAttachmentMarkersTest,
	"Extraction.PlayerWeapon.View.AttachmentMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponViewAttachmentMarkersTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	APlayerWeaponAttachmentView* View =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponAttachmentView>(World);
	TestNotNull(TEXT("attachment view spawns"), View);
	if (!View) return false;
	TestTrue(TEXT("attachment view initializes in editor world"), View->InitializeView());

	TArray<TPair<UPlayerWeaponMarkerComponent*, EPlayerWeaponMarkerKind>> Markers;
	Markers.Reserve(4);
	Markers.Emplace(View->GetAttachmentMountMarker(), EPlayerWeaponMarkerKind::AttachmentMount);
	Markers.Emplace(View->GetAimPointMarker(), EPlayerWeaponMarkerKind::AimPoint);
	Markers.Emplace(View->GetSupportHandTargetMarker(), EPlayerWeaponMarkerKind::SupportHandTarget);
	Markers.Emplace(View->GetSupportHandHintMarker(), EPlayerWeaponMarkerKind::SupportHandHint);

	for (const TPair<UPlayerWeaponMarkerComponent*, EPlayerWeaponMarkerKind>& Entry : Markers)
	{
		TestNotNull(TEXT("required attachment marker exists"), Entry.Key);
		if (!Entry.Key) continue;
		TestEqual(TEXT("attachment marker kind is typed"), Entry.Key->GetMarkerKind(), Entry.Value);
		TestEqual(TEXT("attachment marker is under view root"), Entry.Key->GetAttachParent(), View->GetViewRoot());
		TestFalse(TEXT("attachment marker does not tick"),
			Entry.Key->PrimaryComponentTick.bCanEverTick);
		TestFalse(TEXT("attachment marker does not replicate"), Entry.Key->GetIsReplicated());
		TestTrue(TEXT("attachment marker +X is downrange"),
			Entry.Key->GetForwardAxis().Equals(FVector::ForwardVector));
	}
	TestNotEqual(TEXT("third-party art root is separate from attachment markers"),
		View->GetArtRoot(), static_cast<USceneComponent*>(View->GetAimPointMarker()));
	TestEqual(TEXT("attachment marker cache is complete"), View->GetRegisteredMarkerCount(), Markers.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponViewInitializationTest,
	"Extraction.PlayerWeapon.View.InitializationAndMovingParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponViewInitializationTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	APlayerWeaponView* View =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponView>(World);
	TestNotNull(TEXT("weapon view spawns"), View);
	if (!View) return false;

	const int32 NativeMarkerCount = View->GetRegisteredMarkerCount();
	TestTrue(TEXT("construction initializes the view"), View->IsViewInitialized());
	TestTrue(TEXT("construction registers native markers"), NativeMarkerCount > 0);
	TestTrue(TEXT("repeated initialization succeeds"), View->InitializeView());
	TestEqual(TEXT("repeated initialization preserves markers"),
		View->GetRegisteredMarkerCount(), NativeMarkerCount);

	View->ReleaseView();
	View->ReleaseView();
	TestFalse(TEXT("repeated release leaves view uninitialized"), View->IsViewInitialized());
	TestEqual(TEXT("release clears transient marker cache"), View->GetRegisteredMarkerCount(), 0);
	TestTrue(TEXT("released view can reinitialize"), View->InitializeView());

	UPlayerWeaponMarkerComponent* Magazine = PlayerWeaponViewTest::AddMovingPartMarker(
		*View, EPlayerWeaponMovingPart::Magazine, TEXT("TestMagazineMarker"));
	TestNotNull(TEXT("moving-part marker is added"), Magazine);
	TestTrue(TEXT("view accepts unique moving-part marker"), View->InitializeView());
	TestEqual(TEXT("moving-part lookup is stable"),
		View->GetMovingPartMarker(EPlayerWeaponMovingPart::Magazine), Magazine);

	TestNotNull(TEXT("duplicate moving marker is added"),
		PlayerWeaponViewTest::AddMovingPartMarker(
			*View, EPlayerWeaponMovingPart::Magazine, TEXT("DuplicateMagazineMarker")));
	AddExpectedMessage(TEXT("duplicate moving-part marker"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains);
	TestFalse(TEXT("duplicate moving-part keys reject initialization"), View->InitializeView());
	TestFalse(TEXT("failed initialization does not expose partial state"), View->IsViewInitialized());
	TestNull(TEXT("failed initialization clears lookup"),
		View->GetMovingPartMarker(EPlayerWeaponMovingPart::Magazine));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponViewRequiredMarkerKindsTest,
	"Extraction.PlayerWeapon.View.RequiredMarkerKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponViewRequiredMarkerKindsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	APlayerWeaponView* WeaponView =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponView>(World);
	APlayerWeaponAttachmentView* AttachmentView =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponAttachmentView>(World);
	TestNotNull(TEXT("weapon view spawns"), WeaponView);
	TestNotNull(TEXT("attachment view spawns"), AttachmentView);
	if (!WeaponView || !AttachmentView) return false;
	AddExpectedMessage(TEXT("invalid required WeaponSeat marker"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains);
	AddExpectedMessage(TEXT("invalid required AimPoint marker"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains);

	WeaponView->GetWeaponSeatMarker()->ConfigureMarker(
		EPlayerWeaponMarkerKind::AimPoint);
	TestFalse(TEXT("retyped native weapon marker is rejected"),
		WeaponView->InitializeView());
	WeaponView->GetWeaponSeatMarker()->ConfigureMarker(
		EPlayerWeaponMarkerKind::WeaponSeat);
	TestTrue(TEXT("restored native weapon marker validates"),
		WeaponView->InitializeView());

	AttachmentView->GetAimPointMarker()->ConfigureMarker(
		EPlayerWeaponMarkerKind::Casing);
	TestFalse(TEXT("retyped native attachment marker is rejected"),
		AttachmentView->InitializeView());
	AttachmentView->GetAimPointMarker()->ConfigureMarker(
		EPlayerWeaponMarkerKind::AimPoint);
	TestTrue(TEXT("restored native attachment marker validates"),
		AttachmentView->InitializeView());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponViewComposedArtPassivityTest,
	"Extraction.PlayerWeapon.View.ComposedArtPassivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponViewComposedArtPassivityTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	APlayerWeaponView* View =
		PlayerWeaponViewTest::SpawnView<APlayerWeaponView>(World);
	TestNotNull(TEXT("weapon view spawns"), View);
	if (!View) return false;

	USceneComponent* ArtGroup = NewObject<USceneComponent>(View, TEXT("TestArtGroup"));
	TestNotNull(TEXT("third-party art group is created"), ArtGroup);
	if (!ArtGroup) return false;
	ArtGroup->SetupAttachment(View->GetArtRoot());
	View->AddInstanceComponent(ArtGroup);
	ArtGroup->RegisterComponent();

	UBoxComponent* ArtPrimitive = NewObject<UBoxComponent>(View, TEXT("TestArtPrimitive"));
	TestNotNull(TEXT("third-party art primitive is created"), ArtPrimitive);
	if (!ArtPrimitive) return false;
	View->SetActorLocation(FVector(100.f, 20.f, 5.f));
	ArtPrimitive->SetupAttachment(ArtGroup);
	const FTransform IntendedRelative(
		FRotator(5.f, 10.f, 15.f), FVector(12.f, 3.f, -4.f));
	ArtPrimitive->SetRelativeTransform(IntendedRelative);
	ArtPrimitive->SetMobility(EComponentMobility::Movable);
	View->AddInstanceComponent(ArtPrimitive);
	ArtPrimitive->RegisterComponent();
	ArtPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ArtPrimitive->SetGenerateOverlapEvents(true);
	ArtPrimitive->SetCanEverAffectNavigation(true);
	ArtPrimitive->PrimaryComponentTick.bCanEverTick = true;

	TestTrue(TEXT("view reinitializes after art is added"), View->InitializeView());
	TestEqual(TEXT("art collision is forcibly disabled"),
		ArtPrimitive->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("art overlap generation is forcibly disabled"),
		ArtPrimitive->GetGenerateOverlapEvents());
	TestFalse(TEXT("art physics simulation is forcibly disabled"),
		ArtPrimitive->IsSimulatingPhysics());
	TestFalse(TEXT("art cannot affect navigation"),
		ArtPrimitive->CanEverAffectNavigation());
	TestEqual(TEXT("sanitized art preserves its authored parent"),
		ArtPrimitive->GetAttachParent(), ArtGroup);
	TestTrue(TEXT("authored parent remains under art root"),
		ArtGroup->IsAttachedTo(View->GetArtRoot()));
	TestTrue(TEXT("sanitized art preserves its authored relative transform"),
		ArtPrimitive->GetRelativeTransform().Equals(IntendedRelative));
	TestTrue(TEXT("presentation animation tick capability is preserved"),
		ArtPrimitive->PrimaryComponentTick.bCanEverTick);

	UBoxComponent* DetachedPrimitive =
		NewObject<UBoxComponent>(View, TEXT("DetachedArtPrimitive"));
	TestNotNull(TEXT("detached art primitive is created"), DetachedPrimitive);
	if (!DetachedPrimitive) return false;
	DetachedPrimitive->SetupAttachment(ArtGroup);
	DetachedPrimitive->SetMobility(EComponentMobility::Movable);
	View->AddInstanceComponent(DetachedPrimitive);
	DetachedPrimitive->RegisterComponent();
	DetachedPrimitive->SetSimulatePhysics(true);
	TestTrue(TEXT("invalid art starts with physics simulation"),
		DetachedPrimitive->IsSimulatingPhysics());
	TestNull(TEXT("physics detached the primitive from its authored parent"),
		DetachedPrimitive->GetAttachParent());
	AddExpectedMessage(TEXT("detached or outside ArtRoot"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains);
	TestFalse(TEXT("unknown detached hierarchy is rejected"), View->InitializeView());
	TestFalse(TEXT("rejected detached art is still made passive"),
		DetachedPrimitive->IsSimulatingPhysics());
	TestNull(TEXT("detached art is not silently reparented"),
		DetachedPrimitive->GetAttachParent());
	TestFalse(TEXT("rejected hierarchy leaves view uninitialized"),
		View->IsViewInitialized());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
