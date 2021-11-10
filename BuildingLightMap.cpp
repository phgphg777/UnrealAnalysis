
FLightmassProcessor::ImportMappings() ->
FLightmassProcessor::ProcessAvailableMappings() ->
FStaticLightingSystem::EncodeTextures()



//////////////////////////////////////////////////////////////////////////////////////
class FLightmassProcessor {
	struct FMappingImportHelper {
		FGuid MappingGuid;
	}
	struct FTextureMappingImportHelper : public FMappingImportHelper {
		FStaticLightingTextureMapping* TextureMapping;
		FQuantizedLightmapData* QuantizedData;
		int32 NumSignedDistanceFieldShadowMaps;
		TMap<ULightComponent*, FShadowMapData2D*> ShadowMapData;
	}

	NSwarm::FSwarmInterface& Swarm;

	TMap<FGuid, FStaticMeshStaticLightingTextureMapping*> PendingTextureMappings;

	TMap<FGuid, FMappingImportHelper*> ImportedMappings;
}

/*
[Lightmass]
struct FLightMapDataBase {
	uint32 CompressedDataSize;
	uint32 UncompressedDataSize;
	float Multiply[LM_NUM_STORED_LIGHTMAP_COEF][4];
	float Add[LM_NUM_STORED_LIGHTMAP_COEF][4];
};
struct FLightMapData2DData : public FLightMapDataBase {		
	uint32 SizeX;
	uint32 SizeY;
	bool bHasSkyShadowing;
};
struct FQuantizedLightSampleData {
	uint8 Coverage;
	uint8 Coefficients[LM_NUM_STORED_LIGHTMAP_COEF][4];
	uint8 SkyOcclusion[4];
	uint8 AOMaterialMask;
};

[Unreal]
struct FQuantizedLightmapData {
	float Scale[NUM_STORED_LIGHTMAP_COEF][4];
	float Add[NUM_STORED_LIGHTMAP_COEF][4];
	uint32 SizeX;
	uint32 SizeY;
	bool bHasSkyShadowing;

	TArray<FLightMapCoefficients> Data;
	TArray<FGuid> LightGuids;
};
struct FLightMapCoefficients {
	uint8 Coverage;
	uint8 Coefficients[NUM_STORED_LIGHTMAP_COEF][4];
	uint8 SkyOcclusion[4];
	uint8 AOMaterialMask;
};

[Lightmass]
struct FShadowMapDataBase {
	uint32 CompressedDataSize;
	uint32 UncompressedDataSize;
};
struct FShadowMapData2DData : public FShadowMapDataBase {
	uint32 SizeX;
	uint32 SizeY;
};
struct FQuantizedSignedDistanceFieldShadowSampleData {
	uint8 Distance;
	uint8 PenumbraSize;
	uint8 Coverage;
};

[Unreal]
class FShadowMapData2D {	// The raw data which is used to construct a 2D shadowmap.
	uint32 SizeX;
	uint32 SizeY;
};
class FQuantizedShadowSignedDistanceFieldData2D : public FShadowMapData2D {
	TArray<FQuantizedSignedDistanceFieldShadowSample> Data;
};
struct FQuantizedSignedDistanceFieldShadowSample {
	uint8 Distance;
	uint8 PenumbraSize;
	uint8 Coverage;
};
*/

void FLightmassProcessor::ImportMappings(false)
{
	for(auto& Guid : CompletedMappingTasks.ExtractAll())
	{
		ImportMapping(Guid, false);
	}
}

void FLightmassProcessor::ImportMapping(const FGuid& MappingGuid, )
{
	if (IsStaticLightingTextureMapping(MappingGuid))
	{
		ImportStaticLightingTextureMapping(MappingGuid, false);
	}
	else if(FindLight(MappingGuid))
	{
		ImportStaticShadowDepthMap(FindLight(MappingGuid));
	}
}

