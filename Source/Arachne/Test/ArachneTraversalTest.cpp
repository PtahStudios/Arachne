#include "Test/ArachneTraversalTest.h"
#include "Test/ArachneMover.h"
#include "Creature/ArachnePawn.h"
#include "AI/ArachneBrainComponent.h"
#include "World/ArachneCampPoint.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"

static const int32 NumCases = 6;
static const TCHAR* CaseNames[NumCases] = {
    TEXT("floor_wall_ceiling"), TEXT("inclined_surface"), TEXT("convex_box_up_over_down"),
    TEXT("strafe_and_grounded_ik"), TEXT("corner_anchor"), TEXT("moving_platform")};
static const float CaseDurations[NumCases] = {14.f, 5.f, 12.f, 3.f, 5.f, 4.f};
static const float CaseStartX[NumCases] = {9700.f, 19700.f, 29750.f, 40000.f, 50000.f, 60000.f};

void AArachneTraversalTest::SpawnIfRequested(UWorld* World)
{
#if !UE_BUILD_SHIPPING
    if (!World || !FParse::Param(FCommandLine::Get(), TEXT("ArachneTest"))) return;
    World->GetTimerManager().SetTimerForNextTick([World]() { World->SpawnActor<AArachneTraversalTest>(); });
#endif
}

AArachneTraversalTest::AArachneTraversalTest() { PrimaryActorTick.bCanEverTick = true; }

void AArachneTraversalTest::Box(FVector P, FVector E, FRotator R)
{
    AActor* A = GetWorld()->SpawnActor<AActor>(P, R);
    UBoxComponent* B = NewObject<UBoxComponent>(A);
    A->SetRootComponent(B);
    B->SetBoxExtent(E);
    B->SetCollisionProfileName(TEXT("BlockAll"));
    B->RegisterComponent();
    A->SetActorLocationAndRotation(P, R);
}

void AArachneTraversalTest::BeginPlay()
{
    Super::BeginPlay();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // 0: floor -> wall -> ceiling
    Box(FVector(10000, 0, -25), FVector(750, 450, 25));
    Box(FVector(10625, 0, 300), FVector(25, 450, 300));
    Box(FVector(10000, 0, 625), FVector(650, 450, 25));
    // 1: 25 degree ramp
    Box(FVector(20000, 0, -25), FVector(1100, 450, 25));
    Box(FVector(20200, 0, 151), FVector(350, 250, 25), FRotator(25, 0, 0));
    // 2: box to climb up, over and down
    Box(FVector(30000, 0, -25), FVector(1500, 500, 25));
    Box(FVector(30200, 0, 90), FVector(150, 300, 90));
    // 3: open floor
    Box(FVector(40000, 0, -25), FVector(1200, 1200, 25));
    // 4: corner between two walls and a ceiling (bottom at 300)
    Box(FVector(50000, 0, -25), FVector(1200, 1200, 25));
    Box(FVector(50425, 0, 175), FVector(25, 600, 200));
    Box(FVector(50000, 425, 175), FVector(600, 25, 200));
    Box(FVector(50000, 0, 325), FVector(600, 600, 25));
    Corner = GetWorld()->SpawnActor<AArachneCampPoint>(FVector(50320, 320, 220), FRotator(0, 225, 0), Params);
    // 5: moving platform (cube 100cm scaled -> 1200 x 1200 x 50, top at z = 0)
    Platform = GetWorld()->SpawnActor<AArachneMover>(FVector(60000, 0, -25), FRotator::ZeroRotator, Params);
    if (Platform)
    {
        Platform->SetActorScale3D(FVector(12, 12, .5));
        Platform->Amplitude = FVector(0, 250, 0);
        Platform->Period = 4.f;
    }

    // The spider under test: body only, no brain.
    Pawn = GetWorld()->SpawnActor<AArachnePawn>(AArachnePawn::StaticClass(), FVector(CaseStartX[0], 0, 75), FRotator::ZeroRotator, Params);
    if (!Pawn) { UE_LOG(LogTemp, Error, TEXT("ARACHNE_TEST could not spawn the pawn")); Finish(); return; }
    if (Pawn->Brain) Pawn->Brain->SetBrainEnabled(false);
    Pawn->AddTickPrerequisiteActor(this);
    if (Platform) Pawn->AddTickPrerequisiteActor(Platform);
    Pawn->OnFootPlanted.AddDynamic(this, &AArachneTraversalTest::HandleFootfall);
    BeginCase();
}

void AArachneTraversalTest::HandleFootfall(int32, FVector, FVector, float) { ++Footfalls; }

void AArachneTraversalTest::BeginCase()
{
    Time = 0.f;
    Floor = Wall = Ceiling = Slope = Convex = AnchorIssued = false;
    Highest = 0.f; WorstFoot = 0.f; MaxFrameMove = 0.f; MaxUpTurn = 0.f;
    MinFeet = 8; SteadySamples = 0; Footfalls = 0;
    Start = FVector(CaseStartX[Case], 0, 75);
    if (Case == 5 && Platform) Start = Platform->GetActorLocation() + FVector(0, 0, 100);
    Pawn->ResetCrawler(Start, FRotator::ZeroRotator);
    Previous = Pawn->GetActorLocation();
    PreviousUp = Pawn->SurfaceUp;
    PlatformStart = Platform ? Platform->GetActorLocation() : FVector::ZeroVector;
    Pawn->SetMovementInput(0, 0);
}

