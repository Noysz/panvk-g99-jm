<!-- Copy of REBASE-RESOLUTIONS.md from hafiz's upstream rebase (tree 52a7497a). One section reworded, nothing else changed. -->

# Rebase of panvk-g99-jm (+ funnymdzz kbase fork) onto upstream Mesa feeadb5f

Every conflict hunk, as found, with the decision and why.

### src/panfrost/vulkan/panvk_instance.h — hunk 1
Bit collision: upstream 97905afd took 1<<19 for NO_CRC; the fork used 1<<19 for KBASE_DIAG. Upstream keeps 19, the fork flag moves to the next free bit (enum ends at 1<<18 otherwise).

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   PANVK_DEBUG_NO_CRC = 1 << 19,
=======
   PANVK_DEBUG_KBASE_DIAG = 1 << 19,
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_instance.c — hunk 1
Same collision, parser table: keep both option names.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   {"no_crc", PANVK_DEBUG_NO_CRC},
=======
   {"kbase_diag", PANVK_DEBUG_KBASE_DIAG},
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/meson.build — hunk 1
Upstream c66177bd added v11; panvk-g99-jm 0014 added v9. Both.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
foreach arch : [6, 7, 10, 11, 12, 13, 14]
=======
foreach arch : [6, 7, 9, 10, 12, 13, 14]  # PATCH: tambah 9 (Mali-G57)
>>>>>>> theirs
```
</details>

### Upstream agent-instructions file
Upstream 015ae984 added a short agent-instructions file pointing to AGENTS.md; the fork had its own notes file under the same name. Took upstream; documentation only, no build impact. (Section reworded in this copy; the file itself is left out of 0001.)

### src/panfrost/vulkan/panvk_shader.h — hunk 1
Upstream 8789c376 (large constants on Bifrost) added constant_data to the common sysvals; the fork added libpoly params. Independent fields, keep both (offsets are computed with offsetof).

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   /* Address of the shader constant data buffer */
   aligned_u64 constant_data;
=======
   /* libpoly state used by software geometry/tessellation stages.  Keeping
    * these in the common block gives compute-lowered VS/TCS and the hardware
    * VS-lowered TES identical offsets. */
   aligned_u64 vertex_param_buffer_poly;
   aligned_u64 tess_param_buffer_poly;
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_cmd_push_constant.c — hunk 1
Upstream 8789c376 builds common_inner fresh (printf + constant_data for <v9). The fork memcpys the existing common sysvals first so the libpoly params it stores there survive. Merge: fork's memcpy, then both upstream assignments; constant_data stays PAN_ARCH < 9 as upstream has it. sysvals is a parameter of prepare_push_uniforms.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   struct panvk_common_sysvals_inner common_inner = {
      .printf_buffer_address = dev->printf.bo->addr.dev,
#if PAN_ARCH < 9
      .constant_data = panvk_priv_mem_dev_addr(shader->data_mem),
#endif
   };
=======
   struct panvk_common_sysvals_inner common_inner;
   memcpy(&common_inner, &sysvals[SYSVALS_COMMON_START],
          sizeof(common_inner));
   common_inner.printf_buffer_address = dev->printf.bo->addr.dev;
>>>>>>> theirs
```
</details>

### src/panfrost/lib/pan_fb.c — hunk 1
Upstream f9c22ce2 added YUV render-target emission (emit_yuv_rt_desc is defined for every arch, branching on PAN_ARCH >= 10 internally, so it compiles for v9). panvk-g99-jm 0051 added an RT-format trace before emit_rgb_rt_desc. Keep upstream's branch, keep the trace.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
      if (pan_format_is_yuv(fb->rt_formats[rt])) {
         emit_yuv_rt_desc(info, ct, rt, tile_rt_offset_B, (void *)rts);
      } else {
         emit_rgb_rt_desc(info, ct, rt, tile_rt_offset_B, rts);
      }
=======
      PAN_V9_TRACE( "[PANVK_DEBUG_RTFMT] rt=%u fmt=%d rt_count=%u\n", rt, fb->rt_formats[rt], fb->rt_count);
      emit_rgb_rt_desc(info, ct, rt, tile_rt_offset_B, rts);