/*
1. Allocate FTextureMappingImportHelper ImportData
2. Take out FLightmassProcessor::PendingTextureMappings
3. Read ImportData from Swarm
4. Add FLightmassProcessor::ImportedMappings by (ImportData->MappingGuid, ImportData)
*/
void FLightmassProcessor::ImportStaticLightingTextureMapping( const FGuid& MappingGuid, bool bProcessImmediately )
{
	FString ChannelName = Lightmass::CreateChannelName(MappingGuid, Lightmass::LM_TEXTUREMAPPING_VERSION, Lightmass::LM_TEXTUREMAPPING_EXTENSION);
	int32 Channel = Swarm.OpenChannel( *ChannelName, LM_TEXTUREMAPPING_CHANNEL_FLAGS );
		
	if (Channel >= 0)
	{
		uint32 MappingsImported = 0;
		uint32 NumMappings << Swarm;
		
		while (MappingsImported++ != NumMappings)
		{
			FGuid NextMappingGuid << Swarm;

			FTextureMappingImportHelper* ImportData = new FTextureMappingImportHelper();	// 1.
			ImportData->TextureMapping = GetStaticLightingTextureMapping(NextMappingGuid);	// 2.
			ImportData->MappingGuid = NextMappingGuid;
			
			ImportTextureMapping(Channel, *ImportData);					// 3.
		
			ImportedMappings.Add(ImportData->MappingGuid, ImportData);	// 4.
		}

		Swarm.CloseChannel( Channel );
	}
	else if ( Channel == NSwarm::SWARM_ERROR_CHANNEL_NOT_FOUND )
	{
		// If the channel doesn't exist, then this mapping could've been part of another channel
		// that has already been imported, so attempt to remove the mapping
		FStaticLightingTextureMapping* TextureMapping = GetStaticLightingTextureMapping(MappingGuid);
		// Alternatively, this channel could be part of an invalidated mapping
	}
}

bool FLightmassProcessor::ImportTextureMapping(int32 Channel, FTextureMappingImportHelper& TMImport)
{
	TMImport.ExecutionTime << Swarm;
	Lightmass::FLightMapData2DData LMLightmapData2DData << Swarm;
	TMImport.NumShadowMaps << Swarm;
	TMImport.NumSignedDistanceFieldShadowMaps << Swarm;
	int32 NumLights << Swarm;
	
	TArray<FGuid> LightGuids;
	LightGuids.AddUninitialized(NumLights);
	for (int32 i = 0; i < NumLights; i++)
	{
		LightGuids[i] << Swarm;
	}

	// allocate space to store the quantized data
	TMImport.QuantizedData = new FQuantizedLightmapData;
	FMemory::Memcpy(TMImport.QuantizedData->Scale, LMLightmapData2DData.Multiply, sizeof(TMImport.QuantizedData->Scale));
	FMemory::Memcpy(TMImport.QuantizedData->Add, LMLightmapData2DData.Add, sizeof(TMImport.QuantizedData->Add));
	TMImport.QuantizedData->SizeX = LMLightmapData2DData.SizeX;
	TMImport.QuantizedData->SizeY = LMLightmapData2DData.SizeY;
	TMImport.QuantizedData->bHasSkyShadowing = LMLightmapData2DData.bHasSkyShadowing;
	TMImport.QuantizedData->Data.AddUninitialized(LMLightmapData2DData.SizeX * LMLightmapData2DData.SizeY);
	TMImport.QuantizedData->LightGuids = LightGuids;

	ImportLightMapData2DData(
		Channel, 
		TMImport.QuantizedData, 
		LMLightmapData2DData.UncompressedDataSize, 
		LMLightmapData2DData.CompressedDataSize);

	ImportSignedDistanceFieldShadowMapData2D(
		Channel, 
		TMImport.ShadowMapData, 
		TMImport.NumSignedDistanceFieldShadowMaps);
}

bool FLightmassProcessor::ImportLightMapData2DData(int32 Channel, FQuantizedLightmapData* QuantizedData, int32 UncompressedSize, int32 CompressedSize)
{
	void* CompressedBuffer = FMemory::Malloc(CompressedSize);
	Swarm.ReadChannel(Channel, CompressedBuffer, CompressedSize);

	FLightMapCoefficients* DataBuffer = QuantizedData->Data.GetData();
	FCompression::UncompressMemory(NAME_Zlib, DataBuffer, UncompressedSize, CompressedBuffer, CompressedSize);

	FMemory::Free(CompressedBuffer);
}

bool FLightmassProcessor::ImportSignedDistanceFieldShadowMapData2D(int32 Channel, TMap<ULightComponent*,FShadowMapData2D*>& OutShadowMapData, int32 ShadowMapCount)
{
	for (int32 SMIndex = 0; SMIndex < ShadowMapCount; SMIndex++)
	{
		FGuid LightGuid << Swarm;
		ULightComponent* LightComp = FindLight(LightGuid);

		Lightmass::FShadowMapData2DData SMData << Swarm;
		FQuantizedShadowSignedDistanceFieldData2D* ShadowMapData = new FQuantizedShadowSignedDistanceFieldData2D(SMData.SizeX, SMData.SizeY);
		
		void* CompressedBuffer = FMemory::Malloc(SMData.CompressedDataSize);
		Swarm.ReadChannel(Channel, CompressedBuffer, SMData.CompressedDataSize);

		FQuantizedSignedDistanceFieldShadowSample* DataBuffer = ShadowMapData->Data.GetData();
		FCompression::UncompressMemory(NAME_Zlib, DataBuffer, SMData.UncompressedDataSize, CompressedBuffer, SMData.CompressedDataSize);
		
		FMemory::Free(CompressedBuffer);

		OutShadowMapData.Add(LightComp, ShadowMapData);
	}

	return true;
}
//////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////
static TArray<FLightMapAllocationGroup> PendingLightMaps;
static TArray<FShadowMapAllocationGroup> PendingShadowMaps;

