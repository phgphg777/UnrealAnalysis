
#define BEGIN_SHADER_PARAMETER_STRUCT(StructTypeName, PrefixKeywords)
#define END_SHADER_PARAMETER_STRUCT()

#define BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT/*_WITH_CONSTRUCTOR*/(StructTypeName, PrefixKeywords)
#define END_GLOBAL_SHADER_PARAMETER_STRUCT()
#define IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(StructTypeName,ShaderVariableName)

#define SHADER_PARAMETER(MemberType,MemberName) \
INTERNAL_SHADER_PARAMETER_EXPLICIT(
	TShaderParameterTypeInfo<MemberType>::BaseType, 
	TShaderParameterTypeInfo<MemberType>, 
	MemberType,
	MemberName,
	,,EShaderPrecisionModifier::Float,TEXT(""),false)

#define SHADER_PARAMETER_ARRAY(MemberType,MemberName,ArrayDecl)
#define SHADER_PARAMETER_TEXTURE(ShaderType,MemberName)
//#define SHADER_PARAMETER_SRV(ShaderType,MemberName)
#define SHADER_PARAMETER_SAMPLER(ShaderType,MemberName) 
/*
#define SHADER_PARAMETER_RDG_TEXTURE(ShaderType,MemberName)
#define SHADER_PARAMETER_RDG_TEXTURE_SRV(ShaderType,MemberName)
#define SHADER_PARAMETER_RDG_TEXTURE_UAV(ShaderType,MemberName)
#define SHADER_PARAMETER_RDG_BUFFER(ShaderType,MemberName)
#define SHADER_PARAMETER_RDG_BUFFER_SRV(ShaderType,MemberName)
#define SHADER_PARAMETER_RDG_BUFFER_UAV(ShaderType,MemberName)

#define SHADER_PARAMETER_STRUCT(StructType,MemberName)
#define SHADER_PARAMETER_STRUCT_INCLUDE(StructType,MemberName)
#define SHADER_PARAMETER_STRUCT_REF(StructType,MemberName)
*/
#define RENDER_TARGET_BINDING_SLOTS() \
INTERNAL_SHADER_PARAMETER_EXPLICIT(
	UBMT_RENDER_TARGET_BINDING_SLOTS, 
	FRenderTargetBindingSlots::FTypeInfo, 
	FRenderTargetBindingSlots,
	RenderTargets,
	,,EShaderPrecisionModifier::Float,TEXT(""),false)


namespace EShaderPrecisionModifier {
	enum Type {
		Float,
		Half,
		Fixed
	};
};

struct FResourceTableEntry /** Each entry in a resource table is provided to the shader compiler for creating mappings. */
{
	FString UniformBufferName; /** The name of the uniform buffer in which this resource exists. */
	uint16 Type; /** The type of the resource (EUniformBufferBaseType). */
	uint16 ResourceIndex; /** The index of the resource in the table. */
};

class RENDERCORE_API FShaderParametersMetadata /** A uniform buffer struct. */
{
public:
	enum class EUseCase
	{
		ShaderParameterStruct, /** Stand alone shader parameter struct used for render passes and shader parameters. */
		GlobalShaderParameterStruct, /** Globally named shader parameter struct to be stored in uniform buffer. */
		DataDrivenShaderParameterStruct, /** Shader parameter struct generated from assets, such as material parameter collection or Niagara. */
	};
	
	static constexpr const TCHAR* kRootUniformBufferBindingName = TEXT("_RootShaderParameters"); /** Shader binding name of the uniform buffer that contains the root shader parameters. */

	class FMember /** A member of a shader parameter structure. */
	{
		const TCHAR* Name;							const TCHAR* GetName() const { return Name; }
		const TCHAR* ShaderType;					const TCHAR* GetShaderType() const { return ShaderType; }
		uint32 Offset;								uint32 GetOffset() const { return Offset; }
		EUniformBufferBaseType BaseType;			EUniformBufferBaseType GetBaseType() const { return BaseType; }
		EShaderPrecisionModifier::Type Precision;	EShaderPrecisionModifier::Type GetPrecision() const { return Precision; }
		uint32 NumRows;								uint32 GetNumRows() const { return NumRows; }
		uint32 NumColumns;							uint32 GetNumColumns() const { return NumColumns; }
		uint32 NumElements;							uint32 GetNumElements() const { return NumElements; }
		const FShaderParametersMetadata* Struct;	const FShaderParametersMetadata* GetStructMetadata() const { return Struct; }

