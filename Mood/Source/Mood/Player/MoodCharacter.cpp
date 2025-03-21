// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoodCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "../Weapons/MoodWeaponSlotComponent.h"
#include "../MoodHealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Mood/MoodGameMode.h"
#include "Mood/Enemies/MoodEnemyCharacter.h"
#include "Mood/Weapons/MoodWeaponComponent.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AMoodCharacter

AMoodCharacter::AMoodCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Our original components 
	GetMesh()->SetupAttachment(FirstPersonCameraComponent);
	HealthComponent = CreateDefaultSubobject<UMoodHealthComponent>(TEXT("HealthComponent"));
	WeaponSlotComponent = CreateDefaultSubobject<UMoodWeaponSlotComponent>(TEXT("WeaponSlotComponent"));
	WeaponSlotComponent->SetMuzzleRoot(FirstPersonCameraComponent);
}

void AMoodCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	if (!IsValid(MoodGameMode))
	{
		MoodGameMode = Cast<AMoodGameMode>(GetWorld()->GetAuthGameMode());
		MoodGameMode->OnMoodChanged.AddUniqueDynamic(this, &AMoodCharacter::OnMoodChanged);
		MoodGameMode->OnSlowMotionTriggered.AddUniqueDynamic(this, &AMoodCharacter::OnSlowMotionTriggered);
		MoodGameMode->OnSlowMotionEnded.AddUniqueDynamic(this, &AMoodCharacter::OnSlowMotionEnded);
	}
	
	WalkingSpeed = GetCharacterMovement()->MaxWalkSpeed;
	WalkingFOV = FirstPersonCameraComponent->FieldOfView;

	HealthComponent->OnHurt.AddUniqueDynamic(this, &AMoodCharacter::LoseHealth);
	HealthComponent->OnDeath.AddUniqueDynamic(this, &AMoodCharacter::KillPlayer);
	WeaponSlotComponent->OnWeaponUsed.AddUniqueDynamic(this, &AMoodCharacter::ShootCameraShake);
}

void AMoodCharacter::Tick(float const DeltaTime)
{
	Super::Tick(DeltaTime);

	bIsMidAir = GetCharacterMovement()->Velocity.Z != 0 ? 1 : 0;

	CheckPlayerState();
	FindLedge();
	MoveToExecutee();
	FindExecutee();
	RegenerateHealth();
}

void AMoodCharacter::Jump()
{
	if (CurrentState != Eps_NoControl)
		Super::Jump();
}

void AMoodCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	auto PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController) { return; }

	PlayerController->PlayerCameraManager->StartCameraShake(LandShake, 1.0f);
}

//////////////////////////////////////////////////////////////////////////// Input

void AMoodCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AMoodCharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Climbing
		EnhancedInputComponent->BindAction(ClimbAction, ETriggerEvent::Started, this, &AMoodCharacter::AttemptClimb);
		EnhancedInputComponent->BindAction(ClimbAction, ETriggerEvent::Completed, this, &AMoodCharacter::DontClimb);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMoodCharacter::Move);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Triggered, this, &AMoodCharacter::Sprint);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this,
		                                   &AMoodCharacter::StopSprinting);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMoodCharacter::Look);

		// Attacking
		EnhancedInputComponent->BindAction(ShootAction, ETriggerEvent::Triggered, this, &AMoodCharacter::ShootWeapon);
		EnhancedInputComponent->BindAction(ShootAction, ETriggerEvent::Canceled, this,
		                                   &AMoodCharacter::StopShootWeapon);
		EnhancedInputComponent->BindAction(ExecuteAction, ETriggerEvent::Triggered, this, &AMoodCharacter::ToggleExecute);

		// Weapon Selection
		EnhancedInputComponent->BindAction(ScrollWeaponAction, ETriggerEvent::Triggered, this,
		                                   &AMoodCharacter::WeaponScroll);
		EnhancedInputComponent->BindAction(SelectWeapon1Action, ETriggerEvent::Triggered, this,
		                                   &AMoodCharacter::SelectWeapon1);
		EnhancedInputComponent->BindAction(SelectWeapon2Action, ETriggerEvent::Triggered, this,
		                                   &AMoodCharacter::SelectWeapon2);
		EnhancedInputComponent->BindAction(SelectWeapon3Action, ETriggerEvent::Triggered, this,
		                                   &AMoodCharacter::SelectWeapon3);

		// Interact 
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Triggered, this,
		                                   &AMoodCharacter::ToggleInteraction);

		// Pausing
		EnhancedInputComponent->BindAction(PauseAction, ETriggerEvent::Triggered, this, &AMoodCharacter::PauseGame);
	}

	else
	{
		UE_LOG(LogTemplateCharacter, Error,
		       TEXT(
			       "'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."
		       ), *GetNameSafe(this));
	}
}

