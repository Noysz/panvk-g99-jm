/* draw_indirect_test.c
 *
 * Phase 4.4 — validates vkCmdDrawIndirect on Mali-G57 MC2 (v9/JM).
 *
 * Job Manager hardware has no indirect-draw descriptor, so this exercises a
 * software emulation: the CPU builds a MALLOC_VERTEX job with placeholder
 * counts, parks it as NOT_STARTED, and a helper compute kernel overwrites the
 * counts from the application's buffer and promotes the job type.
 *
 * Uses triangle6.vert, which holds two distinct triangles:
 *   vertices 0,1,2 -> triangle A, covers the centre, 512 non-black
 *   vertices 3,4,5 -> triangle B, corner only,       253 non-black
 *
 * Having two selectable shapes is what makes the test meaningful. A test that
 * only checks "did something draw" cannot tell a working indirect path from one
 * that ignores the buffer and draws the placeholder.
 *
 * Cases, via DI_CASE:
 *   direct     vkCmdDraw(3,1,0,0)                    reference
 *   same       indirect {3,1,0,0}   must be BYTE-IDENTICAL to direct
 *   firstvtx   indirect {3,1,3,0}   must give triangle B, proving firstVertex
 *                                   came from the buffer
 *   vcount0    indirect {0,1,0,0}   nothing drawn, no fault
 *   icount0    indirect {3,0,0,0}   nothing drawn, no fault
 *   twodraws   drawCount=2, {3,1,0,0} then {3,1,3,0}: both triangles
 *
 * 'firstvtx' is the load-bearing case. The placeholder the CPU writes is
 * firstVertex=0, so triangle B can only appear if the helper actually read the
 * buffer.
 *
 * Env: DI_CASE, DI_PPM=<path>, PANVK_ICD_SO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include "vulkan/vulkan_core.h"

#define CHECK(e,m) do { VkResult _r=(e); \
    if(_r!=VK_SUCCESS){printf("FAILED: %s (VkResult=%d)\n",m,_r);return 1;} } while(0)

typedef PFN_vkVoidFunction (*PFN_icdGetInstanceProcAddr)(VkInstance,const char*);
#define DIMS 64

static uint32_t mtype(VkPhysicalDeviceMemoryProperties*mp,uint32_t b,VkMemoryPropertyFlags w){
    for(uint32_t i=0;i<mp->memoryTypeCount;i++)
        if((b&(1u<<i))&&(mp->memoryTypes[i].propertyFlags&w)==w) return i;
    return UINT32_MAX;
}
static char*rf(const char*p,size_t*s){
    FILE*f=fopen(p,"rb"); if(!f){printf("FAILED: open %s\n",p);return NULL;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char*b=malloc(n);
    if(fread(b,1,n,f)!=(size_t)n){printf("FAILED: read %s\n",p);fclose(f);free(b);return NULL;}
    fclose(f);*s=(size_t)n;return b;
}

#ifndef PANVK_DEFAULT_ICD_SO
#define PANVK_DEFAULT_ICD_SO \
 "/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so"
#endif

struct tcase {
    const char *name;
    int         indirect;
    int         indexed;                 /* use the index-buffer path */
    uint32_t    draw_count;
    VkDrawIndirectCommand cmds[2];       /* non-indexed commands */
    VkDrawIndexedIndirectCommand icmds[2]; /* indexed commands */
    uint32_t    nidx;                    /* indices written into the buffer */
    const char *expect;
};