void AArachneTraversalTest::Tick(float Dt)
{
    Super::Tick(Dt);
    if (!Pawn) return;
    Time += Dt;
    TotalTime += Dt;
    if (TotalTime > 90.f) { Finish(); return; }

    const bool bGo = Time > .5f;
    float Forward = 0.f, Right = 0.f;
    if (Case <= 2) Forward = bGo ? 1.f : 0.f;
    if (Case == 3) Right = bGo ? 1.f : 0.f;
    Pawn->SetMovementInput(Forward, Right);
    if (Case == 4 && bGo && !AnchorIssued && Corner) { Pawn->BeginAnchor(Corner->GetAnchor()); AnchorIssued = true; }

    const FVector P = Pawn->GetActorLocation(), Up = Pawn->SurfaceUp;
    Floor |= Pawn->bAttached && Up.Z > .95;
    Wall |= Pawn->bAttached && FMath::Abs(Up.Z) < .2;
    Ceiling |= Pawn->bAttached && Up.Z < -.95;
    Slope |= Up.Z > .7 && Up.Z < .94;
    Convex |= Pawn->IsWrappingEdge();
    Highest = FMath::Max(Highest, static_cast<float>(P.Z));
    if (Time > .05f)
    {
        MaxFrameMove = FMath::Max(MaxFrameMove, static_cast<float>(FVector::Dist(P, Previous)));
        MaxUpTurn = FMath::Max(MaxUpTurn, static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Up, PreviousUp), -1.0, 1.0)))));
    }
    Previous = P;
    PreviousUp = Up;
    if (Time > 1.f && Pawn->bAttached && !Pawn->IsWrappingEdge() && FMath::Abs(Up.Z) > .98)
    {
        WorstFoot = FMath::Max(WorstFoot, Pawn->MaxFootError);
        MinFeet = FMath::Min(MinFeet, Pawn->SupportedFeet);
        ++SteadySamples;
    }
    if (Time >= CaseDurations[Case]) EndCase();
}

void AArachneTraversalTest::EndCase()
{
    const FVector End = Pawn->GetActorLocation();
    bool Passed = Pawn->IsRigValid() && !End.ContainsNaN() && MaxFrameMove < 45.f && MaxUpTurn < 14.f;
    if (Case == 0) Passed &= Floor && Wall && Ceiling;
    if (Case == 1) Passed &= Floor && Slope && Highest > 145.f;
    if (Case == 2) Passed &= Floor && Wall && Convex && Highest > 235.f && End.X > 30400.f && End.Z < 110.f;
    if (Case == 3) Passed &= End.Y > 400.f && FMath::Abs(End.X - Start.X) < 10.f && WorstFoot < 20.f && MinFeet >= 4 && Footfalls > 8;
    double AnchorError = 0.0;
    if (Case == 4)
    {
        AnchorError = Corner ? FVector::Dist(End, Corner->GetAnchor().Body.GetLocation()) : 1e6;
        Passed &= Corner && Pawn->IsAnchorSettled() && AnchorError < 5.0 && Pawn->SupportedFeet >= 6 && Pawn->MaxFootError < 25.f;
    }
    double Follow = 0.0;
    if (Case == 5)
    {
        const FVector PlatformDelta = Platform ? Platform->GetActorLocation() - PlatformStart : FVector::ZeroVector;
        const FVector PawnDelta = End - Start;
        Follow = FVector::Dist2D(PawnDelta, PlatformDelta);
        Passed &= Platform && Pawn->bAttached && Follow < 30.0 && MinFeet >= 6 && WorstFoot < 20.f;
    }

    TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("case"), CaseNames[Case]);
    R->SetBoolField(TEXT("passed"), Passed);
    R->SetBoolField(TEXT("floor"), Floor);
    R->SetBoolField(TEXT("wall"), Wall);
    R->SetBoolField(TEXT("ceiling"), Ceiling);
    R->SetBoolField(TEXT("slope"), Slope);
    R->SetBoolField(TEXT("convex_wrap"), Convex);
    R->SetStringField(TEXT("end"), End.ToString());
    R->SetNumberField(TEXT("highest_z"), Highest);
    R->SetNumberField(TEXT("max_frame_displacement"), MaxFrameMove);
    R->SetNumberField(TEXT("max_frame_up_turn_deg"), MaxUpTurn);
    R->SetNumberField(TEXT("max_steady_foot_error_cm"), WorstFoot);
    R->SetNumberField(TEXT("min_supported_feet"), MinFeet);
    R->SetNumberField(TEXT("supported_feet_at_end"), Pawn->SupportedFeet);
    R->SetNumberField(TEXT("steady_samples"), SteadySamples);
    R->SetNumberField(TEXT("footfalls"), Footfalls);
    R->SetNumberField(TEXT("anchor_error_cm"), AnchorError);
    R->SetNumberField(TEXT("platform_follow_error"), Follow);
    Results.Add(MakeShared<FJsonValueObject>(R));
    UE_LOG(LogTemp, Display, TEXT("ARACHNE_TEST %s: %s, end %s, up-turn %.1f deg/frame, frame move %.1f, foot err %.2f, feet %d"),
        CaseNames[Case], Passed ? TEXT("PASS") : TEXT("FAIL"), *End.ToString(), MaxUpTurn, MaxFrameMove, WorstFoot, MinFeet);
    ++Case;
    if (Case >= NumCases) Finish(); else BeginCase();
}

void AArachneTraversalTest::Finish()
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("cases"), Results);
    bool All = Results.Num() == NumCases;
    for (auto& V : Results) All &= V->AsObject()->GetBoolField(TEXT("passed"));
    Root->SetBoolField(TEXT("passed"), All);
    FString Text;
    auto Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("ArachneTraversalTest.json")));
    UE_LOG(LogTemp, Display, TEXT("ARACHNE_TEST_FINISHED %s"), All ? TEXT("PASS") : TEXT("FAIL"));
    SetActorTickEnabled(false);
    FPlatformMisc::RequestExitWithStatus(false, All ? 0 : 1);
}
