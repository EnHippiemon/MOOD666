﻿#include "MoodUserSettings.h"

float UMoodUserSettings::GetMusicVolume()
{
	return MusicVolume;
}

void UMoodUserSettings::SetMusicVolume(float NewVolume)
{
	MusicVolume = NewVolume;
}

float UMoodUserSettings::GetSFXVolume()
{
	return SFXVolume;
}

void UMoodUserSettings::SetSFXVolume(float NewVolume)
{
	SFXVolume = NewVolume;
}

bool UMoodUserSettings::GetSettingsExists()
{
	return bSettingsExists;
}

void UMoodUserSettings::SetSettingsExists(bool Exists)
{
	bSettingsExists = Exists;
}