>>>>>>> theirs
```
</details>

### src/panfrost/model/pan_model.c — hunk 1
Upstream 93d2c51f added Mali-G68 (9,2,4); the fork added Mali-G710 (10,8,2). Different product IDs, each present once after the merge; keep both in product-ID order.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   VALHALL_MODEL(PAN_PROD_ID(9, 2, 4), 0, "G68",    "G78", MODEL_ANISO(ALL),  MODEL_TB_SIZES(16384,  8192),
                                              MODEL_RATES_X(2, 4, 8,  32,  32, 8)),
=======
   /* Mali-G710 (Odin, e.g. Google Tensor G2 / Pixel 7, GPU_ID 0xa862xxxx).
    * It shares the G710 family rates used by the G610 product below. */
   VALHALL_MODEL(PAN_PROD_ID(10, 8, 2), 0, "G710",   "G710", MODEL_ANISO(ALL),  MODEL_TB_SIZES(32768, 16384),
                                              MODEL_RATES_X(4, 8, 16,  64,  64, 16)),
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_image.c — hunk 1
Upstream (CRC plumbing, 47527ff2 and follow-ups) zero-initialises CRC state at bind; panvk-g99-jm 0051 added a bind trace. Trace first (so it always prints), then upstream's CRC block unchanged including its early return.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   /* Zero-initialize CRC state. If CRC state is not mapped on the host, do a
    * temporary mapping just for this operation. */
   if (plane->image.props.crc) {
      const struct pan_image_slice_layout *slice =
         &plane->plane.layout.slices[0];
      uint64_t state_offset = offset + slice->crc.header_offset_B;

      bool temporary_map = mem->addr.host == NULL;
      uint8_t *cpu_map = temporary_map
                            ? pan_kmod_bo_mmap(mem->bo, PROT_READ | PROT_WRITE,
                                               MAP_SHARED, NULL)
                            : mem->addr.host;
      if (cpu_map == 0 || cpu_map == MAP_FAILED) {
         plane->image.props.crc = false;
         return;
      }

      size_t crc_size = PAN_CRC_HEADER_SIZE_B + slice->crc.size_B;
      memset(cpu_map + state_offset, 0, crc_size);
      pan_kmod_queue_bo_map_sync(mem->bo, state_offset, cpu_map + state_offset,
                                 crc_size, PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH);

      if (temporary_map) {
         int ret = os_munmap(cpu_map, pan_kmod_bo_size(mem->bo));
         assert(!ret);
      }
   }
=======
   PAN_V9_TRACE( "[PANVK_DEBUG_BIND] image plane bind (non-sparse) base=0x%lx (mem->addr.dev=0x%lx offset=%lu)\n",
           (unsigned long)plane->plane.base, (unsigned long)mem->addr.dev, (unsigned long)offset);
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_physical_device.c — hunk 1
Upstream 12563ff5 enables EXT_astc_decode_mode from v7 (superset of the fork's >= 9). Keep the fork's EXT_transform_feedback (v10+, its CSF XFB work; untested here, no CSF hardware).

<details><summary>conflict as found</summary>

```
<<<<<<< ours
      .EXT_astc_decode_mode = PAN_ARCH >= 7,
=======
      .EXT_transform_feedback = PAN_ARCH >= 10,
      .EXT_astc_decode_mode = PAN_ARCH >= 9,
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_physical_device.c — hunk 1
Upstream (83cda621, external_format_resolve) added has_gralloc; the fork disables sparse on kbase (no sparse bind queue). Keep both.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   const bool has_gralloc = vk_android_get_ugralloc() != NULL;
   bool has_sparse = PAN_ARCH >= 10;
=======
   /* The kbase backend does not have a sparse bind queue implementation yet.
    * Do not advertise sparse support there, otherwise CTS will exercise sparse
    * binding paths that can only fail at submit time. */
   bool has_sparse = PAN_ARCH >= 10 && !device->kbase_node_path[0];
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c — hunk 1
Upstream (CRC tracking 9f64d72d/5b11a252) marks CRC valid after the fragment run on v10; the fork marks kbase queue progress at the same point. Independent CS instructions emitted at the same place; keep both. CSF-only path, UNTESTED here (no CSF hardware).

<details><summary>conflict as found</summary>