int main(void){
    const char*cs=getenv("DI_CASE"); if(!cs||!cs[0]) cs="direct";

    struct tcase cases[] = {
      { "direct",   0,0, 1, {{3,1,0,0}}, {{0}}, 0, "reference, triangle A, 512" },
      { "same",     1,0, 1, {{3,1,0,0}}, {{0}}, 0, "byte-identical to direct" },
      { "firstvtx", 1,0, 1, {{3,1,3,0}}, {{0}}, 0, "triangle B, 253, centre black" },
      { "vcount0",  1,0, 1, {{0,1,0,0}}, {{0}}, 0, "nothing drawn, no fault" },
      { "icount0",  1,0, 1, {{3,0,0,0}}, {{0}}, 0, "nothing drawn, no fault" },
      { "twodraws", 1,0, 2, {{3,1,0,0},{3,1,3,0}}, {{0}}, 0, "both triangles, >512" },
      /* Placeholder-stress: the CPU records vertex.count = 1, so a count of 6
       * here is 6x the placeholder. If anything is sized at record time from the
       * placeholder rather than the real count, this is where it shows. 6
       * vertices = both triangles = 765, same as twodraws. */
      { "vcount6",  1,0, 1, {{6,1,0,0}}, {{0}}, 0, "both triangles via one draw, 765" },
      { "idx_count6",1,1, 1, {{0}}, {{6,1,0,0,0}}, 6, "indexed, 6 indices, 765" },
      /* manytri.vert: coverage grows with vertexCount. Use with DI_VS. */
      { "many3",    0,0, 1, {{3,1,0,0}},  {{0}}, 0, "1 column, direct reference" },
      { "many3i",   1,0, 1, {{3,1,0,0}},  {{0}}, 0, "1 column via indirect" },
      { "many30",   0,0, 1, {{30,1,0,0}}, {{0}}, 0, "10 columns, direct reference" },
      { "many30i",  1,0, 1, {{30,1,0,0}}, {{0}}, 0, "10 columns via indirect, 30x placeholder" },
      { "many90",   0,0, 1, {{90,1,0,0}}, {{0}}, 0, "30 columns, direct reference" },
      { "many90i",  1,0, 1, {{90,1,0,0}}, {{0}}, 0, "30 columns via indirect, 90x placeholder" },
      /* indexed indirect. 6 indices {0..5} in the buffer so firstIndex and
       * vertexOffset each have somewhere to point. */
      { "idx_direct",  0,1, 1, {{0}}, {{3,1,0,0,0}}, 6, "reference indexed, triangle A" },
      { "idx_same",    1,1, 1, {{0}}, {{3,1,0,0,0}}, 6, "byte-identical to idx_direct" },
      { "idx_firstidx",1,1, 1, {{0}}, {{3,1,3,0,0}}, 6, "triangle B, firstIndex from buffer" },
      { "idx_voffset", 1,1, 1, {{0}}, {{3,1,0,3,0}}, 6, "triangle B, vertexOffset from buffer" },
      { "idx_zero",    1,1, 1, {{0}}, {{0,1,0,0,0}}, 6, "nothing drawn, no fault" },
    };
    struct tcase *tc=NULL;
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)
        if(!strcmp(cs,cases[i].name)) tc=&cases[i];
    if(!tc){printf("FAILED: unknown DI_CASE=%s\n",cs);return 1;}

    printf("=== case: %s ===\n", tc->name);
    printf("path        : %s\n", tc->indirect ? "vkCmdDrawIndirect" : "vkCmdDraw");
    printf("drawCount   : %u\n", tc->draw_count);
    for(uint32_t i=0;i<tc->draw_count;i++){
        if(tc->indexed)
            printf("icmd[%u]     : idxCount=%u inst=%u firstIdx=%u vtxOff=%d firstInst=%u\n", i,
                   tc->icmds[i].indexCount, tc->icmds[i].instanceCount,
                   tc->icmds[i].firstIndex, tc->icmds[i].vertexOffset,
                   tc->icmds[i].firstInstance);
        else
            printf("cmd[%u]      : vtx=%u inst=%u firstVtx=%u firstInst=%u\n", i,
                   tc->cmds[i].vertexCount, tc->cmds[i].instanceCount,
                   tc->cmds[i].firstVertex, tc->cmds[i].firstInstance);
    }
    printf("expectation : %s\n", tc->expect);
    fflush(stdout);

    const char*icd=getenv("PANVK_ICD_SO"); if(!icd||!icd[0]) icd=PANVK_DEFAULT_ICD_SO;
    void*lib=dlopen(icd,RTLD_NOW); if(!lib){printf("dlopen: %s\n",dlerror());return 1;}
    PFN_icdGetInstanceProcAddr gpa=
        (PFN_icdGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
    if(!gpa){printf("dlsym failed\n");return 1;}
    PFN_vkCreateInstance CI=(PFN_vkCreateInstance)gpa(NULL,"vkCreateInstance");
    VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
                          .apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                             .pApplicationInfo=&ai};
    VkInstance inst; CHECK(CI(&ii,NULL,&inst),"vkCreateInstance");
    #define IP(n) (PFN_vk##n)gpa(inst,"vk" #n)
    PFN_vkGetDeviceProcAddr GDPA=IP(GetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EPD=IP(EnumeratePhysicalDevices);
    PFN_vkCreateDevice CD=IP(CreateDevice);
    PFN_vkGetPhysicalDeviceMemoryProperties GMP=IP(GetPhysicalDeviceMemoryProperties);
    uint32_t n=0; CHECK(EPD(inst,&n,NULL),"enum");
    if(!n){printf("FAILED: no device\n");return 1;}
    VkPhysicalDevice pd; CHECK(EPD(inst,&n,&pd),"enum2");
    VkPhysicalDeviceMemoryProperties mp; GMP(pd,&mp);
    VkPhysicalDeviceVulkan13Features f13={
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering=VK_TRUE};
    float pr=1.0f;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&pr};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f13,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    VkDevice dev; CHECK(CD(pd,&di,NULL,&dev),"vkCreateDevice");
    #define DP(x) (PFN_vk##x)GDPA(dev,"vk" #x)
    PFN_vkGetDeviceQueue GQ=DP(GetDeviceQueue);
    PFN_vkCreateCommandPool CCP=DP(CreateCommandPool);
    PFN_vkAllocateCommandBuffers ACB=DP(AllocateCommandBuffers);
    PFN_vkBeginCommandBuffer BCB=DP(BeginCommandBuffer);
    PFN_vkEndCommandBuffer ECB=DP(EndCommandBuffer);
    PFN_vkQueueSubmit QS=DP(QueueSubmit);
    PFN_vkQueueWaitIdle QWI=DP(QueueWaitIdle);
    PFN_vkCreateImage CIM=DP(CreateImage);
    PFN_vkGetImageMemoryRequirements GIMR=DP(GetImageMemoryRequirements);
    PFN_vkGetImageSubresourceLayout GISL=DP(GetImageSubresourceLayout);
    PFN_vkAllocateMemory AM=DP(AllocateMemory);
    PFN_vkBindImageMemory BIM=DP(BindImageMemory);
    PFN_vkCreateImageView CIV=DP(CreateImageView);
    PFN_vkMapMemory MM=DP(MapMemory);
    PFN_vkUnmapMemory UM=DP(UnmapMemory);
    PFN_vkCreateShaderModule CSM=DP(CreateShaderModule);
    PFN_vkCreatePipelineLayout CPL=DP(CreatePipelineLayout);
    PFN_vkCreateGraphicsPipelines CGP=DP(CreateGraphicsPipelines);
    PFN_vkCmdBeginRendering CBR=DP(CmdBeginRendering);
    PFN_vkCmdEndRendering CER=DP(CmdEndRendering);
    PFN_vkCmdBindPipeline CBP=DP(CmdBindPipeline);
    PFN_vkCmdSetViewport CSV=DP(CmdSetViewport);
    PFN_vkCmdSetScissor CSS=DP(CmdSetScissor);
    PFN_vkCmdDraw CDraw=DP(CmdDraw);
    PFN_vkCmdDrawIndirect CDrawInd=DP(CmdDrawIndirect);
    PFN_vkCmdDrawIndexed CDrawIdx=DP(CmdDrawIndexed);
    PFN_vkCmdDrawIndexedIndirect CDrawIdxInd=DP(CmdDrawIndexedIndirect);
    PFN_vkCmdBindIndexBuffer CBindIB=DP(CmdBindIndexBuffer);
    PFN_vkCmdPipelineBarrier CPB=DP(CmdPipelineBarrier);
    PFN_vkCreateBuffer CB=DP(CreateBuffer);
    PFN_vkGetBufferMemoryRequirements GBMR=DP(GetBufferMemoryRequirements);
    PFN_vkBindBufferMemory BBM=DP(BindBufferMemory);
    if(tc->indirect && !CDrawInd){
        printf("FAILED: vkCmdDrawIndirect is NULL\n"); return 1; }

    VkQueue q; GQ(dev,0,0,&q);
    VkFormat FMT=VK_FORMAT_R8G8B8A8_UNORM;

    VkBuffer ibuf=VK_NULL_HANDLE; VkDeviceMemory imem=VK_NULL_HANDLE;
    const uint32_t STRIDE = tc->indexed ? sizeof(VkDrawIndexedIndirectCommand)
                                        : sizeof(VkDrawIndirectCommand);
    if(tc->indirect){
        VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size=STRIDE*tc->draw_count,
            .usage=VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        CHECK(CB(dev,&bi,NULL,&ibuf),"vkCreateBuffer(indirect)");
        VkMemoryRequirements mr; GBMR(dev,ibuf,&mr);
        uint32_t t=mtype(&mp,mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(t==UINT32_MAX){printf("FAILED: no host-visible mem\n");return 1;}
        VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize=mr.size,.memoryTypeIndex=t};
        CHECK(AM(dev,&ma,NULL,&imem),"vkAllocateMemory(indirect)");
        CHECK(BBM(dev,ibuf,imem,0),"vkBindBufferMemory");
        void*p=NULL; CHECK(MM(dev,imem,0,STRIDE*tc->draw_count,0,&p),"map indirect");
        for(uint32_t i=0;i<tc->draw_count;i++){
            if(tc->indexed) memcpy((uint8_t*)p+i*STRIDE,&tc->icmds[i],STRIDE);
            else            memcpy((uint8_t*)p+i*STRIDE,&tc->cmds[i],STRIDE);
        }
        UM(dev,imem);
        printf("indirect buffer written: %u command(s), stride %u\n",
               tc->draw_count, STRIDE);
    }

    /* index buffer: 6 indices {0,1,2,3,4,5} */
    VkBuffer xbuf=VK_NULL_HANDLE; VkDeviceMemory xmem=VK_NULL_HANDLE;
    if(tc->indexed){
        size_t xsz=tc->nidx*sizeof(uint16_t);
        VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size=xsz,.usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        CHECK(CB(dev,&bi,NULL,&xbuf),"vkCreateBuffer(index)");
        VkMemoryRequirements xr; GBMR(dev,xbuf,&xr);
        uint32_t xt=mtype(&mp,xr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(xt==UINT32_MAX){printf("FAILED: no host-visible mem for index\n");return 1;}
        VkMemoryAllocateInfo xa={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize=xr.size,.memoryTypeIndex=xt};
        CHECK(AM(dev,&xa,NULL,&xmem),"vkAllocateMemory(index)");
        CHECK(BBM(dev,xbuf,xmem,0),"vkBindBufferMemory(index)");
        void*xp=NULL; CHECK(MM(dev,xmem,0,xsz,0,&xp),"map index");
        uint16_t*u=xp; for(uint32_t i=0;i<tc->nidx;i++) u[i]=(uint16_t)i;
        UM(dev,xmem);
        printf("index buffer written: %u x uint16 = {0..%u}\n",tc->nidx,tc->nidx-1);
    }

    VkImageCreateInfo ic={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=FMT,.extent={DIMS,DIMS,1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_LINEAR,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage img; CHECK(CIM(dev,&ic,NULL,&img),"vkCreateImage");
    VkMemoryRequirements mr; GIMR(dev,img,&mr);
    uint32_t t=mtype(&mp,mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=mr.size,.memoryTypeIndex=t};
    VkDeviceMemory imgm; CHECK(AM(dev,&ma,NULL,&imgm),"vkAllocateMemory(image)");
    CHECK(BIM(dev,img,imgm,0),"vkBindImageMemory");
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=img,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=FMT,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; CHECK(CIV(dev,&vi,NULL,&view),"vkCreateImageView");

    size_t vz=0,fz=0;
    char*vc=rf(getenv("DI_VS")?getenv("DI_VS"):"triangle6.vert.spv",&vz); if(!vc)return 1;
    char*fc=rf("triangle.frag.spv",&fz); if(!fc)return 1;
    VkShaderModuleCreateInfo vm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=vz,.pCode=(uint32_t*)vc};
    VkShaderModule vs; CHECK(CSM(dev,&vm,NULL,&vs),"vert");
    VkShaderModuleCreateInfo fm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=fz,.pCode=(uint32_t*)fc};
    VkShaderModule fs; CHECK(CSM(dev,&fm,NULL,&fs),"frag");
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout pl; CHECK(CPL(dev,&pli,NULL,&pl),"layout");
    VkPipelineShaderStageCreateInfo st[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vin={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vpi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rsi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,
        .frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1.0f};
    VkPipelineMultisampleStateCreateInfo msi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState cbaa={
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                        VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cbi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&cbaa};
    VkDynamicState dy[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount=2,.pDynamicStates=dy};
    VkPipelineRenderingCreateInfo pri={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&FMT};
    VkGraphicsPipelineCreateInfo gpi={
        .sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.pNext=&pri,
        .stageCount=2,.pStages=st,.pVertexInputState=&vin,
        .pInputAssemblyState=&ia,.pViewportState=&vpi,
        .pRasterizationState=&rsi,.pMultisampleState=&msi,
        .pColorBlendState=&cbi,.pDynamicState=&dyi,.layout=pl};
    VkPipeline pipe; CHECK(CGP(dev,VK_NULL_HANDLE,1,&gpi,NULL,&pipe),"pipeline");

    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex=0};
    VkCommandPool cp; CHECK(CCP(dev,&cpi,NULL,&cp),"pool");
    VkCommandBufferAllocateInfo cbi2={
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd; CHECK(ACB(dev,&cbi2,&cmd),"cb");
    VkCommandBufferBeginInfo cbb={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(BCB(cmd,&cbb),"begin");
    VkImageMemoryBarrier br={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=img,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
        .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    CPB(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&br);
    VkRenderingAttachmentInfo at={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView=view,.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue={.color={.float32={0,0,0,1}}}};
    VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea={{0,0},{DIMS,DIMS}},.layerCount=1,
        .colorAttachmentCount=1,.pColorAttachments=&at};
    CBR(cmd,&ri);
    VkViewport vp={0,0,(float)DIMS,(float)DIMS,0.0f,1.0f};
    VkRect2D sc={{0,0},{DIMS,DIMS}};
    CSV(cmd,0,1,&vp); CSS(cmd,0,1,&sc);
    CBP(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
    if(tc->indexed){
        if(!CBindIB){printf("FAILED: vkCmdBindIndexBuffer NULL\n");return 1;}
        CBindIB(cmd,xbuf,0,VK_INDEX_TYPE_UINT16);
        printf("index buffer bound (UINT16)\n");
    }
    if(tc->indirect && tc->indexed){
        if(!CDrawIdxInd){printf("FAILED: vkCmdDrawIndexedIndirect NULL\n");return 1;}
        printf("calling vkCmdDrawIndexedIndirect(drawCount=%u, stride=%u)\n",
               tc->draw_count, STRIDE);
        CDrawIdxInd(cmd,ibuf,0,tc->draw_count,STRIDE);
    } else if(tc->indirect){
        printf("calling vkCmdDrawIndirect(drawCount=%u, stride=%u)\n",
               tc->draw_count, STRIDE);
        CDrawInd(cmd,ibuf,0,tc->draw_count,STRIDE);
    } else if(tc->indexed){
        printf("calling vkCmdDrawIndexed(%u,%u,%u,%d,%u)\n",
               tc->icmds[0].indexCount,tc->icmds[0].instanceCount,
               tc->icmds[0].firstIndex,tc->icmds[0].vertexOffset,
               tc->icmds[0].firstInstance);
        CDrawIdx(cmd,tc->icmds[0].indexCount,tc->icmds[0].instanceCount,
                 tc->icmds[0].firstIndex,tc->icmds[0].vertexOffset,
                 tc->icmds[0].firstInstance);
    } else {
        printf("calling vkCmdDraw(%u,%u,%u,%u)\n",
               tc->cmds[0].vertexCount,tc->cmds[0].instanceCount,
               tc->cmds[0].firstVertex,tc->cmds[0].firstInstance);
        CDraw(cmd,tc->cmds[0].vertexCount,tc->cmds[0].instanceCount,
              tc->cmds[0].firstVertex,tc->cmds[0].firstInstance);
    }
    fflush(stdout);
    CER(cmd);
    CHECK(ECB(cmd),"end");
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1,.pCommandBuffers=&cmd};
    CHECK(QS(q,1,&si,VK_NULL_HANDLE),"vkQueueSubmit");
    CHECK(QWI(q),"vkQueueWaitIdle");
    printf("vkQueueWaitIdle returned (no hang)\n");

    VkImageSubresource sr={VK_IMAGE_ASPECT_COLOR_BIT,0,0};
    VkSubresourceLayout sl; GISL(dev,img,&sr,&sl);
    void*m=NULL; CHECK(MM(dev,imgm,0,VK_WHOLE_SIZE,0,&m),"map image");
    uint8_t*base=(uint8_t*)m+sl.offset;
    uint32_t nb=0, quad[4]={0,0,0,0};
    for(int y=0;y<DIMS;y++){uint8_t*row=base+y*sl.rowPitch;
        for(int x=0;x<DIMS;x++){uint8_t*p=row+x*4;
            if(p[0]||p[1]||p[2]){nb++;
                quad[(y<DIMS/2?0:2)+(x<DIMS/2?0:1)]++;}}}
    uint8_t*ce=base+32*sl.rowPitch+32*4;
    printf("\n--- result: %s ---\n",tc->name);
    printf("non-black       : %u / %u\n",nb,DIMS*DIMS);
    printf("centre (32,32)  = %u,%u,%u,%u\n",ce[0],ce[1],ce[2],ce[3]);
    printf("quadrants       = %u,%u,%u,%u\n",quad[0],quad[1],quad[2],quad[3]);
    printf("FINGERPRINT %s n=%u c=%u,%u,%u q=%u,%u,%u,%u\n",
           tc->name,nb,ce[0],ce[1],ce[2],quad[0],quad[1],quad[2],quad[3]);

    const char*pp=getenv("DI_PPM");
    if(pp&&pp[0]){
        FILE*f=fopen(pp,"wb");
        if(f){fprintf(f,"P6\n%d %d\n255\n",DIMS,DIMS);
            for(int y=0;y<DIMS;y++){uint8_t*row=base+y*sl.rowPitch;
                for(int x=0;x<DIMS;x++){uint8_t*p=row+x*4;
                    uint8_t rgb[3]={p[0],p[1],p[2]};fwrite(rgb,1,3,f);}}
            fclose(f);printf("VISUAL_DUMP: %s\n",pp);}
    }
    UM(dev,imgm);
    free(vc); free(fc);
    printf("DONE\n");
    return 0;
}