struct FLightMapAllocationGroup {
	TArray<TUniquePtr<FLightMapAllocation>, TInlineAllocator<1>> Allocations;
	UObject* Outer;
	ELightMapFlags LightmapFlags;
	FBoxSphereBounds Bounds;
	int32 TotalTexels;
};
struct FLightMapAllocation {
	TRefCountPtr<FLightMap2D> LightMap;	// Allocate in FLightMap2D::AllocateLightMap()
	int32 TotalSizeX;	// Set in FLightMap2D::AllocateLightMap() /** Lightmap ResX */
	int32 TotalSizeY;	// Set in FLightMap2D::AllocateLightMap() /** Lightmap ResY */
	FIntRect MappedRect;// Set in FLightMap2D::AllocateLightMap() /** (0,0,ResX,ResY) */
	ELightMapPaddingType PaddingType; 			// Set in FLightMap2D::AllocateLightMap()
	float Scale[NUM_STORED_LIGHTMAP_COEF][4];	// Set in FLightMap2D::AllocateLightMap()
	float Add[NUM_STORED_LIGHTMAP_COEF][4];		// Set in FLightMap2D::AllocateLightMap()
	bool bHasSkyShadowing;						// Set in FLightMap2D::AllocateLightMap()
	TArray<FLightMapCoefficients> RawData;		// Set in FLightMap2D::AllocateLightMap()
	/*
	UPrimitiveComponent* Primitive;		// Used by InstaancedStaticMEsh
	UMapBuildDataRegistry* Registry;	// Used by InstaancedStaticMEsh
	FGuid MapBuildDataId;				// Used by InstaancedStaticMEsh
	int32 InstanceIndex;				// Used by InstaancedStaticMEsh
	*/
	int32 OffsetX;		// Set in FLightMapPendingTexture::AddElement()
	int32 OffsetY;		// Set in FLightMapPendingTexture::AddElement()
}

struct FShadowMapAllocationGroup {
	TArray<TUniquePtr<FShadowMapAllocation>, TInlineAllocator<1>> Allocations;
	UObject* TextureOuter;
	EShadowMapFlags ShadowmapFlags;
	FBoxSphereBounds Bounds;
	int32 TotalTexels;
};
struct FShadowMapAllocation
{
	TRefCountPtr<FShadowMap2D> ShadowMap;
	int32 TotalSizeX;
	int32 TotalSizeY;
	FIntRect MappedRect;
	ELightMapPaddingType PaddingType;
	TMap<ULightComponent*, TArray<FQuantizedSignedDistanceFieldShadowSample> > ShadowMapData;
	/*
	UObject* Primitive;					// Used by InstaancedStaticMEsh
	UMapBuildDataRegistry* Registry;	// Used by InstaancedStaticMEsh
	FGuid MapBuildDataId;				// Used by InstaancedStaticMEsh
	int32 InstanceIndex;				// Used by InstaancedStaticMEsh
	*/
	int32						OffsetX;
	int32						OffsetY;
};


void FLightmassProcessor::ProcessAvailableMappings()
{
	for(FMappingImportHelper* mapping : ImportedMappings)
	{
		FTextureMappingImportHelper* TImportData = (FTextureMappingImportHelper*) mapping;
		System.ApplyMapping(TImportData->TextureMapping, TImportData->QuantizedData, TImportData->ShadowMapData);
	}
}

void FStaticLightingSystem::ApplyMapping(
	FStaticLightingTextureMapping* TextureMapping,
	FQuantizedLightmapData* QuantizedData,
	const TMap<ULightComponent*,FShadowMapData2D*>& ShadowMapData) const
{
	TextureMapping->Apply(QuantizedData, ShadowMapData, nullptr);
}

