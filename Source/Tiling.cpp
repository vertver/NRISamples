// © 2021 NVIDIA Corporation

#include "NRIFramework.h"

#include <array>

constexpr nri::Color32f COLOR_0 = {1.0f, 1.0f, 0.0f, 1.0f};
constexpr nri::Color32f COLOR_1 = {0.46f, 0.72f, 0.0f, 1.0f};

struct Vertex
{
    float position[2];
    float uv[2];
};

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{};

struct Frame
{
    nri::CommandAllocator* commandAllocator;
    nri::CommandBuffer* commandBuffer;
};

class Sample : public SampleBase
{
public:

    Sample()
    {}

    ~Sample();

    bool Initialize(nri::GraphicsAPI graphicsAPI) override;
    void PrepareFrame(uint32_t frameIndex) override;
    void RenderFrame(uint32_t frameIndex) override;

private:

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::Fence* m_FrameFence = nullptr;
    nri::DescriptorPool* m_DescriptorPool = nullptr;
    nri::PipelineLayout* m_PipelineLayout = nullptr;
    nri::Pipeline* m_Pipeline = nullptr;
    nri::DescriptorSet* m_TextureDescriptorSetNearest = nullptr;
    nri::DescriptorSet* m_TextureDescriptorSetLinear = nullptr;
    nri::Descriptor* m_TextureShaderResource = nullptr;
    nri::Descriptor* m_SamplerNearest = nullptr;
    nri::Descriptor* m_SamplerLinear = nullptr;
    nri::Buffer* m_GeometryBuffer = nullptr;
    nri::Texture* m_Texture = nullptr;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::Memory*> m_MemoryAllocations;

    uint64_t m_GeometryOffset = 0;
    bool m_LinearSampler = false;
    float m_Bias = -2.0f;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (Frame& frame : m_Frames)
    {
        NRI.DestroyCommandBuffer(*frame.commandBuffer);
        NRI.DestroyCommandAllocator(*frame.commandAllocator);
    }

    for (BackBuffer& backBuffer : m_SwapChainBuffers)
        NRI.DestroyDescriptor(*backBuffer.colorAttachment);