		uint32 GetMemberSize() const
		{
			check(BaseType == UBMT_BOOL || BaseType == UBMT_FLOAT32 || BaseType == UBMT_INT32 || BaseType == UBMT_UINT32);
			uint32 ElementSize = sizeof(uint32) * NumRows * NumColumns;

			/** If this an array, the alignment of the element are changed. */
			if (NumElements > 0)
			{
				return Align(ElementSize, SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) * NumElements;
			}
			return ElementSize;
		}
	};

	FShaderParametersMetadata(
		EUseCase UseCase,
		const FName& InLayoutName,
		const TCHAR* InStructTypeName,
		const TCHAR* InShaderVariableName,
		uint32 InSize,
		const TArray<FMember>& InMembers);

private:
	const TCHAR* const StructTypeName; /** Name of the structure type in C++ and shader code. */
	const TCHAR* const ShaderVariableName; /** Name of the shader variable name for global shader parameter structs. */
	const uint32 Size; /** Size of the entire struct in bytes. */
	const EUseCase UseCase; /** The use case of this shader parameter struct. */
	FRHIUniformBufferLayout Layout; /** Layout of all the resources in the shader parameter struct. */
	TArray<FMember> Members; /** List of all members. */
	TLinkedList<FShaderParametersMetadata*> GlobalListLink; /** Shackle elements in global link list of globally named shader parameters. */
	uint32 bLayoutInitialized : 1; /** Whether the layout is actually initialized yet or not. */
};


#define INTERNAL_LOCAL_SHADER_PARAMETER_GET_STRUCT_METADATA(FType) \
	static FShaderParametersMetadata StaticStructMetadata(
		FShaderParametersMetadata::EUseCase::ShaderParameterStruct, 
		FName(TEXT("FType")), TEXT("FType"), 
		nullptr, 
		sizeof(FType), FType::zzGetMembers()
	); 
	return &StaticStructMetadata;

#define IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FType, cbufferName) \
	FShaderParametersMetadata FType::StaticStructMetadata( 
		FShaderParametersMetadata::EUseCase::GlobalShaderParameterStruct, 
		FName(TEXT("FType")), TEXT("FType"), 
		TEXT(cbufferName), 
		sizeof(FType), FType::zzGetMembers() 
	)

#define BEGIN_SHADER_PARAMETER_STRUCT(FType, _API) \
	INTERNAL_SHADER_PARAMETER_STRUCT_BEGIN( FType, _API, 
		{}, 
		INTERNAL_LOCAL_SHADER_PARAMETER_GET_STRUCT_METADATA(FType), 
		return nullptr; 
	)
#define BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FType, _API) \
	INTERNAL_SHADER_PARAMETER_STRUCT_BEGIN( FType, _API,
		{} static FShaderParametersMetadata StaticStructMetadata;, 
		return &FType::StaticStructMetadata;,
		return RHICreateUniformBuffer(&InContents, StaticStructMetadata.GetLayout(), InUsage); 
	)
#define BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FType, _API) \
	INTERNAL_SHADER_PARAMETER_STRUCT_BEGIN( FType, _API,
		; static FShaderParametersMetadata StaticStructMetadata;, 
		return &FType::StaticStructMetadata;,
		return RHICreateUniformBuffer(&InContents, StaticStructMetadata.GetLayout(), InUsage); 
	)