/*
1. Allocate UStaticMeshComponent::LODData
2. Allocate UStaticMeshComponent::LODData.MapBuildDataId
3. Add UMapBuildDataRegistry::MeshBuildData by (MapBuildDataId, FMeshMapBuildData())
4. Allocate UMapBuildDataRegistry::MeshBuildData[MapBuildDataId].LightMap (assign ::LightGuids)
5. Allocate UMapBuildDataRegistry::MeshBuildData[MapBuildDataId].ShadowMap (assign ::LightGuids)
6. Assign UMapBuildDataRegistry::MeshBuildData[MapBuildDataId].IrrelevantLights
*/
void FStaticMeshStaticLightingTextureMapping::Apply(
	FQuantizedLightmapData* QuantizedData, 
	const TMap<ULightComponent*, FShadowMapData2D*>& ShadowMapData, )
{
	UStaticMeshComponent* StaticMeshComponent = Primitive.Get();	
	StaticMeshComponent->SetLODDataCount(LODIndex + 1, );			// 1.

	FStaticMeshComponentLODInfo& ComponentLODInfo = StaticMeshComponent->LODData[LODIndex];
	ComponentLODInfo.CreateMapBuildDataId(LODIndex);				// 2.

	UMapBuildDataRegistry* Registry = StaticMeshComponent->GetOwner()->GetLevel()->GetOrCreateMapBuildData();
	FMeshMapBuildData& MeshBuildData = Registry->AllocateMeshBuildData(ComponentLODInfo.MapBuildDataId, true); // 3.

	const bool bNeedsLightMap = QuantizedData->HasNonZeroData() 
		|| QuantizedData->bHasSkyShadowing
		|| ShadowMapData.Num() > 0 
		|| Mesh->RelevantLights.Num() > 0;
	
	MeshBuildData.LightMap = !bNeedsLightMap ? NULL :			// 4.
		FLightMap2D::AllocateLightMap(
			Registry,
			QuantizedData, {},
			StaticMeshComponent->Bounds,
			GAllowLightmapPadding ? LMPT_NormalPadding : LMPT_NoPadding,
			LMF_Streamed);

	MeshBuildData.ShadowMap = ShadowMapData.Num()==0 ? NULL :	// 5.
		FShadowMap2D::AllocateShadowMap(
			Registry,
			ShadowMapData,
			StaticMeshComponent->Bounds,
			GAllowLightmapPadding ? LMPT_NormalPadding : LMPT_NoPadding,
			SMF_Streamed);

	// Build the list of irrelevant lights. IrrelevantLights was cleared in InvalidateLightingCacheDetailed
	for(int32 LightIndex = 0;LightIndex < Mesh->RelevantLights.Num();LightIndex++)
	{
		const ULightComponent* Light = Mesh->RelevantLights[LightIndex];
		const bool bIsInLightMap = MeshBuildData.LightMap && MeshBuildData.LightMap->LightGuids.Contains(Light->LightGuid);
		const bool bIsInShadowMap = MeshBuildData.ShadowMap && MeshBuildData.ShadowMap->LightGuids.Contains(Light->LightGuid);

		// Add the light to the statically irrelevant light list if it is in the potentially relevant light list, but didn't contribute to the light-map.
		if(!bIsInLightMap && !bIsInShadowMap)
		{	
			MeshBuildData.IrrelevantLights.AddUnique(Light->LightGuid);		// 6.
		}
	}
}

TRefCountPtr<FLightMap2D> FLightMap2D::AllocateLightMap(
	UObject* LightMapOuter,
	FQuantizedLightmapData*& SourceQuantizedData, ,
	Bounds, InPaddingType, InLightmapFlags)
{
	int32 SizeX = SourceQuantizedData->SizeX;
	int32 SizeY = SourceQuantizedData->Sizey;

	RefCountPtr<FLightMap2D> LightMap = TRefCountPtr<FLightMap2D>(new FLightMap2D());
	LightMap->LightGuids = SourceQuantizedData->LightGuids;

	TUniquePtr<FLightMapAllocation> Allocation = MakeUnique<FLightMapAllocation>();
	Allocation->LightMap = LightMap;
	Allocation->TotalSizeX = SizeX;
	Allocation->TotalSizeY = SizeY;
	Allocation->MappedRect.Max.X = SizeX;
	Allocation->MappedRect.Max.Y = SizeY;
	Allocation->PaddingType = InPaddingType;
	FMemory::Memcpy(Allocation->Scale, SourceQuantizedData->Scale, sizeof(Allocation->Scale));
	FMemory::Memcpy(Allocation->Add, SourceQuantizedData->Add, sizeof(Allocation->Add));
	Allocation->bHasSkyShadowing = SourceQuantizedData->bHasSkyShadowing;
	
	Allocation->RawData = MoveTemp(SourceQuantizedData->Data);
	
	delete SourceQuantizedData;
	SourceQuantizedData = NULL;

	FLightMapAllocationGroup AllocationGroup;
	AllocationGroup.Outer = LightMapOuter;
	AllocationGroup.LightmapFlags = InLightmapFlags;
	AllocationGroup.Bounds = Bounds;
	AllocationGroup.Allocations.Add(MoveTemp(Allocation));

	PendingLightMaps.Add(MoveTemp(AllocationGroup));

	return LightMap;
}

