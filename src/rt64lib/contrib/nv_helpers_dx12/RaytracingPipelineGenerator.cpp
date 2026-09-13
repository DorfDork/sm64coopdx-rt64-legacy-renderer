/*-----------------------------------------------------------------------
Copyright (c) 2014-2018, NVIDIA. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Neither the name of its contributors may be used to endorse
or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/

/*
Contacts for feedback:
- pgautron@nvidia.com (Pascal Gautron)
- mlefrancois@nvidia.com (Martin-Karl Lefrancois)

The raytracing pipeline combines the raytracing shaders into a state object,
that can be thought of as an executable GPU program. For that, it requires the
shaders compiled as DXIL libraries, where each library exports symbols in a way
similar to DLLs. Those symbols are then used to refer to these shaders libraries
when creating hit groups, associating the shaders to their root signatures and
declaring the steps of the pipeline. All the calls to this helper class can be
done in arbitrary order. Some basic sanity checks are also performed when
compiling in debug mode.

*/

#include "RaytracingPipelineGenerator.h"

#include "dxcapi.h"
#include <stdexcept>

namespace nv_helpers_dx12
{

//--------------------------------------------------------------------------------------------------
// The pipeline helper requires access to the device, as well as the
// raytracing device prior to Windows 10 RS5. Also to the global root signature holding the resources shared by every shader.
RayTracingPipelineGenerator::RayTracingPipelineGenerator(ID3D12Device8 *device, ID3D12RootSignature *globalRootSignature)
    : m_device(device), m_globalRootSignature(globalRootSignature)
{
}

//--------------------------------------------------------------------------------------------------
//
// Add a DXIL library to the pipeline. Note that this library has to be
// compiled with dxc, using a lib_6_3 target. The exported symbols must correspond exactly to the
// names of the shaders declared in the library, although unused ones can be omitted.
void RayTracingPipelineGenerator::AddLibrary(IDxcBlob* dxilLibrary,
                                             const std::vector<std::wstring>& symbolExports)
{
  m_libraries.emplace_back(Library(dxilLibrary, symbolExports));
}

//--------------------------------------------------------------------------------------------------
//
// In DXR the hit-related shaders are grouped into hit groups. Such shaders are:
// - The intersection shader, which can be used to intersect custom geometry, and is called upon
//   hitting the bounding box the the object. A default one exists to intersect triangles
// - The any hit shader, called on each intersection, which can be used to perform early
//   alpha-testing and allow the ray to continue if needed. Default is a pass-through.
// - The closest hit shader, invoked on the hit point closest to the ray start.
// The shaders in a hit group share the same root signature, and are only referred to by the
// hit group name in other places of the program.
void RayTracingPipelineGenerator::AddHitGroup(const std::wstring& hitGroupName,
                                              const std::wstring& closestHitSymbol,
                                              const std::wstring& anyHitSymbol /*= L""*/,
                                              const std::wstring& intersectionSymbol /*= L""*/)
{
  m_hitGroups.emplace_back(
      HitGroup(hitGroupName, closestHitSymbol, anyHitSymbol, intersectionSymbol));
}

//--------------------------------------------------------------------------------------------------
//
// The shaders and hit groups may have various root signatures. This call associates a root
// signature to one or more symbols. All imported symbols must be associated to one root
// signature.
void RayTracingPipelineGenerator::AddRootSignatureAssociation(
    ID3D12RootSignature* rootSignature, const std::vector<std::wstring>& symbols)
{
  m_rootSignatureAssociations.emplace_back(RootSignatureAssociation(rootSignature, symbols));
}

//--------------------------------------------------------------------------------------------------
//
// The payload is the way hit or miss shaders can exchange data with the shader that called
// TraceRay. When several ray types are used (e.g. primary and shadow rays), this value must be
// the largest possible payload size. Note that to optimize performance, this size must be kept
// as low as possible.
void RayTracingPipelineGenerator::SetMaxPayloadSize(UINT sizeInBytes)
{
  m_maxPayLoadSizeInBytes = sizeInBytes;
}

//--------------------------------------------------------------------------------------------------
//
// When hitting geometry, a number of surface attributes can be generated by the intersector.
// Using the built-in triangle intersector the attributes are the barycentric coordinates, with a
// size 2*sizeof(float).
void RayTracingPipelineGenerator::SetMaxAttributeSize(UINT sizeInBytes)
{
  m_maxAttributeSizeInBytes = sizeInBytes;
}

//--------------------------------------------------------------------------------------------------
//
// Upon hitting a surface, a closest hit shader can issue a new TraceRay call. This parameter
// indicates the maximum level of recursion. Note that this depth should be kept as low as
// possible, typically 2, to allow hit shaders to trace shadow rays. Recursive ray tracing
// algorithms must be flattened to a loop in the ray generation program for best performance.
void RayTracingPipelineGenerator::SetMaxRecursionDepth(UINT maxDepth)
{
  m_maxRecursionDepth = maxDepth;
}

//--------------------------------------------------------------------------------------------------
//
// Compiles the raytracing state object
ID3D12StateObject* RayTracingPipelineGenerator::Generate()
{
  // The pipeline is made of a set of sub-objects, representing the DXIL libraries, hit group
  // declarations, root signature associations, plus some configuration objects
  UINT64 subobjectCount =
      m_libraries.size() +                     // DXIL libraries
      m_hitGroups.size() +                     // Hit group declarations
      1 +                                      // Shader configuration
      2 * m_rootSignatureAssociations.size() + // Root signature declaration + association
      1 +                                      // Global root signature
      1;                                       // Final pipeline subobject

  // Initialize a vector with the target object count. It is necessary to make the allocation before
  // adding subobjects as some subobjects reference other subobjects by pointer. Using push_back may
  // reallocate the array and invalidate those pointers.
  std::vector<D3D12_STATE_SUBOBJECT> subobjects(subobjectCount);

  UINT currentIndex = 0;

  // Add all the DXIL libraries
  for (const Library& lib : m_libraries)
  {
    D3D12_STATE_SUBOBJECT libSubobject = {};
    libSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    libSubobject.pDesc = &lib.m_libDesc;

    subobjects[currentIndex++] = libSubobject;
  }

  // Add all the hit group declarations
  for (const HitGroup& group : m_hitGroups)
  {
    D3D12_STATE_SUBOBJECT hitGroup = {};
    hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    hitGroup.pDesc = &group.m_desc;

    subobjects[currentIndex++] = hitGroup;
  }

  // Add a subobject for the shader payload configuration
  D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
  shaderDesc.MaxPayloadSizeInBytes = m_maxPayLoadSizeInBytes;
  shaderDesc.MaxAttributeSizeInBytes = m_maxAttributeSizeInBytes;

  D3D12_STATE_SUBOBJECT shaderConfigObject = {};
  shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  shaderConfigObject.pDesc = &shaderDesc;

  subobjects[currentIndex++] = shaderConfigObject;

  // The root signature association requires two objects for each: one to declare the root
  // signature, and another to associate that root signature to a set of symbols
  for (RootSignatureAssociation& assoc : m_rootSignatureAssociations)
  {

    // Add a subobject to declare the root signature
    D3D12_STATE_SUBOBJECT rootSigObject = {};
    rootSigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    rootSigObject.pDesc = &assoc.m_rootSignature;

    subobjects[currentIndex++] = rootSigObject;

    // Add a subobject for the association between the exported shader symbols and the root
    // signature
    assoc.m_association.NumExports = static_cast<UINT>(assoc.m_symbolPointers.size());
    assoc.m_association.pExports = assoc.m_symbolPointers.data();
    assoc.m_association.pSubobjectToAssociate = &subobjects[(currentIndex - 1)];

    D3D12_STATE_SUBOBJECT rootSigAssociationObject = {};
    rootSigAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    rootSigAssociationObject.pDesc = &assoc.m_association;

    subobjects[currentIndex++] = rootSigAssociationObject;
  }

  // Shaders without a local root signature association get an implicit empty one, so no dummy
  // local root signature is declared.
  D3D12_STATE_SUBOBJECT globalRootSig;
  globalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  ID3D12RootSignature *globalSig = m_globalRootSignature;
  globalRootSig.pDesc = &globalSig;

  subobjects[currentIndex++] = globalRootSig;

  // Add a subobject for the ray tracing pipeline configuration
  D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
  pipelineConfig.MaxTraceRecursionDepth = m_maxRecursionDepth;

  D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
  pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  pipelineConfigObject.pDesc = &pipelineConfig;
  
  subobjects[currentIndex++] = pipelineConfigObject;

  // Describe the ray tracing pipeline state object
  D3D12_STATE_OBJECT_DESC pipelineDesc = {};
  pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  pipelineDesc.NumSubobjects = currentIndex; // static_cast<UINT>(subobjects.size());
  pipelineDesc.pSubobjects = subobjects.data();

  ID3D12StateObject* rtStateObject = nullptr;

  // Create the state object
  HRESULT hr = m_device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&rtStateObject));
  if (FAILED(hr))
  {
    throw std::logic_error("Could not create the raytracing state object");
  }

  return rtStateObject;
}