    NRI.DestroyPipeline(*m_Pipeline);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);
    NRI.DestroyDescriptor(*m_TextureShaderResource);
    NRI.DestroyDescriptor(*m_SamplerNearest);
    NRI.DestroyDescriptor(*m_SamplerLinear);
    NRI.DestroyBuffer(*m_GeometryBuffer);
    NRI.DestroyTexture(*m_Texture);
    NRI.DestroyDescriptorPool(*m_DescriptorPool);
    NRI.DestroyFence(*m_FrameFence);
    NRI.DestroySwapChain(*m_SwapChain);

    for (nri::Memory* memory : m_MemoryAllocations)
        NRI.FreeMemory(*memory);

    DestroyUserInterface();

    nri::nriDestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    nri::Dim_t windowWidth = (nri::Dim_t)GetWindowResolution().x;
    nri::Dim_t windowHeight = (nri::Dim_t)GetWindowResolution().y;


    nri::AdapterDesc bestAdapterDesc = {};
    uint32_t adapterDescsNum = 1;
    NRI_ABORT_ON_FAILURE(nri::nriEnumerateAdapters(&bestAdapterDesc, adapterDescsNum));

    // Device
    nri::DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableAPIValidation = 1;
    deviceCreationDesc.enableNRIValidation = 1;
    deviceCreationDesc.D3D11CommandBufferEmulation = D3D11_COMMANDBUFFER_EMULATION;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    deviceCreationDesc.adapterDesc = &bestAdapterDesc;
    deviceCreationDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    NRI_ABORT_ON_FAILURE( nri::nriCreateDevice(deviceCreationDesc, m_Device) );

    // NRI
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI) );

    // Command queue
    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue) );

    // Fences
    NRI_ABORT_ON_FAILURE( NRI.CreateFence(*m_Device, 0, m_FrameFence) );

    // Swap chain
    nri::Format swapChainFormat;
    {
        nri::SwapChainDesc swapChainDesc = {};
        swapChainDesc.window = GetWindow();
        swapChainDesc.commandQueue = m_CommandQueue;
        swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
        swapChainDesc.verticalSyncInterval = m_VsyncInterval;
        swapChainDesc.width = (uint16_t)GetWindowResolution().x;
        swapChainDesc.height = (uint16_t)GetWindowResolution().y;
        swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
        NRI_ABORT_ON_FAILURE( NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain) );

        uint32_t swapChainTextureNum;
        nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);
        swapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

        for (uint32_t i = 0; i < swapChainTextureNum; i++)
        {
            nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};

            nri::Descriptor* colorAttachment;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(textureViewDesc, colorAttachment) );

            const BackBuffer backBuffer = { colorAttachment, swapChainTextures[i] };
            m_SwapChainBuffers.push_back(backBuffer);
        }
    }

    // Buffered resources
    for (Frame& frame : m_Frames)
    {
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueue, frame.commandAllocator) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffer) );
    }

    // Pipeline
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;
    {
        nri::DescriptorRangeDesc descriptorRangeTexture[2];
        descriptorRangeTexture[0] = {0, 1, nri::DescriptorType::TEXTURE, nri::ShaderStage::FRAGMENT};
        descriptorRangeTexture[1] = {0, 1, nri::DescriptorType::SAMPLER, nri::ShaderStage::FRAGMENT};

        nri::DescriptorSetDesc descriptorSetDescs[] =
        {
            {0, descriptorRangeTexture, helper::GetCountOf(descriptorRangeTexture)},
        };

        nri::PushConstantDesc pushConstant = { 0, sizeof(float), nri::ShaderStage::FRAGMENT };

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = helper::GetCountOf(descriptorSetDescs);
        pipelineLayoutDesc.descriptorSets = descriptorSetDescs;
        pipelineLayoutDesc.pushConstantNum = 1;
        pipelineLayoutDesc.pushConstants = &pushConstant;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

        NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout));

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;
        vertexStreamDesc.stride = sizeof(Vertex);

        nri::VertexAttributeDesc vertexAttributeDesc[2] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[0].streamIndex = 0;
            vertexAttributeDesc[0].offset = helper::GetOffsetOf(&Vertex::position);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk.location = {0};

            vertexAttributeDesc[1].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[1].streamIndex = 0;
            vertexAttributeDesc[1].offset = helper::GetOffsetOf(&Vertex::uv);
            vertexAttributeDesc[1].d3d = {"TEXCOORD", 0};
            vertexAttributeDesc[1].vk.location = {1};
        }

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;
        inputAssemblyDesc.attributes = vertexAttributeDesc;
        inputAssemblyDesc.attributeNum = (uint8_t)helper::GetCountOf(vertexAttributeDesc);
        inputAssemblyDesc.streams = &vertexStreamDesc;
        inputAssemblyDesc.streamNum = 1;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.viewportNum = 1;
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::NONE;
        rasterizationDesc.sampleNum = 1;
        rasterizationDesc.sampleMask = 0xFFFF;

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = swapChainFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDesc.blendEnabled = true;
        colorAttachmentDesc.colorBlend = {nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFunc::ADD};

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.color = &colorAttachmentDesc;

        nri::ShaderDesc shaderStages[] =
        {
            utils::LoadShader(deviceDesc.graphicsAPI, "Tiling.vs", shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, "Tiling.fs", shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
        graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = &rasterizationDesc;
        graphicsPipelineDesc.outputMerger = &outputMergerDesc;
        graphicsPipelineDesc.shaderStages = shaderStages;
        graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);

        NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, m_Pipeline) );
    }

    { // Descriptor pool
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = 2 * (BUFFERED_FRAME_MAX_NUM + 1);
        descriptorPoolDesc.constantBufferMaxNum = 2 * BUFFERED_FRAME_MAX_NUM;
        descriptorPoolDesc.textureMaxNum = 2;
        descriptorPoolDesc.samplerMaxNum = 2;

        NRI_ABORT_ON_FAILURE( NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool) );
    }

    // Load texture
    utils::Texture texture;
    std::string path = utils::GetFullPath("wood.dds", utils::DataFolder::TEXTURES);
    if (!utils::LoadTexture(path, texture))
        return false;

    float aspect = (float)windowWidth / (float)windowHeight;
    const Vertex g_VertexData[] =
    {
        {-0.5f, -0.50f * aspect, 0.0f, 0.0f},
        {-0.5f,  0.50f * aspect, 0.0f, 1.0f},
        { 0.5f, -0.50f * aspect, 1.0f, 0.0f},
        { 0.5f,  0.50f * aspect, 1.0f, 1.0f},
    };

    const uint16_t g_IndexData[] = {
        0,
        1,
        2,
        2,
        3,
        1
    };

    // Resources
    const uint64_t indexDataSize = sizeof(g_IndexData);
    const uint64_t indexDataAlignedSize = helper::Align(indexDataSize, 16);
    const uint64_t vertexDataSize = sizeof(g_VertexData);
    {
        // Texture
        nri::TextureDesc textureDesc = nri::Texture2D(texture.GetFormat(),
            texture.GetWidth(), texture.GetHeight(), texture.GetMipNum());
        NRI_ABORT_ON_FAILURE( NRI.CreateTexture(*m_Device, textureDesc, m_Texture) );

        { // Geometry buffer
            nri::BufferDesc bufferDesc = {};
            bufferDesc.size = indexDataAlignedSize + vertexDataSize;
            bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER;
            NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_GeometryBuffer) );
        }
        m_GeometryOffset = indexDataAlignedSize;
    }

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_GeometryBuffer;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &m_Texture;

    m_MemoryAllocations.resize(1 + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE( NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + 1) );

    { // Descriptors
        // Texture
        nri::Texture2DViewDesc texture2DViewDesc = {m_Texture, nri::Texture2DViewType::SHADER_RESOURCE_2D, texture.GetFormat()};
        NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(texture2DViewDesc, m_TextureShaderResource) );

        // Sampler
        nri::SamplerDesc samplerDesc = {};
        samplerDesc.addressModes = {nri::AddressMode::MIRRORED_REPEAT, nri::AddressMode::MIRRORED_REPEAT};
        samplerDesc.filters = {nri::Filter::LINEAR, nri::Filter::LINEAR, nri::Filter::LINEAR};
        samplerDesc.anisotropy = 4;
        samplerDesc.mipMin = -8.0f;
        samplerDesc.mipMax = 8.0f;
        NRI_ABORT_ON_FAILURE( NRI.CreateSampler(*m_Device, samplerDesc, m_SamplerLinear) );    

        samplerDesc.anisotropy = 1;
        samplerDesc.filters = { nri::Filter::NEAREST, nri::Filter::NEAREST, nri::Filter::NEAREST };
        NRI_ABORT_ON_FAILURE( NRI.CreateSampler(*m_Device, samplerDesc, m_SamplerNearest) );
    }

    // Descriptor sets

    { 
        // Texture
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0,
            &m_TextureDescriptorSetLinear, 1, nri::ALL_NODES, 0) );

        nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs[2] = {};
        descriptorRangeUpdateDescs[0].descriptorNum = 1;
        descriptorRangeUpdateDescs[0].descriptors = &m_TextureShaderResource;

        descriptorRangeUpdateDescs[1].descriptorNum = 1;
        descriptorRangeUpdateDescs[1].descriptors = &m_SamplerLinear;
        NRI.UpdateDescriptorRanges(*m_TextureDescriptorSetLinear, nri::ALL_NODES, 0, helper::GetCountOf(descriptorRangeUpdateDescs), descriptorRangeUpdateDescs);
    }    
    { 
        // Texture
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0,
            &m_TextureDescriptorSetNearest, 1, nri::ALL_NODES, 0) );

        nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs[2] = {};
        descriptorRangeUpdateDescs[0].descriptorNum = 1;
        descriptorRangeUpdateDescs[0].descriptors = &m_TextureShaderResource;

        descriptorRangeUpdateDescs[1].descriptorNum = 1;
        descriptorRangeUpdateDescs[1].descriptors = &m_SamplerNearest;
        NRI.UpdateDescriptorRanges(*m_TextureDescriptorSetNearest, nri::ALL_NODES, 0, helper::GetCountOf(descriptorRangeUpdateDescs), descriptorRangeUpdateDescs);
    }

    { // Upload data
        std::vector<uint8_t> geometryBufferData(indexDataAlignedSize + vertexDataSize);
        memcpy(&geometryBufferData[0], g_IndexData, indexDataSize);
        memcpy(&geometryBufferData[indexDataAlignedSize], g_VertexData, vertexDataSize);

        std::array<nri::TextureSubresourceUploadDesc, 16> subresources;
        for (uint32_t mip = 0; mip < texture.GetMipNum(); mip++)
            texture.GetSubresource(subresources[mip], mip);

        nri::TextureUploadDesc textureData = {};
        textureData.subresources = subresources.data();
        textureData.mipNum = texture.GetMipNum();
        textureData.arraySize = 1;
        textureData.texture = m_Texture;
        textureData.nextState = {nri::AccessBits::SHADER_RESOURCE, nri::TextureLayout::SHADER_RESOURCE};

        nri::BufferUploadDesc bufferData = {};
        bufferData.buffer = m_GeometryBuffer;
        bufferData.data = &geometryBufferData[0];
        bufferData.dataSize = geometryBufferData.size();
        bufferData.nextAccess = nri::AccessBits::INDEX_BUFFER | nri::AccessBits::VERTEX_BUFFER;

        NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, &textureData, 1, &bufferData, 1));
    }

    // User interface
    bool initialized = CreateUserInterface(*m_Device, NRI, NRI, swapChainFormat);

    return initialized;
}

