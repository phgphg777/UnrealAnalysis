
class ENGINE_API FSceneView {
	/** The uniform buffer for the view's parameters. This is only initialized in the rendering thread's copies of the FSceneView. */
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;

	/** Sets up the view rect parameters in the view's uniform shader parameters */
	void SetupViewRectUniformBufferParameters(
		/*Out*/FViewUniformShaderParameters& ViewUniformShaderParameters, 
		const FIntPoint& InBufferSize,
		const FIntRect& InEffectiveViewRect,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrice);

	/** 
	 * Populates the uniform buffer prameters common to all scene view use cases
	 * View parameters should be set up in this method if they are required for the view to render properly.
	 * This is to avoid code duplication and uninitialized parameters in other places that create view uniform parameters (e.g Slate) 
	 */
	void SetupCommonViewUniformBufferParameters(
		/*Out*/FViewUniformShaderParameters& ViewUniformShaderParameters, 
		const FIntPoint& InBufferSize, ,
		const FIntRect& InEffectiveViewRect,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrices);
}

/** A FSceneView with additional state used by the scene renderer. */
class FViewInfo : public FSceneView {
	/** Cached view uniform shader parameters, to allow recreating the view uniform buffer without having to fill out the entire struct. */
	TUniquePtr<FViewUniformShaderParameters> CachedViewUniformShaderParameters;

	/** Tracks dynamic primitive data for upload to GPU Scene, when enabled. */
	TArray<FPrimitiveUniformShaderParameters> DynamicPrimitiveShaderData;

	//TArray<FRHIUniformBuffer*> PrimitiveFadeUniformBuffers;
	//FSceneBitArray PrimitiveFadeUniformBufferMap;
	//FUniformBufferRHIRef DitherFadeInUniformBuffer;
	//FUniformBufferRHIRef DitherFadeOutUniformBuffer;
	//TMap<int32, FUniformBufferRHIRef> TranslucentSelfShadowUniformBufferMap;
	//TUniformBufferRef<FReflectionCaptureShaderData> ReflectionCaptureUniformBuffer;
	//const FAtmosphereUniformShaderParameters* SkyAtmosphereUniformShaderParameters;
	//TUniquePtr<FForwardLightingViewResources> ForwardLightingResourcesStorage;
	//FVolumetricFogViewResources VolumetricFogResources;
	//TUniformBufferRef<FRaytracingLightDataPacked>	RayTracingLightingDataUniformBuffer;

	/** Creates ViewUniformShaderParameters given a set of view transforms. */
	void SetupUniformBufferParameters(
		FSceneRenderTargets& SceneContext,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrices, , ,
		FViewUniformShaderParameters& ViewUniformShaderParameters);
	void SetupGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;
	void SetupVolumetricFogUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;
}

class FScene : public FSceneInterface {
	FPersistentUniformBuffers UniformBuffers;
}

class FPersistentUniformBuffers {
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
	TUniformBufferRef<FInstancedViewUniformShaderParameters> InstancedViewUniformBuffer;
	TUniformBufferRef<FSceneTexturesUniformParameters> DepthPassUniformBuffer;
	TUniformBufferRef<FOpaqueBasePassUniformParameters> OpaqueBasePassUniformBuffer;
	TUniformBufferRef<FTranslucentBasePassUniformParameters> TranslucentBasePassUniformBuffer;
	TUniformBufferRef<FReflectionCaptureShaderData> ReflectionCaptureUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> CSMShadowDepthViewUniformBuffer;
	TUniformBufferRef<FShadowDepthPassUniformParameters> CSMShadowDepthPassUniformBuffer;
	TUniformBufferRef<FDistortionPassUniformParameters> DistortionPassUniformBuffer;
	TUniformBufferRef<FSceneTexturesUniformParameters> VelocityPassUniformBuffer;
	TUniformBufferRef<FSceneTexturesUniformParameters> HitProxyPassUniformBuffer;
	TUniformBufferRef<FSceneTexturesUniformParameters> MeshDecalPassUniformBuffer;
	TUniformBufferRef<FLightmapDensityPassUniformParameters> LightmapDensityPassUniformBuffer;
	TUniformBufferRef<FDebugViewModePassPassUniformParameters> DebugViewModePassUniformBuffer;
	TUniformBufferRef<FVoxelizeVolumePassUniformParameters> VoxelizeVolumePassUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> VoxelizeVolumeViewUniformBuffer;
	TUniformBufferRef<FSceneTexturesUniformParameters> ConvertToUniformMeshPassUniformBuffer;
	TUniformBufferRef<FSceneTexturesUniformParameters> CustomDepthPassUniformBuffer;
	TUniformBufferRef<FMobileSceneTextureUniformParameters> MobileCustomDepthPassUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> CustomDepthViewUniformBuffer;
	TUniformBufferRef<FInstancedViewUniformShaderParameters> InstancedCustomDepthViewUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> VirtualTextureViewUniformBuffer;
	TUniformBufferRef<FSceneTexturesUniformParameters> EditorSelectionPassUniformBuffer;

