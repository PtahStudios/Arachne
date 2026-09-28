#include "GM_ArachneBase.h"
#include "ArachnePawn.h"
#include "ArachneHUD.h"
#include "ArachneTraversalTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "TimerManager.h"
AGM_ArachneBase::AGM_ArachneBase(){DefaultPawnClass=AArachnePawn::StaticClass();HUDClass=AArachneHUD::StaticClass();}
void AGM_ArachneBase::BeginPlay()
{
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ArachneTest")))GetWorldTimerManager().SetTimerForNextTick([this](){GetWorld()->SpawnActor<AArachneTraversalTest>();});
#endif
}