void Sample::PrepareFrame(uint32_t)
{
    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize);
    {
        ImGui::Checkbox("Linear filtering", &m_LinearSampler);
        ImGui::SliderFloat("Bias", &m_Bias, -8.0f, 8.0f);
    }
    ImGui::End();
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    nri::Dim_t windowWidth = (nri::Dim_t)GetWindowResolution().x;
    nri::Dim_t windowHeight = (nri::Dim_t)GetWindowResolution().y;

    const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
    const Frame& frame = m_Frames[bufferedFrameIndex];

    if (frameIndex >= BUFFERED_FRAME_MAX_NUM)
    {
        NRI.Wait(*m_FrameFence, 1 + frameIndex - BUFFERED_FRAME_MAX_NUM);
        NRI.ResetCommandAllocator(*frame.commandAllocator);
    }

    const uint32_t currentTextureIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain);
    BackBuffer& currentBackBuffer = m_SwapChainBuffers[currentTextureIndex];

    nri::TextureTransitionBarrierDesc textureTransitionBarrierDesc = {};
    textureTransitionBarrierDesc.texture = currentBackBuffer.texture;
    textureTransitionBarrierDesc.nextState = {nri::AccessBits::COLOR_ATTACHMENT, nri::TextureLayout::COLOR_ATTACHMENT};
    textureTransitionBarrierDesc.arraySize = 1;
    textureTransitionBarrierDesc.mipNum = 1;

    nri::CommandBuffer* commandBuffer = frame.commandBuffer;
    NRI.BeginCommandBuffer(*commandBuffer, m_DescriptorPool, 0);
    {
        nri::TransitionBarrierDesc transitionBarriers = {};
        transitionBarriers.textureNum = 1;
        transitionBarriers.textures = &textureTransitionBarrierDesc;
        NRI.CmdPipelineBarrier(*commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);

        nri::AttachmentsDesc attachmentsDesc = {};
        attachmentsDesc.colorNum = 1;
        attachmentsDesc.colors = &currentBackBuffer.colorAttachment;

        NRI.CmdBeginRendering(*commandBuffer, attachmentsDesc);
        {
            {
                helper::Annotation annotation(NRI, *commandBuffer, "Clears");

                nri::ClearDesc clearDesc = {};
                clearDesc.attachmentContentType = nri::AttachmentContentType::COLOR;
                clearDesc.value.color32f = {};

                NRI.CmdClearAttachments(*commandBuffer, &clearDesc, 1, nullptr, 0);
            }

            {
                helper::Annotation annotation(NRI, *commandBuffer, "Tiling");

                const nri::Viewport viewport = { 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f };
                NRI.CmdSetViewports( *commandBuffer, &viewport, 1 );

                NRI.CmdSetPipelineLayout(*commandBuffer, *m_PipelineLayout);
                NRI.CmdSetPipeline(*commandBuffer, *m_Pipeline);
                NRI.CmdSetConstants(*commandBuffer, 0, &m_Bias, 4);
                NRI.CmdSetIndexBuffer(*commandBuffer, *m_GeometryBuffer, 0, nri::IndexType::UINT16);
                NRI.CmdSetVertexBuffers(*commandBuffer, 0, 1, &m_GeometryBuffer, &m_GeometryOffset);
                if (m_LinearSampler) 
                    NRI.CmdSetDescriptorSet(*commandBuffer, 0, *m_TextureDescriptorSetLinear, nullptr);
                else
                    NRI.CmdSetDescriptorSet(*commandBuffer, 0, *m_TextureDescriptorSetNearest, nullptr);

                nri::Rect scissor = { 0, 0, windowWidth, windowHeight };
                NRI.CmdSetScissors(*commandBuffer, &scissor, 1);
                NRI.CmdDrawIndexed(*commandBuffer, 6, 1, 0, 0, 0);
            }

            {
                helper::Annotation annotation(NRI, *commandBuffer, "UI");

                RenderUserInterface(*m_Device, *commandBuffer, 1.0f, true);
            }
        }
        NRI.CmdEndRendering(*commandBuffer);

        textureTransitionBarrierDesc.prevState = textureTransitionBarrierDesc.nextState;
        textureTransitionBarrierDesc.nextState = {nri::AccessBits::UNKNOWN, nri::TextureLayout::PRESENT};

        NRI.CmdPipelineBarrier(*commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
    }
    NRI.EndCommandBuffer(*commandBuffer);

    nri::QueueSubmitDesc queueSubmitDesc = {};
    queueSubmitDesc.commandBuffers = &frame.commandBuffer;
    queueSubmitDesc.commandBufferNum = 1;
    NRI.QueueSubmit(*m_CommandQueue, queueSubmitDesc);

    NRI.SwapChainPresent(*m_SwapChain);

    NRI.QueueSignal(*m_CommandQueue, *m_FrameFence, 1 + frameIndex);
}

SAMPLE_MAIN(Sample, 0);