	// View from which ViewUniformBuffer was last updated.
	const FViewInfo* CachedView;

	/** Compares the provided view against the cached view and updates the view uniform buffer
	 *  if the views differ. Returns whether uniform buffer was updated.
	 *  If bShouldWaitForPersistentViewUniformBufferExtensionsJobs == true, it calls Extension->BeginRenderView() which
	 *  waits on the potential jobs dispatched in Extension->PrepareView(). Currently it is false only in FMobileSceneRenderer::InitViews()
	 */
	bool UpdateViewUniformBuffer(const FViewInfo& View, bool bShouldWaitForPersistentViewUniformBufferExtensionsJobs = true);

	/** Updates view uniform buffer and invalidates the internally cached view instance. */
	void UpdateViewUniformBufferImmediate(const FViewUniformShaderParameters& Parameters);
}





bool FDeferredShadingSceneRenderer::InitViews()
{
	ComputeViewVisibility();
	InitDynamicShadows();
	Views[0].InitRHIResources();
	SetupVolumetricFog();
}

void FViewInfo::InitRHIResources()
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(FRHICommandListExecutor::GetImmediateCommandList());

	CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();
	SetupUniformBufferParameters(SceneContext, , , *CachedViewUniformShaderParameters);
	ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
}


void FProjectedShadowInfo::ModifyViewForShadow(FRHICommandList& RHICmdList, FViewInfo* FoundView)
{
	FIntRect OriginalViewRect = FoundView->ViewRect;
	FoundView->ViewRect = FIntRect(0, 0, ResolutionX, ResolutionY);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	
	FoundView->CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();
	FoundView->SetupUniformBufferParameters(SceneContext, , , *FoundView->CachedViewUniformShaderParameters);
	FoundView->ViewUniformBuffer = IsWholeSceneDirectionalShadow() 
		? Scene->UniformBuffers.CSMShadowDepthViewUniformBuffer
		: TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*FoundView->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame)

	FoundView->ViewRect = OriginalViewRect;
}




class FRHIUniformBuffer : public FRHIResource {
	const FRHIUniformBufferLayout* Layout;
	uint32 LayoutConstantBufferSize;
};
typedef FRHIUniformBuffer*              FUniformBufferRHIParamRef;
typedef TRefCountPtr<FRHIUniformBuffer> FUniformBufferRHIRef;
template<typename TBufferStruct> class TUniformBufferRef : public FUniformBufferRHIRef {};

class FShaderUniformBufferParameter {
	uint16 BaseIndex;
	bool bIsBound;
};
template<typename TBufferStruct> class TShaderUniformBufferParameter : public FShaderUniformBufferParameter {};

class FD3D11UniformBuffer : public FRHIUniformBuffer {
private: FD3D11DynamicRHI* D3D11RHI;
public:  TRefCountPtr<ID3D11Buffer> Resource;	
}


FGlobalShaderParameterStruct (type) -> FShaderParametersMetadata -> FRHIUniformBufferLayout
FGlobalShaderParameterStruct (value), FRHIUniformBufferLayout -> FUniformBufferRHIRef
FShader::FShader() -> FShaderUniformBufferParameter
FUniformBufferRHIRef, FShaderUniformBufferParameter -> SetUniformBufferParameter()


class FSomePS : public FGlobalShader
{
public:
	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FSome& Some)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, GetPixelShader(), View.ViewUniformBuffer);
		FSomeUniformParams sup;
		{
			...
		}
		SetUniformBufferParameterImmediate(RHICmdList, GetPixelShader(), GetUniformBufferParameter<FSomeUniformParams>(), sup);
	}
}

// TViewUniformShaderParameters = FViewUniformShaderParameters
template<typename TViewUniformShaderParameters, typename ShaderRHIParamRef, typename TRHICmdList>
void FGlobalShader::SetParameters(TRHICmdList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FRHIUniformBuffer* ViewUniformBuffer)
{
	const auto& ViewUniformBufferParameter 
		= static_cast<const FShaderUniformBufferParameter&> ( GetUniformBufferParameter<TViewUniformShaderParameters>() );
	SetUniformBufferParameter(RHICmdList, ShaderRHI, ViewUniformBufferParameter, ViewUniformBuffer);
}

