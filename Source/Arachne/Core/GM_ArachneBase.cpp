#include "Core/GM_ArachneBase.h"
#include "Core/ArachneHUD.h"
#include "Player/ArachnePlayerCharacter.h"
#include "Test/ArachneTraversalTest.h"

AGM_ArachneBase::AGM_ArachneBase()
{
    DefaultPawnClass = AArachnePlayerCharacter::StaticClass();
    HUDClass = AArachneHUD::StaticClass();
}

void AGM_ArachneBase::BeginPlay()
{
    Super::BeginPlay();
    AArachneTraversalTest::SpawnIfRequested(GetWorld());   // -ArachneTest on the command line
}