/** Begins a uniform buffer struct declaration. */
#define INTERNAL_SHADER_PARAMETER_STRUCT_BEGIN(FType, _API, ConstructorSuffix, GetStructMetadataScope, CreateUniformBufferImpl) \
__declspec(align(16)) class _API FType 
{ 
public: 
	FType() ConstructorSuffix
	/*
	// BEGIN_SHADER_PARAMETER_STRUCT
	FType() {}

	// BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT
	FType() {} 
	static FShaderParametersMetadata StaticStructMetadata;
	*/
	struct FTypeInfo { 
		static constexpr int32 NumRows = 1; 
		static constexpr int32 NumColumns = 1; 
		static constexpr int32 NumElements = 0; 
		static constexpr int32 Alignment = 16; 
		static constexpr bool bIsStoredInConstantBuffer = true; 
		using TAlignedType = FType; 
		static inline const FShaderParametersMetadata* GetStructMetadata() 
		{ 
			GetStructMetadataScope 
			/*
			// BEGIN_SHADER_PARAMETER_STRUCT
			static FShaderParametersMetadata StaticStructMetadata(
				FShaderParametersMetadata::EUseCase::ShaderParameterStruct, 
				TEXT("FType"), 
				TEXT("FType"), 
				nullptr, 
				nullptr,
				sizeof(FType), 
				FType::zzGetMembers()
			); 
			return &StaticStructMetadata;

			// BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT
			return &FType::StaticStructMetadata;
			*/
		} 
	}; 
	
	static FUniformBufferRHIRef CreateUniformBuffer(const FType& InContents, EUniformBufferUsage InUsage) 
	{ 
		CreateUniformBufferImpl 
		/*
		// BEGIN_SHADER_PARAMETER_STRUCT
		return nullptr;

		// BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT
		return RHICreateUniformBuffer(&InContents, StaticStructMetadata.GetLayout(), InUsage);
		*/
	} 

private: 
	typedef FType zzTThisStruct; 
	struct zzFirstMemberId { enum { HasDeclaredResource = 0 }; }; 
	static TArray<FShaderParametersMetadata::FMember> zzGetMembersBefore(zzFirstMemberId) 
	{ 
		return TArray<FShaderParametersMetadata::FMember>(); 
	} 
	typedef zzFirstMemberId
/** */


#define END_SHADER_PARAMETER_STRUCT() \
		zzLastMemberId; \
		static TArray<FShaderParametersMetadata::FMember> zzGetMembers() { return zzGetMembersBefore(zzLastMemberId()); } \
	} GCC_ALIGN(SHADER_PARAMETER_STRUCT_ALIGNMENT);
#define END_GLOBAL_SHADER_PARAMETER_STRUCT() \
	END_SHADER_PARAMETER_STRUCT()




/** Declares a member of a uniform buffer struct. */
#define INTERNAL_SHADER_PARAMETER_EXPLICIT(BaseType, TypeInfo, MemberType, MemberName,     ArrayDecl,DefaultValue,Precision,OptionalShaderType,IsMemberStruct) \
	zzMemberId##MemberName; 
public: 
	TypeInfo::TAlignedType MemberName DefaultValue; 