TRefCountPtr<FShadowMap2D> FShadowMap2D::AllocateShadowMap(
	UObject* LightMapOuter, 
	const TMap<ULightComponent*,FShadowMapData2D*>& ShadowMapData,
	Bounds, InPaddingType, InShadowmapFlags)
{
	int32 SizeX = ShadowMapData[0].Value->GetSizeX();
	int32 SizeY = ShadowMapData[0].Value->GetSizey();

	TRefCountPtr<FShadowMap2D> ShadowMap = TRefCountPtr<FShadowMap2D>(new FShadowMap2D());
	for (const auto& ShadowDataPair : ShadowMapData)
	{
		ShadowMap->LightGuids.Add(ShadowDataPair.Key->LightGuid);
	}
	
	TUniquePtr<FShadowMapAllocation> Allocation = MakeUnique<FShadowMapAllocation>();
	Allocation->ShadowMap = ShadowMap;
	Allocation->TotalSizeX = SizeX;
	Allocation->TotalSizeY = SizeY;
	Allocation->MappedRect = FIntRect(0, 0, SizeX, SizeY);
	Allocation->PaddingType = InPaddingType;
	for (const auto& ShadowDataPair : ShadowMapData)
	{
		const FQuantizedShadowSignedDistanceFieldData2D* SourceShadowData = 
			(FQuantizedShadowSignedDistanceFieldData2D*) ShadowDataPair.Value;

		Allocation->ShadowMapData.Add(ShadowDataPair.Key, MoveTemp(SourceShadowData->Data));
		
		delete SourceShadowData;
	}

	FShadowMapAllocationGroup AllocationGroup;
	AllocationGroup.TextureOuter = LightMapOuter;
	AllocationGroup.ShadowmapFlags = InShadowmapFlags;
	AllocationGroup.Bounds = Bounds;
	AllocationGroup.Allocations.Add(MoveTemp(Allocation));
	AllocationGroup.TotalTexels += ((Allocation->MappedRect.Width() + 3) & ~3) * ((Allocation->MappedRect.Height() + 3) & ~3);

	PendingShadowMaps.Add(MoveTemp(AllocationGroup));

	return ShadowMap;
}
//////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////
void FStaticLightingSystem::EncodeTextures(bool bLightingSuccessful = true)
{
	FLightMap2D::EncodeTextures(World, true, false);
	FShadowMap2D::EncodeTextures(World, nullptr, true, false);
}

class ENGINE_API FLightMap : private FDeferredCleanupInterface {
	TArray<FGuid> LightGuids;	// Set in FLightMap2D::AllocateLightMap() /** The GUIDs of lights (STATIC only) which this light-map stores. */
}
class ENGINE_API FLightMap2D : public FLightMap {
	ULightMapTexture2D* SkyOcclusionTexture;// Set in FLightMapPendingTexture::StartEncoding()
	ULightMapTexture2D* Textures[2]; 		// Set in FLightMapPendingTexture::StartEncoding()
	FVector4 ScaleVectors[NUM_STORED_LIGHTMAP_COEF];// Set in FLightMapPendingTexture::EncodeCoefficientTexture()
	FVector4 AddVectors[NUM_STORED_LIGHTMAP_COEF];	// Set in FLightMapPendingTexture::EncodeCoefficientTexture()
	FVector2D CoordinateScale;	// Set in FLightMapPendingTexture::PostEncode()
	FVector2D CoordinateBias;	// Set in FLightMapPendingTexture::PostEncode()
}

