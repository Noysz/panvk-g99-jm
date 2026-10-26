/* depth_test.c -- T4.7.1, depth testing on Valhall v9 / JM.
 *
 * Two overlapping quads, A (red, z 0.25) and B (green, z 0.75), from
 * depth2.vert. Edges fall on integer pixel coordinates, so coverage is exact.
 *
 * No expected image or depth value is hand-written. The harness runs a CPU
 * model of the Vulkan depth test over the same quads in the same order: for
 * each fragment, if the test is disabled or compare(z, stored) passes, the
 * colour is written, and the depth is written if writes are enabled. Both the
 * colour attachment is LINEAR and host visible. The depth attachment can use
 * LINEAR with direct readback or OPTIMAL with buffer readback. Every pixel is
 * compared with the model, colour exactly and depth to 1e-6.
 *
 * The cases are chosen so that each control produces a different image from the
 * case it controls:
 *   off_ab vs less_ab        test disabled vs enabled, same order
 *   less_ab vs less_ba       LESS must make the result order-independent
 *   greater_ba vs off_ba     GREATER keeps the farther quad, off keeps the last
 *   less_clear05             clear depth 0.5: only A can pass
 *   less_clear01             clear depth 0.1: nothing passes
 *   less_nowrite             writes off: B passes in the overlap although A was
 *                            drawn first and is nearer
 *   equal_clear025           EQUAL against clear 0.25: only A passes
 * DEPTH_LIE=1 makes the CPU model use the opposite compare op, so the
 * comparison MUST fail; that checks the harness, not the driver.
 *
 * Select with DEPTH_CASE. DEPTH_FMT=d16|d32 (default d32).
 * DEPTH_TILING=linear|optimal (default linear).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include "pngdump.h"
#include <math.h>
#include "vulkan/vulkan_core.h"
#include "icd_open.h"

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

struct dcase {
    const char *name;
    int test, write;
    VkCompareOp op;
    float clear;
    int order_ba;       /* 1: draw B then A */
};

