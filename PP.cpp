

#define LERP_PP(NAME) if(Src.bOverride_ ## NAME)	Dest . NAME = FMath::Lerp(Dest . NAME, Src . NAME, Weight);

void FSceneView::OverridePostProcessSettings(const FPostProcessSettings& Src, float Weight)
{
	FFinalPostProcessSettings& Dest = FinalPostProcessSettings;
	
	// LERP_PP(AmbientOcclusionRadius);
	if(Src.bOverride_AmbientOcclusionRadius)	
		Dest.NAME = FMath::Lerp( Dest.AmbientOcclusionRadius, Src.AmbientOcclusionRadius, Weight );

}


class SB_API ASBEnvControlVolume : public AVolume
{
	float Priority;
	float BlendRadius;
	bool bIsUnbound;
	USBEnvSettingData* EnvSettingData;
}

class SB_API USBEnvSettingData : public UDataAsset
{
	FRotator SunDirection;
	TArray<FSBEnvSettingScalarParam> MaterialScalarParams;
	TArray<FSBEnvSettingVectorParam> MaterialVectorParams;
	TArray<FSBEnvSettingTextureParam> MaterialTextureParams;
	FSBEnvControlSettings EnvControlSetting;
}

struct FSBEnvControlSettings
{
	float Priority;
	FVector Origin;
	float InnerRadius;
	float OuterRadius;
	AVolume* VolumeActor;
	UObject* DataPtr;
	TArray<FPostProcessSettings> BlendPPSettings;
	TArray<float> BlendPPSettingsWeight;

	FVector Direction_Sun;
	...
	...
	float SunDiscScale_AS;

	FPostProcessSettings PostProcessData;
}

class FSceneView
{
	FFinalPostProcessSettings FinalPostProcessSettings;
	bool bEnableEnvControlSettings;
	FSBEnvControlSettings FinalEnvControlSettings;
}

class FFinalPostProcessSettings : public FPostProcessSettings
{	
}

struct FPostProcessSettings
{
	uint8 bOverride_WhiteTemp:1;
	...
	...
	float ScreenPercentage;
	FWeightedBlendables WeightedBlendables;
}