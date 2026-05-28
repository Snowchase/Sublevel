#include "Staff/StaffAgent.h"

AStaffAgent::AStaffAgent()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AStaffAgent::Initialize(uint32 InStaffID, EStaffRole InRole, EStaffTrait InTrait, int32 AssignedFloor)
{
    StaffData.ID           = InStaffID;
    StaffData.Role         = InRole;
    StaffData.Trait        = InTrait;
    StaffData.AssignedFloor = AssignedFloor;
    StaffData.Fatigue      = 0.f;
    StaffData.Loyalty      = 1.f;
}

void AStaffAgent::SimTick(uint64 CurrentTick)
{
    // §6 — task queue processing, fatigue decay, loyalty updates
}
