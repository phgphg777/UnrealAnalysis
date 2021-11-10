
// Until UE4.25
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters,)
	...
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters, "FogStruct");

class TExponentialHeightFogPS : public FGlobalShader {

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FHeightFogRenderingParameters& Params)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, RHICmdList.GetBoundPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);
		
		FFogUniformParameters FogUniformParameters;
		SetupFogUniformParameters(View, FogUniformParameters);
		SetUniformBufferParameterImmediate(
			RHICmdList, 
			RHICmdList.GetBoundPixelShader(), 
			GetUniformBufferParameter<FFogUniformParameters>(), 
			FogUniformParameters);
	}
}

// From UE4.26
BEGIN_SHADER_PARAMETER_STRUCT(FFogPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FFogUniformParameters, Fog)
	...
END_SHADER_PARAMETER_STRUCT()

TRDGUniformBufferRef<FFogUniformParameters> CreateFogUniformBuffer(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	auto* FogStruct = GraphBuilder.AllocParameters<FFogUniformParameters>();
	SetupFogUniformParameters(GraphBuilder, View, *FogStruct);
	return GraphBuilder.CreateUniformBuffer(FogStruct);
}

void FDeferredShadingSceneRenderer::RenderFog(...)
{
	FFogPassParameters* PassParameters = GraphBuilder.AllocParameters<FFogPassParameters>();

	TRDGUniformBufferRef<FFogUniformParameters> FogUniformBuffer = CreateFogUniformBuffer(GraphBuilder, View);

	PassParameters->Fog = FogUniformBuffer;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FUniformStruct1,)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FUniformStruct1, "Uniform1");

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FUniformStruct2,)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FUniformStruct2, "Uniform2");

class FSomeCS : public FGlobalShader
{
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FUniformStruct2, UniformRef)
	END_SHADER_PARAMETER_STRUCT()

private:
	FShaderUniformBufferParameter Uniform0;

public:
	FSomeCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer)
	{
		Uniform0.Bind(Initializer.ParameterMap, TEXT("Uniform0"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		// Uniform0 = View.UniformBuffer0;
		SetUniformBufferParameter(RHICmdList, ShaderRHI, Uniform0, View.UniformBuffer0);

		// Uniform0 = RHICreateUniformBuffer(, ,UniformBuffer_SingleDraw);
		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, Uniform0, FUniformStruct0(...));

		// GetUniformBufferParameter<FUniformStruct1>() = View.UniformBuffer1;
		SetUniformBufferParameter(RHICmdList, ShaderRHI, GetUniformBufferParameter<FUniformStruct1>(), View.UniformBuffer1);
	}
}

void FDeferredShadingSceneRenderer::SomePass(FRHICommandListImmediate& RHICmdListImmediate)
{
	FRDGBuilder GraphBuilder(RHICmdListImmediate);

	FSomeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSomeCS::FParameters>();
	PassParameters->UniformRef = View.UniformBuffer2

	GraphBuilder.AddPass(
		RDG_EVENT_NAME(""),
		PassParameters,
		ERenderGraphPassFlags::Compute,
		[PassParameters, &View](FRHICommandListImmediate& RHICmdList)
		{
			auto ComputeShader = View.ShaderMap->GetShader< FSomeCS >();
			RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
			ComputeShader->SetParameters(RHICmdList, View);
			SetShaderParameters(RHICmdList, ComputeShader, ComputeShader->GetComputeShader(), *PassParameters);
			DispatchComputeShader(RHICmdList, 4, 4, 4);
			UnsetShaderUAVs(RHICmdList, ComputeShader, ComputeShader->GetComputeShader());
		}
	);
}




BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FShadowDepthPassUniformParameters,)
	...
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FShadowDepthPassUniformParameters, "ShadowDepthPass");

class RENDERER_API FMeshMaterialShader : public FMaterialShader {
	LAYOUT_FIELD(FShaderUniformBufferParameter, PassUniformBuffer);
}

class FShadowDepthVS : public FMeshMaterialShader {
	LAYOUT_FIELD(FShaderParameter, LayerId);

	FShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, 
			FShadowDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName()); // "ShadowDepthPass"
		LayerId.Bind(Initializer.ParameterMap, TEXT("LayerId"));
	}
};

void FMeshMaterialShader::GetShaderBindings(
	const FScene* Scene,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FMeshPassProcessorRenderState& DrawRenderState,
	const FMeshMaterialShaderElementData& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings) const
{
	ShaderBindings.Add(PassUniformBuffer, DrawRenderState.GetPassUniformBuffer());
	ShaderBindings.Add(GetUniformBufferParameter<FViewUniformShaderParameters>(), DrawRenderState.GetViewUniformBuffer());
}