
/*
#define INTERNAL_SHADER_PARAMETER_EXPLICIT(BaseType, TypeInfo, , MemberName, ArrayDecl, DefaultValue, Precision, OptionalShaderType, IsMemberStruct) \
	TypeInfo::TAlignedType MemberName DefaultValue; 

#define SHADER_PARAMETER(MemberType, MemberName) \
	INTERNAL_SHADER_PARAMETER_EXPLICIT(TShaderParameterTypeInfo<MemberType>::BaseType, TShaderParameterTypeInfo<MemberType>, , MemberName,  , , EShaderPrecisionModifier::Float, TEXT(""), false)
=>	TShaderParameterTypeInfo<MemberType>::TAlignedType MemberName;

#define SHADER_PARAMETER_STRUCT_REF(StructType, MemberName) \
	INTERNAL_SHADER_PARAMETER_EXPLICIT(UBMT_REFERENCED_STRUCT, TShaderParameterTypeInfo<TUniformBufferRef<StructType>>, , MemberName,,,EShaderPrecisionModifier::Float,TEXT(#StructType),false)
=>	TShaderParameterTypeInfo<TUniformBufferRef<StructType>>::TAlignedType MemberName;

#define SHADER_PARAMETER_RDG_TEXTURE_UAV(ShaderType,MemberName) \
	INTERNAL_SHADER_PARAMETER_EXPLICIT(UBMT_RDG_TEXTURE_UAV, TShaderResourceParameterTypeInfo<FRDGTextureUAVRef>, , MemberName, , = nullptr,EShaderPrecisionModifier::Float,TEXT(#ShaderType),false)
=>	TShaderResourceParameterTypeInfo<FRDGTextureUAV*>::TAlignedType MemberName = nullptr;
*/

class FVolumetricFogMaterialSetupCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FVolumetricFogMaterialSetupCS, Global)

	/*
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FLinearColor, GlobalAlbedo)
		SHADER_PARAMETER(FLinearColor, GlobalEmissive)
		SHADER_PARAMETER(float, GlobalExtinctionScale)

		SHADER_PARAMETER_STRUCT_REF(FFogUniformParameters, FogUniformParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(, RWVBufferA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(, RWVBufferB)
	END_SHADER_PARAMETER_STRUCT()
	*/
	class FParameters 
	{
		TShaderParameterTypeInfo< FLinearColor >::TAlignedType 		GlobalAlbedo;
		TShaderParameterTypeInfo< FLinearColor >::TAlignedType 		GlobalEmissive;
		TShaderParameterTypeInfo< float >::TAlignedType 			GlobalExtinctionScale;

		TShaderParameterTypeInfo< TUniformBufferRef<FFogUniformParameters> >::TAlignedType 			FogUniformParameters;
		TShaderParameterTypeInfo< TUniformBufferRef<FViewUniformShaderParameters> >::TAlignedType 	View;

		TShaderResourceParameterTypeInfo< FRDGTextureUAV* >::TAlignedType 	RWVBufferA = nullptr;
		TShaderResourceParameterTypeInfo< FRDGTextureUAV* >::TAlignedType 	RWVBufferB = nullptr;
	}

private:
	LAYOUT_FIELD(FVolumetricFogIntegrationParameters, VolumetricFogParameters);
}

void FDeferredShadingSceneRenderer::ComputeVolumetricFog(FRHICommandListImmediate& RHICmdListImmediate)
{
	FRDGBuilder GraphBuilder(RHICmdListImmediate);

	IntegrationData.VBufferA_UAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(IntegrationData.VBufferA));
	IntegrationData.VBufferB_UAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(IntegrationData.VBufferB));

	FVolumetricFogMaterialSetupCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVolumetricFogMaterialSetupCS::FParameters>();
	PassParameters->GlobalAlbedo = FogInfo.VolumetricFogAlbedo;
	PassParameters->GlobalEmissive = FogInfo.VolumetricFogEmissive;
	PassParameters->GlobalExtinctionScale = FogInfo.VolumetricFogExtinctionScale;
	PassParameters->RWVBufferA = IntegrationData.VBufferA_UAV;
	PassParameters->RWVBufferB = IntegrationData.VBufferB_UAV;

	auto ComputeShader = View.ShaderMap->GetShader< FVolumetricFogMaterialSetupCS >();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("InitializeVolumeAttributes"),
		PassParameters,
		ERDGPassFlags::Compute,
		[, , , , ComputeShader](FRHICommandListImmediate& RHICmdList) {...}
	);

	...
}
