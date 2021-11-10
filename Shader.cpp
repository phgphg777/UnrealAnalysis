
class RENDERCORE_API FShaderType {
	typedef class FShader* (*ConstructSerializedType)();
	typedef void (*GetStreamOutElementsType)(FStreamOutElementList& ElementList, TArray<uint32>& StreamStrides, int32& RasterizedStream);

	EShaderTypeForDynamicCast ShaderTypeForDynamicCast;
	uint32 HashIndex;
	const TCHAR* Name;
	FName TypeName;
	const TCHAR* SourceFilename;
	const TCHAR* FunctionName;
	uint32 Frequency;
	int32 TotalPermutationCount;
	const FShaderParametersMetadata* const RootParametersMetadata;
}
class FGlobalShaderType : public FShaderType {
	typedef FShader* (*ConstructCompiledType)(const CompiledShaderInitializerType&);
	typedef void (*ModifyCompilationEnvironmentType)(const FGlobalShaderPermutationParameters&, FShaderCompilerEnvironment&);
	typedef bool (*ShouldCompilePermutationType)(const FGlobalShaderPermutationParameters&);
	typedef bool(*ValidateCompiledResultType)(EShaderPlatform, const FShaderParameterMap&, TArray<FString>&);
}
class FMaterialShaderType : public FShaderType {
	typedef FShader* (*ConstructCompiledType)(const CompiledShaderInitializerType&);
	typedef void (*ModifyCompilationEnvironmentType)(EShaderPlatform, const FMaterial*, FShaderCompilerEnvironment&);
	typedef bool (*ShouldCompilePermutationType)(EShaderPlatform,const FMaterial*);
	typedef bool(*ValidateCompiledResultType)(EShaderPlatform, const TArray<FMaterial*>&, const FShaderParameterMap&, TArray<FString>&);
}
class FMeshMaterialShaderType : public FShaderType {
	typedef FShader* (*ConstructCompiledType)(const CompiledShaderInitializerType&);
	typedef void (*ModifyCompilationEnvironmentType)(EShaderPlatform, const FMaterial*, FShaderCompilerEnvironment&);
	typedef bool (*ShouldCompilePermutationType)(EShaderPlatform,const FMaterial*,const FVertexFactoryType* VertexFactoryType);
	typedef bool(*ValidateCompiledResultType)(EShaderPlatform, const TArray<FMaterial*>&, const FVertexFactoryType*, const FShaderParameterMap&, TArray<FString>&);
}


#define DECLARE_SHADER_TYPE(ShaderClass, ShaderMetaTypeShortcut) \
public: \
	using FPermutationDomain = FShaderPermutationNone; \
	using ShaderMetaType = F##ShaderMetaTypeShortcut##ShaderType; \
	\
	static ShaderMetaType StaticType; \
	\
	static FShader* ConstructSerializedInstance() { return new ShaderClass(); } \
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) \
	{ return new ShaderClass(Initializer); } \
	\
	virtual uint32 GetTypeSize() const override { return sizeof(*this); }

#define IMPLEMENT_SHADER_TYPE(TemplatePrefix,ShaderClass,SourceFilename,FunctionName,Frequency) \
TemplatePrefix \
ShaderClass::ShaderMetaType ShaderClass::StaticType( \
	TEXT(#ShaderClass), \
	SourceFilename, \
	FunctionName, \
	Frequency, \
	1, \
	ShaderClass::ConstructSerializedInstance, \
	ShaderClass::ConstructCompiledInstance, \
	ShaderClass::ModifyCompilationEnvironment, \
	ShaderClass::ShouldCompilePermutation, \
	ShaderClass::ValidateCompiledResult, \
	ShaderClass::GetStreamOutElements );

#define DECLARE_GLOBAL_SHADER(ShaderClass) \
public: \
	\
	using ShaderMetaType = FGlobalShaderType; \
	\
	static ShaderMetaType StaticType; \
	\
	static FShader* ConstructSerializedInstance() { return new ShaderClass(); } \
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) \
	{ return new ShaderClass(Initializer); } \
	\
	virtual uint32 GetTypeSize() const override { return sizeof(*this); } \
	\
	static bool ShouldCompilePermutationImpl(const FGlobalShaderPermutationParameters& Parameters) \
	{ \
		return ShaderClass::ShouldCompilePermutation(Parameters); \
	} \
	\
	static bool ValidateCompiledResultImpl(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) \
	{ \
		return ShaderClass::ValidateCompiledResult(Platform, ParameterMap, OutErrors); \
	} \
	static void ModifyCompilationEnvironmentImpl( \
		const FGlobalShaderPermutationParameters& Parameters, \
		FShaderCompilerEnvironment& OutEnvironment) \
	{ \
		FPermutationDomain PermutationVector(Parameters.PermutationId); \
		PermutationVector.ModifyCompilationEnvironment(OutEnvironment); \
		ShaderClass::ModifyCompilationEnvironment(Parameters, OutEnvironment); \
	}

