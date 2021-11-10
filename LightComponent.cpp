
class ENGINE_API ULightComponentBase : public USceneComponent {
	float Brightness_DEPRECATED = 1415926535897932f;
	float Intensity = 3.1415926535897932f; /**Total energy that the light emits.*/
	FColor LightColor = FColor(255,255,255,255);
}

class ENGINE_API ULightComponent : public ULightComponentBase {
	FLightSceneProxy* SceneProxy;
}

class ENGINE_API UDirectionalLightComponent : public ULightComponent {}

enum class ELightUnits : uint8 {Unitless, Candelas, Lumens};

class ENGINE_API ULocalLightComponent : public ULightComponent {
	ELightUnits IntensityUnits;
	float AttenuationRadius = 1000.0f;
	ULocalLightComponent() { Intensity = 5000.0f; }
}

class ENGINE_API UPointLightComponent : public ULocalLightComponent {
	uint32 bUseInverseSquaredFalloff = true;
	float LightFalloffExponent = 8.0f;
	float SourceRadius = 0.0f;
	float SoftSourceRadius = 0.0f;
	float SourceLength = 0.0f;

};

class ENGINE_API USpotLightComponent : public UPointLightComponent {}
class ENGINE_API URectLightComponent : public ULocalLightComponent {}

static TAutoConsoleVariable<int32> CVarDefaultPointLightUnits(
	TEXT("r.DefaultFeature.LightUnits"),
	1,
	TEXT("Default units to use for point, spot and rect lights\n")
	TEXT(" 0: unitless \n")
	TEXT(" 1: candelas (default)\n")
	TEXT(" 2: lumens"));

void UActorFactoryPointLight::PostSpawnActor(UObject* Asset, AActor* NewActor) 
{
	ELightUnits DefaultUnits = TEXT("r.DefaultFeature.LightUnits")

	TArray<UPointLightComponent*> PointLightComponents;
	NewActor->GetComponents<UPointLightComponent>(PointLightComponents);

	for (UPointLightComponent* Component : PointLightComponents)
	{
		Component->Intensity *= UPointLightComponent::GetUnitsConversionFactor(Component->IntensityUnits, DefaultUnits);
		Component->IntensityUnits = DefaultUnits;
	}
}

float ULocalLightComponent::GetUnitsConversionFactor(ELightUnits SrcUnits, ELightUnits TargetUnits, float CosHalfConeAngle = -1)
{
	if (SrcUnits == TargetUnits)
	{
		return 1.f;
	}

	float CnvFactor = 1.f;
	
	if (SrcUnits == ELightUnits::Candelas)
	{
		CnvFactor = 100.f * 100.f;
	}
	else if (SrcUnits == ELightUnits::Lumens)
	{
		CnvFactor = 100.f * 100.f / 2.f / PI / (1.f - CosHalfConeAngle);
	}
	else
	{
		CnvFactor = 16.f;
	}

	if (TargetUnits == ELightUnits::Candelas)
	{
		CnvFactor *= 1.f / 100.f / 100.f;
	}
	else if (TargetUnits == ELightUnits::Lumens)
	{
		CnvFactor *= 2.f  * PI * (1.f - CosHalfConeAngle) / 100.f / 100.f;
	}
	else
	{
		CnvFactor *= 1.f / 16.f;
	}

	return CnvFactor;	
}

1 cd = 625 unitless
8 cd = 5000 unitless
0.0016 cd = 1 unitless

1 lm = 625/(4*PI) unitless

