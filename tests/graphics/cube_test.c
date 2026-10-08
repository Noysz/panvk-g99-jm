/* cube_test.c -- T4.9, spinning cube on Valhall v9 / JM.
 *
 * A unit cube with one flat colour per face, rotated a bit more every frame.
 * MVP comes from a push constant (16 floats), position and colour from one
 * interleaved vertex buffer (36 vertices, 12 triangles). Depth test LESS on a
 * D32 attachment, no culling, so hidden faces are removed by depth only.
 *
 * Each frame re-records the same command buffer, submits with a fence and
 * waits on the fence (vkWaitForFences, not vkQueueWaitIdle). Default colour
 * target is OPTIMAL (AFBC) and is copied to a host buffer with
 * vkCmdCopyImageToBuffer; CUBE_TILING=linear renders to a host-visible LINEAR
 * image instead.
 *
 * Every frame is checked against a CPU rasterizer of the same triangles with
 * the same matrix. Pixels where the answer depends on sub-pixel rounding (a
 * pixel centre within CUBE_BAND px of an edge between two different colours,
 * or two faces at the same depth) are counted separately as "edge" pixels;
 * every other pixel must match within 2 per channel.
 *
 * Negative controls:
 *   CUBE_LIE=1      CPU model uses the angle + 3 degrees: MUST fail.
 *   CUBE_NODEPTH=1  pipeline depth test off, model keeps it: MUST fail on
 *                   frames where a back face is drawn after a front face.
 *
 * Output:
 *   CUBE_PNG_DIR=d  write d/cube_NNN.png per frame
 *   CUBE_LIVE=p     overwrite p with the newest frame (dashboard)
 *   CUBE_X11=1      show frames live in an X11 window (DISPLAY), GPU render,
 *                   CPU put-image. CUBE_FRAMES=0 runs until killed.
 * Other: CUBE_FRAMES (36), CUBE_STEP (10 degrees), CUBE_DIM (512),
 *        CUBE_BAND (0.5).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <math.h>
#include <time.h>
#include "pngdump.h"
#include "vulkan/vulkan_core.h"
#ifdef CUBE_WITH_X11
#include <xcb/xcb.h>
#endif

#define CHECK(e,m) do { VkResult _r=(e); \
    if(_r!=VK_SUCCESS){printf("FAILED: %s (VkResult=%d)\n",m,_r);return 1;} } while(0)

typedef PFN_vkVoidFunction (*PFN_icdGetInstanceProcAddr)(VkInstance,const char*);
#ifndef PANVK_DEFAULT_ICD_SO
#define PANVK_DEFAULT_ICD_SO \
 "/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so"
#endif

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
static double now_s(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}

/* ---- geometry ---- */
struct vtx { float p[3], c[3]; };
static const float face_col[6][3] = {
    {1,0,0}, {0,1,1},   /* +X red,   -X cyan    */
    {0,1,0}, {1,0,1},   /* +Y green, -Y magenta */
    {0,0,1}, {1,1,0},   /* +Z blue,  -Z yellow  */
};
static int build_cube(struct vtx*v){
    /* face quad corners, each face as (a,b,c),(a,c,d) */
    static const float q[6][4][3] = {
        {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}},
        {{-1,-1, 1},{-1, 1, 1},{-1, 1,-1},{-1,-1,-1}},
        {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}},
        {{-1,-1, 1},{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1}},
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},
    };
    static const int idx[6]={0,1,2,0,2,3};
    int n=0;
    for(int f=0;f<6;f++) for(int k=0;k<6;k++){
        memcpy(v[n].p,q[f][idx[k]],12); memcpy(v[n].c,face_col[f],12); n++; }
    return n;
}
/* column-major 4x4, m[c*4+r] */
static void mat_mul(double*o,const double*a,const double*b){
    double t[16];
    for(int c=0;c<4;c++) for(int r=0;r<4;r++){
        double s=0; for(int k=0;k<4;k++) s+=a[k*4+r]*b[c*4+k]; t[c*4+r]=s; }
    memcpy(o,t,sizeof t);
}
static void make_mvp(double deg,double*m){
    double a=deg*M_PI/180.0, b=0.7*a;
    double ry[16]={cos(a),0,-sin(a),0, 0,1,0,0, sin(a),0,cos(a),0, 0,0,0,1};
    double rx[16]={1,0,0,0, 0,cos(b),sin(b),0, 0,-sin(b),cos(b),0, 0,0,0,1};
    double tr[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,5,1};
    /* Vulkan clip space: w = z_view, depth z/w in [0,1] for z_view in [n,F] */
    const double f=1.0/tan(30.0*M_PI/180.0), n=1.0, F=10.0;
    double pr[16]={f,0,0,0, 0,f,0,0, 0,0,F/(F-n),1, 0,0,-F*n/(F-n),0};
    double t[16]; mat_mul(t,rx,ry); mat_mul(t,tr,t); mat_mul(m,pr,t);
}

