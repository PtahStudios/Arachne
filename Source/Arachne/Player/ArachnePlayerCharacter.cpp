#include "Player/ArachnePlayerCharacter.h"
#include "World/ArachneStimulusSourceComponent.h"
#include "Core/ArachneDebug.h"
#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "InputActionValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogArachnePlayer, Log, All);

AArachnePlayerCharacter::AArachnePlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
    BaseEyeHeight = 64.f;

    // No body at all: the default character mesh stays empty and hidden.
    GetMesh()->SetVisibility(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0, 0, BaseEyeHeight));
    Camera->bUsePawnControlRotation = true;
    Camera->FieldOfView = 90.f;

    Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
    Flashlight->SetupAttachment(Camera);
    Flashlight->SetRelativeLocation(FVector(10, 12, -12));
    Flashlight->SetIntensityUnits(ELightUnits::Lumens);
    Flashlight->SetIntensity(900.f);
    Flashlight->SetAttenuationRadius(2500.f);
    Flashlight->SetOuterConeAngle(26.f);
    Flashlight->SetInnerConeAngle(14.f);
    Flashlight->SetVisibility(false);

    Stimulus = CreateDefaultSubobject<UArachneStimulusSourceComponent>(TEXT("Stimulus"));

    bUseControllerRotationYaw = true;
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    GetCharacterMovement()->BrakingDecelerationWalking = 1800.f;
}

void AArachnePlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    SetFlashlight(bFlashlightOnAtStart);
    Stimulus->OnCaught.AddDynamic(this, &AArachnePlayerCharacter::HandleCaught);
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
}

void AArachnePlayerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Sprint only counts while actually moving.
    Stimulus->SetSprinting(bSprinting && GetVelocity().Size2D() > WalkSpeed * .5f);

    // Caught: the view is pulled towards the spider.
    if (bCaught && Controller)
        if (const AActor* Predator = CaughtBy.Get())
        {
            const FRotator Want = (Predator->GetActorLocation() - Camera->GetComponentLocation()).Rotation();
            Controller->SetControlRotation(FMath::RInterpTo(Controller->GetControlRotation(), Want, DeltaSeconds, CaughtTurnSpeed));
        }
}

void AArachnePlayerCharacter::SetFlashlight(bool bOn)
{
    Flashlight->SetVisibility(bOn);
    Stimulus->SetFlashlightOn(bOn);
}

bool AArachnePlayerCharacter::IsFlashlightOn() const
{
    return Flashlight->IsVisible();
}

void AArachnePlayerCharacter::ApplySprint(bool bSprint)
{
    bSprinting = bSprint && !bCaught;
    GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

void AArachnePlayerCharacter::HandleCaught(AActor* Predator)
{
    bCaught = true;
    CaughtBy = Predator;
    ApplySprint(false);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        // The HUD reset button needs the cursor and click events.
        PC->bShowMouseCursor = true;
        PC->bEnableClickEvents = true;
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(Mode);
    }
}

void AArachnePlayerCharacter::RestartLevel()
{
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}

// =====================================================================================================================
// Input (Enhanced Input, created at runtime so the project needs no input assets)
// =====================================================================================================================

