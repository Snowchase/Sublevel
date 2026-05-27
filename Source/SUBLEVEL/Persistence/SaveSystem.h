#pragma once

#include "CoreMinimal.h"

// §14 — Save / Load System
// Full JSON snapshot of game state. RNG state serialized for deterministic resume.
// Implemented in a future pass.

class SUBLEVEL_API FSaveSystem
{
public:
    static bool SaveToFile(const FString& SlotName, const UWorld* World);
    static bool LoadFromFile(const FString& SlotName, UWorld* World);
    static TArray<FString> GetSaveSlots();
};