/* ---- CPU model ---- */
struct model { uint8_t*rgb; uint8_t*edge; };
static void cpu_model(int D,double deg,const struct vtx*v,int nv,int depth_test,double band,
                      const uint8_t clear[3],struct model*md){
    double m[16]; make_mvp(deg,m);
    float*zb=malloc(sizeof(float)*D*D);
    /* per pixel: winner depth/colour from definite covers; then a pass for
     * near-edge triangles that could change the colour */
    for(int i=0;i<D*D;i++){ zb[i]=1.0f; memcpy(md->rgb+3*i,clear,3); md->edge[i]=0; }
    double sx[36],sy[36],sz[36];
    for(int i=0;i<nv;i++){
        double p[4]={v[i].p[0],v[i].p[1],v[i].p[2],1},c[4];
        for(int r=0;r<4;r++){ c[r]=0; for(int k=0;k<4;k++) c[r]+=m[k*4+r]*p[k]; }
        sx[i]=(c[0]/c[3]*0.5+0.5)*D; sy[i]=(c[1]/c[3]*0.5+0.5)*D; sz[i]=c[2]/c[3];
    }
    for(int pass=0;pass<2;pass++)
    for(int t=0;t<nv;t+=3){
        int i0=t,i1=t+1,i2=t+2;
        double area=(sx[i1]-sx[i0])*(sy[i2]-sy[i0])-(sx[i2]-sx[i0])*(sy[i1]-sy[i0]);
        if(fabs(area)<1e-12) continue;
        double sg=area>0?1:-1;
        int x0=(int)floor(fmin(sx[i0],fmin(sx[i1],sx[i2]))-1), x1=(int)ceil(fmax(sx[i0],fmax(sx[i1],sx[i2]))+1);
        int y0=(int)floor(fmin(sy[i0],fmin(sy[i1],sy[i2]))-1), y1=(int)ceil(fmax(sy[i0],fmax(sy[i1],sy[i2]))+1);
        if(x0<0)x0=0; if(y0<0)y0=0; if(x1>D)x1=D; if(y1>D)y1=D;
        int e[3][2]={{i1,i2},{i2,i0},{i0,i1}};
        double len[3]; for(int k=0;k<3;k++) len[k]=hypot(sx[e[k][1]]-sx[e[k][0]],sy[e[k][1]]-sy[e[k][0]]);
        uint8_t col[3]={(uint8_t)lrint(v[t].c[0]*255),(uint8_t)lrint(v[t].c[1]*255),(uint8_t)lrint(v[t].c[2]*255)};
        for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++){
            double px=x+0.5,py=y+0.5,w[3],dmin=1e30;
            for(int k=0;k<3;k++){
                int a=e[k][0],b=e[k][1];
                w[k]=sg*((sx[b]-sx[a])*(py-sy[a])-(sy[b]-sy[a])*(px-sx[a]));
                double d=w[k]/len[k]; if(d<dmin) dmin=d;
            }
            if(dmin < -band) continue;
            double l0=w[0]/(sg*area), l1=w[1]/(sg*area), l2=w[2]/(sg*area);
            float z=(float)(l0*sz[i0]+l1*sz[i1]+l2*sz[i2]);
            int pi=y*D+x;
            if(pass==0){
                if(dmin <= band) continue;          /* only definite covers */
                if(!depth_test){ /* model of the NODEPTH case is not needed */ }
                if(z < zb[pi] - 1e-5f){ zb[pi]=z; memcpy(md->rgb+3*pi,col,3); }
                else if(fabsf(z-zb[pi])<=1e-5f && memcmp(md->rgb+3*pi,col,3)) md->edge[pi]=1;
            } else {
                /* near-edge triangle: if it could be the nearest and has a
                 * different colour, the pixel's answer is rounding-dependent */
                if(dmin > band) continue;
                if(z <= zb[pi] + 1e-5f && memcmp(md->rgb+3*pi,col,3)) md->edge[pi]=1;
            }
        }
    }
    free(zb);
}

