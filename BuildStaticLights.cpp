
class FStaticLightingSystem {
	TArray<ULightComponentBase*> Lights;/** The lights in the world which the system is building. */
	const FLightingBuildOptions Options;/** The options the system is building lighting with. */
}

bool FStaticLightingSystem::BeginLightmassProcess()
{
	for (TObjectIterator<ULightComponentBase> LightIt(RF_ClassDefaultObject, /** bIncludeDerivedClasses */ true, /** InternalExcludeFlags */ EInternalObjectFlags::PendingKill); LightIt; ++LightIt)
	{
		ULightComponentBase* const Light = *LightIt;
		const bool bLightIsInWorld = Light->GetOwner() 
			&& World->ContainsActor(Light->GetOwner())
			&& !Light->GetOwner()->IsPendingKill();

		if (bLightIsInWorld && ShouldOperateOnLevel(Light->GetOwner()->GetLevel()))
		{
			if (Light->bAffectsWorld 
				&& Light->IsRegistered()
				&& (Light->HasStaticShadowing() || Light->HasStaticLighting()))
			{
				Light->ValidateLightGUIDs();
				Lights.Add(Light);
			}
		}
	}

	GatherStaticLightingInfo(bRebuildDirtyGeometryForLighting, bForceNoPrecomputedLighting);

	CreateLightmassProcessor();

	GatherScene();

	InitiateLightmassProcessor();
}

void FStaticLightingSystem::GatherStaticLightingInfo(bool bRebuildDirtyGeometryForLighting, bool bForceNoPrecomputedLighting)
{
	// Gather static lighting info from actors.
	for (int32 ActorIndex = 0; ActorIndex < Level->Actors.Num(); ActorIndex++)
	{
		AActor* Actor = Level->Actors[ActorIndex];
		if (Actor)
		{
			TInlineComponentArray<UPrimitiveComponent*> Components;
			Actor->GetComponents(Components);

			TArray<UPrimitiveComponent*> HLODPrimitiveParents;
			PrimitiveActorMap.MultiFind(Actor, HLODPrimitiveParents);
			
			// Gather static lighting info from each of the actor's components.
			for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
			{
				UPrimitiveComponent* Primitive = Components[ComponentIndex];

				// Find the lights relevant to the primitive.
				TArray<ULightComponent*> PrimitiveRelevantLights;
				for (int32 LightIndex = 0; LightIndex < Lights.Num(); LightIndex++)
				{
					ULightComponent* Light = Cast<ULightComponent>( Lights[LightIndex] );

					if (Light && Light->AffectsPrimitive(Primitive))
					{
						PrimitiveRelevantLights.Add(Light);
					}
				}

				// Query the component for its static lighting info.
				FStaticLightingPrimitiveInfo PrimitiveInfo;
				Primitive->GetStaticLightingInfo(PrimitiveInfo, PrimitiveRelevantLights, Options);

				if (PrimitiveInfo.Meshes.Num() > 0 && (Primitive->Mobility == EComponentMobility::Static))
				{
					if (World->GetWorldSettings()->bPrecomputeVisibility)
					{
						bMarkLevelDirty = true;// Make sure the level gets dirtied since we are changing the visibility Id of a component in it
					}

					PrimitiveInfo.VisibilityId = Primitive->VisibilityId = NextVisibilityId;
					NextVisibilityId++;
				}

				AddPrimitiveStaticLightingInfo(PrimitiveInfo, true);
			}
		}
	}
}

void FStaticLightingSystem::AddPrimitiveStaticLightingInfo(FStaticLightingPrimitiveInfo& PrimitiveInfo, bool bBuildActorLighting)
{
}

void FStaticLightingSystem::GatherScene()
{
	AWorldSettings* WorldSettings = World->GetWorldSettings();
	LightmassExporter->SetLevelSettings(WorldSettings->LightmassSettings);
	LightmassExporter->SetNumUnusedLocalCores(Options.NumUnusedLocalCores);
	LightmassExporter->SetQualityLevel(Options.QualityLevel);

	// Meshes
	for( int32 MeshIdx=0; MeshIdx < Meshes.Num(); MeshIdx++ )
	{
		Meshes[MeshIdx]->ExportMeshInstance(LightmassExporter);
	}

	for( int32 MappingIdx=0; MappingIdx < Mappings.Num(); MappingIdx++ )
	{
		Mappings[MappingIdx]->ExportMapping(LightmassExporter);
	}

	for (int32 LightIndex = 0; LightIndex < Lights.Num(); LightIndex++)
	{
		USkyLightComponent* SkyLight = Cast<USkyLightComponent>(Lights[LightIndex]);

		if (SkyLight && (SkyLight->Mobility == EComponentMobility::Static || SkyLight->Mobility == EComponentMobility::Stationary))
		{
			LightmassExporter->AddLight(SkyLight);
		}
	}
}

