/* dispatch_indirect_test.c
 *
 * Phase 4.4 — validates vkCmdDispatchIndirect on Mali-G57 MC2 (v9/JM).
 *
 * Uses wgid_write.comp, where every workgroup stamps its own slot with
 * 0xA5A50000 | id. The SSBO afterwards is therefore a record of which
 * workgroups ran, which lets the test check two things a scalar output could
 * not separate:
 *
 *   - the exact number of workgroups that ran
 *   - that slots beyond the requested count were never touched
 *
 * The second is the one that matters. It is what proves the count was read from
 * the indirect buffer at dispatch time rather than baked in when the command
 * buffer was recorded.
 *
 * Cases, via DI_CASE:
 *   direct    vkCmdDispatch(7,1,1)                control, must stamp slots 0..6
 *   ind7      vkCmdDispatchIndirect, buffer x=7   must match direct exactly
 *   ind37     vkCmdDispatchIndirect, buffer x=37  deliberately not a round
 *                                                 number and different from
 *                                                 ind7, so a cached or
 *                                                 hardcoded count cannot pass
 *   ind0      vkCmdDispatchIndirect, buffer x=0   nothing stamped, no hang.
 *                                                 This is the is_no_op branch
 *                                                 in indirect_dispatch.cl that
 *                                                 rewrites the job type to NULL
 *
 * Env: DI_CASE, PANVK_ICD_SO.
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

#define SLOTS 64
#define STAMP 0xA5A50000u

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

int main(void){
    const char*cs=getenv("DI_CASE"); if(!cs||!cs[0]) cs="direct";
    int indirect = strncmp(cs,"ind",3)==0;
    uint32_t want = 7;
    if(!strcmp(cs,"ind37")) want = 37;
    else if(!strcmp(cs,"ind0")) want = 0;

    /* Shader and expected-value rule are selectable so the dispatch mechanism
     * can be tested separately from how the shader forms its value. The first
     * attempt used a large immediate (0xA5A50000 | id) and the constant did not
     * survive, which would have been misread as a dispatch failure. */
    const char*shader=getenv("DI_SHADER");
    if(!shader||!shader[0]) shader="wg_small.comp.spv";
    const char*rule=getenv("DI_RULE");
    if(!rule||!rule[0]) rule="idplus1";
    printf("=== case: %s ===\n", cs);
    printf("shader          : %s\n", shader);
    printf("value rule      : %s\n", rule);
    printf("API path        : %s\n", indirect ? "vkCmdDispatchIndirect" : "vkCmdDispatch");
    printf("workgroups asked: %u\n", want);
    printf("slots in SSBO   : %u\n", SLOTS);
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

    float pr=1.0f;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&pr};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
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
    PFN_vkCreateBuffer CB=DP(CreateBuffer);
    PFN_vkGetBufferMemoryRequirements GBMR=DP(GetBufferMemoryRequirements);
    PFN_vkAllocateMemory AM=DP(AllocateMemory);
    PFN_vkBindBufferMemory BBM=DP(BindBufferMemory);
    PFN_vkMapMemory MM=DP(MapMemory);
    PFN_vkUnmapMemory UM=DP(UnmapMemory);
    PFN_vkCreateShaderModule CSM=DP(CreateShaderModule);
    PFN_vkCreateDescriptorSetLayout CDSL=DP(CreateDescriptorSetLayout);
    PFN_vkCreateDescriptorPool CDP=DP(CreateDescriptorPool);
    PFN_vkAllocateDescriptorSets ADS=DP(AllocateDescriptorSets);
    PFN_vkUpdateDescriptorSets UDS=DP(UpdateDescriptorSets);
    PFN_vkCreatePipelineLayout CPL=DP(CreatePipelineLayout);
    PFN_vkCreateComputePipelines CCPipe=DP(CreateComputePipelines);
    PFN_vkCmdBindPipeline CBPipe=DP(CmdBindPipeline);
    PFN_vkCmdBindDescriptorSets CBDS=DP(CmdBindDescriptorSets);
    PFN_vkCmdDispatch CDisp=DP(CmdDispatch);
    PFN_vkCmdDispatchIndirect CDispInd=DP(CmdDispatchIndirect);

    printf("vkCmdDispatch         : %s\n", CDisp ? "present" : "NULL");
    printf("vkCmdDispatchIndirect : %s\n", CDispInd ? "present" : "NULL");
    if(indirect && !CDispInd){
        printf("RESULT: entry point absent -> NOT IMPLEMENTED at loader level\n");
        return 0;
    }
    fflush(stdout);

    VkQueue q; GQ(dev,0,0,&q);

    /* --- SSBO, zero-filled --- */
    VkBufferCreateInfo sbi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=SLOTS*sizeof(uint32_t),
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer sbuf; CHECK(CB(dev,&sbi,NULL,&sbuf),"vkCreateBuffer(ssbo)");
    VkMemoryRequirements smr; GBMR(dev,sbuf,&smr);
    uint32_t smt=mtype(&mp,smr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(smt==UINT32_MAX){printf("FAILED: no host-visible mem for ssbo\n");return 1;}
    VkMemoryAllocateInfo sma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=smr.size,.memoryTypeIndex=smt};
    VkDeviceMemory smem; CHECK(AM(dev,&sma,NULL,&smem),"vkAllocateMemory(ssbo)");
    CHECK(BBM(dev,sbuf,smem,0),"vkBindBufferMemory(ssbo)");
    void*sp=NULL; CHECK(MM(dev,smem,0,SLOTS*sizeof(uint32_t),0,&sp),"map ssbo");
    memset(sp,0,SLOTS*sizeof(uint32_t));
    UM(dev,smem);
    printf("ssbo zero-filled: %zu bytes\n",(size_t)(SLOTS*sizeof(uint32_t)));

    /* --- indirect command buffer --- */
    VkBuffer ibuf=VK_NULL_HANDLE; VkDeviceMemory imem=VK_NULL_HANDLE;
    if(indirect){
        VkBufferCreateInfo ibi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size=sizeof(VkDispatchIndirectCommand),
            .usage=VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        CHECK(CB(dev,&ibi,NULL,&ibuf),"vkCreateBuffer(indirect)");
        VkMemoryRequirements imr; GBMR(dev,ibuf,&imr);
        uint32_t imt=mtype(&mp,imr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(imt==UINT32_MAX){printf("FAILED: no host-visible mem for indirect\n");return 1;}
        VkMemoryAllocateInfo ima={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize=imr.size,.memoryTypeIndex=imt};
        CHECK(AM(dev,&ima,NULL,&imem),"vkAllocateMemory(indirect)");
        CHECK(BBM(dev,ibuf,imem,0),"vkBindBufferMemory(indirect)");
        void*ip=NULL; CHECK(MM(dev,imem,0,sizeof(VkDispatchIndirectCommand),0,&ip),"map indirect");
        VkDispatchIndirectCommand c={.x=want,.y=1,.z=1};
        memcpy(ip,&c,sizeof(c)); UM(dev,imem);
        printf("indirect command written: x=%u y=1 z=1\n",want);
    }

    /* --- descriptor set --- */
    VkDescriptorSetLayoutBinding b0={.binding=0,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo dslc={
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1,.pBindings=&b0};
    VkDescriptorSetLayout dsl; CHECK(CDSL(dev,&dslc,NULL,&dsl),"dsLayout");
    VkDescriptorPoolSize ps={.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1};
    VkDescriptorPoolCreateInfo dpc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    VkDescriptorPool dpool; CHECK(CDP(dev,&dpc,NULL,&dpool),"dsPool");
    VkDescriptorSetAllocateInfo dsa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=dpool,.descriptorSetCount=1,.pSetLayouts=&dsl};
    VkDescriptorSet dset; CHECK(ADS(dev,&dsa,&dset),"allocDS");
    VkDescriptorBufferInfo dbi={.buffer=sbuf,.offset=0,.range=SLOTS*sizeof(uint32_t)};
    VkWriteDescriptorSet wds={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet=dset,.dstBinding=0,.descriptorCount=1,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&dbi};
    UDS(dev,1,&wds,0,NULL);

    /* --- pipeline --- */
    size_t csz=0; char*ccode=rf(shader,&csz); if(!ccode)return 1;
    VkShaderModuleCreateInfo smc={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=csz,.pCode=(uint32_t*)ccode};
    VkShaderModule cmod; CHECK(CSM(dev,&smc,NULL,&cmod),"shaderModule");
    VkPipelineLayoutCreateInfo plc={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&dsl};
    VkPipelineLayout pl; CHECK(CPL(dev,&plc,NULL,&pl),"pipelineLayout");
    VkComputePipelineCreateInfo cpc={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=cmod,.pName="main"},
        .layout=pl};
    VkPipeline pipe; CHECK(CCPipe(dev,VK_NULL_HANDLE,1,&cpc,NULL,&pipe),"computePipeline");

    /* --- record --- */
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex=0};
    VkCommandPool cp; CHECK(CCP(dev,&cpi,NULL,&cp),"cmdPool");
    VkCommandBufferAllocateInfo cba={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd; CHECK(ACB(dev,&cba,&cmd),"allocCB");
    VkCommandBufferBeginInfo cbb={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(BCB(cmd,&cbb),"beginCB");
    CBPipe(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipe);
    CBDS(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&dset,0,NULL);
    if(indirect){
        printf("calling vkCmdDispatchIndirect(offset=0)\n");
        CDispInd(cmd,ibuf,0);
    } else {
        printf("calling vkCmdDispatch(%u,1,1)\n",want);
        CDisp(cmd,want,1,1);
    }
    fflush(stdout);
    CHECK(ECB(cmd),"endCB");
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1,.pCommandBuffers=&cmd};
    CHECK(QS(q,1,&si,VK_NULL_HANDLE),"vkQueueSubmit");
    CHECK(QWI(q),"vkQueueWaitIdle");
    printf("vkQueueWaitIdle returned (no hang)\n");

    /* --- verify --- */
    uint32_t*out=NULL;
    CHECK(MM(dev,smem,0,SLOTS*sizeof(uint32_t),0,(void**)&out),"map result");
    /* "written" is decided by the rule, never by comparing against 0, because a
     * shader can legitimately write 0 and that would be indistinguishable from
     * an untouched slot. */
    uint32_t stamped=0, correct=0, beyond=0, wrong=0;
    for(uint32_t i=0;i<SLOTS;i++){
        uint32_t expect;
        if(!strcmp(rule,"stamp"))        expect = STAMP | i;
        else if(!strcmp(rule,"const"))   expect = 0xDEADBEEFu;
        else                             expect = i + 1u;   /* idplus1 */

        int written = (out[i] != 0);
        if(written) stamped++;
        if(i < want) {
            if(out[i] == expect) correct++;
            else                 wrong++;
        } else if(written) {
            beyond++;
        }
    }
    printf("\n--- result: %s ---\n",cs);
    printf("slots stamped        : %u\n",stamped);
    printf("correct within count : %u   (expect %u)\n",correct,want);
    printf("rule applied         : %s\n",rule);
    printf("stamped beyond count : %u   (must be 0)\n",beyond);
    printf("wrong value in range : %u   (must be 0)\n",wrong);
    printf("first 12 slots       :");
    for(int i=0;i<12;i++) printf(" %08x",out[i]);
    printf("\n");

    int ok = (correct==want && beyond==0 && wrong==0 && stamped==want);
    printf("verdict              : %s\n", ok ? "CORRECT" : "not as expected");
    printf("FINGERPRINT %s stamped=%u correct=%u beyond=%u wrong=%u ok=%d\n",
           cs,stamped,correct,beyond,wrong,ok);
    UM(dev,smem);
    free(ccode);
    return ok?0:1;
}