// UniformBufferStructType = FViewUniformShaderParameters
template<typename UniformBufferStructType>
const TShaderUniformBufferParameter<UniformBufferStructType>& FShader::GetUniformBufferParameter() const
{
	const FShaderParametersMetadata* SearchStruct = &UniformBufferStructType::StaticStructMetadata;
	int32 FoundIndex = INDEX_NONE;

	for (int32 StructIndex = 0, Count = UniformBufferParameterStructs.Num(); StructIndex < Count; StructIndex++)
	{
		if (UniformBufferParameterStructs[StructIndex] == SearchStruct)
		{
			FoundIndex = StructIndex;
			break;
		}
	}

	if (FoundIndex != INDEX_NONE)
	{
		const auto& FoundParameter = (const TShaderUniformBufferParameter<UniformBufferStructType>&) *UniformBufferParameters[FoundIndex];
		return FoundParameter;
	}
	else
	{
		static TShaderUniformBufferParameter<UniformBufferStructType> UnboundParameter;
		UnboundParameter.SetInitialized();
		return UnboundParameter;
	}
}

template<typename TShaderRHIRef, typename TRHICmdList>
inline void SetUniformBufferParameter(TRHICmdList& RHICmdList, TShaderRHIRef Shader, const FShaderUniformBufferParameter& Parameter, FRHIUniformBuffer* UniformBufferRHI)
{
	if(Parameter.IsBound())
	{
		RHICmdList.SetShaderUniformBuffer( Shader, Parameter.GetBaseIndex(), UniformBufferRHI);
	}
}

template<typename TShaderRHIRef,typename TBufferStruct>
inline void SetUniformBufferParameterImmediate(
	FRHICommandList& RHICmdList,
	TShaderRHIRef Shader,
	const TShaderUniformBufferParameter<TBufferStruct>& Parameter,
	const TBufferStruct& UniformBufferValue
	)
{
	if(Parameter.IsBound())
	{
		RHICmdList.SetShaderUniformBuffer( Shader, Parameter.GetBaseIndex(),
			RHICreateUniformBuffer(&UniformBufferValue, TBufferStruct::StaticStructMetadata.GetLayout(), UniformBuffer_SingleDraw)
		);
	}
}

FORCEINLINE FUniformBufferRHIRef RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation = EUniformBufferValidation::ValidateResources)
{
	return GDynamicRHI->RHICreateUniformBuffer(Contents, Layout, Usage, Validation);
}
FUniformBufferRHIRef FD3D11DynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation)
{
	FD3D11UniformBuffer* NewUniformBuffer = new FD3D11UniformBuffer(this, Layout, , FRingAllocation());
}


template <typename TShaderRHI>
FORCEINLINE_DEBUGGABLE void FRHICommandList::SetShaderUniformBuffer(TShaderRHI* Shader, uint32 BaseIndex, FUniformBufferRHIParamRef UniformBuffer)
{
	if (Bypass())
	{
		GetContext().RHISetShaderUniformBuffer(Shader, BaseIndex, UniformBuffer);
		return;
	}
	ALLOC_COMMAND(FRHICommandSetShaderUniformBuffer<TShaderRHI*, ECmdList::EGfx>)(Shader, BaseIndex, UniformBuffer);
}

void FD3D11DynamicRHI::RHISetShaderUniformBuffer(FPixelShaderRHIParamRef PixelShader,uint32 BufferIndex,FUniformBufferRHIParamRef BufferRHI)
{
	FD3D11UniformBuffer* Buffer = ResourceCast(BufferRHI);
	{
		ID3D11Buffer* ConstantBuffer = Buffer ? Buffer->Resource : NULL;
		StateCache.SetConstantBuffer<SF_Pixel>(ConstantBuffer, BufferIndex);
	}

	BoundUniformBuffers[SF_Pixel][BufferIndex] = BufferRHI;
	DirtyUniformBuffers[SF_Pixel] |= (1 << BufferIndex);
}

