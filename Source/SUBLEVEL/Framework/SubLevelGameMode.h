#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SubLevelGameMode.generated.h"

UCLASS()
class SUBLEVEL_API ASubLevelGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASubLevelGameMode();

    virtual void InitGame(
        const FString& MapName,
        const FString& Options,
        FString&       ErrorMessage) override;

    virtual void StartPlay() override;

    void LoadSessionFromSeed(int32 SavedSeed, uint64 SavedTick, int32 SavedDay);

    UPROPERTY(EditAnywhere, Category = "Debug")
    int32 DebugForcedSeed = -1;

private:
    int32 GenerateSeed() const;
    void  InitializeSubsystems(int32 Seed);

    UPROPERTY(EditAnywhere, Category = "Simulation")
    float TimeScaleMultiplier = 20.0f;
};
