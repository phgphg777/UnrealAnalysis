
class ENGINE_API ULightComponentBase : public USceneComponent {
	bool IsMovable() { return Mobility == EComponentMobility::Movable; }
	bool HasStaticLighting() { return Mobility == EComponentMobility::Static; }
	bool HasStaticShadowing() { return Mobility == EComponentMobility::Static || Mobility == EComponentMobility::Stationary; }
}

class ENGINE_API FLightSceneProxy {
	const uint8 bMovable = InComponent->IsMovable();
	const uint8 bStaticLighting = InComponent->HasStaticLighting();
	const uint8 bStaticShadowing = InComponent->HasStaticShadowing();

	bool IsMovable() const { return bMovable; }
	bool HasStaticLighting() { return bStaticLighting; }
	bool HasStaticShadowing() { return bStaticShadowing; }
}

class ENGINE_API FSkyLightSceneProxy {
	const uint8 bMovable = InComponent->IsMovable();
	uint8 bHasStaticLighting = InComponent->HasStaticLighting();
	uint8 bWantsStaticShadowing = InComponent->Mobility == EComponentMobility::Stationary;

	bool IsMovable() { return bMovable; }
}




class ENGINE_API UPrimitiveComponent : public USceneComponent, public INavRelevantInterface {
	bool HasStaticLighting() 
	{
		return (Mobility == EComponentMobility::Static || LightmapType == ELightmapType::ForceSurface) 
			&& SupportsStaticLighting();
	}
	virtual bool SupportsStaticLighting() { return false; }
	virtual bool IsPrecomputedLightingValid() { return false; }
	virtual bool HasValidSettingsForStaticLighting(false) { return HasStaticLighting(); }
}

class ENGINE_API UStaticMeshComponent : public UMeshComponent {
	virtual bool SupportsStaticLighting() { return true; }
	virtual bool IsPrecomputedLightingValid();
	virtual bool HasValidSettingsForStaticLighting(bool bOverlookInvalidComponents); 
}

bool UStaticMeshComponent::IsPrecomputedLightingValid() 
{
	if (LightmapType == ELightmapType::ForceVolumetric)
	{
		return true;
	}

	if (LODData.Num() > 0)
	{
		return GetMeshMapBuildData(LODData[0]) != NULL;
	}

	return false;
}

bool UStaticMeshComponent::HasValidSettingsForStaticLighting(false) 
{
	int32 LightMapWidth = 0, LightMapHeight = 0;
	GetLightMapResolution(LightMapWidth, LightMapHeight);

	return HasStaticLighting()
		&& GetStaticMesh()
		&& HasLightmapTextureCoordinates()
		&& LightMapWidth > 0 && LightMapHeight > 0;
}

class FPrimitiveSceneProxy {
	TEnumAsByte<EComponentMobility::Type> Mobility = InComponent->Mobility;
	
	/** True if the primitive will cache static lighting. */
	uint8 bStaticLighting = InComponent->HasStaticLighting();
	
	/** True if the primitive wants to use static lighting, but has invalid content settings to do so. */
	uint8 bHasValidSettingsForStaticLighting = InComponent->HasValidSettingsForStaticLighting(false);

	/** Whether the primitive should be statically lit but has unbuilt lighting, and a preview should be used. */
	uint8 bNeedsUnbuiltPreviewLighting = !InComponent->IsPrecomputedLightingValid() && bHasValidSettingsForStaticLighting;

	bool IsMovable() {return Mobility == EComponentMobility::Movable || Mobility == EComponentMobility::Stationary; }
	bool IsOftenMoving() { return Mobility == EComponentMobility::Movable; }
	bool IsStatic() { return Mobility == EComponentMobility::Static; }
	bool HasStaticLighting() { return bStaticLighting; }
	bool HasValidSettingsForStaticLighting() { return bHasValidSettingsForStaticLighting; }
	bool NeedsUnbuiltPreviewLighting() { return bNeedsUnbuiltPreviewLighting; }
}

NeedsUnbuiltPreviewLighting() => HasValidSettingsForStaticLighting() => HasStaticLighting()

// Assume bStaticLighting() == bHasValidSettingsForStaticLighting, StaticMeshSceneProxy
bool& states[2] = {bStaticLighting, bNeedsUnbuiltPreviewLighting};

/*GetMeshMapBuildData(LODData[0]) == NULL*/
								ELightmapType::Default	ELightmapType::Surface	ELightmapType::ForceVolumetric
EComponentMobility::Static 		{1, 1}					{1, 1}					{1, 0}
EComponentMobility::Movable 	{0, 0}					{1, 1}					{0, 0}

/*GetMeshMapBuildData(LODData[0]) != NULL*/
								ELightmapType::Default	ELightmapType::Surface	ELightmapType::ForceVolumetric
EComponentMobility::Static 		{1, 0}					{1, 0}					{1, 0}
EComponentMobility::Movable 	{0, 0}					{1, 0}					{0, 0}