int main(void){
    const char*cs=getenv("BOUNDS_CASE"); if(!cs||!cs[0]) cs="stored";
    const int nofs = getenv("BOUNDS_NOFS") != NULL;
    const int noatt = getenv("BOUNDS_NOATT") != NULL;
    struct dcase cases[] = {
      { "stored",        1,1, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
      { "reject",        1,1, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
      { "full",          1,1, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
      { "disabled",      1,1, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
      { "upper",         1,1, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
      { "lower",         1,1, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
      { "no_depth_test", 0,1, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
      { "no_write",      1,0, VK_COMPARE_OP_ALWAYS, 1.0f,0 },
    };
    struct dcase*tc=NULL;
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)
        if(!strcmp(cs,cases[i].name)) tc=&cases[i];
    if(!tc){printf("FAILED: unknown DEPTH_CASE=%s\n",cs);return 1;}
    const int lie = getenv("DEPTH_LIE") && getenv("DEPTH_LIE")[0]=='1';
    const char*fmtn=getenv("DEPTH_FMT"); if(!fmtn||!fmtn[0]) fmtn="d32";
    const VkFormat DFMT = !strcmp(fmtn,"d16") ? VK_FORMAT_D16_UNORM : VK_FORMAT_D32_SFLOAT;
    const char *tiling = getenv("DEPTH_TILING");
    if (!tiling) tiling = "linear";
    if (strcmp(tiling,"linear") && strcmp(tiling,"optimal")) return 1;
    const int depth_optimal = !strcmp(tiling,"optimal");
    const float stored_a = DFMT == VK_FORMAT_D16_UNORM ? 16384.0f/65535.0f : 0.25f;
    float bound_min = stored_a, bound_max = stored_a;
    if (!strcmp(cs,"reject")) bound_min = bound_max = 0.5f;
    if (!strcmp(cs,"full")) { bound_min=0.0f; bound_max=1.0f; }
    if (!strcmp(cs,"upper")) bound_min=0.0f;
    if (!strcmp(cs,"lower")) bound_max=1.0f;
    const int bounds_enable = strcmp(cs,"disabled") != 0;
    printf("BOUNDS min=%.9g max=%.9g enabled=%d nofs=%d noatt=%d\n",
           bound_min,bound_max,bounds_enable,nofs,noatt);
    const VkDeviceSize depth_size = DIMS * DIMS * (DFMT == VK_FORMAT_D16_UNORM ? 2 : 4);
    printf("=== case: %s ===\n",tc->name);
    printf("depth       : test=%d write=%d op=%d clear=%.3f order=%s format=%s\n",
           tc->test,tc->write,tc->op,tc->clear,tc->order_ba?"B,A":"A,B",fmtn);
    if(lie) printf("NEGATIVE CONTROL: model tests incoming depth instead of stored depth\n");
    fflush(stdout);

    const char*icd=getenv("PANVK_ICD_SO"); if(!icd||!icd[0]) icd=PANVK_DEFAULT_ICD_SO;
    void*lib=dlopen(icd,RTLD_NOW); if(!lib){printf("dlopen: %s\n",dlerror());return 1;}
    const char*icd_how="?";
    PFN_icdGetInstanceProcAddr gpa=
        (PFN_icdGetInstanceProcAddr)icd_open_gipa(lib,&icd_how);
    if(!gpa){printf("dlsym failed\n");return 1;}
    PFN_vkCreateInstance CI=(PFN_vkCreateInstance)gpa(NULL,"vkCreateInstance");
    VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
                          .apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                             .pApplicationInfo=&ai};
    VkInstance inst; CHECK(CI(&ii,NULL,&inst),"vkCreateInstance");
    #define IP(n) ((PFN_vk##n)gpa(inst,"vk" #n))
    PFN_vkGetDeviceProcAddr GDPA=IP(GetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EPD=IP(EnumeratePhysicalDevices);
    PFN_vkCreateDevice CD=IP(CreateDevice);
    PFN_vkGetPhysicalDeviceMemoryProperties GMP=IP(GetPhysicalDeviceMemoryProperties);
    PFN_vkGetPhysicalDeviceImageFormatProperties GIFP=IP(GetPhysicalDeviceImageFormatProperties);
    uint32_t n=0; CHECK(EPD(inst,&n,NULL),"enum");
    if(!n){printf("FAILED: no device\n");return 1;}
    VkPhysicalDevice pd; n=1; CHECK(EPD(inst,&n,&pd),"enum2");
    VkPhysicalDeviceMemoryProperties mp; GMP(pd,&mp);
    VkPhysicalDeviceVulkan13Features f13={
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering=VK_TRUE};
    float pr=1.0f;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&pr};
    VkPhysicalDeviceFeatures available;
    IP(GetPhysicalDeviceFeatures)(pd,&available);
    const int feature_off = getenv("BOUNDS_FEATURE_OFF") != NULL;
    if (!available.depthBounds && !feature_off) {
        printf("DEPTHFP %s verdict=SKIP (depthBounds unsupported)\n",cs);
        return 3;
    }
    if (feature_off && bounds_enable) return 1;
    VkPhysicalDeviceFeatures features={.depthBounds=!feature_off};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f13,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&features};
    VkDevice dev; CHECK(CD(pd,&di,NULL,&dev),"vkCreateDevice");
    #define DP(x) ((PFN_vk##x)GDPA(dev,"vk" #x))
    PFN_vkGetDeviceQueue GQ=DP(GetDeviceQueue);
    PFN_vkCmdSetDepthBounds SetBounds=DP(CmdSetDepthBounds);
    PFN_vkCmdSetDepthBoundsTestEnable SetBoundsEnable=DP(CmdSetDepthBoundsTestEnable);
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
    PFN_vkCmdCopyImageToBuffer CCIB=DP(CmdCopyImageToBuffer);

    VkQueue q; GQ(dev,0,0,&q);
    VkFormat FMT=VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo dic={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=DFMT,.extent={DIMS,DIMS,1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=depth_optimal ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR,
        .usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
               (depth_optimal ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageFormatProperties limits;
    VkResult supported=GIFP(pd,DFMT,dic.imageType,dic.tiling,dic.usage,0,&limits);
    if (supported==VK_ERROR_FORMAT_NOT_SUPPORTED) {
        printf("DEPTHFP %s verdict=SKIP (unsupported %s depth image)\n",cs,tiling);
        return 3;
    }
    CHECK(supported,"depth image support");
    if (limits.maxExtent.width<DIMS || limits.maxExtent.height<DIMS ||
        !(limits.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) return 1;
    printf("depth tiling: %s\n",tiling);
    VkImage dimg; CHECK(CIM(dev,&dic,NULL,&dimg),"vkCreateImage(depth)");
    VkMemoryRequirements dmr; GIMR(dev,dimg,&dmr);
    uint32_t dt=mtype(&mp,dmr.memoryTypeBits,
        depth_optimal ? 0 : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(dt==UINT32_MAX){printf("FAILED: no compatible memory for depth\n");return 1;}
    VkMemoryAllocateInfo dma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=dmr.size,.memoryTypeIndex=dt};
    VkDeviceMemory dmem; CHECK(AM(dev,&dma,NULL,&dmem),"vkAllocateMemory(depth)");
    CHECK(BIM(dev,dimg,dmem,0),"vkBindImageMemory(depth)");
    VkImageViewCreateInfo dvi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=dimg,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=DFMT,
        .subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
    VkImageView dview; CHECK(CIV(dev,&dvi,NULL,&dview),"vkCreateImageView(depth)");
    VkBuffer depth_buffer=VK_NULL_HANDLE;
    VkDeviceMemory depth_buffer_mem=VK_NULL_HANDLE;
    if (depth_optimal) {
        VkBufferCreateInfo dbi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size=depth_size,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        CHECK(CB(dev,&dbi,NULL,&depth_buffer),"depth readback buffer");
        VkMemoryRequirements bmr; GBMR(dev,depth_buffer,&bmr);
        uint32_t bt=mtype(&mp,bmr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (bt==UINT32_MAX) return 1;
        VkMemoryAllocateInfo bma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize=bmr.size,.memoryTypeIndex=bt};
        CHECK(AM(dev,&bma,NULL,&depth_buffer_mem),"depth readback memory");
        CHECK(BBM(dev,depth_buffer,depth_buffer_mem,0),"depth readback bind");
        void *poison;
        CHECK(MM(dev,depth_buffer_mem,0,VK_WHOLE_SIZE,0,&poison),"depth poison map");
        memset(poison,0xff,depth_size);
        UM(dev,depth_buffer_mem);
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
    char*vc=rf("depth2.vert.spv",&vz); if(!vc)return 1;
    char*fc=rf(getenv("BOUNDS_SHADER") ? getenv("BOUNDS_SHADER") : "late.frag.spv",&fz); if(!fc)return 1;
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
    VkPipelineDepthStencilStateCreateInfo dsi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,
        .depthCompareOp=VK_COMPARE_OP_ALWAYS};
    VkPipelineColorBlendAttachmentState cbaa={
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                        VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cbi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&cbaa};
    VkDynamicState dy[4]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS,VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE};
    VkPipelineDynamicStateCreateInfo dyi={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount=feature_off ? 2 : 4,.pDynamicStates=dy};
    VkPipelineRenderingCreateInfo pri={
        .sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&FMT,
        .depthAttachmentFormat=noatt ? VK_FORMAT_UNDEFINED : DFMT};
    VkGraphicsPipelineCreateInfo gpi={
        .sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.pNext=&pri,
        .stageCount=2,.pStages=st,.pVertexInputState=&vin,
        .pInputAssemblyState=&ia,.pViewportState=&vpi,
        .pRasterizationState=&rsi,.pMultisampleState=&msi,.pDepthStencilState=&dsi,
        .pColorBlendState=&cbi,.pDynamicState=&dyi,.layout=pl};
    VkPipeline first; CHECK(CGP(dev,VK_NULL_HANDLE,1,&gpi,NULL,&first),"first pipeline");
    dsi.depthTestEnable=tc->test; dsi.depthWriteEnable=tc->write;
    if (nofs) { gpi.stageCount=1; cbaa.colorWriteMask=0; }
    VkPipeline pipe; CHECK(CGP(dev,VK_NULL_HANDLE,1,&gpi,NULL,&pipe),"bounded pipeline");

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
    VkImageMemoryBarrier dbr={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=dimg,
        .subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1},
        .dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    VkImageMemoryBarrier brs[2]={br,dbr};
    CPB(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        0,0,NULL,0,NULL,2,brs);
    VkRenderingAttachmentInfo at={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView=view,.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue={.color={.float32={0,0,0,1}}}};
    VkRenderingAttachmentInfo dat={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView=dview,.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue={.depthStencil={tc->clear,0}}};
    VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea={{0,0},{DIMS,DIMS}},.layerCount=1,
        .colorAttachmentCount=1,.pColorAttachments=&at,.pDepthAttachment=noatt ? NULL : &dat};
    CBR(cmd,&ri);
    VkViewport vp={0,0,(float)DIMS,(float)DIMS,0.0f,1.0f};
    VkRect2D sc={{0,0},{DIMS,DIMS}};
    CSV(cmd,0,1,&vp); CSS(cmd,0,1,&sc);
    if (!feature_off) SetBounds(cmd,bound_min,bound_max);
    if (!feature_off) SetBoundsEnable(cmd,VK_FALSE);
    CBP(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,first);
    CDraw(cmd,6,1,0,0);
    if (!feature_off) SetBoundsEnable(cmd,bounds_enable);
    CBP(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
    CDraw(cmd,6,1,6,0);
    fflush(stdout);
    CER(cmd);
    VkImageMemoryBarrier host_read={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=img,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
        .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    VkImageMemoryBarrier host_depth=host_read;
    host_depth.image=dimg;
    host_depth.oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    host_depth.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    host_depth.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (depth_optimal) {
        host_depth.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        host_depth.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    }
    VkImageMemoryBarrier host_reads[2]={host_read,host_depth};
    CPB(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_HOST_BIT|(depth_optimal ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0),
        0,0,NULL,0,NULL,2,host_reads);
    if (depth_optimal) {
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},
            .imageExtent={DIMS,DIMS,1}};
        CCIB(cmd,dimg,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,depth_buffer,1,&copy);
        VkMemoryBarrier depth_host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        CPB(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
            0,1,&depth_host,0,NULL,0,NULL);
    }

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
    /* ---- CPU model of the depth test ---- */
    static uint8_t rc[DIMS][DIMS][3]; static float rd[DIMS][DIMS];

    for(int y=0;y<DIMS;y++) for(int x=0;x<DIMS;x++){ rc[y][x][0]=rc[y][x][1]=rc[y][x][2]=0; rd[y][x]=tc->clear; }
    const int lo[2]={16,24}; const float qz[2]={0.25f,0.75f};
    for(int k=0;k<2;k++){
        int q = tc->order_ba ? 1-k : k;
        for(int y=lo[q];y<lo[q]+24;y++) for(int x=lo[q];x<lo[q]+24;x++){
            const float tested_depth = lie ? qz[q] : rd[y][x];
            if(q==1 && bounds_enable && !noatt &&
               !(tested_depth>=bound_min && tested_depth<=bound_max)) continue;
            if(q==0 || !nofs) {
                rc[y][x][0] = q==0 ? 255 : 0; rc[y][x][1] = q==1 ? 255 : 0; rc[y][x][2]=0;
            }
            if(!noatt && (q==0 || (tc->test && tc->write)))
                rd[y][x]= DFMT==VK_FORMAT_D16_UNORM ? roundf(qz[q]*65535.0f)/65535.0f : qz[q];
        }
    }
    /* Depth writes only happen when the test is enabled (spec: depth writes
     * are disabled when depthTestEnable is VK_FALSE). */

    /* ---- compare colour ---- */
    uint32_t red=0, green=0, cbad=0; int cfx=-1,cfy=-1;
    for(int y=0;y<DIMS;y++){uint8_t*row=base+y*sl.rowPitch;
        for(int x=0;x<DIMS;x++){uint8_t*p=row+x*4;
            if(p[0]==255&&p[1]==0) red++; if(p[1]==255&&p[0]==0) green++;
            if(p[0]!=rc[y][x][0]||p[1]!=rc[y][x][1]||p[2]!=rc[y][x][2]){cbad++; if(cfx<0){cfx=x;cfy=y;}}
        }}
    /* ---- compare depth ---- */
    VkImageSubresource dsr={VK_IMAGE_ASPECT_DEPTH_BIT,0,0};
    VkSubresourceLayout dsl={.rowPitch=depth_size/DIMS};
    if (!depth_optimal) GISL(dev,dimg,&dsr,&dsl);
    VkDeviceMemory readback_mem=depth_optimal ? depth_buffer_mem : dmem;
    void*dm=NULL; CHECK(MM(dev,readback_mem,0,VK_WHOLE_SIZE,0,&dm),"map depth");
    uint8_t*db=(uint8_t*)dm+dsl.offset;
    const int d16 = DFMT==VK_FORMAT_D16_UNORM;
    const double dtol = d16 ? 1.0/65535.0 : 1e-6;
    uint32_t dbad=0; int dfx=-1,dfy=-1; double dmaxe=0, got_at_fx=0;
    for(int y=0;y<DIMS;y++) for(int x=0;x<DIMS;x++){
        double g = d16 ? ((uint16_t*)(db+y*dsl.rowPitch))[x]/65535.0
                       : ((float*)(db+y*dsl.rowPitch))[x];
        double e = fabs(g - rd[y][x]); if(e>dmaxe) dmaxe=e;
        if(!noatt && (!isfinite(g)||e>dtol)){dbad++; if(dfx<0){dfx=x;dfy=y;got_at_fx=g;}}
    }
    UM(dev,readback_mem);
    uint32_t mred=0,mgreen=0;
    for(int y=0;y<DIMS;y++) for(int x=0;x<DIMS;x++){ if(rc[y][x][0]) mred++; if(rc[y][x][1]) mgreen++; }
    int ok = cbad==0 && dbad==0;
    printf("\n--- result: %s ---\n",tc->name);
    printf("colour      : red=%u green=%u   model red=%u green=%u   mismatching pixels=%u\n",
           red,green,mred,mgreen,cbad);
    if(cfx>=0){uint8_t*p=base+cfy*sl.rowPitch+cfx*4;
        printf("  first     : (%d,%d) got %u,%u,%u model %u,%u,%u\n",cfx,cfy,p[0],p[1],p[2],
               rc[cfy][cfx][0],rc[cfy][cfx][1],rc[cfy][cfx][2]);}
    printf("depth       : rowPitch=%llu mismatching=%u max error=%.3g (tol %.3g)\n",
           (unsigned long long)dsl.rowPitch,dbad,dmaxe,dtol);
    if(dfx>=0) printf("  first     : (%d,%d) got %.6f model %.6f\n",dfx,dfy,got_at_fx,rd[dfy][dfx]);
    printf("DEPTHFP %s fmt=%s red=%u green=%u cbad=%u dbad=%u verdict=%s\n",
           tc->name,fmtn,red,green,cbad,dbad,ok?"PASS":"FAIL");

    const char*pp=getenv("DEPTH_PPM");
    if(pp&&pp[0]){
        FILE*f=png_open(pp,"wb");
        if(f){fprintf(f,"P6\n%d %d\n255\n",DIMS,DIMS);
            for(int y=0;y<DIMS;y++){uint8_t*row=base+y*sl.rowPitch;
                for(int x=0;x<DIMS;x++){uint8_t*p=row+x*4;
                    uint8_t rgb[3]={p[0],p[1],p[2]};fwrite(rgb,1,3,f);}}
            png_close(f);printf("VISUAL_DUMP: %s\n",pp);}
    }
    UM(dev,imgm);
    (DP(DestroyPipeline))(dev,pipe,NULL);
    (DP(DestroyPipeline))(dev,first,NULL);
    (DP(DestroyPipelineLayout))(dev,pl,NULL);
    (DP(DestroyShaderModule))(dev,vs,NULL);
    (DP(DestroyShaderModule))(dev,fs,NULL);
    (DP(DestroyCommandPool))(dev,cp,NULL);
    (DP(DestroyImageView))(dev,view,NULL);
    (DP(DestroyImageView))(dev,dview,NULL);
    (DP(DestroyImage))(dev,img,NULL);
    (DP(DestroyImage))(dev,dimg,NULL);
    (DP(FreeMemory))(dev,imgm,NULL);
    (DP(FreeMemory))(dev,dmem,NULL);
    if (depth_buffer) (DP(DestroyBuffer))(dev,depth_buffer,NULL);
    if (depth_buffer_mem) (DP(FreeMemory))(dev,depth_buffer_mem,NULL);
    (DP(DestroyDevice))(dev,NULL);
    (IP(DestroyInstance))(inst,NULL);
    dlclose(lib);
    free(vc); free(fc);
    printf("DONE\n");
    return ok?0:2;
}