//--------------------------------------------------------------------------------------------------
//
// Store data related to a DXIL library: the library itself, the exported symbols, and the
// associated descriptors
RayTracingPipelineGenerator::Library::Library(IDxcBlob* dxil,
                                              const std::vector<std::wstring>& exportedSymbols)
    : m_dxil(dxil), m_exportedSymbols(exportedSymbols), m_exports(exportedSymbols.size())
{
  // Create one export descriptor per symbol
  for (size_t i = 0; i < m_exportedSymbols.size(); i++)
  {
    m_exports[i] = {};
    m_exports[i].Name = m_exportedSymbols[i].c_str();
    m_exports[i].ExportToRename = nullptr;
    m_exports[i].Flags = D3D12_EXPORT_FLAG_NONE;
  }

  // Create a library descriptor combining the DXIL code and the export names
  m_libDesc.DXILLibrary.BytecodeLength = dxil->GetBufferSize();
  m_libDesc.DXILLibrary.pShaderBytecode = dxil->GetBufferPointer();
  m_libDesc.NumExports = static_cast<UINT>(m_exportedSymbols.size());
  m_libDesc.pExports = m_exports.data();
}

//--------------------------------------------------------------------------------------------------
//
// This copy constructor has to be defined so that the export descriptors are set correctly. Using
// the default constructor would copy the string pointers of the symbols into the descriptors, which
// would cause issues when the original Library object gets out of scope
RayTracingPipelineGenerator::Library::Library(const Library& source)
    : Library(source.m_dxil, source.m_exportedSymbols)
{
}

