class ENGINE_API FShadowMap : private FDeferredCleanupInterface {
	TArray<FGuid> LightGuids;	// Set in FShadowMap2D::AllocateShadowMap() /** The GUIDs of lights which this shadow-map stores */
}
class ENGINE_API FShadowMap2D : public FShadowMap {
	UShadowMapTexture2D* Texture;	// Set in FShadowMap2D::EncodeSingleTexture()
	FVector2D CoordinateScale;		// Set in FShadowMap2D::EncodeSingleTexture()
	FVector2D CoordinateBias;		// Set in FShadowMap2D::EncodeSingleTexture()
	bool bChannelValid[4];			// Set in FShadowMap2D::EncodeSingleTexture()
	FVector4 InvUniformPenumbraSize;// Set in FShadowMap2D::EncodeSingleTexture()
};

void FShadowMap2D::EncodeTextures(UWorld* InWorld, ULevel* LightingScenario, bool bLightingSuccessful, bool bMultithreadedEncode)
{
	check(!bMultithreadedEncode);

	const int32 PackedLightAndShadowMapTextureSize = InWorld->GetWorldSettings()->PackedLightAndShadowMapTextureSize;

	ITextureCompressorModule* TextureCompressorModule = &FModuleManager::LoadModuleChecked<ITextureCompressorModule>(TEXTURE_COMPRESSOR_MODULENAME);

	/* Sort PendingShadowMaps.. */

	// Allocate texture space for each shadow-map.
	TIndirectArray<FShadowMapPendingTexture> PendingTextures;

	for (FShadowMapAllocationGroup& PendingGroup : PendingShadowMaps)
	{
		FShadowMapPendingTexture* Texture = nullptr;

		// Find an existing texture which the shadow-map can be stored in.
		// Shadowmaps will always be 4-pixel aligned...
		for (FShadowMapPendingTexture* ExistingTexture : PendingTextures)
		{
			if (ExistingTexture->AddElement(PendingGroup))
			{
				Texture = ExistingTexture;
				break;
			}
		}

		if (!Texture)
		{
			int32 MaxWidth = 0;
			int32 MaxHeight = 0;
			for (auto& Allocation : PendingGroup.Allocations)
			{
				MaxWidth = FMath::Max(MaxWidth, Allocation->MappedRect.Width());
				MaxHeight = FMath::Max(MaxHeight, Allocation->MappedRect.Height());
			}

			int32 NewTextureSizeX = PackedLightAndShadowMapTextureSize;
			int32 NewTextureSizeY = PackedLightAndShadowMapTextureSize;

			// Assumes identically-sized allocations, fit into the smallest square
			const int32 AllocationCountX = 
				FMath::CeilToInt(
					FMath::Sqrt(
						FMath::DivideAndRoundUp(PendingGroup.Allocations.Num() * MaxHeight, MaxWidth)
					)
				);
			const int32 AllocationCountY = FMath::DivideAndRoundUp(PendingGroup.Allocations.Num(), AllocationCountX);
			const int32 AllocationSizeX = AllocationCountX * MaxWidth;
			const int32 AllocationSizeY = AllocationCountY * MaxHeight;

			if (AllocationSizeX > NewTextureSizeX || AllocationSizeY > NewTextureSizeY)
			{
				NewTextureSizeX = FMath::RoundUpToPowerOfTwo(AllocationSizeX);
				NewTextureSizeY = FMath::RoundUpToPowerOfTwo(AllocationSizeY);
			}

			// If there is no existing appropriate texture, create a new one.
			Texture = new FShadowMapPendingTexture(NewTextureSizeX, NewTextureSizeY);
			PendingTextures.Add(Texture);
			Texture->Outer = PendingGroup.TextureOuter;
			Texture->Bounds = PendingGroup.Bounds;
			Texture->ShadowmapFlags = PendingGroup.ShadowmapFlags;
			verify(Texture->AddElement(PendingGroup));
		}

		// Give the texture ownership of the allocations
		for (auto& Allocation : PendingGroup.Allocations)
		{
			Texture->Allocations.Add(MoveTemp(Allocation));
		}
	}

	PendingShadowMaps.Empty();

	// Encode all the pending textures.
	for (int32 TextureIndex = 0; TextureIndex < PendingTextures.Num(); TextureIndex++)
	{
		FShadowMapPendingTexture& PendingTexture = PendingTextures[TextureIndex];
		PendingTexture.StartEncoding(LightingScenario, TextureCompressorModule);
	}

	for (auto& PendingTexture : PendingTextures)
	{
		PendingTexture->PostEncode();
	}
	
	for (auto& PendingTexture : PendingTextures)
	{
		PendingTexture->FinishCachingTextures();
	}

	PendingTextures.Empty();
}