void FLightMap2D::EncodeTextures(UWorld* InWorld, bool bLightingSuccessful, bool bMultithreadedEncode)
{
	check(!GAllowLightmapCropping);
	check(!bMultithreadedEncode);

	int32 PackedLightAndShadowMapTextureSizeX = InWorld->GetWorldSettings()->PackedLightAndShadowMapTextureSize;
	int32 PackedLightAndShadowMapTextureSizeY = PackedLightAndShadowMapTextureSizeX / 2;

	/* Sort PendingLightMaps.. */

	// Allocate texture space for each light-map.
	TArray<FLightMapPendingTexture*> PendingTextures;

	for (FLightMapAllocationGroup& PendingGroup : PendingLightMaps)
	{
		FLightMapPendingTexture* Texture = nullptr;

		// Find an existing texture which the light-map can be stored in.
		for (FLightMapPendingTexture* ExistingTexture : PendingTextures)
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

			int32 NewTextureSizeX = PackedLightAndShadowMapTextureSizeX;
			int32 NewTextureSizeY = PackedLightAndShadowMapTextureSizeY;

			const int32 AllocationCountX = 
				FMath::CeilToInt( 
					FMath::Sqrt(
						FMath::DivideAndRoundUp(PendingGroup.Allocations.Num() * 2 * MaxHeight, MaxWidth)
					) 
				);
			const int32 AllocationCountY = FMath::DivideAndRoundUp(PendingGroup.Allocations.Num(), AllocationCountX);
			const int32 AllocationSizeX = AllocationCountX * MaxWidth;
			const int32 AllocationSizeY = AllocationCountY * MaxHeight;

			if (AllocationSizeX > NewTextureSizeX || AllocationSizeY > NewTextureSizeY)
			{
				NewTextureSizeX = FMath::RoundUpToPowerOfTwo(AllocationSizeX);
				NewTextureSizeY = FMath::RoundUpToPowerOfTwo(AllocationSizeY);
				// Force 2:1 aspect
				NewTextureSizeX = FMath::Max(NewTextureSizeX, NewTextureSizeY * 2);
				NewTextureSizeY = FMath::Max(NewTextureSizeY, NewTextureSizeX / 2);
			}

			Texture = new FLightMapPendingTexture(InWorld, NewTextureSizeX, NewTextureSizeY, ETextureLayoutAspectRatio::Force2To1);
			PendingTextures.Add(Texture);
			Texture->Outer = PendingGroup.Outer;
			Texture->Bounds = PendingGroup.Bounds;
			Texture->LightmapFlags = PendingGroup.LightmapFlags;
			verify(Texture->AddElement(PendingGroup));
		}

		// Give the texture ownership of the allocations
		for (auto& Allocation : PendingGroup.Allocations)
		{
			Texture->Allocations.Add(MoveTemp(Allocation));
		}
	}

	PendingLightMaps.Empty();
	
	// Encode all the pending textures.
	for (int32 TextureIndex = 0; TextureIndex < PendingTextures.Num(); TextureIndex++)
	{
		FLightMapPendingTexture* PendingTexture = PendingTextures[TextureIndex];
		PendingTexture->StartEncoding(nullptr,nullptr);
	}

	for (auto& PendingTexture : PendingTextures)
	{
		PendingTexture->PostEncode();
	}

	for (auto& PendingTexture : PendingTextures)
	{
		PendingTexture->FinishCachingTextures();
		delete PendingTexture;
	}

	PendingTextures.Empty();
}

bool FLightMapPendingTexture::AddElement(FLightMapAllocationGroup& AllocationGroup, bool bForceIntoThisTexture)
{
	check(bForceIntoThisTexture);

	int32 iAllocation = 0;
	for (; iAllocation < AllocationGroup.Allocations.Num(); ++iAllocation)
	{
		auto& Allocation = AllocationGroup.Allocations[iAllocation];
		uint32 BaseX, BaseY;
		const uint32 MappedRectWidth = Allocation->MappedRect.Width();
		const uint32 MappedRectHeight = Allocation->MappedRect.Height();
		if (FTextureLayout::AddElement(BaseX, BaseY, MappedRectWidth, MappedRectHeight))
		{
			Allocation->OffsetX = BaseX;
			Allocation->OffsetY = BaseY;
		}
		else
		{
			break;// failed to add all elements to the texture
		}
	}

	if (iAllocation < AllocationGroup.Allocations.Num())
	{
		// remove the ones added so far to restore our original state
		for (--iAllocation; iAllocation >= 0; --iAllocation)
		{
			auto& Allocation = AllocationGroup.Allocations[iAllocation];
			const uint32 MappedRectWidth = Allocation->MappedRect.Width();
			const uint32 MappedRectHeight = Allocation->MappedRect.Height();
			FTextureLayout::RemoveElement(Allocation->OffsetX, Allocation->OffsetY, MappedRectWidth, MappedRectHeight);
		}

		return false;
	}

	return true;
}