#ifndef CUBE_NO_MAIN
int main(void){
    const int frames = getenv("CUBE_FRAMES") ? atoi(getenv("CUBE_FRAMES")) : 36;
    const double step = getenv("CUBE_STEP") ? atof(getenv("CUBE_STEP")) : 10.0;
    const int D = getenv("CUBE_DIM") ? atoi(getenv("CUBE_DIM")) : 512;
    const double band = getenv("CUBE_BAND") ? atof(getenv("CUBE_BAND")) : 0.5;
    const int lie = getenv("CUBE_LIE") && getenv("CUBE_LIE")[0]=='1';
    const int nodepth = getenv("CUBE_NODEPTH") && getenv("CUBE_NODEPTH")[0]=='1';
    const int linear = getenv("CUBE_TILING") && !strcmp(getenv("CUBE_TILING"),"linear");
    const char*pngdir=getenv("CUBE_PNG_DIR"); const char*live=getenv("CUBE_LIVE");
    const int want_x11 = getenv("CUBE_X11") && getenv("CUBE_X11")[0]=='1';
    /* CUBE_CHECK_EVERY=n: run the CPU check on every n-th frame only (live
     * view). Default 1, every frame. */
    const int check_every = getenv("CUBE_CHECK_EVERY") ? atoi(getenv("CUBE_CHECK_EVERY")) : 1;
    /* CUBE_RESUBMIT=1: record the command buffer once (fixed angle 30, no
     * ONE_TIME_SUBMIT) and submit the same command buffer every frame, like
     * vkcube with prerecorded command buffers. Every frame is checked. */
    const int resubmit = getenv("CUBE_RESUBMIT") && getenv("CUBE_RESUBMIT")[0]=='1';
    int checked=0;
    printf("=== cube: frames=%d step=%.1f dim=%d tiling=%s band=%.2f%s%s%s%s ===\n",
           frames,step,D,linear?"linear":"optimal",band,resubmit?" resubmit":"",
           lie?" NEGATIVE CONTROL: model angle +3":"",
           nodepth?" NEGATIVE CONTROL: pipeline depth test off":"",
           want_x11?" x11":"");
    fflush(stdout);

    const char*icd=getenv("PANVK_ICD_SO"); if(!icd||!icd[0]) icd=PANVK_DEFAULT_ICD_SO;
    void*lib=dlopen(icd,RTLD_NOW); if(!lib){printf("dlopen: %s\n",dlerror());return 1;}
    PFN_icdGetInstanceProcAddr gpa=(PFN_icdGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
    if(!gpa){printf("dlsym failed\n");return 1;}
    PFN_vkCreateInstance CI=(PFN_vkCreateInstance)gpa(NULL,"vkCreateInstance");
    VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
    VkInstance inst; CHECK(CI(&ii,NULL,&inst),"vkCreateInstance");
    #define IP(n) (PFN_vk##n)gpa(inst,"vk" #n)
    PFN_vkGetDeviceProcAddr GDPA=IP(GetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EPD=IP(EnumeratePhysicalDevices);
    PFN_vkCreateDevice CD=IP(CreateDevice);
    PFN_vkGetPhysicalDeviceMemoryProperties GMP=IP(GetPhysicalDeviceMemoryProperties);
    uint32_t n=0; CHECK(EPD(inst,&n,NULL),"enum"); if(!n){printf("FAILED: no device\n");return 1;}
    VkPhysicalDevice pd; n=1; EPD(inst,&n,&pd);
    VkPhysicalDeviceMemoryProperties mp; GMP(pd,&mp);
    VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering=VK_TRUE};
    float pr=1.0f;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&pr};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f13,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    VkDevice dev; CHECK(CD(pd,&di,NULL,&dev),"vkCreateDevice");
    #define DP(x) PFN_vk##x x = (PFN_vk##x)GDPA(dev,"vk" #x)
    DP(GetDeviceQueue); DP(CreateCommandPool); DP(AllocateCommandBuffers); DP(BeginCommandBuffer);
    DP(EndCommandBuffer); DP(ResetCommandBuffer); DP(QueueSubmit); DP(CreateFence); DP(WaitForFences);
    DP(ResetFences); DP(CreateImage); DP(GetImageMemoryRequirements); DP(GetImageSubresourceLayout);
    DP(AllocateMemory); DP(BindImageMemory); DP(CreateImageView); DP(MapMemory);
    DP(CreateShaderModule); DP(CreatePipelineLayout); DP(CreateGraphicsPipelines);
    DP(CmdBeginRendering); DP(CmdEndRendering); DP(CmdBindPipeline); DP(CmdSetViewport);
    DP(CmdSetScissor); DP(CmdDraw); DP(CmdPipelineBarrier); DP(CreateBuffer);
    DP(GetBufferMemoryRequirements); DP(BindBufferMemory); DP(CmdBindVertexBuffers);
    DP(CmdPushConstants); DP(CmdCopyImageToBuffer);

    VkQueue q; GetDeviceQueue(dev,0,0,&q);
    const VkFormat FMT=VK_FORMAT_R8G8B8A8_UNORM, DFMT=VK_FORMAT_D32_SFLOAT;
    const VkMemoryPropertyFlags HV=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    /* colour target */
    VkImageCreateInfo ic={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=FMT,.extent={D,D,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=linear?VK_IMAGE_TILING_LINEAR:VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage img; CHECK(CreateImage(dev,&ic,NULL,&img),"image");
    VkMemoryRequirements mr; GetImageMemoryRequirements(dev,img,&mr);
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
        .memoryTypeIndex=mtype(&mp,mr.memoryTypeBits,linear?HV:0)};
    VkDeviceMemory imgm; CHECK(AllocateMemory(dev,&ma,NULL,&imgm),"image mem");
    CHECK(BindImageMemory(dev,img,imgm,0),"bind image");
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=img,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=FMT,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; CHECK(CreateImageView(dev,&vi,NULL,&view),"view");
    /* depth target */
    VkImageCreateInfo dic=ic; dic.format=DFMT; dic.tiling=VK_IMAGE_TILING_OPTIMAL;
    dic.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImage dimg; CHECK(CreateImage(dev,&dic,NULL,&dimg),"depth image");
    GetImageMemoryRequirements(dev,dimg,&mr);
    VkMemoryAllocateInfo dma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
        .memoryTypeIndex=mtype(&mp,mr.memoryTypeBits,0)};
    VkDeviceMemory dmem; CHECK(AllocateMemory(dev,&dma,NULL,&dmem),"depth mem");
    CHECK(BindImageMemory(dev,dimg,dmem,0),"bind depth");
    VkImageViewCreateInfo dvi=vi; dvi.image=dimg; dvi.format=DFMT;
    dvi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    VkImageView dview; CHECK(CreateImageView(dev,&dvi,NULL,&dview),"depth view");

    /* buffers: vertices, readback */
    struct vtx verts[36]; int nv=build_cube(verts);
    VkBuffer vb,rb; VkDeviceMemory vbm,rbm; void*vmap; void*rmap=NULL;
    {
        VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=sizeof verts,
            .usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
        CHECK(CreateBuffer(dev,&bc,NULL,&vb),"vb"); VkMemoryRequirements r; GetBufferMemoryRequirements(dev,vb,&r);
        VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.size,
            .memoryTypeIndex=mtype(&mp,r.memoryTypeBits,HV)};
        CHECK(AllocateMemory(dev,&a,NULL,&vbm),"vb mem"); BindBufferMemory(dev,vb,vbm,0);
        MapMemory(dev,vbm,0,VK_WHOLE_SIZE,0,&vmap); memcpy(vmap,verts,sizeof verts);
    }
    if(!linear){
        VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=(VkDeviceSize)D*D*4,
            .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        CHECK(CreateBuffer(dev,&bc,NULL,&rb),"rb"); VkMemoryRequirements r; GetBufferMemoryRequirements(dev,rb,&r);
        VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.size,
            .memoryTypeIndex=mtype(&mp,r.memoryTypeBits,HV)};
        CHECK(AllocateMemory(dev,&a,NULL,&rbm),"rb mem"); BindBufferMemory(dev,rb,rbm,0);
        MapMemory(dev,rbm,0,VK_WHOLE_SIZE,0,&rmap);
    }
    uint8_t*pix; size_t pitch;
    if(linear){
        VkImageSubresource sr={VK_IMAGE_ASPECT_COLOR_BIT,0,0}; VkSubresourceLayout sl;
        GetImageSubresourceLayout(dev,img,&sr,&sl); void*m; MapMemory(dev,imgm,0,VK_WHOLE_SIZE,0,&m);
        pix=(uint8_t*)m+sl.offset; pitch=sl.rowPitch;
    } else { pix=rmap; pitch=(size_t)D*4; }

    /* pipeline */
    size_t vz=0,fz=0; char*vc=rf("cube.vert.spv",&vz); char*fc=rf("cube.frag.spv",&fz); if(!vc||!fc) return 1;
    VkShaderModuleCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=vz,.pCode=(uint32_t*)vc};
    VkShaderModule vs; CHECK(CreateShaderModule(dev,&smi,NULL,&vs),"vs");
    smi.codeSize=fz; smi.pCode=(uint32_t*)fc;
    VkShaderModule fs; CHECK(CreateShaderModule(dev,&smi,NULL,&fs),"fs");
    VkPushConstantRange pcr={VK_SHADER_STAGE_VERTEX_BIT,0,64};
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount=1,.pPushConstantRanges=&pcr};
    VkPipelineLayout pl; CHECK(CreatePipelineLayout(dev,&pli,NULL,&pl),"layout");
    VkPipelineShaderStageCreateInfo st[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
    VkVertexInputBindingDescription vbd={0,sizeof(struct vtx),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vad[2]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12}};
    VkPipelineVertexInputStateCreateInfo vin={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&vbd,
        .vertexAttributeDescriptionCount=2,.pVertexAttributeDescriptions=vad};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vpi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rsi={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo msi={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo dsi={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=!nodepth,.depthWriteEnable=!nodepth,.depthCompareOp=VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState cba={.colorWriteMask=0xf};
    VkPipelineColorBlendStateCreateInfo cbs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&cba};
    VkDynamicState dy[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyi={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount=2,.pDynamicStates=dy};
    VkPipelineRenderingCreateInfo pri={.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount=1,.pColorAttachmentFormats=&FMT,.depthAttachmentFormat=DFMT};
    VkGraphicsPipelineCreateInfo gpi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.pNext=&pri,
        .stageCount=2,.pStages=st,.pVertexInputState=&vin,.pInputAssemblyState=&ia,.pViewportState=&vpi,
        .pRasterizationState=&rsi,.pMultisampleState=&msi,.pDepthStencilState=&dsi,
        .pColorBlendState=&cbs,.pDynamicState=&dyi,.layout=pl};
    VkPipeline pipe; CHECK(CreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&gpi,NULL,&pipe),"pipeline");

    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=0};
    VkCommandPool cp; CHECK(CreateCommandPool(dev,&cpi,NULL,&cp),"pool");
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd; CHECK(AllocateCommandBuffers(dev,&cai,&cmd),"cb");
    VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; CHECK(CreateFence(dev,&fci,NULL,&fence),"fence");