void AMoodCharacter::CheckPlayerState()
{
	switch (CurrentState)
	{
	case Eps_Idle:
		FirstPersonCameraComponent->FieldOfView = FMath::Lerp(FirstPersonCameraComponent->FieldOfView, WalkingFOV,
		                                                      AlphaFOV);
		if (!bIsMidAir)
			GetWorld()->GetFirstPlayerController()->PlayerCameraManager->StartCameraShake(IdleHeadBob, 1.f);
		if (GetCharacterMovement()->Velocity != FVector(0, 0, 0))
			CurrentState = Eps_Walking;
		break;
	case Eps_Walking:
		FirstPersonCameraComponent->FieldOfView = FMath::Lerp(FirstPersonCameraComponent->FieldOfView, WalkingFOV,
		                                                      AlphaFOV);
		GetCharacterMovement()->MaxWalkSpeed = WalkingSpeed;
		if (!bIsMidAir)
			GetWorld()->GetFirstPlayerController()->PlayerCameraManager->StartCameraShake(WalkHeadBob, 1.f);
		if (GetCharacterMovement()->Velocity.Length() < 10.f)
			CurrentState = Eps_Idle;
		break;
	case Eps_Sprinting:
		GetCharacterMovement()->MaxWalkSpeed = SprintingSpeed;
		FirstPersonCameraComponent->FieldOfView = FMath::Lerp(FirstPersonCameraComponent->FieldOfView, SprintingFOV,
		                                                      AlphaFOV);
		if (!bIsMidAir)
			GetWorld()->GetFirstPlayerController()->PlayerCameraManager->StartCameraShake(SprintHeadBob, 1.f);
		if (GetCharacterMovement()->Velocity.Length() < 10.f)
			StopSprinting();
		break;
	case Eps_ClimbingLedge:
		GetCharacterMovement()->Velocity = FVector(0, 0, 0);
		GetCharacterMovement()->MaxWalkSpeed = WalkingSpeed;
		GetWorld()->GetFirstPlayerController()->PlayerCameraManager->StartCameraShake(WalkHeadBob, 1.f);
		StopShootWeapon();
		TimeSinceClimbStart += GetWorld()->DeltaTimeSeconds;
		if (TimeSinceClimbStart >= ClimbingTime)
			CurrentState = Eps_Walking;
		break;
	case Eps_NoControl:
		StopShootWeapon();
		DeathCamMovement();
		break;

	default:
		UE_LOG(LogTemp, Error, TEXT("No player state found. MoodCharacter.cpp - CheckPlayerState"));
		CurrentState = Eps_Idle;
		break;
	}

	// Making sure that the player has rotated back after dying. There is a rare bug. 
	if (CurrentState != Eps_NoControl && FirstPersonCameraComponent->GetComponentRotation().Roll > 0)
		GetController()->SetControlRotation(FRotator(GetControlRotation().Pitch, GetControlRotation().Yaw,
															 0.00f));
}

void AMoodCharacter::OnMoodChanged(EMoodState NewState)
{
	switch (NewState)
	{
	case Ems_Mood666:
		MoodSpeedPercent = 1.5f;
		MoodDamagePercent = 2.f;
		MoodHealthLoss = 0.9f;
		break;
	case Ems_Mood444:
		MoodSpeedPercent = 1.2f;
		MoodDamagePercent = 1.6f;
		MoodHealthLoss = 0.9f;
		break;
	case Ems_Mood222:
		MoodSpeedPercent = 1.1f;
		MoodDamagePercent = 1.3f;
		MoodHealthLoss = 1.f;
		break;
	case Ems_NoMood:
		MoodSpeedPercent = 1.f;
		MoodDamagePercent = 1.f;
		MoodHealthLoss = 1.f;
		break;
	default:
		return;
	}

	ActivateHealthRegen(NewState);
	WeaponSlotComponent->SetDamageMultiplier(MoodDamagePercent);
	HealthComponent->AlterHealthLoss(MoodHealthLoss);
}

void AMoodCharacter::OnSlowMotionTriggered(EMoodState NewState)
{
	bIsSlowMotion = true;
	HealthComponent->Heal(50);
	SetWeaponFireRate();
}