bool FStaticLightingSystem::InitiateLightmassProcessor()
{
	LightmassProcessor->OpenJob();
	LightmassProcessor->InitiateExport();
}

void FLightmassProcessor::InitiateExport()
{
	Exporter->WriteToChannel(Statistics, DebugMappingGuid);
}

void FLightmassExporter::WriteToChannel( FLightmassStatistics& Stats, FGuid& DebugMappingGuid )
{
	WriteLights( Channel );
}

void FLightmassExporter::WriteLights( int32 Channel )
{
	for ( int32 LightIndex = 0; LightIndex < PointLights.Num(); ++LightIndex )
	{
		const UPointLightComponent* Light = PointLights[LightIndex];
		Lightmass::FLightData LightData;
		Lightmass::FPointLightData PointData;
		Copy( Light, LightData );
		LightData.IndirectLightingSaturation = Light->LightmassSettings.IndirectLightingSaturation;
		LightData.ShadowExponent = Light->LightmassSettings.ShadowExponent;
		LightData.ShadowResolutionScale = Light->ShadowResolutionScale;
		LightData.LightSourceRadius = FMath::Max( 1.0f, Light->SourceRadius );
		LightData.LightSourceLength = Light->SourceLength;

		TArray< uint8 > LightProfileTextureData;
		CopyLightProfile( Light, LightData, LightProfileTextureData );

		PointData.Radius = Light->AttenuationRadius;
		PointData.FalloffExponent = Light->LightFalloffExponent;
		PointData.LightTangent = Light->GetComponentTransform().GetUnitAxis(EAxis::Z);
		Swarm.WriteChannel( Channel, &LightData, sizeof(LightData) );
		Swarm.WriteChannel( Channel, LightProfileTextureData.GetData(), LightProfileTextureData.Num() * LightProfileTextureData.GetTypeSize() );
		Swarm.WriteChannel( Channel, &PointData, sizeof(PointData) );
	}
}

void Copy( const ULightComponentBase* In, Lightmass::FLightData& Out )
{	
	FMemory::Memzero(Out);

	Out.LightFlags = 0;
	if (In->CastShadows)
	{
		Out.LightFlags |= Lightmass::GI_LIGHT_CASTSHADOWS;
	}

	if (In->HasStaticLighting())
	{
		Out.LightFlags |= Lightmass::GI_LIGHT_HASSTATICSHADOWING;
		Out.LightFlags |= Lightmass::GI_LIGHT_HASSTATICLIGHTING;
	}
	else if (In->HasStaticShadowing())
	{
		Out.LightFlags |= Lightmass::GI_LIGHT_STORE_SEPARATE_SHADOW_FACTOR;
		Out.LightFlags |= Lightmass::GI_LIGHT_HASSTATICSHADOWING;
	}

	if (In->CastStaticShadows)
	{
		Out.LightFlags |= Lightmass::GI_LIGHT_CASTSTATICSHADOWS;
	}

	Out.Color = In->LightColor;

	// Set brightness here for light types that only derive from ULightComponentBase and not from ULightComponent
	Out.Brightness = In->Intensity;
	Out.Guid = In->LightGuid;
	Out.IndirectLightingScale = In->IndirectLightingIntensity;
}

void Copy( const ULightComponent* In, Lightmass::FLightData& Out )
{	
	Copy((const ULightComponentBase*)In, Out);

	const ULocalLightComponent* LocalLight = Cast<const ULocalLightComponent>(In);
	const UPointLightComponent* PointLight = Cast<const UPointLightComponent>(In);

	if( ( LocalLight && LocalLight->GetLightType() == LightType_Rect ) ||
		( PointLight && PointLight->bUseInverseSquaredFalloff ) )
	{
		Out.LightFlags |= Lightmass::GI_LIGHT_INVERSE_SQUARED;
	}

	if (In->GetLightmassSettings().bUseAreaShadowsForStationaryLight)
	{
		Out.LightFlags |= Lightmass::GI_LIGHT_USE_AREA_SHADOWS_FOR_SEPARATE_SHADOW_FACTOR;
	}

	Out.Brightness = In->ComputeLightBrightness();
	Out.Position = In->GetLightPosition();
	Out.Direction = In->GetDirection();

	if( In->bUseTemperature )
	{
		Out.Color *= FLinearColor::MakeFromColorTemperature(In->Temperature);
	}
}