void FLightMapPendingTexture::CreateUObjects()
{
	++GLightmapCounter;
		
	if (NeedsSkyOcclusionTexture())
	{
		SkyOcclusionTexture = NewObject<ULightMapTexture2D>(Outer, GetSkyOcclusionTextureName(GLightmapCounter));
	}

	for (uint32 CoefficientIndex = 0; CoefficientIndex < NUM_STORED_LIGHTMAP_COEF; CoefficientIndex += 2)
	{
		Textures[CoefficientIndex] = NewObject<ULightMapTexture2D>(Outer, GetLightmapName(GLightmapCounter, CoefficientIndex));
	}
	
	bUObjectsCreated = true;
}

void FLightMapPendingTexture::StartEncoding(ULevel* LightingScenario, ITextureCompressorModule* UnusedCompressor)
{
	if (!bUObjectsCreated)
	{
		CreateUObjects();
	}

	const ETextureSourceFormat SkyOcclusionFormat = TSF_BGRA8;
	const ETextureSourceFormat BaseFormat = TSF_BGRA8;

	if (SkyOcclusionTexture != nullptr)
	{
		auto Texture = SkyOcclusionTexture;
		Texture->Source.Init2DWithMipChain(GetSizeX(), GetSizeY(), SkyOcclusionFormat);
		Texture->MipGenSettings = TMGS_LeaveExistingMips;
		Texture->Filter	= GUseBilinearLightmaps ? TF_Default : TF_Nearest;
		Texture->LODGroup = TEXTUREGROUP_Lightmap;
		Texture->LightmapFlags = ELightMapFlags( LightmapFlags );
		EncodeSkyOcclusionTexture(Texture, 0u, );
	}
	
	// Encode and compress the coefficient textures.
	for(uint32 CoefficientIndex = 0; CoefficientIndex < NUM_STORED_LIGHTMAP_COEF; CoefficientIndex += 2)
	{
		auto Texture = Textures[CoefficientIndex];
		
		Texture->Source.Init2DWithMipChain(GetSizeX(), GetSizeY() * 2, BaseFormat);	// Top/bottom atlased
		Texture->MipGenSettings = TMGS_LeaveExistingMips;
		Texture->Filter	= GUseBilinearLightmaps ? TF_Default : TF_Nearest;
		Texture->LODGroup = TEXTUREGROUP_Lightmap;
		Texture->LightmapFlags = ELightMapFlags( LightmapFlags );
		Texture->bForcePVRTC4 = true;

		EncodeCoefficientTexture(CoefficientIndex, Texture, 0u, , false);
	}

	// Link textures to allocations
	for (int32 AllocationIndex = 0; AllocationIndex < Allocations.Num(); AllocationIndex++)
	{
		FLightMapAllocation& Allocation = *Allocations[AllocationIndex];
		Allocation.LightMap->SkyOcclusionTexture = SkyOcclusionTexture;
		Allocation.LightMap->AOMaterialMaskTexture = AOMaterialMaskTexture;
		Allocation.LightMap->ShadowMapTexture = ShadowMapTexture;
		Allocation.LightMap->Textures[0] = Textures[0];
		Allocation.LightMap->Textures[1] = Textures[2];
		Allocation.LightMap->VirtualTexture = VirtualTexture;
	}
}