void AMoodCharacter::OnSlowMotionEnded()
{
	bIsSlowMotion = false;
	SetWeaponFireRate();
}

void AMoodCharacter::SetWeaponFireRate()
{
	WeaponSlotComponent->GetSelectedWeapon()->SetSlowMotion(bIsSlowMotion ? true : false);
}

void AMoodCharacter::AttemptClimb()
{
	bCanClimb = true;
}

void AMoodCharacter::DontClimb()
{
	bCanClimb = false;
}

void AMoodCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr && CurrentState != Eps_NoControl)
	{
		const FVector2D MoodSpeed = MovementVector * MoodSpeedPercent;
		AddMovementInput(GetActorForwardVector(), MoodSpeed.Y);
		AddMovementInput(GetActorRightVector(), MoodSpeed.X);
	}
}

void AMoodCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr && CurrentState != Eps_NoControl)
	{
		FVector2D TotalLookAxis = LookAxisVector * CameraSpeed;

		if (bIsSlowMotion)
		{
			AddControllerYawInput(TotalLookAxis.X *= SlowMotionCameraSpeed);
			AddControllerPitchInput(TotalLookAxis.Y *= SlowMotionCameraSpeed);		
		}
		else
		{
			AddControllerYawInput(TotalLookAxis.X);
			AddControllerPitchInput(TotalLookAxis.Y);
		}
	}
}

void AMoodCharacter::WeaponScroll(const FInputActionValue& Value)
{
	if (WeaponSlotComponent == nullptr || !WeaponSlotComponent->HasWeapon())
		return;

	const float ScrollDirection = Value.Get<float>();

	if (ScrollDirection > 0)
		WeaponSlotComponent->SelectNextWeapon();
	else if (ScrollDirection < 0)
		WeaponSlotComponent->SelectPreviousWeapon();

	SetWeaponFireRate();		
}

void AMoodCharacter::SelectWeapon1()
{
	if (WeaponSlotComponent == nullptr || !WeaponSlotComponent->HasWeapon())
		return;

	WeaponSlotComponent->SelectWeapon(0);
	SetWeaponFireRate();
}

void AMoodCharacter::SelectWeapon2()
{
	if (WeaponSlotComponent == nullptr || !WeaponSlotComponent->HasWeapon())
		return;

	WeaponSlotComponent->SelectWeapon(1);
	SetWeaponFireRate();
}

void AMoodCharacter::SelectWeapon3()
{
	if (WeaponSlotComponent == nullptr || !WeaponSlotComponent->HasWeapon())
		return;

	WeaponSlotComponent->SelectWeapon(2);
	SetWeaponFireRate();
}

void AMoodCharacter::PauseGame()
{
	if (CurrentState == Eps_NoControl)
		return;
	
	OnPaused.Broadcast();
}

void AMoodCharacter::ShootCameraShake(UMoodWeaponComponent* Weapon)
{
	auto PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController) { return; }

	PlayerController->PlayerCameraManager->StartCameraShake(Weapon->GetRecoilCameraShake(), 1.0f);
}

void AMoodCharacter::LoseHealth(int Amount, int NewHealth)
{
	MoodGameMode->ChangeMoodValue(-Amount);

	if (const int RandomValue = FMath::RandRange(0, 9); RandomValue > 6)
		UGameplayStatics::PlaySound2D(GetWorld(), PlayerHurtSound);
}

void AMoodCharacter::ToggleInteraction()
{
}

void AMoodCharacter::ResetPlayer()
{
	bHasRespawned = true;
}

void AMoodCharacter::Sprint()
{
	if (GetCharacterMovement()->Velocity.Length() > 10.f
		&& CurrentState != Eps_ClimbingLedge
		&& CurrentState != Eps_NoControl)
	{
		CurrentState = Eps_Sprinting;
	}
}

void AMoodCharacter::StopSprinting()
{
	CurrentState = Eps_Walking;
}

