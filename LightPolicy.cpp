
class FUniformLightMapPolicyShaderParametersType {
	LAYOUT_FIELD(FShaderUniformBufferParameter, PrecomputedLightingBufferParameter);
	LAYOUT_FIELD(FShaderUniformBufferParameter, IndirectLightingCacheParameter);
	LAYOUT_FIELD(FShaderUniformBufferParameter, LightmapResourceCluster);
};

class FUniformLightMapPolicy {
	ELightMapPolicyType IndirectPolicy;
	typedef const FLightCacheInterface* ElementDataType;
	typedef FUniformLightMapPolicyShaderParametersType PixelParametersType;
	typedef FUniformLightMapPolicyShaderParametersType VertexParametersType;
};
template <ELightMapPolicyType Policy>
class TUniformLightMapPolicy : public FUniformLightMapPolicy {
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		switch (Policy)
		{
		case LMP_NO_LIGHTMAP:
			return FNoLightMapPolicy::ShouldCompilePermutation(Parameters);
		case LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING:
			return FPrecomputedVolumetricLightmapLightingPolicy::ShouldCompilePermutation(Parameters);
		case LMP_LQ_LIGHTMAP:
			return TLightMapPolicy<LQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);
		case LMP_HQ_LIGHTMAP:
			return TLightMapPolicy<HQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);
		case LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP:
			return TDistanceFieldShadowsAndLightMapPolicy<HQ_LIGHTMAP>::ShouldCompilePermutation(Parameters);
		}
	}
};

template< ELightmapQuality LightmapQuality >
struct TLightMapPolicy {
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(LightmapQuality==LQ_LIGHTMAP ? TEXT("LQ_TEXTURE_LIGHTMAP") : TEXT("HQ_TEXTURE_LIGHTMAP"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("NUM_LIGHTMAP_COEFFICIENTS"), 2);
		OutEnvironment.SetDefine(TEXT("LIGHTMAP_VT_ENABLED"), TEXT("r.VirtualTexturedLightmaps"));
	}
};

template< ELightmapQuality LightmapQuality >
struct TDistanceFieldShadowsAndLightMapPolicy : public TLightMapPolicy< LightmapQuality > {
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("STATICLIGHTING_TEXTUREMASK"), 1);
		OutEnvironment.SetDefine(TEXT("STATICLIGHTING_SIGNEDDISTANCEFIELD"), 1);
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

template<typename LightMapPolicyType>
class TBasePassShaderElementData : public FMeshMaterialShaderElementData {
	LightMapPolicyType::ElementDataType LightMapPolicyElementData;
};



class RENDERER_API FMaterialShader : public FShader {
	LAYOUT_FIELD(FShaderUniformBufferParameter, MaterialUniformBuffer);
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
}
class RENDERER_API FMeshMaterialShader : public FMaterialShader {
	LAYOUT_FIELD(FShaderUniformBufferParameter, PassUniformBuffer);
};

template<typename LightMapPolicyType>
class TBasePassVertexShaderPolicyParamType : public FMeshMaterialShader, public LightMapPolicyType::VertexParametersType {
	LAYOUT_FIELD(FShaderUniformBufferParameter, ReflectionCaptureBuffer);
	LAYOUT_FIELD(FShaderUniformBufferParameter, FogControlCurvesBuffer);
};

void TBasePassVertexShaderPolicyParamType<LightMapPolicyType>::GetShaderBindings(
	const FScene* Scene,,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FMeshPassProcessorRenderState& DrawRenderState,
	const TBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings)
{
	FMeshMaterialShader::GetShaderBindings(, , , , , , , );

	if (Scene)
	{
		ShaderBindings.Add(ReflectionCaptureBuffer, Scene->UniformBuffers.ReflectionCaptureUniformBuffer.GetReference());
		ShaderBindings.Add(FogControlCurvesBuffer, Scene->UniformBuffers.FogControlCurvesUniformBuffer.GetReference());
	}

	LightMapPolicyType::GetVertexShaderBindings(
		PrimitiveSceneProxy,
		ShaderElementData.LightMapPolicyElementData,
		this,
		ShaderBindings);
}