void FLightMapPendingTexture::EncodeCoefficientTexture(
	int32 CoefficientIndex, 
	UTexture* Texture, 
	uint32 LayerIndex = 0, , 
	bool bEncodeVirtualTexture = false)
{
	FTextureFormatSettings FormatSettings;
	FormatSettings.SRGB = false;
	FormatSettings.CompressionNoAlpha = CoefficientIndex >= LQ_LIGHTMAP_COEF_INDEX;
	FormatSettings.CompressionNone = !GCompressLightmaps;
	Texture->SetLayerFormatSettings(LayerIndex, FormatSettings);

	const int32 NumMips = Texture->Source.GetNumMips();
	const int32 TextureSizeX = Texture->Source.GetSizeX();
	const int32 TextureSizeY = Texture->Source.GetSizeY(); // == 2 * GetSizeY()

	FColor* MipData [ MAX_TEXTURE_MIP_COUNT ] = { 0 };
	int8* MipCoverageData [ MAX_TEXTURE_MIP_COUNT ] = { 0 };
	
	for (int32 i = 0; i < NumMips; ++i)
	{
		const int32 MipSizeX = FMath::Max(1, TextureSizeX >> i);
		const int32 MipSizeY = FMath::Max(1, TextureSizeY >> i);

		MipData[i] = (FColor*) Texture->Source.LockMip(0, LayerIndex, i);
		MipCoverageData[i] = (int8*) FMemory::Malloc(MipSizeX * MipSizeY);
	}

	// Create the uncompressed top mip-level.
	FMemory::Memzero(MipData[0], TextureSizeX * TextureSizeY * sizeof(FColor));
	FMemory::Memzero(MipCoverageData[0], TextureSizeX * TextureSizeY);

	for (int32 AllocationIndex = 0; AllocationIndex < Allocations.Num(); AllocationIndex++)
	{
		auto& Allocation = Allocations[AllocationIndex];
		for (int k = 0; k < 2; k++)
		{
			Allocation->LightMap->ScaleVectors[CoefficientIndex + k] = FVector4(
				Allocation->Scale[CoefficientIndex + k][0],
				Allocation->Scale[CoefficientIndex + k][1],
				Allocation->Scale[CoefficientIndex + k][2],
				Allocation->Scale[CoefficientIndex + k][3]
			);

			Allocation->LightMap->AddVectors[CoefficientIndex + k] = FVector4(
				Allocation->Add[CoefficientIndex + k][0],
				Allocation->Add[CoefficientIndex + k][1],
				Allocation->Add[CoefficientIndex + k][2],
				Allocation->Add[CoefficientIndex + k][3]
			);
		}

		FIntRect TextureRect(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
		TextureRect.Min.X = Allocation->OffsetX;
		TextureRect.Min.Y = Allocation->OffsetY;
		TextureRect.Max.X = Allocation->OffsetX + Allocation->MappedRect.Width();
		TextureRect.Max.Y = Allocation->OffsetY + Allocation->MappedRect.Height();

		// Copy the raw data for this light-map into the raw texture data array.
		for (int32 Y = Allocation->MappedRect.Min.Y; Y < Allocation->MappedRect.Max.Y; ++Y)
		{
			for (int32 X = Allocation->MappedRect.Min.X; X < Allocation->MappedRect.Max.X; ++X)
			{
				const FLightMapCoefficients& SourceCoefficients = Allocation->RawData[Y * Allocation->TotalSizeX + X];

				int32 DestX = X + Allocation->OffsetX;
				int32 DestY = Y + Allocation->OffsetY;
				
				FColor&	DestColor = MipData[0][DestY * TextureSizeX + DestX];
				int8&	DestCoverage = MipCoverageData[0][DestY * TextureSizeX + DestX];

				FColor&	DestBottomColor = MipData[0][DestX + DestY * TextureSizeX + BottomOffset];
				int8&	DestBottomCoverage = MipCoverageData[0][DestX + DestY * TextureSizeX + BottomOffset];

				DestColor.R = SourceCoefficients.Coefficients[CoefficientIndex][0];
				DestColor.G = SourceCoefficients.Coefficients[CoefficientIndex][1];
				DestColor.B = SourceCoefficients.Coefficients[CoefficientIndex][2];
				DestColor.A = SourceCoefficients.Coefficients[CoefficientIndex][3];

				DestBottomColor.R = SourceCoefficients.Coefficients[CoefficientIndex + 1][0];
				DestBottomColor.G = SourceCoefficients.Coefficients[CoefficientIndex + 1][1];
				DestBottomColor.B = SourceCoefficients.Coefficients[CoefficientIndex + 1][2];
				DestBottomColor.A = SourceCoefficients.Coefficients[CoefficientIndex + 1][3];

				// uint8 -> int8
				DestCoverage = DestBottomCoverage = SourceCoefficients.Coverage / 2;
			}
		}
	}

	GenerateLightmapMipsAndDilateColor(NumMips, TextureSizeX, TextureSizeY, TextureColor, MipData, MipCoverageData);

	for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
	{
		Texture->Source.UnlockMip(0, LayerIndex, MipIndex);
		FMemory::Free(MipCoverageData[MipIndex]);
	}
}

void FLightMapPendingTexture::PostEncode()
{
	for (int32 AllocationIndex = 0; AllocationIndex < Allocations.Num(); AllocationIndex++)
	{
		auto& Allocation = Allocations[AllocationIndex];

		...

		Allocation->LightMap->CoordinateScale = Scale;
		Allocation->LightMap->CoordinateBias = Bias;
		Allocation->PostEncode();

		Allocation->RawData.Empty();
	}

	if (SkyOcclusionTexture!=nullptr)
	{
		PostEncode(SkyOcclusionTexture);
	}

	for (uint32 CoefficientIndex = 0; CoefficientIndex < NUM_STORED_LIGHTMAP_COEF; CoefficientIndex += 2)
	{
		PostEncode(Textures[CoefficientIndex]);
	}
}
//////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////