void AMoodCharacter::FindExecutee()
{
	if (CurrentState == Eps_ClimbingLedge || CurrentState == Eps_NoControl || bIsExecuting /*|| bIsSlowMotion*/)
		return;
	
	FHitResult HitResult;
	FCollisionQueryParams Parameters;
	Parameters.AddIgnoredActor(this);

	const FVector TraceStart = FirstPersonCameraComponent->GetComponentLocation();
	const FVector TraceEnd = FirstPersonCameraComponent->GetComponentLocation()
		+ FirstPersonCameraComponent->GetForwardVector() * ExecutionDistance;

	const FVector ShortTraceEnd = FirstPersonCameraComponent->GetComponentLocation()
		+ FirstPersonCameraComponent->GetForwardVector() * 120;

	const auto ShortEnemyTrace = GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, ShortTraceEnd, InterruptClimbingChannel, Parameters,
													 FCollisionResponseParams());
	const auto EnemyTrace = GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, InterruptClimbingChannel, Parameters,
	                                                             FCollisionResponseParams());
	if (!EnemyTrace)
	{
		bHasFoundExecutableEnemy = false;
		return;
	}
	
	FoundActor = HitResult.GetActor();
	Executee = Cast<AMoodEnemyCharacter>(FoundActor);
	if (!IsValid(Executee))
	{
		bHasFoundExecutableEnemy = false;
		return;
	}
	ExecuteeHealth = Executee->FindComponentByClass<UMoodHealthComponent>();

	if (!IsValid(ExecuteeHealth))
	{
		bHasFoundExecutableEnemy = false;
		return;
	}
	
	if (!ShortEnemyTrace)
	{
		const auto ObstacleTrace = UKismetSystemLibrary::CapsuleTraceSingleForObjects(
		GetWorld(),
		TraceStart,
		FoundActor->GetActorLocation(),
		GetCapsuleComponent()->GetScaledCapsuleRadius() - 30,
		GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 30,
		ObstacleObjectTypes,
		false,
		{ this },
		EDrawDebugTrace::None,
		HitResult,
		true);

		if (ObstacleTrace)
		{
			bHasFoundExecutableEnemy = false;
			return;
		}
	}
	
	if (ExecuteeHealth->HealthPercent() <= ExecutionThresholdEnemyHP
		&& ExecuteeHealth->HealthPercent() > 0.f
		&& (Executee->GetActorLocation() - GetActorLocation()).Length() <= (TraceEnd - TraceStart).Length())
	{
		bHasFoundExecutableEnemy = true;
	}
	else
		bHasFoundExecutableEnemy = false;
}

void AMoodCharacter::ToggleExecute()
{
	if (!bHasFoundExecutableEnemy || bIsExecuting || CurrentState == Eps_NoControl)
		return;

	TimeSinceExecutionStart = 0.f;
	
	if (!IsValid(Executee) || !IsValid(ExecuteeHealth))
	{
		bIsExecuting = false;
		bHasFoundExecutableEnemy = false;
		UE_LOG(LogTemp, Error, TEXT("AMoodCharacter::ToggleExecute - Executee is invalid"));
		return;
	}
	
	bIsExecuting = true;
	CurrentState = Eps_NoControl;
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), ExecutionTimeDilation);
	UGameplayStatics::PlaySound2D(GetWorld(), ExecutionSprint);
}

void AMoodCharacter::MoveToExecutee()
{
	if (!bIsExecuting)
		return;

	if (TimeSinceExecutionStart < 2.f)
		TimeSinceExecutionStart += GetWorld()->DeltaTimeSeconds;
	
	const auto PlayerLocation = FMath::Lerp(
		GetActorLocation(),
		Executee->GetActorLocation(),
		MoveToExecuteTime * GetWorld()->DeltaTimeSeconds
		); 
	SetActorLocation(PlayerLocation);
	
	if ((Executee->GetActorLocation() - GetActorLocation()).Length() < 150.f)
	{
		ExecuteFoundEnemy();
	}

	// If the executee can't be reached within 1 sec, then abort
	if (TimeSinceExecutionStart >= 1.f)
	{
		UE_LOG(LogTemp, Error, TEXT("AMoodCharacter: Couldn't reach enemy."))
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
		bIsExecuting = false;
		bHasFoundExecutableEnemy = false;
		CurrentState = Eps_Walking;
	}
}

void AMoodCharacter::ExecuteFoundEnemy()
{
	if (IsValid(Executee) && IsValid(ExecuteeHealth))
	{
		GetWorld()->GetFirstPlayerController()->PlayerCameraManager->StartCameraShake(ExecuteShake, 1.f);
		ExecuteeHealth->Hurt(ExecutionDamage);
		MoodGameMode->ChangeMoodValue(ExecutionDamage);
		HealthComponent->Heal(ExecutionHealing);
		Executee = nullptr;
		ExecuteeHealth = nullptr;
	}
	else
		UE_LOG(LogTemp, Error, TEXT("AMoodCharacter: Executee or ExecuteeHealth are invalid."))

	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
	bIsExecuting = false;
	bHasFoundExecutableEnemy = false;
	CurrentState = Eps_Walking;
}

