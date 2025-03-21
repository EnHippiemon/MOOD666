﻿#include "MoodPickup.h"
#include "MoodPickUpComponent.h"

AMoodPickup::AMoodPickup() {
	Pickup = CreateDefaultSubobject<UMoodPickUpComponent>("Pickup");
	RootComponent = Pickup;

	Pickup->OnPickUp.AddDynamic(this, &AMoodPickup::PickedUp);
}

void AMoodPickup::Disable() {
	Pickup->OnPickUp.RemoveAll(this);
}

void AMoodPickup::PickedUp(ACharacter* Character) {
	Pickup->DisableInteraction(); 
}