template <EShaderFrequency ShaderFrequency>
D3D11_STATE_CACHE_INLINE void FD3D11StateCacheBase::SetConstantBuffer(ID3D11Buffer* ConstantBuffer, uint32 SlotIndex)
{
	switch (ShaderFrequency)
	{
	case SF_Vertex:		Direct3DDeviceIMContext->VSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
	case SF_Hull:		Direct3DDeviceIMContext->HSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
	case SF_Domain:		Direct3DDeviceIMContext->DSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
	case SF_Geometry:	Direct3DDeviceIMContext->GSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
	case SF_Pixel:		Direct3DDeviceIMContext->PSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
	case SF_Compute:	Direct3DDeviceIMContext->CSSetConstantBuffers(SlotIndex, 1, &ConstantBuffer); break;
	}
}






IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSomeType, "cbufferName"); ->
FShaderParametersMetadata::FShaderParametersMetadata(GlobalShaderParameterStruct, , , , , ) -> GlobalListLink.LinkHead(GetStructList())

FShader::FShader(const CompiledShaderInitializerType& Initializer)
{
	for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
	{
		if (Initializer.ParameterMap.ContainsParameterAllocation(StructIt->GetShaderVariableName()))
		{
			UniformBufferParameterStructs.Add(*StructIt);
			UniformBufferParameters.Add(new FShaderUniformBufferParameter());
			FShaderUniformBufferParameter* Parameter = UniformBufferParameters.Last();
			Parameter->Bind(Initializer.ParameterMap, StructIt->GetShaderVariableName(), SPF_Mandatory);
		}
	}
}




BEGIN_SHADER_PARAMETER_STRUCT(FSomeType, )
...
END_SHADER_PARAMETER_STRUCT()

FShader::GetUniformBufferParameter<FSomeType>() CALL -> X
SetUniformBufferParameterImmediate() CALL -> X







///////////////////////////////////////////////////////////////////////////////////////////////////////

class FScene : public FSceneInterface
{
	FPersistentUniformBuffers UniformBuffers;
}


class FPersistentUniformBuffers
{
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
	...
	...
	TUniformBufferRef<FOpaqueBasePassUniformParameters> OpaqueBasePassUniformBuffer;
	const FViewInfo* CachedView;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////

struct FRHIUniformBufferLayout
{
	struct FResourceParameter
	{
		uint16 MemberOffset;
		EUniformBufferBaseType MemberType;
	};
	uint32 ConstantBufferSize;
	TArray<FResourceParameter> Resources;
	FName Name;
	uint32 Hash;
};


class FRHIUniformBuffer : public FRHIResource
{
	const FRHIUniformBufferLayout* Layout;
	uint32 LayoutConstantBufferSize;
};


class FD3D11UniformBuffer : public FRHIUniformBuffer
{
	TRefCountPtr<ID3D11Buffer> Resource;
	FRingAllocation RingAllocation;
	TArray<TRefCountPtr<FRHIResource> > ResourceTable;
	FD3D11DynamicRHI* D3D11RHI;
};


template<typename Params>
class TUniformBufferRef : public TRefCountPtr<FRHIUniformBuffer>
{
	static TUniformBufferRef<Params> CreateUniformBufferImmediate( const Params&, EUniformBufferUsage );
	static FLocalUniformBuffer CreateLocalUniformBuffer( FRHICommandList&, const Params&, EUniformBufferUsage );
	void UpdateUniformBufferImmediate(const Params& Value)
	{
		::RHIUpdateUniformBuffer( FRHIUniformBuffer* Buffer = GetReference(), const void* Contents = &Value )
		{
			// FD3D11DynamicRHI::RHIUpdateUniformBuffer( FRHIUniformBuffer*, const void* )
			GDynamicRHI->RHIUpdateUniformBuffer( Buffer, Contents )
		}
	}
};


void FD3D11DynamicRHI::RHIUpdateUniformBuffer( FRHIUniformBuffer* Buffer, const void* Contents )
{
	FD3D11UniformBuffer* UniformBuffer = ResourceCast(Buffer);
	const FRHIUniformBufferLayout& Layout = *(Buffer->Layout);

	D3D11_MAPPED_SUBRESOURCE MappedSubresource;
	Direct3DDeviceIMContext->Map( UniformBuffer->Resource.GetReference(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubresource );
	FMemory::Memcpy( MappedSubresource.pData, Contents, Layout.ConstantBufferSize );
	Direct3DDeviceIMContext->Unmap( UniformBuffer->Resource.GetReference(), 0 );

	for (int32 i = 0; i < Layout.Resources.Num(); ++i)
	{
		FRHIResource* Resource = * (FRHIResource**) ( (uint8*)Contents + Layout.Resources[i].MemberOffset );
		UniformBuffer->ResourceTable[i] = Resource;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