#ifdef CUBE_WITH_X11
    xcb_connection_t*xc=NULL; xcb_window_t win=0; xcb_gcontext_t gc=0; uint8_t*xbuf=NULL; uint32_t xmax=0;
    if(want_x11){
        xc=xcb_connect(NULL,NULL);
        if(xcb_connection_has_error(xc)){printf("FAILED: xcb_connect (DISPLAY=%s)\n",getenv("DISPLAY"));return 1;}
        xcb_screen_t*scr=xcb_setup_roots_iterator(xcb_get_setup(xc)).data;
        win=xcb_generate_id(xc);
        uint32_t vals[2]={scr->black_pixel,1};
        int wx=(scr->width_in_pixels>D)?(scr->width_in_pixels-D)/2:0;
        int wy=(scr->height_in_pixels>D)?(scr->height_in_pixels-D)/2:0;
        xcb_create_window(xc,XCB_COPY_FROM_PARENT,win,scr->root,wx,wy,D,D,0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT,scr->root_visual,XCB_CW_BACK_PIXEL|XCB_CW_OVERRIDE_REDIRECT,vals);
        xcb_map_window(xc,win);
        gc=xcb_generate_id(xc); xcb_create_gc(xc,gc,win,0,NULL);
        xcb_flush(xc);
        xbuf=malloc((size_t)D*D*4);
        xmax=xcb_get_maximum_request_length(xc)*4;
        printf("x11         : window %dx%d at %d,%d on %dx%d, depth %u, max request %u bytes\n",
               D,D,wx,wy,scr->width_in_pixels,scr->height_in_pixels,scr->root_depth,xmax);
    }