void FUniformLightMapPolicy::GetVertexShaderBindings(
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const ElementDataType& ShaderElementData,
	const VertexParametersType* VertexShaderParameters,
	FMeshDrawSingleShaderBindings& ShaderBindings) 
{
	FRHIUniformBuffer* PrecomputedLightingBuffer = nullptr;
	FRHIUniformBuffer* LightmapResourceClusterBuffer = nullptr;
	FRHIUniformBuffer* IndirectLightingCacheBuffer = nullptr;
	SetupLCIUniformBuffers(PrimitiveSceneProxy, ShaderElementData, PrecomputedLightingBuffer, LightmapResourceClusterBuffer, IndirectLightingCacheBuffer);

	ShaderBindings.Add(VertexShaderParameters->PrecomputedLightingBufferParameter, PrecomputedLightingBuffer);
	ShaderBindings.Add(VertexShaderParameters->IndirectLightingCacheParameter, IndirectLightingCacheBuffer);
	ShaderBindings.Add(VertexShaderParameters->LightmapResourceCluster, LightmapResourceClusterBuffer);
}

void SetupLCIUniformBuffers(const FPrimitiveSceneProxy* PrimitiveSceneProxy, const FLightCacheInterface* LCI, FRHIUniformBuffer*& PrecomputedLightingBuffer, FRHIUniformBuffer*& LightmapResourceClusterBuffer, FRHIUniformBuffer*& IndirectLightingCacheBuffer)
{
	if (LCI) {
		PrecomputedLightingBuffer = LCI->GetPrecomputedLightingBuffer();
	}
	if (LCI && LCI->GetResourceCluster()) {
		LightmapResourceClusterBuffer = LCI->GetResourceCluster()->UniformBuffer;
	}
	if (PrimitiveSceneProxy && PrimitiveSceneProxy->GetPrimitiveSceneInfo()) {
		IndirectLightingCacheBuffer = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->IndirectLightingCacheUniformBuffer;
	}

	if (!PrecomputedLightingBuffer) {
		PrecomputedLightingBuffer = GEmptyPrecomputedLightingUniformBuffer.GetUniformBufferRHI();
	}
	if (!LightmapResourceClusterBuffer) {
		LightmapResourceClusterBuffer = GDefaultLightmapResourceClusterUniformBuffer.GetUniformBufferRHI();
	}
	if (!IndirectLightingCacheBuffer) {
		IndirectLightingCacheBuffer = GEmptyIndirectLightingCacheUniformBuffer.GetUniformBufferRHI();
	}
}

class TBasePassVertexShaderPolicyParamType<LightMapPolicyType> : public FMeshMaterialShader, public LightMapPolicyType::VertexParametersType {}
class TBasePassVertexShaderBaseType<LightMapPolicyType> : public TBasePassVertexShaderPolicyParamType<LightMapPolicyType> {}
class TBasePassVS<LightMapPolicyType> : public TBasePassVertexShaderBaseType<LightMapPolicyType> {}

template<typename LightMapPolicyType>
void FBasePassMeshProcessor::Process()
{
	TMeshProcessorShaders<
		TBasePassVertexShaderPolicyParamType<LightMapPolicyType>,
		FBaseHS,
		FBaseDS,
		TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> BasePassShaders;

	GetBasePassShaders<LightMapPolicyType>(
		MaterialResource,
		VertexFactory->GetType(),
		LightMapPolicy,,
		bRenderAtmosphericFog,
		bRenderSkylight,
		Get128BitRequirement(),
		BasePassShaders.HullShader,
		BasePassShaders.DomainShader,
		BasePassShaders.VertexShader,
		BasePassShaders.PixelShader
		);
}

template<>
void GetBasePassShaders<FUniformLightMapPolicy>(,, FUniformLightMapPolicy LightMapPolicy,,,,,,,,)
{
	switch (LightMapPolicy.GetIndirectPolicy())
	{
	case LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING:
		GetUniformBasePassShaders<LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING>(, , , , , , , , , );
	case LMP_LQ_LIGHTMAP:
		GetUniformBasePassShaders<LMP_LQ_LIGHTMAP>(, , , , , , , , , );
	case LMP_HQ_LIGHTMAP:
		GetUniformBasePassShaders<LMP_HQ_LIGHTMAP>(, , , , , , , , , );
	case LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP:
		GetUniformBasePassShaders<LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP>(, , , , , , , , , );
	case LMP_NO_LIGHTMAP:
		GetUniformBasePassShaders<LMP_NO_LIGHTMAP>(, , , , , , , , , );
	}
}

