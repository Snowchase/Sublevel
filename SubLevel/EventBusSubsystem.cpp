#include "Subsystems/EventBusSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

UEventBusSubsystem* UEventBusSubsystem::Get(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;

    const UWorld* World = GEngine->GetWorldFromContextObject(
        WorldContext,
        EGetWorldErrorMode::LogAndReturnNull
    );

    if (!World) return nullptr;

    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UEventBusSubsystem>() : nullptr;
}