#else
    if(want_x11){printf("FAILED: built without CUBE_WITH_X11\n");return 1;}
#endif

    const uint8_t clear8[3]={32,32,40};
    struct model md={malloc((size_t)D*D*3),malloc((size_t)D*D)};
    /* second model with no band: plain point-in-triangle at the pixel centre.
     * Its diff measures how far the hardware's edges are from exact. */
    struct model mx={malloc((size_t)D*D*3),malloc((size_t)D*D)};
    uint64_t tot_exact=0; uint32_t max_exact=0;
    uint64_t tot_bad=0,tot_edge_diff=0,tot_edge=0; int bad_frames=0;
    double t_gpu=0,t_start=now_s();
    for(int fr=0; frames==0 || fr<frames; fr++){
        double deg=resubmit?30.0:fr*step;
        double m[16]; make_mvp(deg,m); float mf[16]; for(int i=0;i<16;i++) mf[i]=(float)m[i];
        double t0=now_s();
        if(resubmit && fr>0) goto submit;
        CHECK(ResetCommandBuffer(cmd,0),"reset cb");
        VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags=resubmit?0:VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        CHECK(BeginCommandBuffer(cmd,&bi),"begin");
        VkImageMemoryBarrier b0[2]={
            {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
             .newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=img,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
             .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
            {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
             .newLayout=VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=dimg,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1},
             .dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT}};
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,0,0,NULL,0,NULL,2,b0);
        VkRenderingAttachmentInfo at={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,.imageView=view,
            .imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp=VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue={.color={.float32={32/255.f,32/255.f,40/255.f,1}}}};
        VkRenderingAttachmentInfo dat={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,.imageView=dview,
            .imageLayout=VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.clearValue={.depthStencil={1.0f,0}}};
        VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,.renderArea={{0,0},{D,D}},.layerCount=1,
            .colorAttachmentCount=1,.pColorAttachments=&at,.pDepthAttachment=&dat};
        CmdBeginRendering(cmd,&ri);
        VkViewport vp={0,0,(float)D,(float)D,0,1}; VkRect2D sc={{0,0},{D,D}};
        CmdSetViewport(cmd,0,1,&vp); CmdSetScissor(cmd,0,1,&sc);
        CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
        VkDeviceSize off=0; CmdBindVertexBuffers(cmd,0,1,&vb,&off);
        CmdPushConstants(cmd,pl,VK_SHADER_STAGE_VERTEX_BIT,0,64,mf);
        CmdDraw(cmd,nv,1,0,0);
        CmdEndRendering(cmd);
        if(!linear){
            VkImageMemoryBarrier b1={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=img,
                .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
                .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
            CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,0,NULL,0,NULL,1,&b1);
            VkBufferImageCopy rg={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={D,D,1}};
            CmdCopyImageToBuffer(cmd,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,rb,1,&rg);
            VkMemoryBarrier hb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
            CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&hb,0,NULL,0,NULL);
        }
        CHECK(EndCommandBuffer(cmd),"end");