//--------------------------------------------------------------------------------------------------
//
// Create a hit group descriptor from the input hit group name and shader symbols
RayTracingPipelineGenerator::HitGroup::HitGroup(std::wstring hitGroupName,
                                                std::wstring closestHitSymbol,
                                                std::wstring anyHitSymbol /*= L""*/,
                                                std::wstring intersectionSymbol /*= L""*/)
    : m_hitGroupName(std::move(hitGroupName)), m_closestHitSymbol(std::move(closestHitSymbol)),
      m_anyHitSymbol(std::move(anyHitSymbol)), m_intersectionSymbol(std::move(intersectionSymbol))
{
  // Indicate which shader program is used for closest hit, leave the other
  // ones undefined (default behavior), export the name of the group
  m_desc.HitGroupExport = m_hitGroupName.c_str();
  m_desc.ClosestHitShaderImport = m_closestHitSymbol.empty() ? nullptr : m_closestHitSymbol.c_str();
  m_desc.AnyHitShaderImport = m_anyHitSymbol.empty() ? nullptr : m_anyHitSymbol.c_str();
  m_desc.IntersectionShaderImport =
      m_intersectionSymbol.empty() ? nullptr : m_intersectionSymbol.c_str();
}

//--------------------------------------------------------------------------------------------------
//
// This copy constructor has to be defined so that the export descriptors are set correctly. Using
// the default constructor would copy the string pointers of the symbols into the descriptors, which
// would cause issues when the original HitGroup object gets out of scope
RayTracingPipelineGenerator::HitGroup::HitGroup(const HitGroup& source)
    : HitGroup(source.m_hitGroupName, source.m_closestHitSymbol, source.m_anyHitSymbol,
               source.m_intersectionSymbol)
{
}

//--------------------------------------------------------------------------------------------------
//
// Store the association between a set of symbols and a root signature. The associated descriptors
// will be built when compiling the pipeline. We store the symbol pointers directly so that they can
// be used without processing during compilation.
RayTracingPipelineGenerator::RootSignatureAssociation::RootSignatureAssociation(
    ID3D12RootSignature* rootSignature, const std::vector<std::wstring>& symbols)
    : m_rootSignature(rootSignature), m_symbols(symbols), m_symbolPointers(symbols.size())
{
  for (size_t i = 0; i < m_symbols.size(); i++)
  {
    m_symbolPointers[i] = m_symbols[i].c_str();
  }
  m_rootSignaturePointer = m_rootSignature;
}

//--------------------------------------------------------------------------------------------------
//
// This copy constructor has to be defined so that the export descriptors are set correctly. Using
// the default constructor would copy the string pointers of the symbols into the descriptors, which
// would cause issues when the original RootSignatureAssociation object gets out of scope
RayTracingPipelineGenerator::RootSignatureAssociation::RootSignatureAssociation(
    const RootSignatureAssociation& source)
    : RootSignatureAssociation(source.m_rootSignature, source.m_symbols)
{
}
} // namespace nv_helpers_dx12