void FShadowMapPendingTexture::CreateUObjects()
{
	ShadowMapTexture = NewObject<UShadowMapTexture2D>(Outer);
	bCreatedUObjects = true;
}

void FShadowMapPendingTexture::StartEncoding(ULevel* LightingScenario, ITextureCompressorModule* Compressor)
{
	if (!bUObjectsCreated)
	{
		CreateUObjects();
	}

	UShadowMapTexture2D* Texture = ShadowMapTexture;
	Texture->Filter			= GUseBilinearLightmaps ? TF_Default : TF_Nearest;
	Texture->SRGB			= false;// Signed distance field textures get stored in linear space, since they need more precision near .5.
	Texture->LODGroup		= TEXTUREGROUP_Shadowmap;
	Texture->ShadowmapFlags	= ShadowmapFlags;

	{
		// Create the uncompressed top mip-level.
		TArray< TArray<FFourDistanceFieldSamples> > MipData;
		FShadowMap2D::EncodeSingleTexture(LightingScenario, *this, Texture, MipData);

		Texture->Source.Init2DWithMipChain(GetSizeX(), GetSizeY(), TSF_BGRA8);
		Texture->MipGenSettings = TMGS_LeaveExistingMips;
		Texture->CompressionNone = true;

		// Copy the mip-map data into the UShadowMapTexture2D's mip-map array.
		for(int32 MipIndex = 0;MipIndex < MipData.Num();MipIndex++)
		{
			FColor* DestMipData = (FColor*) Texture->Source.LockMip(MipIndex);
			uint32 MipSizeX = FMath::Max<uint32>(1, GetSizeX() >> MipIndex);
			uint32 MipSizeY = FMath::Max<uint32>(1, GetSizeY() >> MipIndex);

			for(uint32 Y = 0;Y < MipSizeY;Y++)
			{
				for(uint32 X = 0;X < MipSizeX;X++)
				{
					const FFourDistanceFieldSamples& SourceSample = MipData[MipIndex][Y * MipSizeX + X];

					DestMipData[Y * MipSizeX + X] = FColor(
						SourceSample.Samples[0].Distance, 
						SourceSample.Samples[1].Distance, 
						SourceSample.Samples[2].Distance, 
						SourceSample.Samples[3].Distance);				
				}
			}

			Texture->Source.UnlockMip(MipIndex);
		}
	}

	// Update the texture resource.
	Texture->CachePlatformData(true, true, false, Compressor);
}