submit:
        if(!linear) memset(rmap,0xAB,(size_t)D*D*4);   /* poison: a skipped copy cannot pass */
        VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
        CHECK(ResetFences(dev,1,&fence),"reset fence");
        CHECK(QueueSubmit(q,1,&si,fence),"submit");
        VkResult wr=WaitForFences(dev,1,&fence,VK_TRUE,5000000000ull);
        if(wr!=VK_SUCCESS){printf("FAILED: vkWaitForFences frame %d -> %d\n",fr,wr);return 1;}
        t_gpu+=now_s()-t0;

        /* check */
        if(fr % check_every == 0){ checked++;
        cpu_model(D,lie?deg+3.0:deg,verts,nv,1,band,clear8,&md);
        cpu_model(D,lie?deg+3.0:deg,verts,nv,1,0.0,clear8,&mx);
        uint32_t exact=0;
        for(int y=0;y<D;y++){ const uint8_t*row=pix+y*pitch;
            for(int x=0;x<D;x++){ const uint8_t*p=row+x*4,*e=mx.rgb+3*(y*D+x);
                exact+=abs(p[0]-e[0])>2||abs(p[1]-e[1])>2||abs(p[2]-e[2])>2; }}
        tot_exact+=exact; if(exact>max_exact) max_exact=exact;
        uint32_t bad=0,edge=0,edge_diff=0,cnt[7]={0}; int fx=-1,fy=-1;
        for(int y=0;y<D;y++){ const uint8_t*row=pix+y*pitch;
            for(int x=0;x<D;x++){ const uint8_t*p=row+x*4,*e=md.rgb+3*(y*D+x);
                int diff=abs(p[0]-e[0])>2||abs(p[1]-e[1])>2||abs(p[2]-e[2])>2;
                if(md.edge[y*D+x]){ edge++; edge_diff+=diff; continue; }
                if(diff){ bad++; if(fx<0){fx=x;fy=y;} }
                for(int f=0;f<6;f++) if(p[0]==lrint(face_col[f][0]*255)&&p[1]==lrint(face_col[f][1]*255)&&p[2]==lrint(face_col[f][2]*255)) cnt[f]++;
            }}
        tot_bad+=bad; tot_edge+=edge; tot_edge_diff+=edge_diff; bad_frames+=bad!=0;
        if(frames!=0 || fr%(30*check_every)==0){
            printf("CUBEFP frame=%03d deg=%6.1f bad=%u exact_diff=%u edge=%u edge_diff=%u faces[R C G M B Y]=%u %u %u %u %u %u",
                   fr,deg,bad,exact,edge,edge_diff,cnt[0],cnt[1],cnt[2],cnt[3],cnt[4],cnt[5]);
            if(fx>=0){const uint8_t*p=pix+fy*pitch+fx*4,*e=md.rgb+3*(fy*D+fx);
                printf(" first=(%d,%d) got=%u,%u,%u model=%u,%u,%u",fx,fy,p[0],p[1],p[2],e[0],e[1],e[2]);}
            printf("\n"); fflush(stdout);
        }
        }
        /* outputs */
        char path[512];
        for(int o=0;o<2;o++){
            const char*pp=NULL;
            if(o==0&&pngdir){snprintf(path,sizeof path,"%s/cube_%03d.png",pngdir,fr);pp=path;}
            if(o==1&&live) pp=live;
            if(!pp) continue;
            char tmp[520]; snprintf(tmp,sizeof tmp,"%s.tmp.png",pp);
            FILE*f=png_open(o==1?tmp:pp,"wb"); if(!f) continue;
            fprintf(f,"P6\n%d %d\n255\n",D,D);
            for(int y=0;y<D;y++){const uint8_t*row=pix+y*pitch;
                for(int x=0;x<D;x++) fwrite(row+x*4,1,3,f);}
            png_close(f);
            if(o==1) rename(tmp,pp);   /* dashboard never sees half a file */
        }