```
<<<<<<< ours
#if PAN_ARCH == 10
   /* CRC becomes valid only after full-frame fragment completion without IR. */
   mark_crc_valid_after_fragment(b, cmdbuf);
#endif
=======
   panvk_per_arch(kbase_mark_progress)(
      cmdbuf, PANVK_SUBQUEUE_FRAGMENT,
      PANVK_KBASE_PROGRESS_FRAG_AFTER_RUN);
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c — hunk 1
Include list: upstream needs util/log.h, the fork needs util/os_time.h. Both.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
#include "util/log.h"
=======
#include "util/os_time.h"
>>>>>>> theirs
```
</details>

### src/compiler/nir/nir_lower_xfb_to_stores.c — hunk 1
Upstream 9a80fe85 and the fork independently fixed non-zero store_output components in XFB lowering. Equivalent when start_component >= component (the fork asserts exactly that). Take upstream's form; drop the fork's local + assert.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
=======
   const unsigned component = nir_intrinsic_component(intr);
   assert(start_component >= component);
>>>>>>> theirs
```
</details>

### src/compiler/nir/nir_lower_xfb_to_stores.c — hunk 1
Second half of the same duplicate fix: upstream's mask computation.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
   mask = (mask << start_component) >> nir_intrinsic_component(intr);
   nir_def *value = nir_channels(b, src, mask);
=======
   nir_def *value =
      nir_channels(b, src, mask << (start_component - component));
>>>>>>> theirs
```
</details>

### src/vulkan/runtime/vk_android.c — hunk 1
Upstream restructured AHB usage (deferred create info, 1320fc8b/b1bf53eb; force linear for COMPRESSION_DISABLED, e39a340f) and removed the mutable-format linear forcing (491bb61a) that panvk-g99-jm 0054's comment cites. Merge: upstream's computation, then 0054's MediaTek fallback-gralloc linear fix OR'd on top in both branches (PANVK_AHB_NO_LINEAR_FIX control preserved). Comment updated to reference the upstream case that still exists.

<details><summary>conflict as found</summary>