void AMoodCharacter::ShootWeapon()
{
	if (CurrentState != Eps_ClimbingLedge && CurrentState != Eps_NoControl)
	{
		WeaponSlotComponent->SetTriggerHeld(true);
	}
}

void AMoodCharacter::StopShootWeapon()
{
	WeaponSlotComponent->SetTriggerHeld(false);
}

void AMoodCharacter::FindLedge()
{
	if (CurrentState == Eps_ClimbingLedge || CurrentState == Eps_NoControl || !bCanClimb)
		return;

	FHitResult BottomHitResult;
	FHitResult TopHitResult;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const FVector BottomTraceStart = GetActorLocation() + GetActorUpVector() * ReachLedgeLocation.Z;
	const FVector BottomTraceEnd = GetActorLocation() + GetActorForwardVector() * ReachLedgeLocation.X + GetActorUpVector() * ReachLedgeLocation.Z;

	const FVector TopTraceStart = GetActorLocation() + GetActorUpVector() * 60.f;
	const FVector TopTraceEnd = GetActorLocation() + GetActorForwardVector() * 80.f + GetActorUpVector() * 60.f;

	auto WallInFront = GetWorld()->LineTraceSingleByChannel(BottomHitResult, BottomTraceStart, BottomTraceEnd,
	                                                        ClimbableChannel, QueryParams, FCollisionResponseParams());
	auto WallAbove = GetWorld()->LineTraceSingleByChannel(TopHitResult, TopTraceStart, TopTraceEnd,
	                                                      InterruptClimbingChannel, QueryParams,
	                                                      FCollisionResponseParams());
	if (WallInFront && !WallAbove && bIsMidAir)
	{
		TimeSinceClimbStart = 0.f;
		CurrentState = Eps_ClimbingLedge;

		FLatentActionInfo LatentInfo;
		LatentInfo.CallbackTarget = Owner;

		UKismetSystemLibrary::MoveComponentTo(
			RootComponent,
			GetActorLocation() + GetActorForwardVector() * ClimbingLocation.X + GetActorUpVector() * ClimbingLocation.Z,
			GetActorRotation(),
			true,
			true,
			ClimbingTime,
			false,
			EMoveComponentAction::Move,
			LatentInfo);
	}
}

void AMoodCharacter::KillPlayer(AActor* DeadActor)
{
	bIsDead = true;
	MoodGameMode->ResetMoodValue();
	CurrentState = Eps_NoControl;
}

void AMoodCharacter::RevivePlayer()
{
	HealthComponent->Reset();
}

void AMoodCharacter::ActivateHealthRegen(EMoodState CurrentMood)
{
	bIsGeneratingHealth = CurrentMood == Ems_Mood666 ? 1 : 0;
}

void AMoodCharacter::RegenerateHealth()
{
	if (!bIsGeneratingHealth)
		return;

	TimeSinceHealthRegenerated += GetWorld()->DeltaTimeSeconds;
	if (TimeSinceHealthRegenerated >= HealthGenerationDelay)
	{
		HealthComponent->Heal(HealthGenerationAmount);
		TimeSinceHealthRegenerated = 0.f;
	}
}

void AMoodCharacter::DeathCamMovement()
{
	if (bIsDead)
	{
		if (FirstPersonCameraComponent->GetComponentRotation().Roll < 30.f && !bHasRespawned)
		{
			GetController()->SetControlRotation(FMath::Lerp(GetControlRotation(),
			                                                FRotator(GetControlRotation().Pitch,
			                                                         GetControlRotation().Yaw, 40), 1.25f * GetWorld()->DeltaTimeSeconds));
			AddMovementInput(GetActorForwardVector() * GetWorld()->DeltaTimeSeconds * DeathFallSpeed / 4);
			AddMovementInput(GetActorRightVector() * GetWorld()->DeltaTimeSeconds * DeathFallSpeed / 4);
		}

		if (bHasRespawned)
		{
			if (FirstPersonCameraComponent->GetComponentRotation().Roll > 0)
			{
				GetController()->SetControlRotation(FMath::Lerp(GetControlRotation(),
				                                                FRotator(GetControlRotation().Pitch,
				                                                         GetControlRotation().Yaw, -2), 2.5f * GetWorld()->DeltaTimeSeconds));
			}

			else
			{
				GetController()->SetControlRotation(FRotator(GetControlRotation().Pitch, GetControlRotation().Yaw,
				                                             0.00f));
				bIsDead = false;
				bHasRespawned = false;
				CurrentState = Eps_Idle;
				RevivePlayer();
			}
		}
	}
}