#ifdef CUBE_WITH_X11
        if(want_x11){
            for(int y=0;y<D;y++){const uint8_t*row=pix+y*pitch; uint8_t*o=xbuf+(size_t)y*D*4;
                for(int x=0;x<D;x++){o[4*x]=row[4*x+2];o[4*x+1]=row[4*x+1];o[4*x+2]=row[4*x];o[4*x+3]=0;}}
            uint32_t rows=(xmax-64)/((uint32_t)D*4); if(rows<1) rows=1;
            for(uint32_t y=0;y<(uint32_t)D;y+=rows){ uint32_t h=(y+rows>(uint32_t)D)?(uint32_t)D-y:rows;
                xcb_put_image(xc,XCB_IMAGE_FORMAT_Z_PIXMAP,win,gc,D,h,0,y,0,24,h*D*4,xbuf+(size_t)y*D*4); }
            xcb_flush(xc);
        }
#endif
    }
    double wall=now_s()-t_start; int nfr=frames?frames:1;
    printf("\n--- result ---\n");
    printf("frames=%d bad_frames=%d bad_pixels=%llu edge_pixels=%llu edge_diff=%llu gpu+wait=%.1f ms/frame wall=%.1f ms/frame\n",
           nfr,bad_frames,(unsigned long long)tot_bad,(unsigned long long)tot_edge,(unsigned long long)tot_edge_diff,
           1000*t_gpu/nfr,1000*wall/nfr);
    int ok=tot_bad==0;
    printf("checked frames=%d of %d\n",checked,nfr);
    printf("CUBESUM tiling=%s frames=%d bad_frames=%d bad=%llu exact_diff=%llu max_exact_frame=%u verdict=%s\n",
           linear?"linear":"optimal",nfr,bad_frames,(unsigned long long)tot_bad,(unsigned long long)tot_exact,max_exact,ok?"PASS":"FAIL");
    printf("DONE\n");
    return ok?0:2;
}
#endif /* CUBE_NO_MAIN */