private: 
	struct zzNextMemberId##MemberName { enum { HasDeclaredResource = zzMemberId##MemberName::HasDeclaredResource || !TypeInfo::bIsStoredInConstantBuffer }; }; 
	static TArray<FShaderParametersMetadata::FMember> zzGetMembersBefore(zzNextMemberId##MemberName) 
	{ 
		TArray<FShaderParametersMetadata::FMember> OutMembers = zzGetMembersBefore(zzMemberId##MemberName()); 

		OutMembers.Add(FShaderParametersMetadata::FMember( 
			TEXT(#MemberName), 
			OptionalShaderType, 
			STRUCT_OFFSET(zzTThisStruct,MemberName), 
			EUniformBufferBaseType(BaseType), 
			Precision, 
			TypeInfo::NumRows, 
			TypeInfo::NumColumns, 
			TypeInfo::NumElements, 
			TypeInfo::GetStructMetadata() 
			)); 
		return OutMembers; 
	} 
	typedef zzNextMemberId##MemberName
/** */



/** */
/*
#define SHADER_PARAMETER(MemberType,_MemberName) \
INTERNAL_SHADER_PARAMETER_EXPLICIT(
	TShaderParameterTypeInfo<MemberType>::BaseType, 
	TShaderParameterTypeInfo<MemberType>, 
	MemberType,
	_MemberName,
	,,EShaderPrecisionModifier::Float,TEXT(""),false)   
*/
	zzMemberId_MemberName; 
public: 
	TShaderParameterTypeInfo<MemberType>::TAlignedType _MemberName; 
private: 
	struct zzNextMemberId_MemberName { 
		enum { 
			HasDeclaredResource = zzMemberId_MemberName::HasDeclaredResource || !TShaderParameterTypeInfo<MemberType>::bIsStoredInConstantBuffer 
		}; 
	}; 
	static TArray<FShaderParametersMetadata::FMember> zzGetMembersBefore(zzNextMemberId_MemberName) 
	{ 
		/* Route the member enumeration on to the function for the member following this. */ 
		TArray<FShaderParametersMetadata::FMember> OutMembers = zzGetMembersBefore(zzMemberId_MemberName()); 
		/* Add this member. */ 
		OutMembers.Add( 
			FShaderParametersMetadata::FMember( 
				TEXT("_MemberName"), 
				TEXT(""), 
				STRUCT_OFFSET(zzTThisStruct, _MemberName), 
				EUniformBufferBaseType(TShaderParameterTypeInfo<MemberType>::BaseType), 
				EShaderPrecisionModifier::Float, 
				TShaderParameterTypeInfo<MemberType>::NumRows, 
				TShaderParameterTypeInfo<MemberType>::NumColumns, 
				TShaderParameterTypeInfo<MemberType>::NumElements, 
				TShaderParameterTypeInfo<MemberType>::GetStructMetadata() 
			)
		); 
		return OutMembers; 
	} 
	typedef zzNextMemberId_MemberName
/** */


/** */
/*
#define SHADER_PARAMETER_STRUCT_REF(StructType, _MemberName) \
INTERNAL_SHADER_PARAMETER_EXPLICIT(
	UBMT_REFERENCED_STRUCT, 
	TShaderParameterTypeInfo<TUniformBufferRef<StructType>>, 
	TUniformBufferRef<StructType>,
	_MemberName,
	,,EShaderPrecisionModifier::Float, TEXT("StructType"), false) 
*/
	zzMemberId_MemberName; 
public: 
	TShaderParameterTypeInfo<TUniformBufferRef<StructType>>::TAlignedType _MemberName; 
private: 
	struct zzNextMemberId_MemberName { 
		enum { 
			HasDeclaredResource = zzMemberId_MemberName::HasDeclaredResource || !TShaderParameterTypeInfo<TUniformBufferRef<StructType>>::bIsStoredInConstantBuffer 
		}; 
	}; 
	static TArray<FShaderParametersMetadata::FMember> zzGetMembersBefore(zzNextMemberId_MemberName) 
	{ 
		/* Route the member enumeration on to the function for the member following this. */ 
		TArray<FShaderParametersMetadata::FMember> OutMembers = zzGetMembersBefore(zzMemberId_MemberName()); 
		/* Add this member. */ 
		OutMembers.Add( 
			FShaderParametersMetadata::FMember( 
				TEXT("_MemberName"), 
				TEXT("StructType"), 
				STRUCT_OFFSET(zzTThisStruct, _MemberName), 
				EUniformBufferBaseType(UBMT_REFERENCED_STRUCT), 
				EShaderPrecisionModifier::Float, 
				TShaderParameterTypeInfo<TUniformBufferRef<StructType>>::NumRows, 
				TShaderParameterTypeInfo<TUniformBufferRef<StructType>>::NumColumns, 
				TShaderParameterTypeInfo<TUniformBufferRef<StructType>>::NumElements, 
				TShaderParameterTypeInfo<TUniformBufferRef<StructType>>::GetStructMetadata() 
			)
		); 
		return OutMembers; 
	} 
	typedef zzNextMemberId_MemberName
/** */





/*
struct FSomeType
{
	TShaderParameterTypeInfo<FVector>::TAlignedType _M0; 
	TShaderParameterTypeInfo<int>::TAlignedType _M1; 
}
*/
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSomeType,)
	SHADER_PARAMETER(float, _M0)
	SHADER_PARAMETER(int, _M1)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


//BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSomeType,)
__declspec(align(16)) class FSomeType 
{ 
public: 
	FSomeType() {}
	static FShaderParametersMetadata StaticStructMetadata; 

	struct FTypeInfo { 
		static constexpr int32 NumRows = 1; 
		static constexpr int32 NumColumns = 1; 
		static constexpr int32 NumElements = 0; 
		static constexpr int32 Alignment = 16; 
		static constexpr bool bIsStoredInConstantBuffer = true; 
		using TAlignedType = FSomeType; 
		static inline const FShaderParametersMetadata* GetStructMetadata() 
			{ return &FSomeType::StaticStructMetadata; } 
	}; 
	
	static FUniformBufferRHIRef CreateUniformBuffer(const FSomeType& InContents, EUniformBufferUsage InUsage) 
		{ return RHICreateUniformBuffer(&InContents, StaticStructMetadata.GetLayout(), InUsage); } 

private: 
	typedef FSomeType zzTThisStruct; 
	struct zzFirstMemberId { 
		enum { HasDeclaredResource = 0 }; 
	}; 
	static TArray<FShaderParametersMetadata::FMember> zzGetMembersBefore(zzFirstMemberId) 
		{ return TArray<FShaderParametersMetadata::FMember>(); } 
	
	typedef zzFirstMemberId
//BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSomeType,)

//SHADER_PARAMETER(FVector, _M0)
		zzMemberId_M0; 
public: 
	TShaderParameterTypeInfo<FVector>::TAlignedType _M0; 
private: 
	struct zzNextMemberId_M0 { 
		enum { HasDeclaredResource = zzMemberId_M0::HasDeclaredResource || !TShaderParameterTypeInfo<FVector>::bIsStoredInConstantBuffer }; 
	}; 
	static TArray<FShaderParametersMetadata::FMember> zzGetMembersBefore(zzNextMemberId_M0) 
	{ 
		TArray<FShaderParametersMetadata::FMember> OutMembers = zzGetMembersBefore(zzMemberId_M0()); 
		OutMembers.Add( 
			FShaderParametersMetadata::FMember( 
				TEXT("_M0"), TEXT(""), STRUCT_OFFSET(zzTThisStruct, _M0), 
				UBMT_FLOAT32, 
				EShaderPrecisionModifier::Float, 
				1, 3, 0, nullptr
			)
		); 
		return OutMembers; 
	} 

	typedef zzNextMemberId_M0
//SHADER_PARAMETER(FVector, _M0)

//SHADER_PARAMETER(int, _M1)
		zzMemberId_M1; 
public: 
	TShaderParameterTypeInfo<int>::TAlignedType _M1; 
private: 
	struct zzNextMemberId_M1 { 
		enum { HasDeclaredResource = zzMemberId_M1::HasDeclaredResource || !TShaderParameterTypeInfo<int>::bIsStoredInConstantBuffer }; 
	}; 
	static TArray<FShaderParametersMetadata::FMember> zzGetMembersBefore(zzNextMemberId_M1) 
	{ 
		TArray<FShaderParametersMetadata::FMember> OutMembers = zzGetMembersBefore(zzMemberId_M1()); 
		OutMembers.Add( 
			FShaderParametersMetadata::FMember( 
				TEXT("_M1"), TEXT(""), STRUCT_OFFSET(zzTThisStruct, _M1), 
				UBMT_INT32, 
				EShaderPrecisionModifier::Float, 
				1, 1, 0, nullptr
			)
		); 
		return OutMembers; 
	} 

	typedef zzNextMemberId_M1
//SHADER_PARAMETER(int, _M1)

//END_GLOBAL_SHADER_PARAMETER_STRUCT()
	zzLastMemberId; 

	static TArray<FShaderParametersMetadata::FMember> zzGetMembers() { 
		return zzGetMembersBefore(zzLastMemberId()); 
	} 
};
//END_GLOBAL_SHADER_PARAMETER_STRUCT()

