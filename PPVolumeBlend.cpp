struct FPostProcessVolumeProperties
{
	const FPostProcessSettings* Settings;
	float Priority;
	float BlendRadius;
	float BlendWeight;
	bool bIsEnabled;
	bool bIsUnbound;
};

class IInterface_PostProcessVolume {};
class ENGINE_API APostProcessVolume : public AVolume, public IInterface_PostProcessVolume {}
class ENGINE_API UWorld final : public UObject, public FNetworkNotify {
	TArray< IInterface_PostProcessVolume * > PostProcessVolumes;/** An array of post processing volumes, sorted in ascending order of priority.	*/
}

void UWorld::InsertPostProcessVolume(IInterface_PostProcessVolume* InVolume)
{
	const int32 NumVolumes = PostProcessVolumes.Num();
	float TargetPriority = InVolume->GetProperties().Priority;
	int32 i = 0;
	for (; i < PostProcessVolumes.Num(); i++)
	{
		float CurrentPriority = PostProcessVolumes[i]->GetProperties().Priority;

		if (TargetPriority < CurrentPriority)
		{
			break;
		}
	}
	PostProcessVolumes.Insert(InVolume, i);
}




void FSceneView::StartFinalPostprocessSettings(FVector InViewLocation)
{
	...
	World->AddPostProcessingSettings(InViewLocation, this);
}

void UWorld::AddPostProcessingSettings(FVector ViewLocation, FSceneView* SceneView)
{
	OnBeginPostProcessSettings.Broadcast(ViewLocation, SceneView);

	for (IInterface_PostProcessVolume* PPVolume : PostProcessVolumes)
	{
		DoPostProcessVolume(PPVolume, ViewLocation, SceneView);
	}
}

void DoPostProcessVolume(IInterface_PostProcessVolume* Volume, FVector ViewLocation, FSceneView* SceneView)
{
	const FPostProcessVolumeProperties VolumeProperties = Volume->GetProperties();
	if (!VolumeProperties.bIsEnabled)
	{
		return;
	}

	float LocalWeight = FMath::Clamp(VolumeProperties.BlendWeight, 0.0f, 1.0f);

	if (!VolumeProperties.bIsUnbound)
	{
		float DistanceToPoint = 0.0f;
		Volume->EncompassesPoint(ViewLocation, 0.0f, &DistanceToPoint); // DistanceToPoint is zero if ViewLocation is in the volume, and is positive otherwise.

		if (DistanceToPoint >= 0)
		{
			if (DistanceToPoint > VolumeProperties.BlendRadius)
			{
				LocalWeight = 0.0f;
			}
			else
			{
				if (VolumeProperties.BlendRadius > 0.0f)
				{
					LocalWeight *= 1.0f - DistanceToPoint / VolumeProperties.BlendRadius;
				}
			}
		}
		else
		{
			LocalWeight = 0;
		}
	}

	if (LocalWeight > 0)
	{
		SceneView->OverridePostProcessSettings(*VolumeProperties.Settings, LocalWeight);
	}
}

void FSceneView::OverridePostProcessSettings(const FPostProcessSettings& Src, float Weight)
{
	FFinalPostProcessSettings& Dest = FinalPostProcessSettings;
	...
}







class ENGINE_API FSceneView {
	FFinalPostProcessSettings FinalPostProcessSettings;
	FSBEnvControlSettings FinalEnvControlSettings;
}

struct FSBEnvControlSettings {
	float Priority;
	TArray<FPostProcessSettings> BlendPPSettings;
	TArray<float> BlendPPSettingsWeight;

	FPostProcessSettings PostProcessData;
}


void UWorld::AddPostProcessingSettings(FVector ViewLocation, FSceneView* SceneView)
{
	OnBeginPostProcessSettings.Broadcast(ViewLocation, SceneView);
	
	for (IInterface_PostProcessVolume* PPVolume : PostProcessVolumes)
	{
		DoPostProcessVolume(PPVolume, ViewLocation, SceneView);
	}
	

	if (GetWorldSettings() && SceneView)
	{
		SceneView->bEnableEnvControlSettings = GetWorldSettings()->CalcEnvControlSettings(ViewLocation, SceneView->FinalEnvControlSettings);
		...
	}

	...
}

bool AWorldSettings::CalcEnvControlSettings(FVector InViewLocation, FSBEnvControlSettings& OutSettings)
{
	TArray<FSBEnvControlSettings> InVolumeObjects;
	InVolumeObjects.Empty();

	TArray<float> InVolumeObjectWeights;
	InVolumeObjectWeights.Empty();

	for (int32 i = 0; i < WorldEnvControlSettings.Num(); i++)
	{
		AVolume* pVolume = WorldEnvControlSettings[i].VolumeActor;
		if (pVolume && pVolume->IsValidLowLevelFast() && !pVolume->IsPendingKill())
		{
			float Weight = pVolume->GetBlendWeight(InViewLocation);
			if (Weight > 0.0f)
			{
				InVolumeObjects.Add(WorldEnvControlSettings[i]);

			}
		}
	}

	InVolumeObjects.Sort(FSBEnvControlSettings::Compare);
	// 동일 객체를 이용하는 다수의 볼륨의 경우 Blend 시, 정확한 처리가 되지 않는다 Lerp 하므로.
	// 하여, 그런 경우 통합처리 한다
	for (int32 i = 0; i < InVolumeObjects.Num(); i++)
	{
		float Weight = InVolumeObjects[i].VolumeActor->GetBlendWeight(InViewLocation);
		InVolumeObjectWeights.Add(Weight);
	}

	for (int32 i = 0; i < InVolumeObjects.Num(); i++)
	{
		UObject* pCheckDataPtr = InVolumeObjects[i].DataPtr;
		for (int32 j = i + 1; j < InVolumeObjects.Num(); j++)
		{
			UObject* pDataPtr = InVolumeObjects[j].DataPtr;
			if (pCheckDataPtr == pDataPtr && InVolumeObjectWeights[j] > 0.0f)
			{
				InVolumeObjectWeights[i] += InVolumeObjectWeights[j];
				InVolumeObjectWeights[j] = 0.0f;
			}
		}
	}

	for (int32 i = InVolumeObjects.Num() - 1; i >= 0; i--)
	{
		if (InVolumeObjectWeights[i] <= 0.0f)
		{
			InVolumeObjectWeights.RemoveAt(i);
			InVolumeObjects.RemoveAt(i);
		}

	}

	OutSettings.Init();

	float TotalWeight = 0.0f;

	for (int32 i = 0; i < InVolumeObjects.Num(); i++)
	{
		AVolume* pVolume = InVolumeObjects[i].VolumeActor;
		if (pVolume && pVolume->IsValidLowLevelFast() && !pVolume->IsPendingKill())
		{
			float RemainWeight = 1.0f - TotalWeight;

			float Weight = InVolumeObjectWeights[i];// pVolume->GetBlendWeight(InViewLocation);
			Weight = FMath::Clamp<float>(Weight, 0.0f, 1.0f);

			if (Weight > RemainWeight)
			{
				Weight = RemainWeight;
			}
			if (Weight > 0.0f)
			{
				OutSettings.Blend(InVolumeObjects[i], Weight);
				bSet = true;
				TotalWeight += Weight;
				if (TotalWeight >= 1.0f)
				{
					break;

				}
			}
		}
	}
}