void AArachnePlayerCharacter::EnsureInputAssets()
{
    if (InputContext) return;
    auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
    {
        UInputAction* Action = NewObject<UInputAction>(this, Name);
        Action->ValueType = Type;
        return Action;
    };
    MoveAction       = MakeAction(TEXT("IA_PlayerMove"), EInputActionValueType::Axis2D);
    LookAction       = MakeAction(TEXT("IA_PlayerLook"), EInputActionValueType::Axis2D);
    SprintAction     = MakeAction(TEXT("IA_PlayerSprint"), EInputActionValueType::Boolean);
    FlashlightAction = MakeAction(TEXT("IA_PlayerFlashlight"), EInputActionValueType::Boolean);
    DebugAction      = MakeAction(TEXT("IA_PlayerDebug"), EInputActionValueType::Boolean);
    IgnoreAction     = MakeAction(TEXT("IA_PlayerIgnoreToggle"), EInputActionValueType::Boolean);
    ResetAction      = MakeAction(TEXT("IA_PlayerReset"), EInputActionValueType::Boolean);

    InputContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Player"));
    auto Map = [this](UInputAction* Action, const FKey& Key, bool bSwizzle, bool bNegate) -> FEnhancedActionKeyMapping&
    {
        FEnhancedActionKeyMapping& Mapping = InputContext->MapKey(Action, Key);
        if (bSwizzle) Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(InputContext));  // default order YXZ
        if (bNegate) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(InputContext));
        return Mapping;
    };
    // Move: X = forward, Y = right
    Map(MoveAction, EKeys::W, false, false);
    Map(MoveAction, EKeys::S, false, true);
    Map(MoveAction, EKeys::D, true, false);
    Map(MoveAction, EKeys::A, true, true);
    {
        FEnhancedActionKeyMapping& Stick = Map(MoveAction, EKeys::Gamepad_Left2D, true, false);
        Stick.Modifiers.Insert(NewObject<UInputModifierDeadZone>(InputContext), 0);
    }
    Map(LookAction, EKeys::Mouse2D, false, false);
    {
        FEnhancedActionKeyMapping& Stick = Map(LookAction, EKeys::Gamepad_Right2D, false, false);
        Stick.Modifiers.Add(NewObject<UInputModifierDeadZone>(InputContext));
        Stick.Modifiers.Add(NewObject<UInputModifierScaleByDeltaTime>(InputContext));
        UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(InputContext);
        Scalar->Scalar = FVector(60.0, 45.0, 1.0);
        Stick.Modifiers.Add(Scalar);
    }
    Map(SprintAction, EKeys::LeftShift, false, false);
    Map(SprintAction, EKeys::Gamepad_LeftThumbstick, false, false);
    Map(FlashlightAction, EKeys::F, false, false);
    Map(FlashlightAction, EKeys::Gamepad_FaceButton_Top, false, false);
    Map(DebugAction, EKeys::Zero, false, false);   // F1 stays free for the editor wireframe view mode
    Map(IgnoreAction, EKeys::Nine, false, false);
    Map(ResetAction, EKeys::R, false, false);
    Map(ResetAction, EKeys::Gamepad_Special_Left, false, false);
}

void AArachnePlayerCharacter::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();
    EnsureInputAssets();
    if (APlayerController* PC = Cast<APlayerController>(Controller))
        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                Subsystem->RemoveMappingContext(InputContext);
                Subsystem->AddMappingContext(InputContext, 0);
            }
}

void AArachnePlayerCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    EnsureInputAssets();
    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Input);
    if (!EIC)
    {
        UE_LOG(LogArachnePlayer, Error, TEXT("ARACHNE: Enhanced Input component required (Project Settings > Input > Default Input Component Class)"));
        return;
    }
    EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AArachnePlayerCharacter::OnMove);
    EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AArachnePlayerCharacter::OnLook);
    EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &AArachnePlayerCharacter::OnSprintOn);
    EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AArachnePlayerCharacter::OnSprintOff);
    EIC->BindAction(FlashlightAction, ETriggerEvent::Started, this, &AArachnePlayerCharacter::OnFlashlight);
    EIC->BindAction(DebugAction, ETriggerEvent::Started, this, &AArachnePlayerCharacter::OnDebug);
    EIC->BindAction(IgnoreAction, ETriggerEvent::Started, this, &AArachnePlayerCharacter::OnIgnoreToggle);
    EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &AArachnePlayerCharacter::OnReset);
}

void AArachnePlayerCharacter::OnMove(const FInputActionValue& Value)
{
    if (bCaught || !Controller) return;
    const FVector2D V = Value.Get<FVector2D>();
    const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.X);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.Y);
}

void AArachnePlayerCharacter::OnLook(const FInputActionValue& Value)
{
    if (bCaught) return;
    const FVector2D V = Value.Get<FVector2D>();
    AddControllerYawInput(V.X * LookSensitivity);
    AddControllerPitchInput(-V.Y * LookSensitivity);
}

void AArachnePlayerCharacter::OnSprintOn(const FInputActionValue&) { ApplySprint(true); }
void AArachnePlayerCharacter::OnSprintOff(const FInputActionValue&) { ApplySprint(false); }
void AArachnePlayerCharacter::OnFlashlight(const FInputActionValue&) { if (!bCaught) SetFlashlight(!IsFlashlightOn()); }
void AArachnePlayerCharacter::OnDebug(const FInputActionValue&) { ArachneDebug::Toggle(); }
void AArachnePlayerCharacter::OnIgnoreToggle(const FInputActionValue&) { ArachneDebug::ToggleIgnorePlayer(); }
void AArachnePlayerCharacter::OnReset(const FInputActionValue&) { RestartLevel(); }