template <ELightMapPolicyType Policy>
void GetUniformBasePassShaders(
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	ERHIFeatureLevel::Type FeatureLevel,
	bool bEnableAtmosphericFog,
	bool bEnableSkyLight,,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<TBasePassVertexShaderPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>>& PixelShader)
{
	if( RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders() 
		&& Material.GetTessellationMode() != MTM_NoTessellation )
	{
		DomainShader = Material.GetShader< TBasePassDS<TUniformLightMapPolicy<Policy>> > (VertexFactoryType);
		HullShader = Material.GetShader< TBasePassHS<TUniformLightMapPolicy<Policy>,false> > (VertexFactoryType);
	}

	VertexShader = TShaderRef< TBasePassVertexShaderPolicyParamType <FUniformLightMapPolicy> >::ReinterpretCast( 
		Material.GetShader< TBasePassVS <TUniformLightMapPolicy<Policy>, bEnableAtmosphericFog> > (VertexFactoryType) 
	);

	PixelShader = TShaderRef< TBasePassPixelShaderPolicyParamType <FUniformLightMapPolicy> >::ReinterpretCast(
		Material.GetShader< TBasePassPS <TUniformLightMapPolicy<Policy>, bEnableSkyLight> > (VertexFactoryType)
	);
}



#define IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE(A, _B_) \
IMPLEMENT_BASEPASS_VERTEXSHADER_TYPE(A,_B_) \
IMPLEMENT_BASEPASS_VERTEXSHADER_ONLY_TYPE(A,_B_,AtmosphericFog) \
IMPLEMENT_BASEPASS_PIXELSHADER_TYPE(A,_B_,true,Skylight) \
IMPLEMENT_BASEPASS_PIXELSHADER_TYPE(A,_B_,false,);

#define IMPLEMENT_BASEPASS_VERTEXSHADER_TYPE(A, _B_) \
typedef TBasePassVS< A, false > TBasePassVS_B_ ; \
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassVS##B,TEXT("/Engine/Private/BasePassVertexShader.usf"),TEXT("Main"),SF_Vertex); \
typedef TBasePassHS< A, false > TBasePassHS_B_; \
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassHS##B,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainHull"),SF_Hull); \
typedef TBasePassDS< A > TBasePassDS_B_; \
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassDS##B,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainDomain"),SF_Domain); 

#define IMPLEMENT_BASEPASS_VERTEXSHADER_ONLY_TYPE(A,_B_,AtmosphericFogShaderName) \
typedef TBasePassVS< A, true > TBasePassVS_B_AtmosphericFogShaderName; \
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassVS_B_AtmosphericFogShaderName,TEXT("/Engine/Private/BasePassVertexShader.usf"),TEXT("Main"),SF_Vertex)	\
typedef TBasePassHS< A, true > TBasePassHS_B_AtmosphericFogShaderName; \
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassHS_B_AtmosphericFogShaderName,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainHull"),SF_Hull);

#define IMPLEMENT_BASEPASS_PIXELSHADER_TYPE(A,_B_,bEnableSkyLight,SkyLightName) \
typedef TBasePassPS< A, bEnableSkyLight > TBasePassPS_B_SkyLightName; \
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassPS_B_SkyLightName,TEXT("/Engine/Private/BasePassPixelShader.usf"),TEXT("MainPS"),SF_Pixel);


IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, _HQ_ );
=>
typedef TBasePassVS< TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, false > 	TBasePassVS_HQ_; 
typedef TBasePassVS< TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, true > 	TBasePassVS_HQ_AtmosphericFog;
typedef TBasePassHS< TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, false > 	TBasePassHS_HQ_; 
typedef TBasePassHS< TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, true > 	TBasePassHS_HQ_AtmosphericFog;
typedef TBasePassDS< TUniformLightMapPolicy<LMP_HQ_LIGHTMAP> > 			TBasePassDS_HQ_; 
typedef TBasePassPS< TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, false > 	TBasePassPS_HQ_; 
typedef TBasePassPS< TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, true > 	TBasePassPS_HQ_SkyLight; 
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassVS_HQ_,TEXT("/Engine/Private/BasePassVertexShader.usf"),TEXT("Main"),SF_Vertex); 
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassVS_HQ_AtmosphericFog,TEXT("/Engine/Private/BasePassVertexShader.usf"),TEXT("Main"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassHS_HQ_,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainHull"),SF_Hull); 
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassHS_HQ_AtmosphericFog,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainHull"),SF_Hull);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassDS_HQ_,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainDomain"),SF_Domain); 
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassPS_HQ_,TEXT("/Engine/Private/BasePassPixelShader.usf"),TEXT("MainPS"),SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassPS_HQ_SkyLight,TEXT("/Engine/Private/BasePassPixelShader.usf"),TEXT("MainPS"),SF_Pixel);