#define IMPLEMENT_GLOBAL_SHADER(ShaderClass,SourceFilename,FunctionName,Frequency) \
ShaderClass::ShaderMetaType ShaderClass::StaticType( \
	TEXT(#ShaderClass), \
	TEXT(SourceFilename), \
	TEXT(FunctionName), \
	Frequency, \
	ShaderClass::FPermutationDomain::PermutationCount, \
	ShaderClass::ConstructSerializedInstance, \
	ShaderClass::ConstructCompiledInstance, \
	ShaderClass::ModifyCompilationEnvironmentImpl, \
	ShaderClass::ShouldCompilePermutationImpl, \
	ShaderClass::ValidateCompiledResultImpl, \
	ShaderClass::GetStreamOutElements, \
	ShaderClass::GetRootParametersMetadata() );



class RENDERCORE_API FShader : public FDeferredCleanupInterface 
{
	TRefCountPtr<FShaderResource> Resource; /** Reference to the shader resource, which stores the compiled bytecode and the RHI shader resource. */	
	FVertexFactoryType* VFType; /** Vertex factory type this shader was created for, stored so that an FShaderId can be constructed from this shader. */
	FShaderType* Type; /** Shader Type metadata for this shader. */
	int32 PermutationId; /** Unique permutation identifier of the shader in the shader type. */
	FShaderTarget Target; /** Target platform and frequency. */
public:
	FShaderParameterBindings Bindings; /** Shader parameter bindings. */
	
	static void GetStreamOutElements(FStreamOutElementList& ElementList, TArray<uint32>& StreamStrides, int32& RasterizedStream) {}
	static const FShaderParametersMetadata* GetRootParametersMetadata() {return nullptr;}
}


class FGlobalShader : public FShader
{
// DECLARE_SHADER_TYPE(FGlobalShader, Global);
public: 
	using FPermutationDomain = FShaderPermutationNone; 
	using ShaderMetaType = FGlobalShaderType; 
	static ShaderMetaType StaticType; 
	
	static FShader* ConstructSerializedInstance() { return new FGlobalShader(); } 
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) { return new FGlobalShader(Initializer); } 
	virtual uint32 GetTypeSize() const override { return sizeof(*this); }
// DECLARE_SHADER_TYPE(FGlobalShader, Global);	

	RENDERCORE_API static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) {}
	RENDERCORE_API static bool ValidateCompiledResult(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) { return true; }
};

class FVolumetricFogMaterialSetupCS : public FGlobalShader
{
// DECLARE_SHADER_TYPE(FVolumetricFogMaterialSetupCS, Global)
public: 
	using FPermutationDomain = FShaderPermutationNone; 
	using ShaderMetaType = FGlobalShaderType; 
	static ShaderMetaType StaticType; 
	
	static FShader* ConstructSerializedInstance() { return new FVolumetricFogMaterialSetupCS(); } 
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) { return new FVolumetricFogMaterialSetupCS(Initializer); } 
	virtual uint32 GetTypeSize() const override { return sizeof(*this); }
// DECLARE_SHADER_TYPE(FVolumetricFogMaterialSetupCS, Global)
}
// IMPLEMENT_SHADER_TYPE(, FVolumetricFogMaterialSetupCS, TEXT("/Engine/Private/VolumetricFog.usf"), TEXT("MaterialSetupCS"), SF_Compute);
FVolumetricFogMaterialSetupCS::ShaderMetaType FVolumetricFogMaterialSetupCS::StaticType( 
	TEXT("FVolumetricFogMaterialSetupCS"), 
	TEXT("/Engine/Private/VolumetricFog.usf"), 
	TEXT("MaterialSetupCS"), 
	SF_Compute, 
	1, 
	FVolumetricFogMaterialSetupCS::ConstructSerializedInstance, 
	FVolumetricFogMaterialSetupCS::ConstructCompiledInstance, 
	FVolumetricFogMaterialSetupCS::ModifyCompilationEnvironment, 
	FVolumetricFogMaterialSetupCS::ShouldCompilePermutation, 
	FVolumetricFogMaterialSetupCS::ValidateCompiledResult, 
	FVolumetricFogMaterialSetupCS::GetStreamOutElements );
