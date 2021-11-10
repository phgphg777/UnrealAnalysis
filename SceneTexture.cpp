/*
class RENDERER_API FSceneTextureUniformParameters {
	static FShaderParametersMetadata StaticStructMetadata;
	...
}

FShaderParametersMetadata FSceneTextureUniformParameters::StaticStructMetadata( 
	FShaderParametersMetadata::EUseCase::UniformBuffer, 
	TEXT("FSceneTextureUniformParameters"), 
	TEXT("FSceneTextureUniformParameters"),
	TEXT("SceneTexturesStruct"), 				// ShaderVariable name
	TEXT("SceneTextures"),   					// Slot name
	sizeof(FSceneTextureUniformParameters), 
	FSceneTextureUniformParameters::zzGetMembers())
*/
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, RENDERER_API)
	// Scene Color / Depth
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)

	// GBuffer
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferATexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferBTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferCTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferDTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferETexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferFTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferVelocityTexture)

	// SSAO
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceAOTexture)

	// Custom Depth / Stencil
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_SRV(Texture2D<uint2>, CustomStencilTexture)

	// Misc
	SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(SceneTextures);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FSceneTextureUniformParameters, "SceneTexturesStruct", SceneTextures);


template <typename ParameterStructType>
FORCEINLINE ParameterStructType* FRDGBuilder::AllocParameters()
{
	return Allocator.AllocObject<ParameterStructType>();
}

template <typename ParameterStructType>
TRDGUniformBufferRef<ParameterStructType> FRDGBuilder::CreateUniformBuffer(ParameterStructType* ParameterStruct)
{
	const TCHAR* Name = ParameterStructType::StaticStructMetadata.GetShaderVariableName();
	return UniformBuffers.Allocate< TRDGUniformBuffer<ParameterStructType> >(Allocator, ParameterStruct, Name);
}

TRDGUniformBufferRef<FSceneTextureUniformParameters> CreateSceneTextureUniformBuffer(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

/* 	FSceneTextureUniformParameters* SceneTextures 
		= GraphBuilder.Allocator.AllocObject<FSceneTextureUniformParameters>();

	SetupSceneTextureUniformParameters(&GraphBuilder, FeatureLevel, SceneContext, SetupMode, *SceneTextures);

	TRDGUniformBuffer<FSceneTextureUniformParameters>* UniformBuff; 
		= GraphBuilder.Allocator.AllocObject< TRDGUniformBuffer<FSceneTextureUniformParameters> >(SceneTextures, TEXT("SceneTexturesStruct"));
	
	GraphBuilder.UniformBuffers.Insert(UniformBuff);

	return UniformBuff; */

	FSceneTextureUniformParameters* SceneTextures = GraphBuilder.AllocParameters<FSceneTextureUniformParameters>();
	SetupSceneTextureUniformParameters(&GraphBuilder, FeatureLevel, SceneContext, SetupMode, *SceneTextures);
	return GraphBuilder.CreateUniformBuffer(SceneTextures);
}


BEGIN_SHADER_PARAMETER_STRUCT(FSceneTextureShaderParameters, RENDERER_API)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileSceneTextureUniformParameters, MobileSceneTextures)
END_SHADER_PARAMETER_STRUCT()

FORCEINLINE FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.SceneTextures = UniformBuffer;
	return Parameters;
}