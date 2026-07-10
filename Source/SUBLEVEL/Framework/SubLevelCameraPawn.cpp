#include "Framework/SubLevelCameraPawn.h"
#include "Camera/CameraComponent.h"

ASubLevelCameraPawn::ASubLevelCameraPawn()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
    Camera->SetupAttachment(SceneRoot);
    Camera->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));   // straight down
    Camera->SetFieldOfView(60.f);

    // Ignore controller rotation — this is a fixed top-down rig.
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;
}

void ASubLevelCameraPawn::AddPanInput(const FVector2D& WorldDelta)
{
    AddActorWorldOffset(FVector(WorldDelta.X, WorldDelta.Y, 0.f));
}

void ASubLevelCameraPawn::SetCameraHeight(float NewHeight)
{
    FVector Location = GetActorLocation();
    Location.Z = NewHeight;
    SetActorLocation(Location);
}
