#include "MoodMainMenuWidget.h"

#include "MoodGameInstance.h"
#include "MoodCyberButton.h"
#include "MoodLevelSelectWidget.h"
#include "MoodOptionsMenuWidget.h"
#include "Kismet/GameplayStatics.h"

void UMoodMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	MoodGameInstance = Cast<UMoodGameInstance>(UGameplayStatics::GetGameInstance(GetWorld()));
	StartGameButton->ButtonClickedSig.AddUniqueDynamic(this, &UMoodMainMenuWidget::RequestFadeAnim);
	LevelSelectButton->ButtonClickedSig.AddUniqueDynamic(this, &UMoodMainMenuWidget::ShowLevelSelectMenu);
	OptionsButton->ButtonClickedSig.AddUniqueDynamic(this, &UMoodMainMenuWidget::ShowOptionsMenu);
	ExitGameButton->ButtonClickedSig.AddUniqueDynamic(this, &UMoodMainMenuWidget::ExitGame);

	//LevelSelectWidget->SetVisibility(ESlateVisibility::Hidden);
	//OptionsMenuWidget->SetVisibility(ESlateVisibility::Hidden);
	
}

void UMoodMainMenuWidget::StartGame()
{

	if (MoodGameInstance != nullptr)
	{	
		UGameplayStatics::OpenLevelBySoftObjectPtr(GetWorld(), MoodGameInstance->Level1);
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, TEXT("Game Instance is nullptr in MainMenuWidget!"));
	}
	
}

void UMoodMainMenuWidget::RequestFadeAnim_Implementation()
{

}

void UMoodMainMenuWidget::ShowLevelSelectMenu()
{
	//LevelSelectWidget->SetVisibility(ESlateVisibility::Visible);
	LevelSelectWidget->OpenWidget();
}

void UMoodMainMenuWidget::ShowOptionsMenu()
{
	OptionsMenuWidget->OpenWidget();
}

void UMoodMainMenuWidget::ExitGame()
{
	//FGenericPlatformMisc::RequestExit(false);
	//UKismetSystemLibrary::QuitGame(GetWorld(),UGameplayStatics::GetPlayerController(GetWorld(), 0),EQuitPreference::Quit,true);
}