// IMPLEMENT_SHADER_TYPE(, FVolumetricFogMaterialSetupCS, TEXT("/Engine/Private/VolumetricFog.usf"), TEXT("MaterialSetupCS"), SF_Compute);

class TVolumetricFogLightScatteringCS : public FGlobalShader
{
// DECLARE_GLOBAL_SHADER(TVolumetricFogLightScatteringCS)
public: 
	using ShaderMetaType = FGlobalShaderType; 
	static ShaderMetaType StaticType; 
	
	static FShader* ConstructSerializedInstance() { return new TVolumetricFogLightScatteringCS(); } 
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) { return new TVolumetricFogLightScatteringCS(Initializer); } 
	virtual uint32 GetTypeSize() const override { return sizeof(*this); } 
	
	static bool ShouldCompilePermutationImpl(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return TVolumetricFogLightScatteringCS::ShouldCompilePermutation(Parameters); 
	} 
	
	static bool ValidateCompiledResultImpl(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) 
	{ 
		return TVolumetricFogLightScatteringCS::ValidateCompiledResult(Platform, ParameterMap, OutErrors); 
	} 
	static void ModifyCompilationEnvironmentImpl(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) 
	{ 
		FPermutationDomain PermutationVector(Parameters.PermutationId); 
		PermutationVector.ModifyCompilationEnvironment(OutEnvironment); 
		TVolumetricFogLightScatteringCS::ModifyCompilationEnvironment(Parameters, OutEnvironment); 
	}
// DECLARE_GLOBAL_SHADER(TVolumetricFogLightScatteringCS)
}

// IMPLEMENT_GLOBAL_SHADER(TVolumetricFogLightScatteringCS, "/Engine/Private/VolumetricFog.usf", "LightScatteringCS", SF_Compute);
TVolumetricFogLightScatteringCS::ShaderMetaType TVolumetricFogLightScatteringCS::StaticType( 
	TEXT("TVolumetricFogLightScatteringCS"), 
	TEXT("/Engine/Private/VolumetricFog.usf"), 
	TEXT("LightScatteringCS"), 
	SF_Compute, 
	TVolumetricFogLightScatteringCS::FPermutationDomain::PermutationCount, 
	TVolumetricFogLightScatteringCS::ConstructSerializedInstance, 
	TVolumetricFogLightScatteringCS::ConstructCompiledInstance, 
	TVolumetricFogLightScatteringCS::ModifyCompilationEnvironmentImpl, 
	TVolumetricFogLightScatteringCS::ShouldCompilePermutationImpl, 
	TVolumetricFogLightScatteringCS::ValidateCompiledResultImpl, 
	TVolumetricFogLightScatteringCS::GetStreamOutElements, 
	TVolumetricFogLightScatteringCS::GetRootParametersMetadata() );
// IMPLEMENT_GLOBAL_SHADER(TVolumetricFogLightScatteringCS, "/Engine/Private/VolumetricFog.usf", "LightScatteringCS", SF_Compute);




//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<typename ShaderMetaType>  // FGlobalShaderType || FMaterialShaderType || FMeshMaterialShaderType
class TShaderMap {
	struct FShaderPrimaryKey { FShaderType* Type; int32 PermutationId; };
	TMap< FShaderPrimaryKey, TRefCountPtr<FShader> > Shaders;
	uint32 GetNumShaders() { return Shaders.Num(); }
}

class FMeshMaterialShaderMap : public TShaderMap<FMeshMaterialShaderType> { /** The shaders which the render the material on a mesh generated by a particular vertex factory type. */
	FVertexFactoryType* VertexFactoryType; /** The vertex factory type these shaders are for. */
};

class FMaterialShaderMap : public TShaderMap<FMaterialShaderType>, public FDeferredCleanupInterface { /** The set of material shaders for a single material. */
	TIndirectArray<FMeshMaterialShaderMap> MeshShaderMaps; /** The material's cached shaders for vertex factory type dependent shaders. */
	TArray<FMeshMaterialShaderMap*> OrderedMeshShaderMaps; /** The material's mesh shader maps, indexed by VFType->GetId(), for fast lookup at runtime. */
	
	FMeshMaterialShaderMap* FMaterialShaderMap::GetMeshShaderMap(FVertexFactoryType* VertexFactoryType) {return OrderedMeshShaderMaps[VertexFactoryType->GetId()];}
}

