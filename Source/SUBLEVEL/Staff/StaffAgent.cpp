#include "Staff/StaffAgent.h"

AStaffAgent::AStaffAgent()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AStaffAgent::Initialize(uint32 InStaffID, EStaffRole Role, EStaffTrait Trait, int32 AssignedFloor)
{
    StaffData.ID           = InStaffID;
    StaffData.Role         = Role;
    StaffData.Trait        = Trait;
    StaffData.AssignedFloor = AssignedFloor;
    StaffData.Fatigue      = 0.f;
    StaffData.Loyalty      = 1.f;
}

void AStaffAgent::SimTick(uint64 CurrentTick)
{
    // §6 — task queue processing, fatigue decay, loyalty updates
}