/*
Out: PendingTexture.Allocations
Out: MipData
*/
int32 FShadowMap2D::EncodeSingleTexture(
	ULevel* LightingScenario = nullptr, 
	FShadowMapPendingTexture& PendingTexture, 
	UShadowMapTexture2D* Texture, 
	TArray< TArray<FFourDistanceFieldSamples> >& MipData)
{
	int32 TextureSizeX = PendingTexture.GetSizeX();
	int32 TextureSizeY = PendingTexture.GetSizeY();
	int32 MaxChannelsUsed = 0;

	MipData.Add( TArray<FFourDistanceFieldSamples>() );
	MipData[0].Empty(TextureSizeX * TextureSizeY);
	MipData[0].AddZeroed(TextureSizeX * TextureSizeY);

	for (FShadowMapAllocation& Allocation : PendingTexture.Allocations)
	{
		bool bChannelUsed[4] = {0};
		FVector4 InvUniformPenumbraSize(0, 0, 0, 0);

		for (const auto& ShadowMapPair : Allocation.ShadowMapData)
		{
			ULightComponent* CurrentLight = ShadowMapPair.Key;
			const TArray<FQuantizedSignedDistanceFieldShadowSample>& SourceSamples = ShadowMapPair.Value;

			const FLightComponentMapBuildData* LightBuildData = CurrentLight->GetOwner()->GetLevel()->MapBuildData->GetLightBuildData(CurrentLight->LightGuid);
			int32 CurrentLightChannel = LightBuildData->ShadowMapChannel;

			MaxChannelsUsed = FMath::Max(MaxChannelsUsed, CurrentLightChannel + 1);

			// Warning - storing one penumbra size per allocation even though multiple lights can share a channel
			bChannelUsed[CurrentLightChannel] = true;
			InvUniformPenumbraSize[CurrentLightChannel] = 1.0f / ShadowMapPair.Key->GetUniformPenumbraSize();

			// Copy the raw data for this light-map into the raw texture data array.
			for (int32 Y = Allocation.MappedRect.Min.Y; Y < Allocation.MappedRect.Max.Y; ++Y)
			{
				for (int32 X = Allocation.MappedRect.Min.X; X < Allocation.MappedRect.Max.X; ++X)
				{
					int32 DestY = Y + Allocation.OffsetY;
					int32 DestX = X + Allocation.OffsetX;

					FFourDistanceFieldSamples& 
						DestSample = MipData[0] [DestY * TextureSizeX + DestX];

					const FQuantizedSignedDistanceFieldShadowSample& 
						SourceSample = SourceSamples [Y * Allocation.TotalSizeX + X];

					if ( SourceSample.Coverage > 0 )
					{
						DestSample.Samples[CurrentLightChannel] = SourceSample;
					}
				}
			}
		}

		// Link the shadow-map to the texture.
		Allocation.ShadowMap->Texture = Texture;

		// Free the shadow-map's raw data.
		for (auto& ShadowMapPair : Allocation.ShadowMapData)
		{
			ShadowMapPair.Value.Empty();
		}
		
		int32 PaddedSizeX = Allocation.TotalSizeX;
		int32 PaddedSizeY = Allocation.TotalSizeY;
		int32 BaseX = Allocation.OffsetX - Allocation.MappedRect.Min.X;
		int32 BaseY = Allocation.OffsetY - Allocation.MappedRect.Min.Y;

		// Calculate the coordinate scale/biases for each shadow-map stored in the texture.
		Allocation.ShadowMap->CoordinateScale = FVector2D(
			(float)PaddedSizeX / (float)PendingTexture.GetSizeX(),
			(float)PaddedSizeY / (float)PendingTexture.GetSizeY()
			);
		Allocation.ShadowMap->CoordinateBias = FVector2D(
			(float)BaseX / (float)PendingTexture.GetSizeX(),
			(float)BaseY / (float)PendingTexture.GetSizeY()
			);

		for (int32 ChannelIndex = 0; ChannelIndex < 4; ChannelIndex++)
		{
			Allocation.ShadowMap->bChannelValid[ChannelIndex] = bChannelUsed[ChannelIndex];
		}

		Allocation.ShadowMap->InvUniformPenumbraSize = InvUniformPenumbraSize;
	}

	/* Generate mipmap */

	for (auto& Allocation : PendingTexture.Allocations)
	{
		Allocation->PostEncode();
	}

	return MaxChannelsUsed;
}

1. FQuantizedSignedDistanceFieldShadowSample::PenumbraSize; not used?
2. FShadowMap2D::InvUniformPenumbraSize[i] == 1.0 means what?