```
<<<<<<< ours

      /* Populate more accurate AHB usage via vk_image_create_info_to_ahb_usage
       * if vk_android_init_deferred_image has been adopted. Otherwise, fallback
       * to vk_image_usage_to_ahb_usage.
       */
      if (image->android_deferred_create_info) {
         usage = vk_image_create_info_to_ahb_usage(
            image->android_deferred_create_info);
      } else {
         usage = vk_image_usage_to_ahb_usage(image->create_flags, image->usage);
         if ((image->compr_flags & VK_IMAGE_COMPRESSION_DISABLED_EXT) &&
             !vk_format_is_depth_or_stencil(image->format))
            usage |= AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;
      }
=======
      usage = vk_image_usage_to_ahb_usage(image->create_flags,
                                          image->usage);
      /* With the fallback gralloc the real layout of a GPU-only AHB cannot
       * be queried (MediaTek gralloc allocates AFBC for it). Ask for CPU
       * access, which makes gralloc allocate a plain linear buffer whose
       * layout the fallback path describes correctly. Same idea as the
       * mutable-format case in vk_image_usage_to_ahb_usage().
       */
      if (vk_android_gralloc_is_fallback() &&
          !vk_android_ahb_linear_fix_disabled())
         usage |= AHARDWAREBUFFER_USAGE_CPU_READ_RARELY |
                  AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_shader.c — hunk 1
Upstream 9d25b83f dropped panvk_lower_load_vs_input and moved load_input lowering into pan_nir_lower_vs_inputs() (bifrost_nir.c), emitting load_raw_vertex_id and the renamed intrinsic nir_load_attr_pan (old nir_load_attribute_pan no longer exists: 0 uses upstream). On Valhall load_vertex_id and load_raw_vertex_id compile to the same bi_vertex_id move (bifrost_compile.c), so HW/XFB variants on v9 keep their meaning with upstream's pass. The fork's SW (compute-backed tessellation) variant needs load_vertex_id because poly_nir_lower_vs.c only rewrites load_vertex_id; it gets a local copy of upstream's lowering that emits load_vertex_id. Keep 0042's PAN_ARCH <= 9 guard.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
#if PAN_ARCH < 9
=======
static bool
panvk_lower_load_vs_input(nir_builder *b, nir_intrinsic_instr *intrin,
                           void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_input)
      return false;

   const bool no_idvs = *(const bool *)data;
   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *ld_attr = nir_load_attribute_pan(
      b, intrin->def.num_components, intrin->def.bit_size,
      PAN_ARCH < 9 || no_idvs ?
         nir_load_raw_vertex_id(b) :
         nir_load_vertex_id(b),
      nir_load_instance_id(b),
      nir_get_io_offset_src(intrin)->ssa,
      .base = nir_intrinsic_base(intrin),
      .component = nir_intrinsic_component(intrin),
      .dest_type = nir_intrinsic_dest_type(intrin));
   nir_def_replace(&intrin->def, ld_attr);

   return true;
}

#if PAN_ARCH <= 9 /* v9 JM: see the lower_layer_writes call below */
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_shader.c — hunk 1
Drop the fork's call of the removed pass: upstream lowers load_input inside the compiler for HW/XFB variants (see hunk 1). Keep 0046/0050's v9 sampler min/max emulation before pan_postprocess_nir, unchanged.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
=======
   if (nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, panvk_lower_load_vs_input,
               nir_metadata_control_flow, &input.no_idvs);

#if PAN_ARCH == 9
   /* EXPERIMENTAL, opt-in: emulated sampler min/max reduction and cubic
    * filtering. PANVK_V9_NO_MINMAX=1 skips it (negative control; compile
    * time, needs MESA_SHADER_CACHE_DISABLE=true). */
   if (panvk_v9_sampler_emulation_active(&dev->vk) &&
       !getenv("PANVK_V9_NO_MINMAX"))
      NIR_PASS(_, nir, panvk_v9_lower_sampler_minmax);
#endif

>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_shader.c — hunk 1
Upstream 2d327535 (large constants on Valhall) asserts the inline constant pool stays within one 4 GB window (PC + 32-bit offset); panvk-g99-jm 0051 dumps shader binaries under PAN_V9_TRACE. Keep both.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
#if PAN_ARCH >= 9
   /* The inline constant pool is addressed as PC plus a 32-bit offset */
   ASSERTED uint64_t code_dev_addr = panvk_priv_mem_dev_addr(shader->code_mem);
   assert(code_dev_addr >> 32 == (code_dev_addr + shader->bin_size - 1) >> 32);
#endif
=======
   {
      const char *stage_name =
         shader->info.stage == MESA_SHADER_VERTEX ? "VERTEX" :
         shader->info.stage == MESA_SHADER_FRAGMENT ? "FRAGMENT" : "OTHER";
      PAN_V9_TRACE( "[PANVK_DEBUG_SHADERBIN] stage=%s bin_size=%u bytes:",
              stage_name, shader->bin_size);
      const uint8_t *b = shader->bin_ptr;
      for (uint32_t i = 0; i < shader->bin_size; i++)
         PAN_V9_TRACE( "%s%02x", (i % 16 == 0) ? "\n  " : " ", b[i]);
      PAN_V9_TRACE( "\n");
   }
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_shader.c — hunk 1
Upstream e0907dc5 removed trust_highp_types and b2c14af8 lowers mediump varyings early, so pan_varying_collect_formats() lost both extra parameters. Keep the fork's condition (every variant except SW, so its XFB variant still gets a varying layout) with upstream's 3-argument call. XFB is a fork CSF feature, UNTESTED here.

<details><summary>conflict as found</summary>

```
<<<<<<< ours
         if (v == PANVK_VS_VARIANT_HW) {
            pan_varying_collect_formats(&varying_layout, nir, inputs.gpu_id);
=======
         if (v != PANVK_VS_VARIANT_SW) {
            pan_varying_collect_formats(&varying_layout, nir, inputs.gpu_id,
                                        inputs.trust_varying_flat_highp_types,
                                        true);
>>>>>>> theirs
```
</details>

### src/panfrost/vulkan/panvk_vX_shader.c — fork tessellation call site
The merged (non-conflicting) fork code still called the removed pass with no_idvs=false; switched to panvk_lower_vs_input_for_poly (see hunk 1). UNTESTED (CSF-only path).

### src/panfrost/vulkan/panvk_vX_shader.c — fork TES path (compile error, not a textual conflict)
The fork's tessellation-evaluation path set inputs.trust_varying_flat_highp_types and used the 5-argument pan_varying_collect_formats(). Upstream e0907dc5 removed trust_highp_types and b2c14af8 lowers mediump varyings early; ported to the 3-argument call, same as hunk 4. CSF-only path, UNTESTED.
